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
#include <mysql/service_my_plugin_log.h>
#include <mysql/service_mysql_alloc.h>

#include <curl/curl.h>
#include <json/json.h>
#include <json/value.h>
#include <sstream>
#include <string>

// Plugin system variables
static char *external_authorization_url;
static int external_authorization_timeout = 5000;  // milliseconds

// SSL/TLS configuration variables (disabled by default for HTTP)
static bool external_authorization_ssl_verify_peer = false;
static bool external_authorization_ssl_verify_host = false;
static char *external_authorization_ssl_ca_file = nullptr;
static char *external_authorization_ssl_cert_file = nullptr;
static char *external_authorization_ssl_key_file = nullptr;

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

static Json::Value privileges_to_json(unsigned long privileges) {
  Json::Value privileges_value(Json::arrayValue);
  for (const auto &[privilege, offset] : privs::global_acls_map) {
    if (privileges & (1 << offset)) {
      privileges_value.append(privilege);
    }
  }
  return privileges_value;
}

static const char *requirement_mode_to_string(
    mysql_authorization_event::mysql_authorization_requirement_t mode) {
  switch (mode) {
    case mysql_authorization_event::MYSQL_AUTHZ_REQ_ALL_OF:
      return "all";
    case mysql_authorization_event::MYSQL_AUTHZ_REQ_ANY_OF:
      return "any";
    case mysql_authorization_event::MYSQL_AUTHZ_REQ_PRESENCE:
      return "presence";
    case mysql_authorization_event::MYSQL_AUTHZ_REQ_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

// Convert event subclass to string
static std::string event_type_to_string(
    mysql_authorization_event_subclass_t event_type) {
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

// Send authorization request to external server
static mysql_authorization_result_t call_external_service(
    const mysql_authorization_event *event) {
  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "External authorization service called for user: "
                          "%s@%s, database: %s, table: %s, event: %s",
                          event->user.str ? event->user.str : "NULL",
                          event->host.str ? event->host.str : "NULL",
                          event->database.str ? event->database.str : "NULL",
                          event->table.str ? event->table.str : "NULL",
                          event_type_to_string(event->event_subclass).c_str());
  }

  if (!external_authorization_url || strlen(external_authorization_url) == 0) {
    if (plugin_handle) {
      my_plugin_log_message(
          &plugin_handle, MY_WARNING_LEVEL,
          "External authorization URL not configured, returning IGNORE");
    }
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "External authorization URL configured: %s, timeout: %d ms",
        external_authorization_url, external_authorization_timeout);
  }

  // Initialize libcurl
  CURL *curl = curl_easy_init();
  if (!curl) {
    if (plugin_handle) {
      my_plugin_log_message(
          &plugin_handle, MY_ERROR_LEVEL,
          "Failed to initialize libcurl for external authorization");
    }
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
  json_payload["privileges"] = privileges_to_json(event->privileges);
  json_payload["missing_privileges"] =
      privileges_to_json(event->missing_privileges);
  json_payload["event_type"] = event_type_to_string(event->event_subclass);
  json_payload["requirement_mode"] =
      requirement_mode_to_string(event->requirement_mode);
  json_payload["sql_command"] = event->sql_command;
  json_payload["query"] = event->query.str ? event->query.str : "";
  json_payload["is_procedure"] = event->is_procedure;

  Json::StreamWriterBuilder builder;
  std::string json_string = Json::writeString(builder, json_payload);

  // Log the complete request payload
  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Sending external authorization request to %s",
                          external_authorization_url);
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Request payload: %s", json_string.c_str());
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Privileges string: %s",
                          privileges_to_string(event->privileges).c_str());
  }

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

  // Configure SSL/TLS options
  if (external_authorization_ssl_verify_peer) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  } else {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "SSL peer verification disabled");
    }
  }

  if (external_authorization_ssl_verify_host) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  } else {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "SSL host verification disabled");
    }
  }

  if (external_authorization_ssl_ca_file &&
      strlen(external_authorization_ssl_ca_file) > 0) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, external_authorization_ssl_ca_file);
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Using CA certificate file: %s",
                            external_authorization_ssl_ca_file);
    }
  }

  if (external_authorization_ssl_cert_file &&
      strlen(external_authorization_ssl_cert_file) > 0) {
    curl_easy_setopt(curl, CURLOPT_SSLCERT,
                     external_authorization_ssl_cert_file);
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Using client certificate file: %s",
                            external_authorization_ssl_cert_file);
    }
  }

  if (external_authorization_ssl_key_file &&
      strlen(external_authorization_ssl_key_file) > 0) {
    curl_easy_setopt(curl, CURLOPT_SSLKEY, external_authorization_ssl_key_file);
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Using client private key file: %s",
                            external_authorization_ssl_key_file);
    }
  }

  // Perform the request
  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Performing HTTP request to external service...");
  }

  CURLcode res = curl_easy_perform(curl);
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  // Log response details
  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "HTTP request completed. cURL result: %d, HTTP code: %ld", res,
        response_code);
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Response body: %s", response.data.c_str());
  }

  // Cleanup
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    if (plugin_handle)
      my_plugin_log_message(
          &plugin_handle, MY_ERROR_LEVEL,
          "External authorization request failed: %s (cURL error: %d)",
          curl_easy_strerror(res), res);
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  if (response_code != 200) {
    if (plugin_handle)
      my_plugin_log_message(
          &plugin_handle, MY_WARNING_LEVEL,
          "External authorization server returned HTTP %ld, response: %s",
          response_code, response.data.c_str());
    return MYSQL_AUTHORIZATION_IGNORE;
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
          "Failed to parse external authorization response: %s",
          parse_errors.c_str());
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  if (!json_response.isMember("result")) {
    if (plugin_handle)
      my_plugin_log_message(
          &plugin_handle, MY_WARNING_LEVEL,
          "External authorization response missing 'result' field");
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  std::string result = json_response["result"].asString();

  mysql_authorization_result_t final_decision;
  if (result == "grant") {
    final_decision = MYSQL_AUTHORIZATION_GRANT;
  } else if (result == "deny") {
    final_decision = MYSQL_AUTHORIZATION_DENY;
  } else {
    final_decision = MYSQL_AUTHORIZATION_IGNORE;
  }

  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "External authorization result: '%s' -> Decision: %s", result.c_str(),
        final_decision == MYSQL_AUTHORIZATION_GRANT  ? "GRANT"
        : final_decision == MYSQL_AUTHORIZATION_DENY ? "DENY"
                                                     : "IGNORE");
  }

  return final_decision;
}

// Main authorization callback function
static mysql_authorization_result_t external_authorization_check(
    const mysql_authorization_event *event) {
  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "External authorization callback invoked! Plugin initialized: %s",
        plugin_initialized ? "YES" : "NO");
  }

  if (!plugin_initialized) {
    if (plugin_handle) {
      my_plugin_log_message(
          &plugin_handle, MY_WARNING_LEVEL,
          "External authorization plugin not initialized, returning IGNORE");
    }
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Proceeding with external authorization check...");
  }

  return call_external_service(event);
}

// Plugin descriptor
static st_mysql_authorization external_authorization_descriptor = {
    MYSQL_AUTHORIZATION_INTERFACE_VERSION, external_authorization_check};

// Plugin initialization
static int external_authorization_init(MYSQL_PLUGIN plugin_info) {
  // Save plugin handle for logging first
  plugin_handle = plugin_info;

  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "External authorization plugin initialization starting...");
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
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "External authorization plugin successfully initialized!");
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Plugin will check external_authorization_url system "
                          "variable for service URL");
  }

  return 0;
}

// Plugin deinitialization
static int external_authorization_deinit(MYSQL_PLUGIN plugin_info
                                         [[maybe_unused]]) {
  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "External authorization plugin deinitialization starting...");
  }

  plugin_initialized = false;
  curl_global_cleanup();

  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "External authorization plugin successfully deinitialized");
  }

  plugin_handle = nullptr;
  return 0;
}

// System variables
static MYSQL_SYSVAR_STR(url,                                        // name
                        external_authorization_url,                 // var
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,  // flags
                        "URL of external authorization service",    // comment
                        nullptr,                                    // check
                        nullptr,                                    // update
                        nullptr                                     // default
);

static MYSQL_SYSVAR_INT(
    timeout,                                                        // name
    external_authorization_timeout,                                 // var
    PLUGIN_VAR_RQCMDARG,                                            // flags
    "Timeout for external authorization requests in milliseconds",  // comment
    nullptr,                                                        // check
    nullptr,                                                        // update
    5000,                                                           // default
    1000,                                                           // min
    60000,                                                          // max
    0  // block_size
);

static MYSQL_SYSVAR_BOOL(
    ssl_verify_peer, external_authorization_ssl_verify_peer,
    PLUGIN_VAR_RQCMDARG,
    "Enable SSL peer certificate verification for HTTPS connections (default: "
    "disabled)",
    nullptr, nullptr, false);

static MYSQL_SYSVAR_BOOL(
    ssl_verify_host, external_authorization_ssl_verify_host,
    PLUGIN_VAR_RQCMDARG,
    "Enable SSL host name verification for HTTPS connections (default: "
    "disabled)",
    nullptr, nullptr, false);

static MYSQL_SYSVAR_STR(ssl_ca_file, external_authorization_ssl_ca_file,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
                        "Path to CA certificate file for SSL/TLS verification",
                        nullptr, nullptr, nullptr);

static MYSQL_SYSVAR_STR(
    ssl_cert_file, external_authorization_ssl_cert_file,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Path to client certificate file for mutual TLS authentication", nullptr,
    nullptr, nullptr);

static MYSQL_SYSVAR_STR(
    ssl_key_file, external_authorization_ssl_key_file,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Path to client private key file for mutual TLS authentication", nullptr,
    nullptr, nullptr);

// System variables array
static SYS_VAR *external_authorization_system_vars[] = {
    MYSQL_SYSVAR(url),
    MYSQL_SYSVAR(timeout),
    MYSQL_SYSVAR(ssl_verify_peer),
    MYSQL_SYSVAR(ssl_verify_host),
    MYSQL_SYSVAR(ssl_ca_file),
    MYSQL_SYSVAR(ssl_cert_file),
    MYSQL_SYSVAR(ssl_key_file),
    nullptr};

// Plugin declaration
mysql_declare_plugin(external_authorization){
    MYSQL_AUTHORIZATION_PLUGIN,               // type
    &external_authorization_descriptor,       // descriptor
    "external_authorization",                 // name
    PLUGIN_AUTHOR_ORACLE,                     // author
    "External Authorization Plugin Example",  // description
    PLUGIN_LICENSE_GPL,                       // license
    external_authorization_init,              // init function
    nullptr,                                  // check_uninstall
    external_authorization_deinit,            // deinit function
    0x0100,                                   // version
    nullptr,                                  // status vars
    external_authorization_system_vars,       // system vars
    nullptr,                                  // config options
    0,                                        // flags
} mysql_declare_plugin_end;
