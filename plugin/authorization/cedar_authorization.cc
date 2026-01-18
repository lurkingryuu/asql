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
  The plugin sends separate POST requests to /v1/is_authorized for each
  privilege:
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
#include <json/value.h>
#include <sstream>
#include <string>
#include <vector>

#include "plugin/authorization/authorization_common.h"
#include "plugin/authorization/cedar_authorization.h"

#include "sql/auth/auth_acls.h"
#include "sql/sql_class.h"
// Needed for Protocol_classic definition used via THD
#include "my_dbug.h"
#include "sql/protocol_classic.h"
#include "violite.h"

#include <algorithm>
#include <cctype>
#include <ctime>
using namespace std;

// Plugin system variables
static char *cedar_authorization_url;
static char *cedar_authorization_namespace;
static int cedar_authorization_timeout = 5000;  // milliseconds

// Caching system variables
static bool cedar_authorization_cache_enabled = true;
static int cedar_authorization_cache_size = 1024;
static int cedar_authorization_cache_ttl = 300;  // seconds
static bool cedar_authorization_cache_flush = false;

// Cache implementation
#include <mutex>
#include <unordered_map>

// Cache structures
struct AuthCacheKey {
  std::string user;
  std::string resource;
  std::string action;
  std::string day;
  uint32_t date;
  uint32_t time;
  std::string ip;

  bool operator==(const AuthCacheKey &other) const {
    return user == other.user && resource == other.resource &&
           action == other.action && day == other.day && date == other.date &&
           time == other.time && ip == other.ip;
  }
};

struct AuthCacheEntry {
  int result;  // -1 (IGNORE), 0 (DENY), 1 (GRANT)
  std::time_t expires;
};

struct AuthCacheKeyHash {
  std::size_t operator()(const AuthCacheKey &k) const {
    size_t h = std::hash<std::string>{}(k.user);
    h ^=
        std::hash<std::string>{}(k.resource) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(k.action) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(k.day) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(k.date) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(k.time) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(k.ip) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

static std::unordered_map<AuthCacheKey, AuthCacheEntry, AuthCacheKeyHash>
    auth_cache;
static mysql_mutex_t LOCK_auth_cache;

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

#ifdef EXTRA_CODE_FOR_UNIT_TESTING
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
#endif

// Use shared helpers from authorization_common for JSON and mapping

// Check a single privilege with Cedar authorization service
static int check_single_privilege_cedar(
    const std::string &user_uid_value, const std::string &resource_identifier,
    const std::string &privilege, const std::string &day, uint32_t date,
    uint32_t fmt_time, const std::string &fmt_ip, const std::string &ns) {
  // Check cache if enabled
  if (cedar_authorization_cache_enabled) {
    AuthCacheKey key{
        user_uid_value, resource_identifier, privilege, day, date, fmt_time,
        fmt_ip};
    std::time_t now = std::time(nullptr);

    mysql_mutex_lock(&LOCK_auth_cache);
    auto it = auth_cache.find(key);
    if (it != auth_cache.end()) {
      if (now < it->second.expires) {
        int result = it->second.result;
        mysql_mutex_unlock(&LOCK_auth_cache);
        if (plugin_handle) {
          my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                                "Cache hit for privilege %s: %d",
                                privilege.c_str(), result);
        }
        return result;
      } else {
        // Expired
        auth_cache.erase(it);
      }
    }
    mysql_mutex_unlock(&LOCK_auth_cache);
  }

  // Check if URL is configured
  if (!cedar_authorization_url || strlen(cedar_authorization_url) == 0) {
    if (plugin_handle) {
      my_plugin_log_message(
          &plugin_handle, MY_WARNING_LEVEL,
          "Cedar authorization URL not configured; returning IGNORE");
    }
    return -1;  // signal IGNORE
  }

#ifdef EXTRA_CODE_FOR_UNIT_TESTING
  // Support for mock URLs in tests to verify caching logic without network
  if (strcmp(cedar_authorization_url, "http://mock-allow") == 0) {
    int result = 1;
    if (cedar_authorization_cache_enabled) {
      AuthCacheKey key{
          user_uid_value, resource_identifier, privilege, day, date, fmt_time,
          fmt_ip};
      std::time_t now = std::time(nullptr);
      AuthCacheEntry entry{result, now + cedar_authorization_cache_ttl};
      mysql_mutex_lock(&LOCK_auth_cache);
      auth_cache[key] = entry;
      mysql_mutex_unlock(&LOCK_auth_cache);
    }
    return result;
  }
  if (strcmp(cedar_authorization_url, "http://mock-deny") == 0) {
    int result = 0;
    if (cedar_authorization_cache_enabled) {
      AuthCacheKey key{
          user_uid_value, resource_identifier, privilege, day, date, fmt_time,
          fmt_ip};
      std::time_t now = std::time(nullptr);
      AuthCacheEntry entry{result, now + cedar_authorization_cache_ttl};
      mysql_mutex_lock(&LOCK_auth_cache);
      auth_cache[key] = entry;
      mysql_mutex_unlock(&LOCK_auth_cache);
    }
    return result;
  }
#endif

  // Initialize libcurl
  CURL *curl = curl_easy_init();
  if (!curl) {
    if (plugin_handle) {
      my_plugin_log_message(
          &plugin_handle, MY_ERROR_LEVEL,
          "Failed to initialize libcurl for Cedar authorization");
    }
    return 0;
  }

  // Create JSON payload for single privilege
  Json::Value json_payload = auth_common::auth_build_cedar_payload(
      user_uid_value, resource_identifier, privilege, day, date, fmt_time,
      fmt_ip, ns);

  Json::StreamWriterBuilder builder;
  std::string json_string = Json::writeString(builder, json_payload);

  // Log the request payload
  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "Sending Cedar authorization request for privilege: %s",
        privilege.c_str());
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
                          "HTTP request completed for privilege %s. cURL "
                          "result: %d, HTTP code: %ld",
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
                            "Cedar authorization request failed for privilege "
                            "%s: %s (cURL error: %d)",
                            privilege.c_str(), curl_easy_strerror(res), res);
    return -1;  // Signal error
  }

  if (response_code != 200) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "Cedar authorization server returned HTTP %ld for "
                            "privilege %s, response: %s",
                            response_code, privilege.c_str(),
                            response.data.c_str());
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
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "Cedar authorization response missing 'decision' "
                            "field for privilege %s",
                            privilege.c_str());
    return -1;  // Signal error
  }

  std::string decision = json_response["decision"].asString();
  int result = (decision == "Allow") ? 1 : 0;

  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization result for privilege %s: '%s'",
                          privilege.c_str(), decision.c_str());
  }

  // Update cache if enabled
  if (cedar_authorization_cache_enabled) {
    AuthCacheKey key{
        user_uid_value, resource_identifier, privilege, day, date, fmt_time,
        fmt_ip};
    std::time_t now = std::time(nullptr);
    AuthCacheEntry entry{result, now + cedar_authorization_cache_ttl};

    mysql_mutex_lock(&LOCK_auth_cache);
    // Simple eviction if full: clear half the cache or just one?
    // For now, if we exceed size, we just clear it all to be safe and simple.
    if (auth_cache.size() >= (size_t)cedar_authorization_cache_size) {
      if (plugin_handle) {
        my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                              "Cache full, clearing %zu entries",
                              auth_cache.size());
      }
      auth_cache.clear();
    }
    auth_cache[key] = entry;
    mysql_mutex_unlock(&LOCK_auth_cache);
  }

  return result;
}

// Access check core returns -1 (IGNORE), 0 (DENY), 1 (GRANT for all privs)
static int cedar_check_access_core(const mysql_authorization_event *event) {
  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "Cedar authorization service called for user: %s@%s, database: %s, "
        "table: %s, column: %s, event: %s",
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
      my_plugin_log_message(
          &plugin_handle, MY_WARNING_LEVEL,
          "Cedar authorization URL not configured; caller should IGNORE");
    }
    return -1;  // signal IGNORE
  }

  // Build principal UID (user only)
  std::string user_uid_value = auth_common::auth_build_user_uid(event);

  // Create resource identifier (without namespace - auth_build_cedar_payload
  // adds it)
  std::string ns =
      cedar_authorization_namespace ? cedar_authorization_namespace : "MySQL";
  std::string resource_identifier =
      auth_common::auth_create_resource_identifier(event, "");

  if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization check: user=%s, resource=%s, "
                          "privileges=%lu, namespace=%s",
                          user_uid_value.c_str(), resource_identifier.c_str(),
                          event->privileges, ns.c_str());
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
  // We need to make separate requests for each privilege since Cedar only
  // handles one action per request

  // Static list of standard MySQL privileges
  static const std::pair<const char *, int> kStandardPrivileges[] = {
      {"SELECT", 0},
      {"INSERT", 1},
      {"UPDATE", 2},
      {"DELETE", 3},
      {"CREATE", 4},
      {"DROP", 5},
      {"RELOAD", 6},
      {"SHUTDOWN", 7},
      {"PROCESS", 8},
      {"FILE", 9},
      {"GRANT", 10},
      {"REFERENCES", 11},
      {"INDEX", 12},
      {"ALTER", 13},
      {"SHOW DATABASES", 14},
      {"SUPER", 15},
      {"CREATE TEMPORARY TABLES", 16},
      {"LOCK TABLES", 17},
      {"EXECUTE", 18},
      {"REPLICATION SLAVE", 19},
      {"REPLICATION CLIENT", 20},
      {"CREATE VIEW", 21},
      {"SHOW VIEW", 22},
      {"CREATE ROUTINE", 23},
      {"ALTER ROUTINE", 24},
      {"CREATE USER", 25},
      {"EVENT", 26},
      {"TRIGGER", 27},
      {"CREATE TABLESPACE", 28},
      {"CREATE ROLE", 29},
      {"DROP ROLE", 30}};

  // Collect all privileges that need to be checked
  std::vector<std::pair<std::string, int>> privs_to_check;

  // 1. Standard privileges
  for (const auto &p : kStandardPrivileges) {
    if (event->privileges & (1UL << p.second)) {
      privs_to_check.push_back({p.first, p.second});
    }
  }

  // 2. Extra privileges from map
  for (const auto &[privilege, offset] : privs::global_acls_map) {
    if (event->privileges & (1UL << offset)) {
      // Avoid duplicates if standard privs are also in the map (unlikely but
      // safe)
      bool exists = false;
      for (const auto &existing : privs_to_check) {
        if (existing.second == offset) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        privs_to_check.push_back({privilege, offset});
      }
    }
  }

  bool is_any_of = (event->requirement_mode ==
                    mysql_authorization_event::MYSQL_AUTHZ_REQ_ANY_OF);
  bool authorized = !is_any_of;  // Default true for ALL_OF, false for ANY_OF

  bool any_implied = false;  // Track if we checked any privilege

  for (const auto &p : privs_to_check) {
    const std::string &privilege = p.first;

    any_implied = true;
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Checking Cedar authorization for privilege: %s",
                            privilege.c_str());
    }

    // Make individual Cedar request for this privilege
    int privilege_result = check_single_privilege_cedar(
        user_uid_value, resource_identifier, privilege, day, date, fmt_time,
        client_ip, ns);

    if (privilege_result == -1) {
      // Error occurred, return IGNORE
      if (plugin_handle) {
        my_plugin_log_message(&plugin_handle, MY_ERROR_LEVEL,
                              "Cedar authorization error for privilege: %s",
                              privilege.c_str());
      }
      return -1;  // Signal IGNORE
    }

    if (is_any_of) {
      if (privilege_result == 1) {
        // ANY_OF: One success is enough
        authorized = true;
        if (plugin_handle) {
          my_plugin_log_message(
              &plugin_handle, MY_INFORMATION_LEVEL,
              "Cedar allowed privilege %s in ANY_OF mode -> GRANT",
              privilege.c_str());
        }
        break;
      }
    } else {
      // ALL_OF: One failure is enough to fail
      if (privilege_result == 0) {
        authorized = false;
        if (plugin_handle) {
          my_plugin_log_message(
              &plugin_handle, MY_INFORMATION_LEVEL,
              "Cedar denied privilege %s in ALL_OF mode -> DENY",
              privilege.c_str());
        }
        // We could break here, but logging all denials might be useful.
        // For performance, let's break.
        break;
      }
    }
  }
  if (!any_implied) {
    // No privileges checked? Should be GRANT (handled by zero-priv check
    // earlier usually)
    return 1;
  }

  if (authorized) {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: Access GRANTED");
    }
    return 1;
  } else {
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: Access DENIED");
    }
    return 0;
  }
}

#ifdef EXTRA_CODE_FOR_UNIT_TESTING
// Create resource identifier based on event type (exposed for tests)
std::string cedar_create_resource_identifier(
    const mysql_authorization_event *event) {
  std::string ns =
      cedar_authorization_namespace ? cedar_authorization_namespace : "MySQL";
  return auth_common::auth_create_resource_identifier(event, ns);
}
#endif

// Main authorization callback function
mysql_authorization_result_t cedar_check(
    const mysql_authorization_event *event) {
  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "Cedar authorization callback invoked for user: %s@%s, event: %s, "
        "database: %s, table: %s, column: %s, privileges: %lu",
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
      my_plugin_log_message(
          &plugin_handle, MY_WARNING_LEVEL,
          "Cedar authorization plugin not initialized, returning IGNORE");
    }
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  // Handle presence checks by allowing them (these are MySQL's internal
  // discovery probes)
  if (event->requirement_mode ==
      mysql_authorization_event::MYSQL_AUTHZ_REQ_PRESENCE) {
    if (plugin_handle) {
      my_plugin_log_message(
          &plugin_handle, MY_INFORMATION_LEVEL,
          "Cedar authorization: presence check -> GRANT (internal probe)");
    }
    return MYSQL_AUTHORIZATION_GRANT;
  }

  // Handle zero-privilege checks by allowing them (these are also internal
  // checks)
  if (event->privileges == 0) {
    if (plugin_handle) {
      my_plugin_log_message(
          &plugin_handle, MY_INFORMATION_LEVEL,
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
      my_plugin_log_message(
          &plugin_handle, MY_INFORMATION_LEVEL,
          "Cedar authorization: unsupported event type %s, returning IGNORE",
          auth_common::auth_event_type_to_string(event->event_subclass)
              .c_str());
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
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
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

  // Initialize cache mutex
  mysql_mutex_init(0, &LOCK_auth_cache, MY_MUTEX_INIT_FAST);

  plugin_initialized = true;

  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "Cedar authorization plugin successfully initialized!");
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Plugin will check cedar_authorization_url system "
                          "variable for service URL");
  }

  return 0;
}

// Plugin deinitialization
int cedar_authorization_deinit(MYSQL_PLUGIN plugin_info [[maybe_unused]]) {
  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "Cedar authorization plugin deinitialization starting...");
  }

  plugin_initialized = false;
  curl_global_cleanup();

  // Destroy cache mutex and clear cache
  mysql_mutex_destroy(&LOCK_auth_cache);
  auth_cache.clear();

  if (plugin_handle) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "Cedar authorization plugin successfully deinitialized");
  }

  plugin_handle = nullptr;
  return 0;
}

// System variables
static MYSQL_SYSVAR_STR(url,                                        // name
                        cedar_authorization_url,                    // var
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,  // flags
                        "URL of Cedar authorization service",       // comment
                        nullptr,                                    // check
                        nullptr,                                    // update
                        nullptr                                     // default
);

static MYSQL_SYSVAR_STR(
    namespace, cedar_authorization_namespace,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Namespace for Cedar authorization (e.g., MySQL, MariaDB)", nullptr,
    nullptr, "MySQL");

static MYSQL_SYSVAR_INT(
    timeout,                                                     // name
    cedar_authorization_timeout,                                 // var
    PLUGIN_VAR_RQCMDARG,                                         // flags
    "Timeout for Cedar authorization requests in milliseconds",  // comment
    nullptr,                                                     // check
    nullptr,                                                     // update
    5000,                                                        // default
    1000,                                                        // min
    60000,                                                       // max
    0                                                            // block_size
);

static MYSQL_SYSVAR_BOOL(cache_enabled, cedar_authorization_cache_enabled,
                         PLUGIN_VAR_RQCMDARG, "Enable authorization caching",
                         nullptr, nullptr, true);

static MYSQL_SYSVAR_INT(cache_size, cedar_authorization_cache_size,
                        PLUGIN_VAR_RQCMDARG,
                        "Maximum number of entries in auth cache", nullptr,
                        nullptr, 1024, 64, 100000, 0);

static MYSQL_SYSVAR_INT(cache_ttl, cedar_authorization_cache_ttl,
                        PLUGIN_VAR_RQCMDARG, "TTL for cache entries in seconds",
                        nullptr, nullptr, 300, 1, 86400, 0);

// Cache flush update callback
static void cedar_authorization_cache_flush_update(
    MYSQL_THD thd [[maybe_unused]], SYS_VAR *var [[maybe_unused]],
    void *var_ptr [[maybe_unused]], const void *save) {
  bool new_val = *static_cast<const bool *>(save);
  if (new_val) {
    // Flush the cache
    mysql_mutex_lock(&LOCK_auth_cache);
    auth_cache.clear();
    mysql_mutex_unlock(&LOCK_auth_cache);

    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Authorization cache flushed");
    }

    // Reset the variable to false
    cedar_authorization_cache_flush = false;
  }
}

static MYSQL_SYSVAR_BOOL(
    cache_flush, cedar_authorization_cache_flush, PLUGIN_VAR_RQCMDARG,
    "Flush the authorization cache (automatically resets to 0)", nullptr,
    cedar_authorization_cache_flush_update, false);

// System variables array
static SYS_VAR *cedar_authorization_system_vars[] = {
    MYSQL_SYSVAR(url),         MYSQL_SYSVAR(namespace),
    MYSQL_SYSVAR(timeout),     MYSQL_SYSVAR(cache_enabled),
    MYSQL_SYSVAR(cache_size),  MYSQL_SYSVAR(cache_ttl),
    MYSQL_SYSVAR(cache_flush), nullptr};

// Plugin declaration
mysql_declare_plugin(cedar_authorization){
    MYSQL_AUTHORIZATION_PLUGIN,       // type
    &cedar_authorization_descriptor,  // descriptor
    "cedar_authorization",            // name
    PLUGIN_AUTHOR_ORACLE,             // author
    "Cedar Authorization Plugin",     // description
    PLUGIN_LICENSE_GPL,               // license
    cedar_authorization_init,         // init function
    nullptr,                          // check_uninstall
    cedar_authorization_deinit,       // deinit function
    0x0100,                           // version
    nullptr,                          // status vars
    cedar_authorization_system_vars,  // system vars
    nullptr,                          // config options
    0,                                // flags
} mysql_declare_plugin_end;

// No test-specific wrappers; tests include the public header and call directly

#ifdef EXTRA_CODE_FOR_UNIT_TESTING
void cedar_auth_cache_reset() {
  mysql_mutex_lock(&LOCK_auth_cache);
  auth_cache.clear();
  mysql_mutex_unlock(&LOCK_auth_cache);
}

size_t cedar_auth_cache_size() {
  mysql_mutex_lock(&LOCK_auth_cache);
  size_t s = auth_cache.size();
  mysql_mutex_unlock(&LOCK_auth_cache);
  return s;
}

void cedar_set_authorization_url(const char *url) {
  if (cedar_authorization_url) free(cedar_authorization_url);
  cedar_authorization_url = url ? strdup(url) : nullptr;
}

void cedar_set_cache_enabled(bool enabled) {
  cedar_authorization_cache_enabled = enabled;
}
#endif
