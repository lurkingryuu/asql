/* Copyright (c) 2025, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is designed to work with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have either included with
   the program or referenced in the documentation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file plugin/authorization/cedar_authorization.cc

  Cedar Authorization Plugin

  This plugin implements authorization using Amazon Cedar policy engine.
  It delegates authorization decisions to a Cedar service running over HTTP.

  Features:
  - Policy-based authorization using Cedar
  - Supports all authorization event types (DB, Table, Column, Routine)
  - Configurable Cedar service URL and timeout
  - Rich context information (time, date, IP address)
  - Detailed logging for debugging

  Configuration:
  INSTALL PLUGIN cedar_authorization SONAME 'cedar_authorization.so';
  SET GLOBAL cedar_authorization_url = 'http://0.0.0.0:8180';
  SET GLOBAL cedar_authorization_timeout = 5000;  -- milliseconds

  Cedar Service API:
  The plugin sends separate POST requests to /v1/is_authorized for each privilege:
  {
    "principal": "User::\"user_hash\"",
    "action": "Action::\"Select\"",
    "resource": "Table::\"table_hash\"",
    "context": {
      "day": "mon",
      "date": 20250101,
      "time": 120000,
      "ip": {
        "__extn": {
          "fn": "ip",
          "arg": "192.168.1.1"
        }
      }
    }
  }
  
  Note: Since Cedar only handles one action per request, the plugin makes
  separate requests for each privilege (SELECT, INSERT, UPDATE, etc.) and
  only grants access if ALL privileges are allowed by Cedar.

  Expected response:
  {
    "decision": "Allow" | "Deny",
    "diagnostics": {
      "errors": []
    }
  }
*/

#include <mysql/plugin.h>
#include <mysql/plugin_authorization.h>
#include <mysql/service_my_plugin_log.h>
#include <mysql/service_mysql_alloc.h>

#include <curl/curl.h>
#include <json/json.h>
#include <sstream>
#include <string>
#include <vector>
#include <json/value.h>

#include "sql/auth/auth_acls.h"
#include "sql/sql_class.h"
// Needed for Protocol_classic definition used via THD
#include "sql/protocol_classic.h"
#include "my_dbug.h"
#include "violite.h"

#include <ctime>
#include <algorithm>
#include <cctype>
using namespace std;

// Plugin system variables
static char *cedar_authorization_url;
static int cedar_authorization_timeout = 5000;  // milliseconds

// Plugin initialization flag
static bool plugin_initialized = false;
// Saved plugin handle for logging
static MYSQL_PLUGIN plugin_handle = nullptr;

// Helper structure for HTTP response
struct HttpResponse {
  std::string data;
};

// Callback function for libcurl to write response data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            HttpResponse *response) {
  size_t total_size = size * nmemb;
  response->data.append(static_cast<char *>(contents), total_size);
  return total_size;
}

// Convert privileges bitmask to string for logging
static std::string privileges_to_string(unsigned long privileges) {
  std::ostringstream oss;
  for (const auto &[privilege, offset] : privs::global_acls_map) {
    if (privileges & (1 << offset)) {
      oss << privilege << ",";
    }
  }

  if (oss.str().empty()) {
    return "[]";
  }

  return "[" + oss.str() + "]";
}

// Convert privileges to JSON array
static Json::Value privileges_to_json(unsigned long privileges) {
  Json::Value privileges_value(Json::arrayValue);
  for (const auto &[privilege, offset] : privs::global_acls_map) {
    if (privileges & (1 << offset)) {
      privileges_value.append(privilege);
    }
  }
  return privileges_value;
}

// Determine the primary action from privileges bitmask
// Returns the highest priority privilege that is set
static std::string get_primary_action(unsigned long privileges) {
  // Priority order: SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER, etc.
  // This matches the order in the documentation example
  if (privileges & (1 << 0)) return "SELECT";      // SELECT_ACL
  if (privileges & (1 << 1)) return "INSERT";      // INSERT_ACL  
  if (privileges & (1 << 2)) return "UPDATE";      // UPDATE_ACL
  if (privileges & (1 << 3)) return "DELETE";      // DELETE_ACL
  if (privileges & (1 << 4)) return "CREATE";      // CREATE_ACL
  if (privileges & (1 << 5)) return "DROP";        // DROP_ACL
  if (privileges & (1 << 13)) return "ALTER";      // ALTER_ACL
  if (privileges & (1 << 6)) return "RELOAD";      // RELOAD_ACL
  if (privileges & (1 << 7)) return "SHUTDOWN";    // SHUTDOWN_ACL
  if (privileges & (1 << 8)) return "PROCESS";      // PROCESS_ACL
  if (privileges & (1 << 9)) return "FILE";         // FILE_ACL
  if (privileges & (1 << 10)) return "GRANT";      // GRANT_ACL
  if (privileges & (1 << 11)) return "REFERENCES"; // REFERENCES_ACL
  if (privileges & (1 << 12)) return "INDEX";       // INDEX_ACL
  if (privileges & (1 << 14)) return "SHOW DATABASES"; // SHOW_DB_ACL
  if (privileges & (1 << 15)) return "SUPER";       // SUPER_ACL
  if (privileges & (1 << 16)) return "CREATE TEMPORARY TABLES"; // CREATE_TMP_ACL
  if (privileges & (1 << 17)) return "LOCK TABLES"; // LOCK_TABLES_ACL
  if (privileges & (1 << 18)) return "EXECUTE";     // EXECUTE_ACL
  if (privileges & (1 << 19)) return "REPLICATION SLAVE"; // REPL_SLAVE_ACL
  if (privileges & (1 << 20)) return "REPLICATION CLIENT"; // REPL_CLIENT_ACL
  if (privileges & (1 << 21)) return "CREATE VIEW"; // CREATE_VIEW_ACL
  if (privileges & (1 << 22)) return "SHOW VIEW";   // SHOW_VIEW_ACL
  if (privileges & (1 << 23)) return "CREATE ROUTINE"; // CREATE_PROC_ACL
  if (privileges & (1 << 24)) return "ALTER ROUTINE"; // ALTER_PROC_ACL
  if (privileges & (1 << 25)) return "CREATE USER"; // CREATE_USER_ACL
  if (privileges & (1 << 26)) return "EVENT";       // EVENT_ACL
  if (privileges & (1 << 27)) return "TRIGGER";     // TRIGGER_ACL
  if (privileges & (1 << 28)) return "CREATE TABLESPACE"; // CREATE_TABLESPACE_ACL
  if (privileges & (1 << 29)) return "CREATE ROLE"; // CREATE_ROLE_ACL
  if (privileges & (1 << 30)) return "DROP ROLE";   // DROP_ROLE_ACL
  
  // Fallback to first privilege found
  for (const auto &[privilege, offset] : privs::global_acls_map) {
    if (privileges & (1 << offset)) {
      return privilege;
    }
  }
  
  return "UNKNOWN";
}

// Get current day as lowercase string
std::string getDay() {
  std::time_t currentTime = std::time(0);
  std::tm* now = std::localtime(&currentTime);
  char dayString[4];
  std::strftime(dayString, sizeof(dayString), "%a", now);
  std::string day(dayString);
  std::transform(day.begin(), day.end(), day.begin(), ::tolower);
  return day;
}

// Get current date as YYYYMMDD integer
uint32_t getDate() {
  std::time_t currentTime = std::time(0);
  std::tm* now = std::localtime(&currentTime);
  char dateString[9];
  std::strftime(dateString, sizeof(dateString), "%Y%m%d", now);
  return std::stoul(dateString);
}

// Get current time as HHMMSS integer
uint32_t getTime() {
  std::time_t currentTime = std::time(0);
  std::tm* now = std::localtime(&currentTime);
  char timeString[7];
  std::strftime(timeString, sizeof(timeString), "%H%M%S", now);
  return std::stoul(timeString);
}

// Get client IP address from THD
std::string getClientIP(THD *thd) {
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
    return std::string(ip);
  }
  
  return "unknown";
}

// Convert event subclass to string
static std::string event_type_to_string(mysql_authorization_event_subclass_t event_type) {
  switch (event_type) {
    case MYSQL_AUTHORIZATION_DB_ACCESS:
      return "db_access";
    case MYSQL_AUTHORIZATION_TABLE_ACCESS:
      return "table_access";
    case MYSQL_AUTHORIZATION_COLUMN_ACCESS:
      return "column_access";
    case MYSQL_AUTHORIZATION_ROUTINE_ACCESS:
      return "routine_access";
    default:
      return "unknown";
  }
}

// UID helpers to keep uniform across plugins
static std::string buildUserUID(const mysql_authorization_event *event) {
  // For Cedar authorization plugin, we don't include host information in entity UIDs
  // Host information is available in authorization context
  std::string user = event->user.str ? std::string(event->user.str, event->user.length) : std::string("unknown");
  return user;
}

static std::string makeDbId(const mysql_authorization_event *event) {
  return event->database.str ? std::string(event->database.str, event->database.length) : std::string("");
}

static std::string makeTableId(const mysql_authorization_event *event) {
  std::string db = makeDbId(event);
  std::string tbl = event->table.str ? std::string(event->table.str, event->table.length) : std::string("");
  if (!db.empty() && !tbl.empty()) return db + "." + tbl;
  if (!tbl.empty()) return tbl;
  return db.empty() ? std::string("unknown") : db;
}

static std::string makeColumnId(const mysql_authorization_event *event) {
  std::string tbl = makeTableId(event);
  std::string col = event->column.str ? std::string(event->column.str, event->column.length) : std::string("");
  if (!col.empty()) return tbl + "." + col;
  return tbl;
}

// Create resource identifier based on event type
std::string createResourceIdentifier(const mysql_authorization_event *event) {
  std::string resource;
  
  switch (event->event_subclass) {
    case MYSQL_AUTHORIZATION_DB_ACCESS: {
      std::string db = makeDbId(event);
      resource = "Database::\"" + (db.empty() ? std::string("unknown") : db) + "\"";
      break;
    }
    case MYSQL_AUTHORIZATION_TABLE_ACCESS: {
      std::string table_id = makeTableId(event);
      if (!table_id.empty())
        resource = "Table::\"" + table_id + "\"";
      else
        resource = "Table::\"unknown\"";
      break;
    }
    case MYSQL_AUTHORIZATION_COLUMN_ACCESS: {
      std::string column_id = makeColumnId(event);
      resource = "Column::\"" + column_id + "\"";
      break;
    }
    case MYSQL_AUTHORIZATION_ROUTINE_ACCESS: {
      std::string db = makeDbId(event);
      std::string routine = event->routine.str ? std::string(event->routine.str) : std::string("unknown");
      if (!db.empty()) routine = db + "." + routine;
      resource = "Routine::\"" + routine + "\"";
      break;
    }
    default:
      resource = "Unknown::\"unknown\"";
      break;
  }
  
  return resource;
}

// Check a single privilege with Cedar authorization service
static int check_single_privilege_cedar(const std::string& user_uid_value,
                                       const std::string& resource_identifier,
                                       const std::string& privilege,
                                       const std::string& day,
                                       uint32_t date,
                                       uint32_t fmt_time,
                                       const std::string& client_ip) {
  // Initialize libcurl
  CURL *curl = curl_easy_init();
  if (!curl) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_ERROR_LEVEL,
                            "Failed to initialize libcurl for Cedar authorization");
    }
    return 0;
  }

  // Create JSON payload for single privilege
  Json::Value json_payload;
  json_payload["principal"] = "User::\"" + user_uid_value + "\"";
  json_payload["action"] = "Action::\"" + privilege + "\"";
  json_payload["resource"] = resource_identifier;
  json_payload["context"]["day"] = day;
  json_payload["context"]["date"] = date;
  json_payload["context"]["time"] = fmt_time;
  json_payload["context"]["ip"]["__extn"]["fn"] = "ip";
  json_payload["context"]["ip"]["__extn"]["arg"] = client_ip;

  Json::StreamWriterBuilder builder;
  std::string json_string = Json::writeString(builder, json_payload);

  // Log the request payload
  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Sending Cedar authorization request for privilege: %s", privilege.c_str());
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Request payload: %s", json_string.c_str());
  }

  // Configure curl options
  HttpResponse response;
  curl_easy_setopt(curl, CURLOPT_URL, cedar_authorization_url);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_string.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, cedar_authorization_timeout);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  // Perform the request
  CURLcode res = curl_easy_perform(curl);
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  // Log response details
  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "HTTP request completed for privilege %s. cURL result: %d, HTTP code: %ld",
                          privilege.c_str(), res, response_code);
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Response body: %s", response.data.c_str());
  }

  // Cleanup
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_ERROR_LEVEL,
                            "Cedar authorization request failed for privilege %s: %s (cURL error: %d)",
                            privilege.c_str(), curl_easy_strerror(res), res);
    return -1;  // Signal error
  }

  if (response_code != 200) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "Cedar authorization server returned HTTP %ld for privilege %s, response: %s",
                            response_code, privilege.c_str(), response.data.c_str());
    return -1;  // Signal error
  }

  // Parse response
  Json::Value json_response;
  Json::CharReaderBuilder reader_builder;
  std::string parse_errors;
  std::istringstream response_stream(response.data);

  if (!Json::parseFromStream(reader_builder, response_stream, &json_response,
                             &parse_errors)) {
    if (plugin_handle)
      my_plugin_log_message(
          &plugin_handle, MY_WARNING_LEVEL,
          "Failed to parse Cedar authorization response for privilege %s: %s",
          privilege.c_str(), parse_errors.c_str());
    return -1;  // Signal error
  }

  if (!json_response.isMember("decision")) {
    if (plugin_handle)
      my_plugin_log_message(
          &plugin_handle, MY_WARNING_LEVEL,
          "Cedar authorization response missing 'decision' field for privilege %s",
          privilege.c_str());
    return -1;  // Signal error
  }

  std::string decision = json_response["decision"].asString();

  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization result for privilege %s: '%s'",
                          privilege.c_str(), decision.c_str());
  }

  if (decision == "Allow") {
    return 1;  // Access granted
  } else {
    return 0;  // Access denied
  }
}

// Check access using Cedar authorization service
static int check_access_cedar(const mysql_authorization_event *event) {
  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization service called for user: %s@%s, database: %s, table: %s, event: %s",
                          event->user.str ? event->user.str : "NULL",
                          event->host.str ? event->host.str : "NULL",
                          event->database.str ? event->database.str : "NULL",
                          event->table.str ? event->table.str : "NULL",
                          event_type_to_string(event->event_subclass).c_str());
  }

  if (!plugin_initialized) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "Cedar authorization plugin not initialized");
    }
    return 0;
  }

  if (!cedar_authorization_url || strlen(cedar_authorization_url) == 0) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "Cedar authorization URL not configured; caller should IGNORE");
    }
    return -1;  // signal IGNORE
  }

  // Build principal UID (user@host)
  std::string user_uid_value = buildUserUID(event);

  // Create resource identifier
  std::string resource_identifier = createResourceIdentifier(event);

  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization check: user=%s, resource=%s, privileges=%lu",
                          user_uid_value.c_str(), resource_identifier.c_str(), event->privileges);
  }

  // Get context information
  std::string day = getDay();
  auto date = getDate();
  auto fmt_time = getTime();
  std::string client_ip = getClientIP(event->thd);

  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Context: day=%s, date=%u, time=%u, ip=%s",
                          day.c_str(), date, fmt_time, client_ip.c_str());
  }

  // Check each privilege individually with Cedar
  // We need to make separate requests for each privilege since Cedar only handles one action per request
  bool all_privileges_allowed = true;
  
  for (const auto &[privilege, offset] : privs::global_acls_map) {
    if (event->privileges & (1 << offset)) {
      if (plugin_handle) {
        my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                              "Checking Cedar authorization for privilege: %s", privilege.c_str());
      }
      
      // Make individual Cedar request for this privilege
      int privilege_result = check_single_privilege_cedar(
          user_uid_value, resource_identifier, privilege, day, date, fmt_time, client_ip);
      
      if (privilege_result == -1) {
        // Error occurred, return IGNORE
        if (plugin_handle) {
          my_plugin_log_message(&plugin_handle, MY_ERROR_LEVEL,
                                "Cedar authorization error for privilege: %s", privilege.c_str());
        }
        return -1;  // Signal IGNORE
      } else if (privilege_result == 0) {
        // This privilege was denied
        all_privileges_allowed = false;
        if (plugin_handle) {
          my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                                "Cedar denied privilege: %s", privilege.c_str());
        }
        // Continue checking other privileges to log all denials
      } else {
        // This privilege was allowed
        if (plugin_handle) {
          my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                                "Cedar allowed privilege: %s", privilege.c_str());
        }
      }
    }
  }

  if (all_privileges_allowed) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: All privileges allowed");
    }
    return 1;  // All privileges allowed
  } else {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: Some privileges denied");
    }
    return 0;  // Some privileges denied
  }
}

// Main authorization callback function
static mysql_authorization_result_t cedar_authorization_check(
    const mysql_authorization_event *event) {
  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization callback invoked for user: %s@%s, event: %s",
                          event->user.str ? event->user.str : "NULL",
                          event->host.str ? event->host.str : "NULL", 
                          event_type_to_string(event->event_subclass).c_str());
  }
  
  if (!plugin_initialized) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "Cedar authorization plugin not initialized, returning IGNORE");
    }
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  // Handle presence checks by allowing them (these are MySQL's internal discovery probes)
  if (event->requirement_mode == mysql_authorization_event::MYSQL_AUTHZ_REQ_PRESENCE) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: presence check -> GRANT (internal probe)");
    }
    return MYSQL_AUTHORIZATION_GRANT;
  }
  
  // Handle zero-privilege checks by allowing them (these are also internal checks)
  if (event->privileges == 0) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: zero-priv check -> GRANT (internal check)");
    }
    return MYSQL_AUTHORIZATION_GRANT;
  }

  // Perform Cedar authorization check
  int result = check_access_cedar(event);
  
  if (result == -1) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: IGNORE");
    }
    return MYSQL_AUTHORIZATION_IGNORE;
  } else if (result == 1) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: GRANT");
    }
    return MYSQL_AUTHORIZATION_GRANT;
  } else {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: DENY");
    }
    return MYSQL_AUTHORIZATION_DENY;
  }
}

// Plugin descriptor
static st_mysql_authorization cedar_authorization_descriptor = {
    MYSQL_AUTHORIZATION_INTERFACE_VERSION, cedar_authorization_check};

// Plugin initialization
static int cedar_authorization_init(MYSQL_PLUGIN plugin_info) {
  // Save plugin handle for logging first
  plugin_handle = plugin_info;

  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization plugin initialization starting...");
  }

  // Initialize libcurl globally
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_ERROR_LEVEL,
                            "Failed to initialize libcurl globally");
    }
    return 1;
  }

  plugin_initialized = true;

  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization plugin successfully initialized!");
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Plugin will check cedar_authorization_url system variable for service URL");
  }

  return 0;
}

// Plugin deinitialization
static int cedar_authorization_deinit(MYSQL_PLUGIN plugin_info [[maybe_unused]]) {
  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization plugin deinitialization starting...");
  }

  plugin_initialized = false;
  curl_global_cleanup();

  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization plugin successfully deinitialized");
  }

  plugin_handle = nullptr;
  return 0;
}

// System variables
static MYSQL_SYSVAR_STR(url,                                        // name
                        cedar_authorization_url,                     // var
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,  // flags
                        "URL of Cedar authorization service",        // comment
                        nullptr,                                     // check
                        nullptr,                                     // update
                        nullptr                                      // default
);

static MYSQL_SYSVAR_INT(
    timeout,                                                         // name
    cedar_authorization_timeout,                                     // var
    PLUGIN_VAR_RQCMDARG,                                            // flags
    "Timeout for Cedar authorization requests in milliseconds",     // comment
    nullptr,                                                         // check
    nullptr,                                                         // update
    5000,                                                           // default
    1000,                                                           // min
    60000,                                                          // max
    0  // block_size
);

// System variables array
static SYS_VAR *cedar_authorization_system_vars[] = {
    MYSQL_SYSVAR(url), MYSQL_SYSVAR(timeout), nullptr};

// Plugin declaration
mysql_declare_plugin(cedar_authorization){
    MYSQL_AUTHORIZATION_PLUGIN,               // type
    &cedar_authorization_descriptor,          // descriptor
    "cedar_authorization",                    // name
    PLUGIN_AUTHOR_ORACLE,                     // author
    "Cedar Authorization Plugin",             // description
    PLUGIN_LICENSE_GPL,                       // license
    cedar_authorization_init,                 // init function
    nullptr,                                  // check_uninstall
    cedar_authorization_deinit,               // deinit function
    0x0100,                                   // version
    nullptr,                                  // status vars
    cedar_authorization_system_vars,          // system vars
    nullptr,                                  // config options
    0,                                        // flags
} mysql_declare_plugin_end;
