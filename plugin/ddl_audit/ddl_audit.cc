/* Copyright (c) 2025, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is designed to work with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have either included with the
   program or referenced in the documentation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file plugin/ddl_audit/ddl_audit.cc

  DDL Audit Plugin

  This plugin captures DDL statements and sends them to a Cedar server
  for data population. It follows MySQL's standard audit plugin patterns
  and uses proper event structures for accurate data extraction.

  Features:
  - Captures all DDL statements (CREATE, ALTER, DROP, etc.)
  - Uses MySQL's internal event structures for accurate parsing
  - Sends structured data to Cedar server
  - Configurable Cedar service URL and timeout
  - Proper status variables and counters
  - Detailed logging for debugging

  Configuration:
  INSTALL PLUGIN ddl_audit SONAME 'ddl_audit.so';
  SET GLOBAL ddl_audit_cedar_url = 'http://localhost:8180';
  SET GLOBAL ddl_audit_cedar_timeout = 5000;  -- milliseconds
  SET GLOBAL ddl_audit_enabled = ON;

  Cedar Service API:
  The plugin sends POST requests to /v1/ddl_audit with JSON payload:
  {
    "ddl_type": "CREATE_TABLE",
    "sql_command_id": 1,
    "query": "CREATE TABLE test (id INT)",
    "database": "test_db",
    "table": "test",
    "user": "root",
    "host": "localhost",
    "timestamp": "2025-01-01T12:00:00Z",
    "context": {
      "ip_address": "127.0.0.1",
      "connection_id": 123,
      "event_class": "MYSQL_AUDIT_QUERY_CLASS",
      "event_subclass": "MYSQL_AUDIT_QUERY_STATUS_END"
    }
  }
*/

#include <mysql/plugin.h>
#include <mysql/plugin_audit.h>
#include <mysql/service_my_plugin_log.h>
#include <mysql/service_mysql_alloc.h>
#include <mysqld_error.h>

#include <curl/curl.h>
#include <json/json.h>
#include <json/value.h>
#include <sstream>
#include <string>
#include <vector>

#include "plugin/ddl_audit/ddl_audit.h"

#include "lex_string.h"
#include "m_ctype.h"
#include "my_compiler.h"
#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_macros.h"
#include "my_sqlcommand.h"
#include "my_sys.h"
#include "mysql/psi/mysql_mutex.h"
#include "sql/auth/auth_acls.h"
#include "sql/protocol_classic.h"
#include "sql/sql_class.h"
#include "sql/sql_lex.h"
#include "sql/table.h"
#include "thr_mutex.h"
#include "violite.h"

#include <ctime>
using namespace std;

// Plugin system variables
static char *ddl_audit_cedar_url = nullptr;
static char *ddl_audit_cedar_namespace = nullptr;
static int ddl_audit_cedar_timeout = 5000;  // milliseconds
static bool ddl_audit_enabled = true;

// Plugin initialization flag
static bool g_plugin_installed = false;

// Saved plugin handle for logging
static MYSQL_PLUGIN ddl_audit_plugin = nullptr;

// Status variables counters
static volatile int number_of_ddl_events = 0;
static volatile int number_of_cedar_requests = 0;
static volatile int number_of_cedar_successes = 0;
static volatile int number_of_cedar_failures = 0;
static volatile int number_of_create_table = 0;
static volatile int number_of_alter_table = 0;
static volatile int number_of_drop_table = 0;
static volatile int number_of_create_database = 0;
static volatile int number_of_drop_database = 0;
static volatile int number_of_create_user = 0;
static volatile int number_of_drop_user = 0;
static volatile int number_of_other_ddl = 0;

// Additional counters for new event classes
static volatile int number_of_auth_events = 0;
static volatile int number_of_table_access_events = 0;
static volatile int number_of_stored_program_events = 0;

// Record buffer mutex for thread safety
static mysql_mutex_t g_ddl_audit_mutex;

// Forward declarations for event handlers
static int handle_query_event(MYSQL_THD thd, const void *event);
static int handle_authentication_event(MYSQL_THD thd, const void *event);
static int handle_table_access_event(MYSQL_THD thd, const void *event);
static int handle_stored_program_event(MYSQL_THD thd, const void *event);

// UID helpers (uniform across ddl_audit and cedar_authorization)
std::string make_user_uid(const std::string &user,
                          const std::string &host [[maybe_unused]],
                          const std::string &ns);
std::string make_db_uid(const std::string &db, const std::string &ns);
std::string make_table_uid(const std::string &db, const std::string &table,
                           const std::string &ns);

// Cedar agent /data helpers
static bool cedar_upsert_entity(const std::string &entity_type,
                                const std::string &entity_id,
                                const std::string &ns);
static bool cedar_delete_entity(const std::string &entity_id,
                                const std::string &entity_type,
                                const std::string &ns);

// DDL command IDs that we want to capture
static const int ddl_commands[] = {SQLCOM_CREATE_TABLE,
                                   SQLCOM_ALTER_TABLE,
                                   SQLCOM_DROP_TABLE,
                                   SQLCOM_CREATE_INDEX,
                                   SQLCOM_DROP_INDEX,
                                   SQLCOM_CREATE_DB,
                                   SQLCOM_ALTER_DB,
                                   SQLCOM_DROP_DB,
                                   SQLCOM_CREATE_USER,
                                   SQLCOM_DROP_USER,
                                   SQLCOM_RENAME_USER,
                                   SQLCOM_ALTER_USER,
                                   SQLCOM_CREATE_FUNCTION,
                                   SQLCOM_DROP_FUNCTION,
                                   SQLCOM_ALTER_FUNCTION,
                                   SQLCOM_CREATE_PROCEDURE,
                                   SQLCOM_DROP_PROCEDURE,
                                   SQLCOM_ALTER_PROCEDURE,
                                   SQLCOM_CREATE_VIEW,
                                   SQLCOM_DROP_VIEW,
                                   SQLCOM_CREATE_TRIGGER,
                                   SQLCOM_DROP_TRIGGER,
                                   SQLCOM_CREATE_EVENT,
                                   SQLCOM_ALTER_EVENT,
                                   SQLCOM_DROP_EVENT,
                                   SQLCOM_CREATE_SERVER,
                                   SQLCOM_DROP_SERVER,
                                   SQLCOM_ALTER_SERVER,
                                   SQLCOM_CREATE_ROLE,
                                   SQLCOM_DROP_ROLE,
                                   SQLCOM_ALTER_TABLESPACE,
                                   SQLCOM_CREATE_RESOURCE_GROUP,
                                   SQLCOM_ALTER_RESOURCE_GROUP,
                                   SQLCOM_DROP_RESOURCE_GROUP,
                                   SQLCOM_CREATE_SRS,
                                   SQLCOM_DROP_SRS,
                                   SQLCOM_RENAME_TABLE};

// Plugin status variables for SHOW STATUS
static SHOW_VAR ddl_audit_status[] = {
    {"DDL_audit_events_total",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_ddl_events)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_cedar_requests",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_cedar_requests)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_cedar_successes",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_cedar_successes)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_cedar_failures",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_cedar_failures)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_create_table",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_create_table)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_alter_table",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_alter_table)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_drop_table",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_drop_table)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_create_database",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_create_database)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_drop_database",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_drop_database)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_create_user",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_create_user)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_drop_user",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_drop_user)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_other_ddl",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_other_ddl)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_auth_events",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_auth_events)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_table_access_events",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_table_access_events)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_stored_program_events",
     const_cast<char *>(
         reinterpret_cast<volatile char *>(&number_of_stored_program_events)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {nullptr, nullptr, SHOW_UNDEF, SHOW_SCOPE_GLOBAL}};

// Helper function to check if a command is DDL
bool is_ddl_command(int sql_command_id) {
  for (size_t i = 0; i < sizeof(ddl_commands) / sizeof(ddl_commands[0]); i++) {
    if (ddl_commands[i] == sql_command_id) {
      return true;
    }
  }
  return false;
}

// Helper function to get command name from ID
const char *get_command_name(int sql_command_id) {
  switch (sql_command_id) {
    case SQLCOM_CREATE_TABLE:
      return "CREATE_TABLE";
    case SQLCOM_ALTER_TABLE:
      return "ALTER_TABLE";
    case SQLCOM_DROP_TABLE:
      return "DROP_TABLE";
    case SQLCOM_CREATE_INDEX:
      return "CREATE_INDEX";
    case SQLCOM_DROP_INDEX:
      return "DROP_INDEX";
    case SQLCOM_CREATE_DB:
      return "CREATE_DATABASE";
    case SQLCOM_ALTER_DB:
      return "ALTER_DATABASE";
    case SQLCOM_DROP_DB:
      return "DROP_DATABASE";
    case SQLCOM_CREATE_USER:
      return "CREATE_USER";
    case SQLCOM_DROP_USER:
      return "DROP_USER";
    case SQLCOM_RENAME_USER:
      return "RENAME_USER";
    case SQLCOM_ALTER_USER:
      return "ALTER_USER";
    case SQLCOM_CREATE_FUNCTION:
      return "CREATE_FUNCTION";
    case SQLCOM_DROP_FUNCTION:
      return "DROP_FUNCTION";
    case SQLCOM_ALTER_FUNCTION:
      return "ALTER_FUNCTION";
    case SQLCOM_CREATE_PROCEDURE:
      return "CREATE_PROCEDURE";
    case SQLCOM_DROP_PROCEDURE:
      return "DROP_PROCEDURE";
    case SQLCOM_ALTER_PROCEDURE:
      return "ALTER_PROCEDURE";
    case SQLCOM_CREATE_VIEW:
      return "CREATE_VIEW";
    case SQLCOM_DROP_VIEW:
      return "DROP_VIEW";
    case SQLCOM_CREATE_TRIGGER:
      return "CREATE_TRIGGER";
    case SQLCOM_DROP_TRIGGER:
      return "DROP_TRIGGER";
    case SQLCOM_CREATE_EVENT:
      return "CREATE_EVENT";
    case SQLCOM_ALTER_EVENT:
      return "ALTER_EVENT";
    case SQLCOM_DROP_EVENT:
      return "DROP_EVENT";
    case SQLCOM_CREATE_SERVER:
      return "CREATE_SERVER";
    case SQLCOM_DROP_SERVER:
      return "DROP_SERVER";
    case SQLCOM_ALTER_SERVER:
      return "ALTER_SERVER";
    case SQLCOM_CREATE_ROLE:
      return "CREATE_ROLE";
    case SQLCOM_DROP_ROLE:
      return "DROP_ROLE";
    case SQLCOM_ALTER_TABLESPACE:
      return "ALTER_TABLESPACE";
    case SQLCOM_CREATE_RESOURCE_GROUP:
      return "CREATE_RESOURCE_GROUP";
    case SQLCOM_ALTER_RESOURCE_GROUP:
      return "ALTER_RESOURCE_GROUP";
    case SQLCOM_DROP_RESOURCE_GROUP:
      return "DROP_RESOURCE_GROUP";
    case SQLCOM_CREATE_SRS:
      return "CREATE_SRS";
    case SQLCOM_DROP_SRS:
      return "DROP_SRS";
    case SQLCOM_RENAME_TABLE:
      return "RENAME_TABLE";
    default:
      return "UNKNOWN";
  }
}

// Helper function to update counters based on DDL command type
static void update_ddl_counters(int sql_command_id) {
  number_of_ddl_events++;

  switch (sql_command_id) {
    case SQLCOM_CREATE_TABLE:
      number_of_create_table++;
      break;
    case SQLCOM_ALTER_TABLE:
      number_of_alter_table++;
      break;
    case SQLCOM_DROP_TABLE:
      number_of_drop_table++;
      break;
    case SQLCOM_CREATE_DB:
      number_of_create_database++;
      break;
    case SQLCOM_DROP_DB:
      number_of_drop_database++;
      break;
    case SQLCOM_CREATE_USER:
      number_of_create_user++;
      break;
    case SQLCOM_DROP_USER:
      number_of_drop_user++;
      break;
    default:
      number_of_other_ddl++;
      break;
  }
}

// Helper function to extract database name from LEX structure or thread context
string extract_database_name_from_lex(MYSQL_THD thd, int sql_command_id) {
  if (!thd || !thd->lex) {
    return "";
  }

  LEX *lex = thd->lex;

  // For CREATE_DATABASE and DROP_DATABASE, use the name from LEX
  // This handles IF NOT EXISTS and all other modifiers automatically
  if (sql_command_id == SQLCOM_CREATE_DB || sql_command_id == SQLCOM_DROP_DB ||
      sql_command_id == SQLCOM_ALTER_DB) {
    if (lex->name.str && lex->name.length > 0) {
      return string(lex->name.str, lex->name.length);
    }
    return "";
  }

  // For other commands, get database from current database context
  if (thd->db().str) {
    return string(thd->db().str, thd->db().length);
  }
  return "";
}

// Helper function to extract user information from LEX structure
void extract_users_from_lex(MYSQL_THD thd, int sql_command_id,
                            vector<pair<string, string>> &users) {
  if (!thd || !thd->lex) {
    return;
  }

  LEX *lex = thd->lex;

  // For user-related commands, extract from users_list
  // This handles all SQL syntax variations automatically (IF NOT EXISTS,
  // multiple users, etc.)
  if (sql_command_id == SQLCOM_CREATE_USER ||
      sql_command_id == SQLCOM_DROP_USER ||
      sql_command_id == SQLCOM_ALTER_USER ||
      sql_command_id == SQLCOM_RENAME_USER) {
    List_iterator<LEX_USER> user_list_it(lex->users_list);
    LEX_USER *lex_user;

    while ((lex_user = user_list_it++)) {
      string user_name = "";
      string user_host = "";

      if (lex_user->user.str && lex_user->user.length > 0) {
        user_name = string(lex_user->user.str, lex_user->user.length);
      }
      if (lex_user->host.str && lex_user->host.length > 0) {
        user_host = string(lex_user->host.str, lex_user->host.length);
      }

      if (!user_name.empty()) {
        users.push_back(make_pair(user_name, user_host));
      }
    }
  }
}

// Helper function to extract a table's db/name from LEX (first table)
void extract_table_from_lex(MYSQL_THD thd, int sql_command_id, string &out_db,
                            string &out_table) {
  out_db.clear();
  out_table.clear();
  if (!thd || !thd->lex) {
    return;
  }

  // For table-related statements, the table list is populated in LEX
  switch (sql_command_id) {
    case SQLCOM_CREATE_TABLE:
    case SQLCOM_ALTER_TABLE:
    case SQLCOM_DROP_TABLE:
    case SQLCOM_RENAME_TABLE:
    case SQLCOM_CREATE_INDEX:
    case SQLCOM_DROP_INDEX: {
      Table_ref *tr = thd->lex->query_tables;
      for (; tr != nullptr; tr = tr->next_global) {
        const char *db = tr->db;
        const char *name = tr->table_name;
        if (name && name[0] != '\0') {
          out_table.assign(name);
          if (db && db[0] != '\0') out_db.assign(db);
          break;  // use first table occurrence
        }
      }
      break;
    }
    default:
      // Not a table-related statement
      break;
  }
}

// Helper function to extract rename table pairs from LEX structure
void extract_rename_tables_from_lex(
    MYSQL_THD thd, int sql_command_id,
    std::vector<std::pair<std::pair<std::string, std::string>,
                          std::pair<std::string, std::string>>> &tables) {
  if (!thd || !thd->lex || sql_command_id != SQLCOM_RENAME_TABLE) {
    return;
  }

  // In SQLCOM_RENAME_TABLE, the table_list contains pairs of tables (from, to)
  // they are linked sequentially.
  Table_ref *tr = thd->lex->query_tables;
  while (tr) {
    // Source table
    const char *src_db = tr->db;
    const char *src_name = tr->table_name;

    // Advance to destination table
    tr = tr->next_global;
    if (!tr) break;  // Should not happen for valid RENAME

    const char *dst_db = tr->db;
    const char *dst_name = tr->table_name;

    if (src_name && src_name[0] && dst_name && dst_name[0]) {
      std::pair<std::string, std::string> src;
      src.first =
          (src_db && src_db[0])
              ? src_db
              : (thd->db().str ? std::string(thd->db().str, thd->db().length)
                               : "");
      src.second = src_name;

      std::pair<std::string, std::string> dst;
      dst.first =
          (dst_db && dst_db[0])
              ? dst_db
              : (thd->db().str ? std::string(thd->db().str, thd->db().length)
                               : "");
      dst.second = dst_name;

      tables.push_back({src, dst});
    }

    // Advance to next pair
    tr = tr->next_global;
  }
}

// Helper function to extract table name from query using simple parsing

// Helper function to get current timestamp
string get_current_timestamp() {
  time_t now = time(0);
  char buffer[100];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
  return string(buffer);
}

// Helper function to get client IP address
string get_client_ip(MYSQL_THD thd) {
  if (!thd || !thd->get_protocol_classic()) {
    return "unknown";
  }

  Vio *vio = thd->get_protocol_classic()->get_vio();
  if (!vio) {
    return "unknown";
  }

  char ip[INET6_ADDRSTRLEN];
  uint16_t port;
  if (!vio_peer_addr(vio, ip, &port, sizeof(ip))) {
    return string(ip);
  }

  return "unknown";
}

// Callback function for libcurl to write response
static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            string *s) {
  size_t newLength = size * nmemb;
  try {
    s->append((char *)contents, newLength);
    return newLength;
  } catch (bad_alloc &e) {
    return 0;
  }
}

// UID helpers (definitions)
std::string make_user_uid(const std::string &user,
                          const std::string &host [[maybe_unused]],
                          const std::string &ns) {
  // For DDL audit plugin, we don't include host information in entity UIDs
  // Host information is available in authorization context via
  // cedar_authorization plugin
  std::string prefix = ns.empty() ? "" : ns + "::";
  return prefix + "User::\"" + user + "\"";
}

std::string make_db_uid(const std::string &db, const std::string &ns) {
  std::string prefix = ns.empty() ? "" : ns + "::";
  return prefix + "Database::\"" + db + "\"";
}

std::string make_table_uid(const std::string &db, const std::string &table,
                           const std::string &ns) {
  std::string prefix = ns.empty() ? "" : ns + "::";
  std::string table_id;
  if (!db.empty() && !table.empty())
    table_id = db + "." + table;
  else if (!table.empty())
    table_id = table;
  else
    table_id = db;

  return prefix + "Table::\"" + table_id + "\"";
}

// Mock storage for tests
#ifdef EXTRA_CODE_FOR_UNIT_TESTING
static std::vector<MockCedarCall> g_test_cedar_calls;
static std::string g_test_cedar_url_override;

void ddl_audit_test_reset() {
  g_test_cedar_calls.clear();
  g_test_cedar_url_override.clear();
}

const std::vector<MockCedarCall> &ddl_audit_test_get_calls() {
  return g_test_cedar_calls;
}

void ddl_audit_test_set_mock_url(const char *url) {
  if (url)
    g_test_cedar_url_override = url;
  else
    g_test_cedar_url_override.clear();
}
#endif

// Cedar agent /data helpers implementations
bool cedar_upsert_entity(const std::string &entity_type,
                         const std::string &entity_id, const std::string &ns) {
#ifdef EXTRA_CODE_FOR_UNIT_TESTING
  g_test_cedar_calls.push_back({"upsert", entity_type, entity_id, ns});
  return true;
#endif

  if (!ddl_audit_cedar_url || strlen(ddl_audit_cedar_url) == 0) {
    if (ddl_audit_plugin) {
      my_plugin_log_message(
          &ddl_audit_plugin, MY_ERROR_LEVEL,
          "DDL Audit: Cedar URL not configured for /data upsert");
    }
    number_of_cedar_failures++;
    return false;
  }

  number_of_cedar_requests++;

  CURL *curl = curl_easy_init();
  if (!curl) {
    if (ddl_audit_plugin) {
      my_plugin_log_message(
          &ddl_audit_plugin, MY_ERROR_LEVEL,
          "DDL Audit: Failed to initialize curl for /data upsert");
    }
    number_of_cedar_failures++;
    return false;
  }

  // Build full type with namespace
  std::string full_type = ns.empty() ? entity_type : ns + "::" + entity_type;
  std::string full_uid = full_type + "::\"" + entity_id + "\"";

  // Build URL: <base>[/v1]/data/single/<urlencoded id>
  std::string base = std::string(ddl_audit_cedar_url);
  if (!base.empty() && base.back() == '/') base.pop_back();
  bool has_v1 = base.size() >= 3 && base.substr(base.size() - 3) == "/v1";
  char *escaped =
      curl_easy_escape(curl, full_uid.c_str(), (int)full_uid.length());
  std::string url = base + (has_v1 ? "" : "/v1") + "/data/single/" +
                    (escaped ? escaped : full_uid.c_str());

  // Build JSON payload: array with single entity
  Json::Value entity(Json::objectValue);
  entity["uid"]["id"] = entity_id;
  entity["uid"]["type"] = full_type;
  entity["attrs"] = Json::Value(Json::objectValue);
  entity["parents"] = Json::Value(Json::arrayValue);
  Json::Value arr(Json::arrayValue);
  arr.append(entity);

  Json::StreamWriterBuilder builder;
  std::string payload = Json::writeString(builder, arr);

  std::string response;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.length());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, ddl_audit_cedar_timeout);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000);

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "X-Cedar-Write-Origin: db-entity-sync");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl);
  long response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  if (escaped) curl_free(escaped);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    if (ddl_audit_plugin) {
      my_plugin_log_message(&ddl_audit_plugin, MY_ERROR_LEVEL,
                            "DDL Audit: /data upsert failed: %s",
                            curl_easy_strerror(res));
    }
    number_of_cedar_failures++;
    return false;
  }

  if (response_code >= 200 && response_code < 300) {
    number_of_cedar_successes++;
    return true;
  } else if (response_code == 409) {  // Conflict - entity already exists
    if (ddl_audit_plugin) {
      my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                            "DDL Audit: /data upsert HTTP 409 Conflict, entity "
                            "'%s' already exists.",
                            entity_id.c_str());
    }
    number_of_cedar_successes++;  // Treat as success for idempotency
    return true;
  } else {
    if (ddl_audit_plugin) {
      my_plugin_log_message(&ddl_audit_plugin, MY_ERROR_LEVEL,
                            "DDL Audit: /data upsert HTTP %ld, response: %s",
                            response_code, response.c_str());
    }
    number_of_cedar_failures++;
    return false;
  }
}

bool cedar_delete_entity(const std::string &entity_id,
                         const std::string &entity_type,
                         const std::string &ns) {
#ifdef EXTRA_CODE_FOR_UNIT_TESTING
  g_test_cedar_calls.push_back({"delete", entity_type, entity_id, ns});
  return true;
#endif

  if (!ddl_audit_cedar_url || strlen(ddl_audit_cedar_url) == 0) {
    if (ddl_audit_plugin) {
      my_plugin_log_message(
          &ddl_audit_plugin, MY_ERROR_LEVEL,
          "DDL Audit: Cedar URL not configured for /data delete");
    }
    number_of_cedar_failures++;
    return false;
  }

  number_of_cedar_requests++;

  CURL *curl = curl_easy_init();
  if (!curl) {
    if (ddl_audit_plugin) {
      my_plugin_log_message(
          &ddl_audit_plugin, MY_ERROR_LEVEL,
          "DDL Audit: Failed to initialize curl for /data delete");
    }
    number_of_cedar_failures++;
    return false;
  }

  // Build full UID with namespace and type
  std::string full_type = ns.empty() ? entity_type : ns + "::" + entity_type;
  std::string full_uid = full_type + "::\"" + entity_id + "\"";

  // Build URL: <base>[/v1]/data/single/<urlencoded id>
  std::string base = std::string(ddl_audit_cedar_url);
  if (!base.empty() && base.back() == '/') base.pop_back();
  bool has_v1 = base.size() >= 3 && base.substr(base.size() - 3) == "/v1";
  char *escaped =
      curl_easy_escape(curl, full_uid.c_str(), (int)full_uid.length());
  std::string url = base + (has_v1 ? "" : "/v1") + "/data/single/" +
                    (escaped ? escaped : full_uid.c_str());

  std::string response;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, ddl_audit_cedar_timeout);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000);

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "X-Cedar-Write-Origin: db-entity-sync");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl);
  long response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  if (escaped) curl_free(escaped);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    if (ddl_audit_plugin) {
      my_plugin_log_message(&ddl_audit_plugin, MY_ERROR_LEVEL,
                            "DDL Audit: /data delete failed: %s",
                            curl_easy_strerror(res));
    }
    number_of_cedar_failures++;
    return false;
  }

  if (response_code >= 200 && response_code < 300) {
    number_of_cedar_successes++;
    return true;
  } else {
    if (ddl_audit_plugin) {
      my_plugin_log_message(&ddl_audit_plugin, MY_ERROR_LEVEL,
                            "DDL Audit: /data delete HTTP %ld, response: %s",
                            response_code, response.c_str());
    }
    number_of_cedar_failures++;
    return false;
  }
}

// Helper function to create DDL data JSON
Json::Value create_ddl_data(MYSQL_THD thd, const string &ddl_type,
                            enum_sql_command_t sql_command_id,
                            const string &query, const string &database,
                            const string &table, const string &event_class_name,
                            const string &event_subclass_name,
                            const Json::Value &extra_context = Json::Value()) {
  Json::Value ddl_data;
  ddl_data["ddl_type"] = ddl_type;
  ddl_data["sql_command_id"] = (int)sql_command_id;
  ddl_data["query"] = query;
  ddl_data["database"] = database;
  ddl_data["table"] = table;
  ddl_data["timestamp"] = get_current_timestamp();

  // Get user information
  string user = "unknown";
  string host = "unknown";
  if (thd && thd->security_context()) {
    const char *user_ptr = thd->security_context()->user().str;
    const char *host_ptr = thd->security_context()->host().str;
    if (user_ptr)
      user = string(user_ptr, thd->security_context()->user().length);
    if (host_ptr)
      host = string(host_ptr, thd->security_context()->host().length);
  }
  ddl_data["user"] = user;
  ddl_data["host"] = host;

  // Add context information
  Json::Value context;
  context["ip_address"] = get_client_ip(thd);
  context["event_class"] = event_class_name;
  context["event_subclass"] = event_subclass_name;
  if (thd) {
    context["connection_id"] = thd->thread_id();
  }

  // Merge extra context if provided
  if (!extra_context.isNull()) {
    for (const auto &key : extra_context.getMemberNames()) {
      context[key] = extra_context[key];
    }
  }

  ddl_data["context"] = context;
  return ddl_data;
}

// Main audit notification function
int ddl_audit_notify(MYSQL_THD thd, mysql_event_class_t event_class,
                     const void *event) {
  // Check if plugin is enabled first
  if (!ddl_audit_enabled || !g_plugin_installed) {
    return 0;
  }

  // Handle different event classes
  switch (event_class) {
    case MYSQL_AUDIT_QUERY_CLASS:
      return handle_query_event(thd, event);

    case MYSQL_AUDIT_AUTHENTICATION_CLASS:
      return handle_authentication_event(thd, event);

    case MYSQL_AUDIT_TABLE_ACCESS_CLASS:
      return handle_table_access_event(thd, event);

    case MYSQL_AUDIT_STORED_PROGRAM_CLASS:
      return handle_stored_program_event(thd, event);

    default:
      // Ignore other event classes
      return 0;
  }
}

// Handle MYSQL_AUDIT_QUERY_CLASS events (our original DDL handling)
int handle_query_event(MYSQL_THD thd, const void *event) {
  const struct mysql_event_query *event_query =
      (const struct mysql_event_query *)event;

  // Only process post-execution events (after successful execution)
  if (event_query->event_subclass != MYSQL_AUDIT_QUERY_STATUS_END) {
    return 0;
  }

  // Check if this is a DDL command
  if (!is_ddl_command(event_query->sql_command_id)) {
    return 0;
  }

  // Thread-safe counter updates
  mysql_mutex_lock(&g_ddl_audit_mutex);
  update_ddl_counters(event_query->sql_command_id);
  mysql_mutex_unlock(&g_ddl_audit_mutex);

  // Extract information from event and context using LEX structure
  string database =
      extract_database_name_from_lex(thd, event_query->sql_command_id);
  string table;
  // Use LEX for table extraction to handle IF [NOT] EXISTS, quoting, etc.
  extract_table_from_lex(thd, event_query->sql_command_id, database, table);
  string command_name = get_command_name(event_query->sql_command_id);
  string ns = ddl_audit_cedar_namespace ? ddl_audit_cedar_namespace : "MySQL";

  // Keep Cedar agent's /data in sync using uniform UIDs
  // Handle database-level entities
  if (event_query->sql_command_id == SQLCOM_CREATE_DB ||
      event_query->sql_command_id == SQLCOM_ALTER_DB) {
    if (!database.empty()) {
      if (ddl_audit_plugin) {
        my_plugin_log_message(
            &ddl_audit_plugin, MY_INFORMATION_LEVEL,
            "DDL Audit: Calling cedar_upsert_entity for Database '%s'",
            database.c_str());
      }
      cedar_upsert_entity("Database", database, ns);
    } else {
      if (ddl_audit_plugin) {
        my_plugin_log_message(&ddl_audit_plugin, MY_WARNING_LEVEL,
                              "DDL Audit: Skipping cedar_upsert_entity for "
                              "Database - empty database name");
      }
    }
  } else if (event_query->sql_command_id == SQLCOM_DROP_DB) {
    if (!database.empty()) {
      if (ddl_audit_plugin) {
        my_plugin_log_message(
            &ddl_audit_plugin, MY_INFORMATION_LEVEL,
            "DDL Audit: Calling cedar_delete_entity for Database '%s'",
            database.c_str());
      }
      cedar_delete_entity(database, "Database", ns);
    } else {
      if (ddl_audit_plugin) {
        my_plugin_log_message(&ddl_audit_plugin, MY_WARNING_LEVEL,
                              "DDL Audit: Skipping cedar_delete_entity for "
                              "Database - empty database name");
      }
    }
  }

  // Handle user-level entities using LEX structure
  if (event_query->sql_command_id == SQLCOM_CREATE_USER ||
      event_query->sql_command_id == SQLCOM_ALTER_USER ||
      event_query->sql_command_id == SQLCOM_RENAME_USER ||
      event_query->sql_command_id == SQLCOM_DROP_USER) {
    vector<pair<string, string>> users;
    extract_users_from_lex(thd, event_query->sql_command_id, users);

    for (const auto &user_pair : users) {
      string user_name = user_pair.first;
      string user_host = user_pair.second;

      if (event_query->sql_command_id == SQLCOM_DROP_USER) {
        if (ddl_audit_plugin) {
          my_plugin_log_message(
              &ddl_audit_plugin, MY_INFORMATION_LEVEL,
              "DDL Audit: Calling cedar_delete_entity for User '%s'",
              user_name.c_str());
        }
        cedar_delete_entity(user_name, "User", ns);
      } else {
        if (ddl_audit_plugin) {
          my_plugin_log_message(
              &ddl_audit_plugin, MY_INFORMATION_LEVEL,
              "DDL Audit: Calling cedar_upsert_entity for User '%s'",
              user_name.c_str());
        }
        cedar_upsert_entity("User", user_name, ns);
      }
    }

    if (users.empty()) {
      if (ddl_audit_plugin) {
        my_plugin_log_message(
            &ddl_audit_plugin, MY_WARNING_LEVEL,
            "DDL Audit: No users found in LEX structure for %s",
            command_name.c_str());
      }
    }
  }

  if (event_query->sql_command_id == SQLCOM_RENAME_TABLE) {
    // Special handling for RENAME TABLE to handle multiple pairs and old->new
    // transition
    std::vector<std::pair<std::pair<std::string, std::string>,
                          std::pair<std::string, std::string>>>
        rename_pairs;
    extract_rename_tables_from_lex(thd, event_query->sql_command_id,
                                   rename_pairs);

    for (const auto &pair : rename_pairs) {
      std::string old_db = pair.first.first;
      std::string old_table = pair.first.second;
      std::string new_db = pair.second.first;
      std::string new_table = pair.second.second;

      std::string old_id = old_table;
      if (!old_db.empty()) old_id = old_db + "." + old_table;
      
      std::string new_id = new_table;
      if (!new_db.empty()) new_id = new_db + "." + new_table;

      if (ddl_audit_plugin) {
        my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                              "DDL Audit: Processing RENAME from '%s' to '%s'",
                              old_id.c_str(), new_id.c_str());
      }

      // Delete old entity
      cedar_delete_entity(old_id, "Table", ns);

      // Upsert new entity
      cedar_upsert_entity("Table", new_id, ns);

      // Upsert database for new table if needed
      if (!new_db.empty()) {
        cedar_upsert_entity("Database", new_db, ns);
      }
    }
  } else if (!table.empty()) {
    std::string table_id = table;
    if (!database.empty()) table_id = database + "." + table;

    switch (event_query->sql_command_id) {
      case SQLCOM_CREATE_TABLE:
      case SQLCOM_ALTER_TABLE:
        if (!database.empty()) {
          cedar_upsert_entity("Database", database, ns);
        }
        cedar_upsert_entity("Table", table_id, ns);
        break;
      case SQLCOM_DROP_TABLE:
        cedar_delete_entity(table_id, "Table", ns);
        break;
      case SQLCOM_CREATE_INDEX:
      case SQLCOM_DROP_INDEX:
        // Just update the table definition
        cedar_upsert_entity("Table", table_id, ns);
        break;
      default:
        break;
    }
  }

  if (ddl_audit_plugin) {
    my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                          "DDL Audit: Captured %s on database='%s', table='%s'",
                          command_name.c_str(), database.c_str(),
                          table.c_str());
  }

  return 0;
}

// Handle MYSQL_AUDIT_AUTHENTICATION_CLASS events (user operations)
int handle_authentication_event(MYSQL_THD thd [[maybe_unused]],
                                const void *event) {
  const struct mysql_event_authentication *auth_event =
      (const struct mysql_event_authentication *)event;

  mysql_mutex_lock(&g_ddl_audit_mutex);
  number_of_auth_events++;
  mysql_mutex_unlock(&g_ddl_audit_mutex);

  string ddl_type;
  string subclass_name;

  // Map authentication events to DDL types
  switch (auth_event->event_subclass) {
    case MYSQL_AUDIT_AUTHENTICATION_AUTHID_CREATE:
      ddl_type = auth_event->is_role ? "CREATE_ROLE" : "CREATE_USER";
      subclass_name = "MYSQL_AUDIT_AUTHENTICATION_AUTHID_CREATE";
      mysql_mutex_lock(&g_ddl_audit_mutex);
      number_of_create_user++;
      mysql_mutex_unlock(&g_ddl_audit_mutex);
      break;

    case MYSQL_AUDIT_AUTHENTICATION_AUTHID_DROP:
      ddl_type = auth_event->is_role ? "DROP_ROLE" : "DROP_USER";
      subclass_name = "MYSQL_AUDIT_AUTHENTICATION_AUTHID_DROP";
      mysql_mutex_lock(&g_ddl_audit_mutex);
      number_of_drop_user++;
      mysql_mutex_unlock(&g_ddl_audit_mutex);
      break;

    case MYSQL_AUDIT_AUTHENTICATION_AUTHID_RENAME:
      ddl_type = "RENAME_USER";
      subclass_name = "MYSQL_AUDIT_AUTHENTICATION_AUTHID_RENAME";
      break;

    case MYSQL_AUDIT_AUTHENTICATION_CREDENTIAL_CHANGE:
      ddl_type = "ALTER_USER";
      subclass_name = "MYSQL_AUDIT_AUTHENTICATION_CREDENTIAL_CHANGE";
      break;

    default:
      return 0;  // Not interested in other auth events
  }

  // Get query string
  string query = auth_event->query.str
                     ? string(auth_event->query.str, auth_event->query.length)
                     : "";

  // Create extra context for authentication events
  Json::Value extra_context;
  extra_context["target_user"] =
      auth_event->user.str
          ? string(auth_event->user.str, auth_event->user.length)
          : "";
  extra_context["target_host"] =
      auth_event->host.str
          ? string(auth_event->host.str, auth_event->host.length)
          : "";
  extra_context["is_role"] = auth_event->is_role;
  if (auth_event->new_user.str) {
    extra_context["new_user"] =
        string(auth_event->new_user.str, auth_event->new_user.length);
  }
  if (auth_event->new_host.str) {
    extra_context["new_host"] =
        string(auth_event->new_host.str, auth_event->new_host.length);
  }

  // Keep Cedar agent's /data in sync for User entity
  std::string tgt_user =
      auth_event->user.str
          ? std::string(auth_event->user.str, auth_event->user.length)
          : "";
  std::string tgt_host =
      auth_event->host.str
          ? std::string(auth_event->host.str, auth_event->host.length)
          : "";
  std::string ns =
      ddl_audit_cedar_namespace ? ddl_audit_cedar_namespace : "MySQL";

  if (ddl_audit_plugin) {
    my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                          "DDL Audit: Auth event subclass=%d, user='%s', "
                          "host='%s', namespace='%s'",
                          auth_event->event_subclass, tgt_user.c_str(),
                          tgt_host.c_str(), ns.c_str());
  }

  switch (auth_event->event_subclass) {
    case MYSQL_AUDIT_AUTHENTICATION_AUTHID_CREATE:
    case MYSQL_AUDIT_AUTHENTICATION_CREDENTIAL_CHANGE:
      if (!tgt_user.empty()) {
        if (ddl_audit_plugin) {
          my_plugin_log_message(
              &ddl_audit_plugin, MY_INFORMATION_LEVEL,
              "DDL Audit: Calling cedar_upsert_entity for User '%s'",
              tgt_user.c_str());
        }
        cedar_upsert_entity("User", tgt_user, ns);
      } else {
        if (ddl_audit_plugin) {
          my_plugin_log_message(&ddl_audit_plugin, MY_WARNING_LEVEL,
                                "DDL Audit: Skipping cedar_upsert_entity for "
                                "User - empty username");
        }
      }
      break;
    case MYSQL_AUDIT_AUTHENTICATION_AUTHID_DROP:
      if (!tgt_user.empty()) {
        if (ddl_audit_plugin) {
          my_plugin_log_message(
              &ddl_audit_plugin, MY_INFORMATION_LEVEL,
              "DDL Audit: Calling cedar_delete_entity for User '%s'",
              tgt_user.c_str());
        }
        cedar_delete_entity(tgt_user, "User", ns);
      } else {
        if (ddl_audit_plugin) {
          my_plugin_log_message(&ddl_audit_plugin, MY_WARNING_LEVEL,
                                "DDL Audit: Skipping cedar_delete_entity for "
                                "User - empty username");
        }
      }
      break;
    case MYSQL_AUDIT_AUTHENTICATION_AUTHID_RENAME: {
      std::string new_user = auth_event->new_user.str
                                 ? std::string(auth_event->new_user.str,
                                               auth_event->new_user.length)
                                 : "";
      std::string new_host = auth_event->new_host.str
                                 ? std::string(auth_event->new_host.str,
                                               auth_event->new_host.length)
                                 : tgt_host;
      if (!tgt_user.empty()) cedar_delete_entity(tgt_user, "User", ns);
      if (!new_user.empty()) cedar_upsert_entity("User", new_user, ns);
      break;
    }
    default:
      break;
  }

  if (ddl_audit_plugin) {
    my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                          "DDL Audit: Captured %s for user='%s@%s'",
                          ddl_type.c_str(),
                          extra_context["target_user"].asString().c_str(),
                          extra_context["target_host"].asString().c_str());
  }

  return 0;
}

// Handle MYSQL_AUDIT_TABLE_ACCESS_CLASS events (table operations)
int handle_table_access_event(MYSQL_THD thd, const void *event) {
  const struct mysql_event_table_access *table_event =
      (const struct mysql_event_table_access *)event;

  // Only process DDL-related table access
  if (!is_ddl_command(table_event->sql_command_id)) {
    return 0;
  }

  mysql_mutex_lock(&g_ddl_audit_mutex);
  number_of_table_access_events++;
  mysql_mutex_unlock(&g_ddl_audit_mutex);

  // This gives us exact table information without parsing!
  string database = table_event->table_database.str
                        ? string(table_event->table_database.str,
                                 table_event->table_database.length)
                        : "";
  string table =
      table_event->table_name.str
          ? string(table_event->table_name.str, table_event->table_name.length)
          : "";
  string query = table_event->query.str
                     ? string(table_event->query.str, table_event->query.length)
                     : "";

  // Create extra context
  Json::Value extra_context;
  string access_type;
  switch (table_event->event_subclass) {
    case MYSQL_AUDIT_TABLE_ACCESS_READ:
      access_type = "READ";
      break;
    case MYSQL_AUDIT_TABLE_ACCESS_INSERT:
      access_type = "INSERT";
      break;
    case MYSQL_AUDIT_TABLE_ACCESS_UPDATE:
      access_type = "UPDATE";
      break;
    case MYSQL_AUDIT_TABLE_ACCESS_DELETE:
      access_type = "DELETE";
      break;
    default:
      access_type = "UNKNOWN";
      break;
  }
  extra_context["table_access_type"] = access_type;

  // Create DDL data
  Json::Value ddl_data = create_ddl_data(
      thd, get_command_name(table_event->sql_command_id),
      table_event->sql_command_id, query, database, table,
      "MYSQL_AUDIT_TABLE_ACCESS_CLASS", access_type, extra_context);

  if (ddl_audit_plugin) {
    my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                          "DDL Audit: Table access %s on %s.%s",
                          extra_context["table_access_type"].asString().c_str(),
                          database.c_str(), table.c_str());
  }

  return 0;
}

// Handle MYSQL_AUDIT_STORED_PROGRAM_CLASS events (procedures/functions)
int handle_stored_program_event(MYSQL_THD thd, const void *event) {
  const struct mysql_event_stored_program *prog_event =
      (const struct mysql_event_stored_program *)event;

  mysql_mutex_lock(&g_ddl_audit_mutex);
  number_of_stored_program_events++;
  mysql_mutex_unlock(&g_ddl_audit_mutex);

  string database =
      prog_event->database.str
          ? string(prog_event->database.str, prog_event->database.length)
          : "";
  string name = prog_event->name.str
                    ? string(prog_event->name.str, prog_event->name.length)
                    : "";
  string query = prog_event->query.str
                     ? string(prog_event->query.str, prog_event->query.length)
                     : "";

  // Create extra context
  Json::Value extra_context;
  extra_context["program_name"] = name;
  extra_context["program_database"] = database;

  // Create DDL data
  Json::Value ddl_data =
      create_ddl_data(thd, get_command_name(prog_event->sql_command_id),
                      prog_event->sql_command_id, query, database, name,
                      "MYSQL_AUDIT_STORED_PROGRAM_CLASS",
                      "MYSQL_AUDIT_STORED_PROGRAM_EXECUTE", extra_context);

  if (ddl_audit_plugin) {
    my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                          "DDL Audit: Stored program event on %s.%s",
                          database.c_str(), name.c_str());
  }

  return 0;
}

// Plugin initialization
int ddl_audit_plugin_init(MYSQL_PLUGIN plugin_info) {
  // Save plugin handle for logging first
  ddl_audit_plugin = plugin_info;

  if (ddl_audit_plugin) {
    my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                          "DDL Audit Plugin initialization starting...");
  }

  // Initialize status variables
  SHOW_VAR *var;
  for (var = ddl_audit_status; var->value != nullptr; var++) {
    *((int *)var->value) = 0;
  }

  // Initialize mutex for thread safety
  mysql_mutex_init(PSI_NOT_INSTRUMENTED, &g_ddl_audit_mutex,
                   MY_MUTEX_INIT_FAST);

  // Initialize curl
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
    if (ddl_audit_plugin) {
      my_plugin_log_message(&ddl_audit_plugin, MY_ERROR_LEVEL,
                            "Failed to initialize libcurl globally");
    }
    mysql_mutex_destroy(&g_ddl_audit_mutex);
    return 1;
  }

  g_plugin_installed = true;

  if (ddl_audit_plugin) {
    my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                          "DDL Audit Plugin initialized successfully");
  }

  return 0;
}

// Plugin deinitialization
int ddl_audit_plugin_deinit(MYSQL_PLUGIN plugin_info [[maybe_unused]]) {
  if (ddl_audit_plugin) {
    my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                          "DDL Audit Plugin deinitialization starting...");
  }

  if (g_plugin_installed) {
    // Cleanup curl
    curl_global_cleanup();

    // Destroy mutex
    mysql_mutex_destroy(&g_ddl_audit_mutex);

    g_plugin_installed = false;

    if (ddl_audit_plugin) {
      my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                            "DDL Audit Plugin deinitialized successfully");
    }
  }

  ddl_audit_plugin = nullptr;
  return 0;
}

// Plugin system variables
static MYSQL_SYSVAR_STR(cedar_url, ddl_audit_cedar_url,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
                        "Cedar server URL for DDL audit data", nullptr, nullptr,
                        "http://localhost:8280");

static MYSQL_SYSVAR_INT(cedar_timeout, ddl_audit_cedar_timeout,
                        PLUGIN_VAR_RQCMDARG,
                        "Timeout for Cedar server requests in milliseconds",
                        nullptr, nullptr, 5000, 100, 30000, 0);

static MYSQL_SYSVAR_BOOL(enabled, ddl_audit_enabled, PLUGIN_VAR_RQCMDARG,
                         "Enable/disable DDL audit plugin", nullptr, nullptr,
                         true);

static MYSQL_SYSVAR_STR(cedar_namespace, ddl_audit_cedar_namespace,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
                        "Namespace for Cedar entities (e.g., MySQL)", nullptr,
                        nullptr, "MySQL");

static SYS_VAR *ddl_audit_system_variables[] = {
    MYSQL_SYSVAR(cedar_url), MYSQL_SYSVAR(cedar_namespace),
    MYSQL_SYSVAR(cedar_timeout), MYSQL_SYSVAR(enabled), nullptr};

// Audit plugin descriptor
static struct st_mysql_audit ddl_audit_descriptor = {
    MYSQL_AUDIT_INTERFACE_VERSION, /* interface version */
    nullptr,                       /* release_thd function */
    ddl_audit_notify,              /* notify function */
    {
        0, /* MYSQL_AUDIT_GENERAL_CLASS */
        0, /* MYSQL_AUDIT_CONNECTION_CLASS */
        0, /* MYSQL_AUDIT_PARSE_CLASS */
        0, /* MYSQL_AUDIT_AUTHORIZATION_CLASS - not supported */
        (unsigned long)
            MYSQL_AUDIT_TABLE_ACCESS_ALL, /* MYSQL_AUDIT_TABLE_ACCESS_CLASS */
        0, /* MYSQL_AUDIT_GLOBAL_VARIABLE_CLASS */
        0, /* MYSQL_AUDIT_SERVER_STARTUP_CLASS */
        0, /* MYSQL_AUDIT_SERVER_SHUTDOWN_CLASS */
        0, /* MYSQL_AUDIT_COMMAND_CLASS */
        (unsigned long)MYSQL_AUDIT_QUERY_ALL, /* MYSQL_AUDIT_QUERY_CLASS */
        (unsigned long)
            MYSQL_AUDIT_STORED_PROGRAM_ALL, /* MYSQL_AUDIT_STORED_PROGRAM_CLASS
                                             */
        (unsigned long)
            MYSQL_AUDIT_AUTHENTICATION_ALL, /* MYSQL_AUDIT_AUTHENTICATION_CLASS
                                             */
        0                                   /* MYSQL_AUDIT_MESSAGE_CLASS */
    } /* class mask */
};

// Plugin descriptor
mysql_declare_plugin(ddl_audit){
    MYSQL_AUDIT_PLUGIN,           /* plugin type */
    &ddl_audit_descriptor,        /* type specific descriptor */
    "ddl_audit",                  /* plugin name */
    PLUGIN_AUTHOR_ORACLE,         /* author */
    "DDL Audit Plugin for Cedar", /* description */
    PLUGIN_LICENSE_GPL,           /* license */
    ddl_audit_plugin_init,        /* plugin initializer */
    nullptr,                      /* plugin check uninstall */
    ddl_audit_plugin_deinit,      /* plugin deinitializer */
    0x0100,                       /* version */
    ddl_audit_status,             /* status variables */
    ddl_audit_system_variables,   /* system variables */
    nullptr,                      /* reserved */
    0                             /* flags */
} mysql_declare_plugin_end;
