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
#include <sstream>
#include <string>
#include <vector>
#include <json/value.h>

#include "sql/auth/auth_acls.h"
#include "sql/sql_class.h"
#include "sql/protocol_classic.h"
#include "my_dbug.h"
#include "violite.h"
#include "my_sqlcommand.h"
#include "lex_string.h"
#include "m_ctype.h"
#include "my_compiler.h"
#include "my_inttypes.h"
#include "my_macros.h"
#include "my_sys.h"
#include "mysql/psi/mysql_mutex.h"
#include "thr_mutex.h"

#include <ctime>
#include <algorithm>
#include <cctype>
using namespace std;

// Plugin system variables
static char *ddl_audit_cedar_url = nullptr;
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

// DDL command IDs that we want to capture
static const int ddl_commands[] = {
    SQLCOM_CREATE_TABLE,
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
    SQLCOM_RENAME_TABLE
};

// Plugin status variables for SHOW STATUS
static SHOW_VAR ddl_audit_status[] = {
    {"DDL_audit_events_total",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_ddl_events)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_cedar_requests",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_cedar_requests)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_cedar_successes",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_cedar_successes)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_cedar_failures",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_cedar_failures)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_create_table",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_create_table)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_alter_table",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_alter_table)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_drop_table",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_drop_table)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_create_database",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_create_database)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_drop_database",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_drop_database)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_create_user",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_create_user)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_drop_user",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_drop_user)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_other_ddl",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_other_ddl)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_auth_events",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_auth_events)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_table_access_events",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_table_access_events)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {"DDL_audit_stored_program_events",
     const_cast<char *>(reinterpret_cast<volatile char *>(&number_of_stored_program_events)),
     SHOW_INT, SHOW_SCOPE_GLOBAL},
    {nullptr, nullptr, SHOW_UNDEF, SHOW_SCOPE_GLOBAL}
};

// Helper function to check if a command is DDL
static bool is_ddl_command(int sql_command_id) {
    for (size_t i = 0; i < sizeof(ddl_commands) / sizeof(ddl_commands[0]); i++) {
        if (ddl_commands[i] == sql_command_id) {
            return true;
        }
    }
    return false;
}

// Helper function to get command name from ID
static const char* get_command_name(int sql_command_id) {
    switch (sql_command_id) {
        case SQLCOM_CREATE_TABLE: return "CREATE_TABLE";
        case SQLCOM_ALTER_TABLE: return "ALTER_TABLE";
        case SQLCOM_DROP_TABLE: return "DROP_TABLE";
        case SQLCOM_CREATE_INDEX: return "CREATE_INDEX";
        case SQLCOM_DROP_INDEX: return "DROP_INDEX";
        case SQLCOM_CREATE_DB: return "CREATE_DATABASE";
        case SQLCOM_ALTER_DB: return "ALTER_DATABASE";
        case SQLCOM_DROP_DB: return "DROP_DATABASE";
        case SQLCOM_CREATE_USER: return "CREATE_USER";
        case SQLCOM_DROP_USER: return "DROP_USER";
        case SQLCOM_RENAME_USER: return "RENAME_USER";
        case SQLCOM_ALTER_USER: return "ALTER_USER";
        case SQLCOM_CREATE_FUNCTION: return "CREATE_FUNCTION";
        case SQLCOM_DROP_FUNCTION: return "DROP_FUNCTION";
        case SQLCOM_ALTER_FUNCTION: return "ALTER_FUNCTION";
        case SQLCOM_CREATE_PROCEDURE: return "CREATE_PROCEDURE";
        case SQLCOM_DROP_PROCEDURE: return "DROP_PROCEDURE";
        case SQLCOM_ALTER_PROCEDURE: return "ALTER_PROCEDURE";
        case SQLCOM_CREATE_VIEW: return "CREATE_VIEW";
        case SQLCOM_DROP_VIEW: return "DROP_VIEW";
        case SQLCOM_CREATE_TRIGGER: return "CREATE_TRIGGER";
        case SQLCOM_DROP_TRIGGER: return "DROP_TRIGGER";
        case SQLCOM_CREATE_EVENT: return "CREATE_EVENT";
        case SQLCOM_ALTER_EVENT: return "ALTER_EVENT";
        case SQLCOM_DROP_EVENT: return "DROP_EVENT";
        case SQLCOM_CREATE_SERVER: return "CREATE_SERVER";
        case SQLCOM_DROP_SERVER: return "DROP_SERVER";
        case SQLCOM_ALTER_SERVER: return "ALTER_SERVER";
        case SQLCOM_CREATE_ROLE: return "CREATE_ROLE";
        case SQLCOM_DROP_ROLE: return "DROP_ROLE";
        case SQLCOM_ALTER_TABLESPACE: return "ALTER_TABLESPACE";
        case SQLCOM_CREATE_RESOURCE_GROUP: return "CREATE_RESOURCE_GROUP";
        case SQLCOM_ALTER_RESOURCE_GROUP: return "ALTER_RESOURCE_GROUP";
        case SQLCOM_DROP_RESOURCE_GROUP: return "DROP_RESOURCE_GROUP";
        case SQLCOM_CREATE_SRS: return "CREATE_SRS";
        case SQLCOM_DROP_SRS: return "DROP_SRS";
        case SQLCOM_RENAME_TABLE: return "RENAME_TABLE";
        default: return "UNKNOWN";
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

// Helper function to extract database name from thread context
static string extract_database_name(MYSQL_THD thd) {
    // Get database from current database context
    if (thd && thd->db().str) {
        return string(thd->db().str, thd->db().length);
    }
    return "";
}

// Helper function to extract table name from query using simple parsing
static string extract_table_name(const string& query, int sql_command_id) {
    string upper_query = query;
    transform(upper_query.begin(), upper_query.end(), upper_query.begin(), ::toupper);
    
    size_t pos = string::npos;
    
    switch (sql_command_id) {
        case SQLCOM_CREATE_TABLE:
            pos = upper_query.find("CREATE TABLE");
            if (pos != string::npos) {
                pos = upper_query.find("TABLE", pos) + 5;
                // Skip "IF NOT EXISTS" if present
                size_t if_pos = upper_query.find("IF NOT EXISTS", pos);
                if (if_pos == pos + 1) {
                    pos = upper_query.find("EXISTS", if_pos) + 6;
                }
            }
            break;
            
        case SQLCOM_ALTER_TABLE:
            pos = upper_query.find("ALTER TABLE");
            if (pos != string::npos) {
                pos = upper_query.find("TABLE", pos) + 5;
            }
            break;
            
        case SQLCOM_DROP_TABLE:
            pos = upper_query.find("DROP TABLE");
            if (pos != string::npos) {
                pos = upper_query.find("TABLE", pos) + 5;
                // Skip "IF EXISTS" if present
                size_t if_pos = upper_query.find("IF EXISTS", pos);
                if (if_pos == pos + 1) {
                    pos = upper_query.find("EXISTS", if_pos) + 6;
                }
            }
            break;
            
        case SQLCOM_RENAME_TABLE:
            pos = upper_query.find("RENAME TABLE");
            if (pos != string::npos) {
                pos = upper_query.find("TABLE", pos) + 5;
            }
            break;
            
        case SQLCOM_CREATE_INDEX:
        case SQLCOM_DROP_INDEX:
            pos = upper_query.find(" ON ");
            if (pos != string::npos) {
                pos += 4; // Skip " ON "
            }
            break;
            
        default:
            return "";
    }
    
    if (pos == string::npos) {
        return "";
    }
    
    // Skip whitespace
    while (pos < query.length() && isspace(query[pos])) {
        pos++;
    }
    
    if (pos >= query.length()) {
        return "";
    }
    
    // Extract table name (handle quoted names)
    size_t start = pos;
    size_t end = pos;
    
    if (query[pos] == '`') {
        // Quoted table name
        start = pos + 1;
        end = query.find('`', start);
        if (end == string::npos) {
            return "";
        }
    } else {
        // Unquoted table name - find end of identifier
        while (end < query.length() && 
               (isalnum(query[end]) || query[end] == '_' || query[end] == '.')) {
            end++;
        }
    }
    
    if (end > start) {
        return query.substr(start, end - start);
    }
    
    return "";
}

// Helper function to get current timestamp
static string get_current_timestamp() {
    time_t now = time(0);
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    return string(buffer);
}

// Helper function to get client IP address
static string get_client_ip(MYSQL_THD thd) {
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
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
        return newLength;
    } catch (bad_alloc& e) {
        return 0;
    }
}

// Function to send DDL data to Cedar server
static bool send_to_cedar_server(const Json::Value& ddl_data) {
    if (!ddl_audit_cedar_url || strlen(ddl_audit_cedar_url) == 0) {
        if (ddl_audit_plugin) {
            my_plugin_log_message(&ddl_audit_plugin, MY_ERROR_LEVEL,
                           "DDL Audit: Cedar URL not configured");
        }
        number_of_cedar_failures++;
        return false;
    }
    
    // Increment request counter
    number_of_cedar_requests++;
    
    CURL* curl;
    CURLcode res;
    string response;
    
    curl = curl_easy_init();
    if (!curl) {
        if (ddl_audit_plugin) {
            my_plugin_log_message(&ddl_audit_plugin, MY_ERROR_LEVEL,
                           "DDL Audit: Failed to initialize curl");
        }
        number_of_cedar_failures++;
        return false;
    }
    
    // Prepare JSON payload
    Json::StreamWriterBuilder builder;
    string json_payload = Json::writeString(builder, ddl_data);
    
    // Create the full URL (append /v1/ddl_audit if not already present)
    string full_url = string(ddl_audit_cedar_url);
    if (full_url.find("/v1/ddl_audit") == string::npos) {
        if (full_url.back() != '/') {
            full_url += "/";
        }
        full_url += "v1/ddl_audit";
    }
    
    // Set curl options
    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_payload.length());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, ddl_audit_cedar_timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000);
    
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Perform the request
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        if (ddl_audit_plugin) {
            my_plugin_log_message(&ddl_audit_plugin, MY_ERROR_LEVEL,
                           "DDL Audit: curl_easy_perform() failed: %s", 
                           curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
        number_of_cedar_failures++;
        return false;
    }
    
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (response_code >= 200 && response_code < 300) {
        if (ddl_audit_plugin) {
            my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                           "DDL Audit: Successfully sent DDL data to Cedar server");
        }
        number_of_cedar_successes++;
        return true;
    } else {
        if (ddl_audit_plugin) {
            my_plugin_log_message(&ddl_audit_plugin, MY_ERROR_LEVEL,
                           "DDL Audit: Cedar server returned error code: %ld", 
                           response_code);
        }
        number_of_cedar_failures++;
        return false;
    }
}


// Helper function to create DDL data JSON
static Json::Value create_ddl_data(MYSQL_THD thd, const string& ddl_type, 
                                  enum_sql_command_t sql_command_id, const string& query,
                                  const string& database, const string& table, 
                                  const string& event_class_name, const string& event_subclass_name,
                                  const Json::Value& extra_context = Json::Value()) {
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
        const char* user_ptr = thd->security_context()->user().str;
        const char* host_ptr = thd->security_context()->host().str;
        if (user_ptr) user = string(user_ptr, thd->security_context()->user().length);
        if (host_ptr) host = string(host_ptr, thd->security_context()->host().length);
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
        for (const auto& key : extra_context.getMemberNames()) {
            context[key] = extra_context[key];
        }
    }
    
    ddl_data["context"] = context;
    return ddl_data;
}

// Main audit notification function
static int ddl_audit_notify(MYSQL_THD thd, mysql_event_class_t event_class,
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
static int handle_query_event(MYSQL_THD thd, const void *event) {
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
    
    // Get query string
    string query(event_query->query.str, event_query->query.length);
    
    // Extract information from event and context
    string database = extract_database_name(thd);
    string table = extract_table_name(query, event_query->sql_command_id);
    string command_name = get_command_name(event_query->sql_command_id);
    
    // Create DDL data
    Json::Value ddl_data = create_ddl_data(thd, command_name, event_query->sql_command_id,
                                          query, database, table,
                                          "MYSQL_AUDIT_QUERY_CLASS", "MYSQL_AUDIT_QUERY_STATUS_END");
    
    // Send to Cedar server
    if (send_to_cedar_server(ddl_data)) {
        if (ddl_audit_plugin) {
            my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                           "DDL Audit: Captured %s on database='%s', table='%s', user='%s'", 
                           command_name.c_str(), database.c_str(), table.c_str(), 
                           ddl_data["user"].asString().c_str());
        }
    }
    
    return 0;
}

// Handle MYSQL_AUDIT_AUTHENTICATION_CLASS events (user operations)
static int handle_authentication_event(MYSQL_THD thd, const void *event) {
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
            return 0; // Not interested in other auth events
    }
    
    // Get query string
    string query = auth_event->query.str ? 
                   string(auth_event->query.str, auth_event->query.length) : "";
    
    // Create extra context for authentication events
    Json::Value extra_context;
    extra_context["target_user"] = auth_event->user.str ? 
                                   string(auth_event->user.str, auth_event->user.length) : "";
    extra_context["target_host"] = auth_event->host.str ? 
                                   string(auth_event->host.str, auth_event->host.length) : "";
    extra_context["is_role"] = auth_event->is_role;
    if (auth_event->new_user.str) {
        extra_context["new_user"] = string(auth_event->new_user.str, auth_event->new_user.length);
    }
    if (auth_event->new_host.str) {
        extra_context["new_host"] = string(auth_event->new_host.str, auth_event->new_host.length);
    }
    
    // Create DDL data
    Json::Value ddl_data = create_ddl_data(thd, ddl_type, auth_event->sql_command_id,
                                          query, "", "", // No database/table for user operations
                                          "MYSQL_AUDIT_AUTHENTICATION_CLASS", subclass_name,
                                          extra_context);
    
    // Send to Cedar server
    if (send_to_cedar_server(ddl_data)) {
        if (ddl_audit_plugin) {
            my_plugin_log_message(&ddl_audit_plugin, MY_INFORMATION_LEVEL,
                           "DDL Audit: Captured %s for user='%s@%s'", 
                           ddl_type.c_str(), 
                           extra_context["target_user"].asString().c_str(),
                           extra_context["target_host"].asString().c_str());
        }
    }
    
    return 0;
}


// Handle MYSQL_AUDIT_TABLE_ACCESS_CLASS events (table operations)
static int handle_table_access_event(MYSQL_THD thd, const void *event) {
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
    string database = table_event->table_database.str ? 
                      string(table_event->table_database.str, table_event->table_database.length) : "";
    string table = table_event->table_name.str ? 
                   string(table_event->table_name.str, table_event->table_name.length) : "";
    string query = table_event->query.str ? 
                   string(table_event->query.str, table_event->query.length) : "";
    
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
    Json::Value ddl_data = create_ddl_data(thd, get_command_name(table_event->sql_command_id), 
                                          table_event->sql_command_id, query, database, table,
                                          "MYSQL_AUDIT_TABLE_ACCESS_CLASS", access_type,
                                          extra_context);
    
    // Send to Cedar server (this provides exact table access patterns)
    send_to_cedar_server(ddl_data);
    
    return 0;
}

// Handle MYSQL_AUDIT_STORED_PROGRAM_CLASS events (procedures/functions)
static int handle_stored_program_event(MYSQL_THD thd, const void *event) {
    const struct mysql_event_stored_program *prog_event =
        (const struct mysql_event_stored_program *)event;
    
    mysql_mutex_lock(&g_ddl_audit_mutex);
    number_of_stored_program_events++;
    mysql_mutex_unlock(&g_ddl_audit_mutex);
    
    string database = prog_event->database.str ? 
                      string(prog_event->database.str, prog_event->database.length) : "";
    string name = prog_event->name.str ? 
                  string(prog_event->name.str, prog_event->name.length) : "";
    string query = prog_event->query.str ? 
                   string(prog_event->query.str, prog_event->query.length) : "";
    
    // Create extra context
    Json::Value extra_context;
    extra_context["program_name"] = name;
    extra_context["program_database"] = database;
    
    // Create DDL data
    Json::Value ddl_data = create_ddl_data(thd, get_command_name(prog_event->sql_command_id), 
                                          prog_event->sql_command_id, query, database, name,
                                          "MYSQL_AUDIT_STORED_PROGRAM_CLASS", "MYSQL_AUDIT_STORED_PROGRAM_EXECUTE",
                                          extra_context);
    
    // Send to Cedar server
    send_to_cedar_server(ddl_data);
    
    return 0;
}

// Plugin initialization
static int ddl_audit_plugin_init(MYSQL_PLUGIN plugin_info) {
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
static int ddl_audit_plugin_deinit(MYSQL_PLUGIN plugin_info [[maybe_unused]]) {
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
                        "Cedar server URL for DDL audit data",
                        nullptr, nullptr, "http://localhost:8180");

static MYSQL_SYSVAR_INT(cedar_timeout, ddl_audit_cedar_timeout,
                        PLUGIN_VAR_RQCMDARG,
                        "Timeout for Cedar server requests in milliseconds",
                        nullptr, nullptr, 5000, 100, 30000, 0);

static MYSQL_SYSVAR_BOOL(enabled, ddl_audit_enabled,
                         PLUGIN_VAR_RQCMDARG,
                         "Enable/disable DDL audit plugin",
                         nullptr, nullptr, true);

static SYS_VAR *ddl_audit_system_variables[] = {
    MYSQL_SYSVAR(cedar_url),
    MYSQL_SYSVAR(cedar_timeout),
    MYSQL_SYSVAR(enabled),
    nullptr
};

// Audit plugin descriptor
static struct st_mysql_audit ddl_audit_descriptor = {
    MYSQL_AUDIT_INTERFACE_VERSION, /* interface version */
    nullptr,                       /* release_thd function */
    ddl_audit_notify,              /* notify function */
    {
        0,                                                    /* MYSQL_AUDIT_GENERAL_CLASS */
        0,                                                    /* MYSQL_AUDIT_CONNECTION_CLASS */
        0,                                                    /* MYSQL_AUDIT_PARSE_CLASS */
        0,                                                    /* MYSQL_AUDIT_AUTHORIZATION_CLASS - not supported */
        (unsigned long)MYSQL_AUDIT_TABLE_ACCESS_ALL,          /* MYSQL_AUDIT_TABLE_ACCESS_CLASS */
        0,                                                    /* MYSQL_AUDIT_GLOBAL_VARIABLE_CLASS */
        0,                                                    /* MYSQL_AUDIT_SERVER_STARTUP_CLASS */
        0,                                                    /* MYSQL_AUDIT_SERVER_SHUTDOWN_CLASS */
        0,                                                    /* MYSQL_AUDIT_COMMAND_CLASS */
        (unsigned long)MYSQL_AUDIT_QUERY_ALL,                /* MYSQL_AUDIT_QUERY_CLASS */
        (unsigned long)MYSQL_AUDIT_STORED_PROGRAM_ALL,        /* MYSQL_AUDIT_STORED_PROGRAM_CLASS */
        (unsigned long)MYSQL_AUDIT_AUTHENTICATION_ALL,       /* MYSQL_AUDIT_AUTHENTICATION_CLASS */
        0                                                     /* MYSQL_AUDIT_MESSAGE_CLASS */
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
