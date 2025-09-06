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
  @file plugin/authorization/external_authorization.cc
  
  External Authorization Plugin Example
  
  This plugin demonstrates how to implement a custom authorization plugin
  that can delegate authorization decisions to an external server.
  
  Features:
  - Delegates authorization decisions to an external HTTP service
  - Supports all authorization event types (DB, Table, Column, Routine)
  - Configurable external server URL
  - Timeout handling and fallback behavior
  - Detailed logging for debugging
  
  Configuration:
  INSTALL PLUGIN external_authorization SONAME 'external_authorization.so';
  SET GLOBAL external_authorization_url = 'http://localhost:8080/auth';
  SET GLOBAL external_authorization_timeout = 5000;  -- milliseconds
  
  External Service API:
  The plugin sends POST requests to the configured URL with JSON payload:
  {
    "user": "username",
    "host": "hostname", 
    "database": "dbname",
    "table": "tablename",
    "column": "columnname",
    "routine": "routinename",
    "privileges": 123,
    "event_type": "db_access|table_access|column_access|routine_access",
    "sql_command": "SELECT",
    "query": "SELECT * FROM table1",
    "is_procedure": false
  }
  
  Expected response:
  {
    "result": "grant|deny|ignore"
  }
*/

#include <mysql/plugin.h>
#include <mysql/plugin_authorization.h>
#include <mysql/service_mysql_alloc.h>
#include <mysql/service_my_plugin_log.h>

#include <string>
#include <sstream>
#include <curl/curl.h>
#include <json/json.h>

// Plugin system variables
static char *external_authorization_url;
static int external_authorization_timeout = 5000; // milliseconds

// Plugin initialization flag
static bool plugin_initialized = false;
// Saved plugin handle for logging
static MYSQL_PLUGIN plugin_handle = nullptr;

// Helper structure for HTTP response
struct HttpResponse {
  std::string data;
};

// Callback function for libcurl to write response data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, HttpResponse *response) {
  size_t total_size = size * nmemb;
  response->data.append(static_cast<char*>(contents), total_size);
  return total_size;
}

// Convert privileges bitmask to string for logging
static std::string privileges_to_string(unsigned long privileges) {
  std::ostringstream oss;
  if (privileges & 1) oss << "SELECT ";
  if (privileges & 2) oss << "INSERT ";
  if (privileges & 4) oss << "UPDATE ";
  if (privileges & 8) oss << "DELETE ";
  if (privileges & 16) oss << "CREATE ";
  if (privileges & 32) oss << "DROP ";
  if (privileges & 64) oss << "RELOAD ";
  if (privileges & 128) oss << "SHUTDOWN ";
  if (privileges & 256) oss << "PROCESS ";
  if (privileges & 512) oss << "FILE ";
  if (privileges & 1024) oss << "GRANT ";
  // ... add more as needed
  return oss.str();
}

// Convert event subclass to string
static std::string event_type_to_string(mysql_authorization_event_subclass_t event_type) {
  switch (event_type) {
    case MYSQL_AUTHORIZATION_DB_ACCESS: return "db_access";
    case MYSQL_AUTHORIZATION_TABLE_ACCESS: return "table_access";
    case MYSQL_AUTHORIZATION_COLUMN_ACCESS: return "column_access";
    case MYSQL_AUTHORIZATION_ROUTINE_ACCESS: return "routine_access";
    default: return "unknown";
  }
}

// Send authorization request to external server
static mysql_authorization_result_t call_external_service(
    const mysql_authorization_event *event) {
    
  if (!external_authorization_url || strlen(external_authorization_url) == 0) {
    // Note: Cannot use plugin reference here since we're in callback context
    // Log message will be handled through other means if needed
    return MYSQL_AUTHORIZATION_IGNORE;
  }
  
  // Initialize libcurl
  CURL *curl = curl_easy_init();
  if (!curl) {
    // Log error through other means if needed
    return MYSQL_AUTHORIZATION_IGNORE;
  }
  
  // Create JSON payload
  Json::Value json_payload;
  json_payload["user"] = event->user.str ? event->user.str : "";
  json_payload["host"] = event->host.str ? event->host.str : "";
  json_payload["database"] = event->database.str ? event->database.str : "";
  json_payload["table"] = event->table.str ? event->table.str : "";
  json_payload["column"] = event->column.str ? event->column.str : "";
  json_payload["routine"] = event->routine.str ? event->routine.str : "";
  json_payload["privileges"] = static_cast<int>(event->privileges);
  json_payload["event_type"] = event_type_to_string(event->event_subclass);
  json_payload["sql_command"] = event->sql_command;
  json_payload["query"] = event->query.str ? event->query.str : "";
  json_payload["is_procedure"] = event->is_procedure;
  
  Json::StreamWriterBuilder builder;
  std::string json_string = Json::writeString(builder, json_payload);
  
  // Log authorization request if needed
  
  // Configure curl options
  HttpResponse response;
  curl_easy_setopt(curl, CURLOPT_URL, external_authorization_url);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_string.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, external_authorization_timeout);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  
  // Perform the request
  CURLcode res = curl_easy_perform(curl);
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  
  // Cleanup
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  
  if (res != CURLE_OK) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "External authorization request failed: %s",
                            curl_easy_strerror(res));
    return MYSQL_AUTHORIZATION_IGNORE;
  }
  
  if (response_code != 200) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "External authorization server returned HTTP %ld",
                            response_code);
    return MYSQL_AUTHORIZATION_IGNORE;
  }
  
  // Parse response
  Json::Value json_response;
  Json::CharReaderBuilder reader_builder;
  std::string parse_errors;
  std::istringstream response_stream(response.data);
  
  if (!Json::parseFromStream(reader_builder, response_stream, &json_response, &parse_errors)) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "Failed to parse external authorization response: %s",
                            parse_errors.c_str());
    return MYSQL_AUTHORIZATION_IGNORE;
  }
  
  if (!json_response.isMember("result")) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "External authorization response missing 'result' field");
    return MYSQL_AUTHORIZATION_IGNORE;
  }
  
  std::string result = json_response["result"].asString();
  if (plugin_handle)
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "External authorization result: %s",
                          result.c_str());
  
  if (result == "grant") {
    return MYSQL_AUTHORIZATION_GRANT;
  } else if (result == "deny") {
    return MYSQL_AUTHORIZATION_DENY;
  } else {
    return MYSQL_AUTHORIZATION_IGNORE;
  }
}

// Main authorization callback function
static mysql_authorization_result_t external_authorization_check(
    const mysql_authorization_event *event) {
    
  if (!plugin_initialized) {
    return MYSQL_AUTHORIZATION_IGNORE;
  }
  
  return call_external_service(event);
}

// Plugin descriptor
static st_mysql_authorization external_authorization_descriptor = {
  MYSQL_AUTHORIZATION_INTERFACE_VERSION,
  external_authorization_check
};

// Plugin initialization
static int external_authorization_init(MYSQL_PLUGIN plugin_info) {
  // Initialize libcurl globally
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
    // Log error through other means if needed
    return 1;
  }

  // Save plugin handle for logging
  plugin_handle = plugin_info;
  plugin_initialized = true;
  // Log message will be handled through other means if needed
  return 0;
}

// Plugin deinitialization
static int external_authorization_deinit(MYSQL_PLUGIN plugin_info [[maybe_unused]]) {
  plugin_initialized = false;
  curl_global_cleanup();
  plugin_handle = nullptr;
  // Log message will be handled through other means if needed
  return 0;
}

// System variables
static MYSQL_SYSVAR_STR(
  url,                                         // name
  external_authorization_url,                  // var
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,  // flags
  "URL of external authorization service",     // comment
  nullptr,                                     // check
  nullptr,                                     // update
  nullptr                                      // default
);

static MYSQL_SYSVAR_INT(
  timeout,                                     // name
  external_authorization_timeout,              // var
  PLUGIN_VAR_RQCMDARG,                        // flags
  "Timeout for external authorization requests in milliseconds", // comment
  nullptr,                                     // check
  nullptr,                                     // update
  5000,                                        // default
  1000,                                        // min
  60000,                                       // max
  0                                           // block_size
);

// System variables array
static SYS_VAR *external_authorization_system_vars[] = {
  MYSQL_SYSVAR(url),
  MYSQL_SYSVAR(timeout),
  nullptr
};

// Plugin declaration
mysql_declare_plugin(external_authorization) {
  MYSQL_AUTHORIZATION_PLUGIN,              // type
  &external_authorization_descriptor,      // descriptor
  "external_authorization",                // name
  PLUGIN_AUTHOR_ORACLE,                   // author
  "External Authorization Plugin Example", // description
  PLUGIN_LICENSE_GPL,                     // license
  external_authorization_init,            // init function
  nullptr,                                // check_uninstall
  external_authorization_deinit,          // deinit function
  0x0100,                                // version
  nullptr,                               // status vars
  external_authorization_system_vars,     // system vars
  nullptr,                               // config options
  0,                                     // flags
}
mysql_declare_plugin_end;
