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

#include "plugin/authorization/authorization_common.h"
#include "plugin/authorization/cedar_authorization.h"

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
std::string privileges_to_string(unsigned long privileges) {
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

// Use shared helpers from authorization_common for JSON and mapping

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
  Json::Value json_payload = auth_common::auth_build_cedar_payload(
      user_uid_value, resource_identifier, privilege, day, date, fmt_time,
      client_ip);

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
int cedar_check_access_core(const mysql_authorization_event *event) {
  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization service called for user: %s@%s, database: %s, table: %s, column: %s, event: %s",
                          event->user.str ? event->user.str : "NULL",
                          event->host.str ? event->host.str : "NULL",
                          event->database.str ? event->database.str : "NULL",
                          event->table.str ? event->table.str : "NULL",
                          event->column.str ? event->column.str : "NULL",
                          auth_common::auth_event_type_to_string(event->event_subclass).c_str());
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

  // Build principal UID (user only)
  std::string user_uid_value = auth_common::auth_build_user_uid(event);

  // Create resource identifier
  std::string resource_identifier = auth_common::auth_create_resource_identifier(event);

  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization check: user=%s, resource=%s, privileges=%lu",
                          user_uid_value.c_str(), resource_identifier.c_str(), event->privileges);
  }

  // Get context information
  std::string day = auth_common::auth_get_day();
  auto date = auth_common::auth_get_date();
  auto fmt_time = auth_common::auth_get_time();
  std::string client_ip = auth_common::auth_get_client_ip(event->thd);

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

// Create resource identifier based on event type (exposed for tests)
std::string cedar_create_resource_identifier(const mysql_authorization_event *event) {
  return auth_common::auth_create_resource_identifier(event);
}

// Main authorization callback function
mysql_authorization_result_t cedar_check(
    const mysql_authorization_event *event) {
  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization callback invoked for user: %s@%s, event: %s, database: %s, table: %s, column: %s, privileges: %lu",
                          event->user.str ? event->user.str : "NULL",
                          event->host.str ? event->host.str : "NULL",
                          auth_common::auth_event_type_to_string(event->event_subclass).c_str(),
                          event->database.str ? event->database.str : "NULL",
                          event->table.str ? event->table.str : "NULL",
                          event->column.str ? event->column.str : "NULL",
                          (unsigned long)event->privileges);
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

  // Check if this is a supported event type
  if (event->event_subclass != MYSQL_AUTHORIZATION_DB_ACCESS &&
      event->event_subclass != MYSQL_AUTHORIZATION_TABLE_ACCESS &&
      event->event_subclass != MYSQL_AUTHORIZATION_COLUMN_ACCESS &&
      event->event_subclass != MYSQL_AUTHORIZATION_ROUTINE_ACCESS) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: unsupported event type %s, returning IGNORE",
                            auth_common::auth_event_type_to_string(event->event_subclass).c_str());
    }
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  int result = cedar_check_access_core(event);

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
    MYSQL_AUTHORIZATION_INTERFACE_VERSION, cedar_check};

// Plugin initialization
int cedar_authorization_init(MYSQL_PLUGIN plugin_info) {
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
int cedar_authorization_deinit(MYSQL_PLUGIN plugin_info [[maybe_unused]]) {
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

// No test-specific wrappers; tests include the public header and call directly
