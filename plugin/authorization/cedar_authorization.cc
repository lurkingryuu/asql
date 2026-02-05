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
  It delegates authorization decisions to a Cedar service running over HTTP or
  HTTPS.

  Features:
  - Policy-based authorization using Cedar
  - Supports all authorization event types (DB, Table, Column, Routine)
  - Configurable Cedar service URL and timeout
  - HTTP support by default
  - Optional HTTPS support with configurable SSL/TLS certificate verification
  - Optional mutual TLS (mTLS) authentication support
  - Rich context information (time, date, IP address)
  - Authorization response caching for performance
  - Detailed logging for debugging

  Basic Configuration (HTTP):
  INSTALL PLUGIN cedar_authorization SONAME 'cedar_authorization.so';
  SET GLOBAL cedar_authorization_url = 'http://localhost:8180';
  SET GLOBAL cedar_authorization_timeout = 5000;  -- milliseconds

  Optional HTTPS Configuration:
  -- Use HTTPS URL
  SET GLOBAL cedar_authorization_url = 'https://cedar-service.example.com:8180';

  -- Enable SSL certificate verification (recommended for production)
  SET GLOBAL cedar_authorization_ssl_verify_peer = 1;  -- Verify server
  certificate SET GLOBAL cedar_authorization_ssl_verify_host = 1;  -- Verify
  hostname

  -- Optional: Specify custom CA certificate bundle
  SET GLOBAL cedar_authorization_ssl_ca_file = '/path/to/ca-cert.pem';

  -- Optional: Enable mutual TLS (client authentication)
  SET GLOBAL cedar_authorization_ssl_cert_file = '/path/to/client-cert.pem';
  SET GLOBAL cedar_authorization_ssl_key_file = '/path/to/client-key.pem';

  Cache Configuration (optional):
  SET GLOBAL cedar_authorization_cache_enabled = 1;
  SET GLOBAL cedar_authorization_cache_size = 1024;
  SET GLOBAL cedar_authorization_cache_ttl = 300;  -- seconds

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

  Security Notes:
  - HTTP is used by default for simplicity and backward compatibility
  - For production environments with sensitive data, enable HTTPS with SSL
  verification
  - SSL certificate verification is disabled by default to avoid issues with
  self-signed certs
  - Enable ssl_verify_peer and ssl_verify_host for production deployments
  - Use mutual TLS for enhanced service-to-service authentication
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
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
using namespace std;

// Statistics structure
struct AuthStats {
  int64_t requests{0};
  int64_t grants{0};
  int64_t denies{0};
  int64_t errors{0};
  int64_t cache_hits{0};
  int64_t cache_misses{0};
  int64_t cache_evictions{0};
  int64_t total_time_us{0};
  int64_t remote_time_us{0};
};

// Thread-local stats for reduced contention
static thread_local AuthStats t_auth_stats;

// Registry for aggregating all thread-local stats
// Protected by mutex, only accessed on SHOW STATUS (not hot path)
static mysql_mutex_t LOCK_stats_registry;
static std::vector<AuthStats*> g_stats_registry;
static bool g_stats_registry_initialized = false;

// Register current thread's stats (called on first use)
static AuthStats& get_thread_stats() {
  static thread_local bool registered = false;
  if (!registered && g_stats_registry_initialized) {
    mysql_mutex_lock(&LOCK_stats_registry);
    g_stats_registry.push_back(&t_auth_stats);
    mysql_mutex_unlock(&LOCK_stats_registry);
    registered = true;
  }
  return t_auth_stats;
}

// Aggregate all thread-local stats
static int64_t aggregate_stat(int64_t AuthStats::*member) {
  int64_t total = 0;
  mysql_mutex_lock(&LOCK_stats_registry);
  for (AuthStats* stats : g_stats_registry) {
    total += stats->*member;
  }
  mysql_mutex_unlock(&LOCK_stats_registry);
  return total;
}

// Plugin system variables
static char *cedar_authorization_url;
static char *cedar_authorization_namespace;
static int cedar_authorization_timeout = 5000;  // milliseconds

// SSL/TLS configuration variables (disabled by default for HTTP)
static bool cedar_authorization_ssl_verify_peer = false;
static bool cedar_authorization_ssl_verify_host = false;
static char *cedar_authorization_ssl_ca_file = nullptr;
static char *cedar_authorization_ssl_cert_file = nullptr;
static char *cedar_authorization_ssl_key_file = nullptr;

// Caching system variables
static bool cedar_authorization_cache_enabled = true;
static int cedar_authorization_cache_size = 1024;
static int cedar_authorization_cache_ttl = 300;  // seconds
static bool cedar_authorization_cache_flush = false;

// Statistics system variables
static bool cedar_authorization_collect_stats = true;
static bool cedar_authorization_reset_stats = false;

// Logging system variables
// Gate info-level logs; warnings/errors remain enabled.
static bool cedar_authorization_log_info = false;

// Column access authorization: disabled by default for performance.
// Column callbacks dominate OLTP workloads (80%+ in TPC-C). Enable only if
// column-level Cedar policies are required.
static bool cedar_authorization_enable_column_access = false;

// Cache implementation
#include <list>
#include <mutex>
#include <unordered_map>

// Cache structures
struct AuthCacheKey {
  std::string user;
  std::string resource;
  std::string action;
  std::string day;
  uint32_t date;
  std::string ip;

  bool operator==(const AuthCacheKey &other) const {
    return user == other.user && resource == other.resource &&
           action == other.action && day == other.day && date == other.date &&
           ip == other.ip;
  }
};

struct AuthCacheEntry {
  int result;  // -1 (IGNORE), 0 (DENY), 1 (GRANT)
  std::time_t expires;
  std::list<AuthCacheKey>::iterator lru_iter;  // For O(1) LRU operations
};

struct AuthCacheKeyHash {
  std::size_t operator()(const AuthCacheKey &k) const {
    size_t h = std::hash<std::string>{}(k.user);
    h ^=
        std::hash<std::string>{}(k.resource) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(k.action) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(k.day) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(k.date) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(k.ip) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

// Sharded cache for reduced mutex contention
static constexpr size_t kNumShards = 64;
static constexpr size_t kShardShift = 64 - 6;  // log2(64) = 6

struct alignas(64) CacheShard {
  mysql_mutex_t mutex;
  std::unordered_map<AuthCacheKey, AuthCacheEntry, AuthCacheKeyHash> entries;
  std::list<AuthCacheKey> lru_list;  // Front = most recent, back = oldest
  // Padding to fill cache line and prevent false sharing
  char padding[64 - sizeof(mysql_mutex_t) % 64];
};

class ShardedAuthCache {
 public:
  void init() {
    for (size_t i = 0; i < kNumShards; ++i) {
      mysql_mutex_init(0, &shards_[i].mutex, MY_MUTEX_INIT_FAST);
    }
  }

  void destroy() {
    for (size_t i = 0; i < kNumShards; ++i) {
      mysql_mutex_destroy(&shards_[i].mutex);
    }
  }

  bool get(const AuthCacheKey &key, AuthCacheEntry &entry) {
    size_t shard_idx = get_shard_index(key);
    CacheShard &shard = shards_[shard_idx];

    mysql_mutex_lock(&shard.mutex);
    auto it = shard.entries.find(key);
    if (it != shard.entries.end()) {
      std::time_t now = std::time(nullptr);
      if (now < it->second.expires) {
        // Move to front of LRU list (O(1) with splice, no allocation)
        shard.lru_list.splice(shard.lru_list.begin(), shard.lru_list,
                              it->second.lru_iter);
        entry = it->second;
        mysql_mutex_unlock(&shard.mutex);
        return true;
      } else {
        // Expired - remove from LRU list and map
        shard.lru_list.erase(it->second.lru_iter);
        shard.entries.erase(it);
      }
    }
    mysql_mutex_unlock(&shard.mutex);
    return false;
  }

  void put(const AuthCacheKey &key, const AuthCacheEntry &entry,
           int max_per_shard) {
    size_t shard_idx = get_shard_index(key);
    CacheShard &shard = shards_[shard_idx];

    mysql_mutex_lock(&shard.mutex);
    // Check if key already exists
    auto existing = shard.entries.find(key);
    if (existing != shard.entries.end()) {
      // Update existing entry, move to front of LRU
      shard.lru_list.splice(shard.lru_list.begin(), shard.lru_list,
                            existing->second.lru_iter);
      existing->second.result = entry.result;
      existing->second.expires = entry.expires;
      mysql_mutex_unlock(&shard.mutex);
      return;
    }

    // Evict oldest entry if shard is full
    while (static_cast<int>(shard.entries.size()) >= max_per_shard &&
           !shard.lru_list.empty()) {
      // Remove oldest (back of list)
      const AuthCacheKey &oldest_key = shard.lru_list.back();
      shard.entries.erase(oldest_key);
      shard.lru_list.pop_back();
    }

    // Insert new entry at front of LRU list
    shard.lru_list.push_front(key);
    AuthCacheEntry new_entry = entry;
    new_entry.lru_iter = shard.lru_list.begin();
    shard.entries[key] = new_entry;
    mysql_mutex_unlock(&shard.mutex);
  }

  void clear() {
    for (size_t i = 0; i < kNumShards; ++i) {
      mysql_mutex_lock(&shards_[i].mutex);
      shards_[i].entries.clear();
      shards_[i].lru_list.clear();
      mysql_mutex_unlock(&shards_[i].mutex);
    }
  }

  size_t size() {
    size_t total = 0;
    for (size_t i = 0; i < kNumShards; ++i) {
      mysql_mutex_lock(&shards_[i].mutex);
      total += shards_[i].entries.size();
      mysql_mutex_unlock(&shards_[i].mutex);
    }
    return total;
  }

 private:
  size_t get_shard_index(const AuthCacheKey &key) {
    return AuthCacheKeyHash{}(key) >> kShardShift;
  }

  CacheShard shards_[kNumShards];
};

// Sharded cache instance
static ShardedAuthCache g_sharded_auth_cache;

// Plugin initialization flag
static bool plugin_initialized = false;
// Saved plugin handle for logging
static MYSQL_PLUGIN plugin_handle = nullptr;

static inline bool cedar_should_log_info() {
  return cedar_authorization_log_info && plugin_handle;
}

// Helper structure for HTTP response
struct HttpResponse {
  std::string data;
};

// Thread-local CURL handle pool for connection reuse
class CurlHandlePool {
 public:
  // Acquire a CURL handle (from pool or new)
  static CURL* acquire() {
    auto& pool = get_thread_pool();
    if (!pool.empty()) {
      CURL* handle = pool.back();
      pool.pop_back();
      return handle;
    }
    return curl_easy_init();
  }

  // Release a CURL handle back to the pool
  static void release(CURL* handle) {
    if (handle) {
      curl_easy_reset(handle);  // Clear options, keep connection
      get_thread_pool().push_back(handle);
    }
  }

  // Cleanup all handles in current thread's pool
  static void cleanup_thread() {
    auto& pool = get_thread_pool();
    for (CURL* handle : pool) {
      if (handle) {
        curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 100);  // Short timeout for cleanup
        curl_easy_cleanup(handle);
      }
    }
    pool.clear();
  }

#ifdef EXTRA_CODE_FOR_UNIT_TESTING
  static size_t pool_size_for_test() { return get_thread_pool().size(); }
#endif

 private:
  static std::vector<CURL*>& get_thread_pool() {
    thread_local std::vector<CURL*> pool;
    return pool;
  }
};

// RAII wrapper for automatic handle management
class ScopedCurlHandle {
 public:
  ScopedCurlHandle() : handle_(CurlHandlePool::acquire()) {}
  ~ScopedCurlHandle() { CurlHandlePool::release(handle_); }

  ScopedCurlHandle(const ScopedCurlHandle&) = delete;
  ScopedCurlHandle& operator=(const ScopedCurlHandle&) = delete;

  CURL* get() const { return handle_; }
  explicit operator bool() const { return handle_ != nullptr; }

 private:
  CURL* handle_;
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
    AuthCacheKey key{user_uid_value, resource_identifier, privilege, day, date,
                     fmt_ip};

     AuthCacheEntry entry;
     if (g_sharded_auth_cache.get(key, entry)) {
       if (cedar_authorization_collect_stats) {
         get_thread_stats().cache_hits++;
       }
       if (cedar_should_log_info()) {
         my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                               "Cache hit for privilege %s: %d",
                               privilege.c_str(), entry.result);
       }
       return entry.result;
     } else {
       if (cedar_authorization_collect_stats) {
         get_thread_stats().cache_misses++;
       }
     }
  } else {
    // Cache disabled counts as miss? Or just ignore?
    // pg_authorization counts misses if cache lookup fails.
    // If cache is disabled, we don't look up, so strictly it's not a cache miss
    // event.
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
          user_uid_value, resource_identifier, privilege, day, date, fmt_ip};
      std::time_t now = std::time(nullptr);
      AuthCacheEntry entry{result, now + cedar_authorization_cache_ttl,
                           std::list<AuthCacheKey>::iterator{}};
      int max_per_shard = cedar_authorization_cache_size / kNumShards;
      g_sharded_auth_cache.put(key, entry, max_per_shard);
    }
    return result;
  }
  if (strcmp(cedar_authorization_url, "http://mock-deny") == 0) {
    int result = 0;
    if (cedar_authorization_cache_enabled) {
      AuthCacheKey key{
          user_uid_value, resource_identifier, privilege, day, date, fmt_ip};
      std::time_t now = std::time(nullptr);
      AuthCacheEntry entry{result, now + cedar_authorization_cache_ttl,
                           std::list<AuthCacheKey>::iterator{}};
      int max_per_shard = cedar_authorization_cache_size / kNumShards;
      g_sharded_auth_cache.put(key, entry, max_per_shard);
    }
    return result;
  }
#endif

  // Acquire pooled CURL handle
  ScopedCurlHandle scoped_curl;
  CURL *curl = scoped_curl.get();
  if (!curl) {
    if (plugin_handle) {
      my_plugin_log_message(
          &plugin_handle, MY_ERROR_LEVEL,
          "Failed to acquire CURL handle for Cedar authorization");
    }
    return -1;
  }

  // Create JSON payload for single privilege
  Json::Value json_payload = auth_common::auth_build_cedar_payload(
      user_uid_value, resource_identifier, privilege, day, date, fmt_time,
      fmt_ip, ns);

  Json::StreamWriterBuilder builder;
  std::string json_string = Json::writeString(builder, json_payload);

  // Log the request payload
  if (cedar_should_log_info()) {
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

  // Configure SSL/TLS options
  if (cedar_authorization_ssl_verify_peer) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  } else {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "SSL peer verification disabled for Cedar service");
    }
  }

  if (cedar_authorization_ssl_verify_host) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  } else {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    if (plugin_handle) {
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "SSL host verification disabled for Cedar service");
    }
  }

  if (cedar_authorization_ssl_ca_file &&
      strlen(cedar_authorization_ssl_ca_file) > 0) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, cedar_authorization_ssl_ca_file);
    if (cedar_should_log_info()) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Using CA certificate file: %s",
                            cedar_authorization_ssl_ca_file);
    }
  }

  if (cedar_authorization_ssl_cert_file &&
      strlen(cedar_authorization_ssl_cert_file) > 0) {
    curl_easy_setopt(curl, CURLOPT_SSLCERT, cedar_authorization_ssl_cert_file);
    if (cedar_should_log_info()) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Using client certificate file: %s",
                            cedar_authorization_ssl_cert_file);
    }
  }

  if (cedar_authorization_ssl_key_file &&
      strlen(cedar_authorization_ssl_key_file) > 0) {
    curl_easy_setopt(curl, CURLOPT_SSLKEY, cedar_authorization_ssl_key_file);
    if (cedar_should_log_info()) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Using client private key file: %s",
                            cedar_authorization_ssl_key_file);
    }
  }

   // Perform the request
   CURLcode res;
   if (cedar_authorization_collect_stats) {
     auto start_remote = std::chrono::high_resolution_clock::now();
     res = curl_easy_perform(curl);
     auto end_remote = std::chrono::high_resolution_clock::now();
     get_thread_stats().remote_time_us +=
         std::chrono::duration_cast<std::chrono::microseconds>(end_remote -
                                                               start_remote)
             .count();
   } else {
     res = curl_easy_perform(curl);
   }

  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  // Log response details
  if (cedar_should_log_info()) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "HTTP request completed for privilege %s. cURL "
                          "result: %d, HTTP code: %ld",
                          privilege.c_str(), res, response_code);
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Response body: %s", response.data.c_str());
  }

  curl_slist_free_all(headers);

  if (res != CURLE_OK) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_ERROR_LEVEL,
                            "Cedar authorization request failed for privilege "
                            "%s: %s (cURL error: %d)",
                            privilege.c_str(), curl_easy_strerror(res), res);
    if (cedar_authorization_collect_stats) {
      get_thread_stats().errors++;
    }
    return -1;  // Signal error (fail-open to IGNORE)
  }

  if (response_code != 200) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "Cedar authorization server returned HTTP %ld for "
                            "privilege %s, response: %s",
                            response_code, privilege.c_str(),
                            response.data.c_str());
    if (cedar_authorization_collect_stats) {
      get_thread_stats().errors++;
    }
    return -1;  // Signal error (fail-open to IGNORE)
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
    if (cedar_authorization_collect_stats) {
      get_thread_stats().errors++;
    }
    return -1;  // Signal error (fail-open to IGNORE)
  }

  if (!json_response.isMember("decision")) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "Cedar authorization response missing 'decision' "
                            "field for privilege %s",
                            privilege.c_str());
    if (cedar_authorization_collect_stats) {
      get_thread_stats().errors++;
    }
    return -1;  // Signal error (fail-open to IGNORE)
  }

  std::string decision = json_response["decision"].asString();
  int result = (decision == "Allow") ? 1 : 0;

  if (cedar_should_log_info()) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization result for privilege %s: '%s'",
                          privilege.c_str(), decision.c_str());
  }

  // Update cache if enabled
  if (cedar_authorization_cache_enabled) {
    AuthCacheKey key{user_uid_value, resource_identifier, privilege, day, date,
                     fmt_ip};
    std::time_t now = std::time(nullptr);
    AuthCacheEntry entry{result, now + cedar_authorization_cache_ttl,
                         std::list<AuthCacheKey>::iterator{}};
    int max_per_shard = cedar_authorization_cache_size / kNumShards;
    g_sharded_auth_cache.put(key, entry, max_per_shard);
  }

  return result;
}

// Access check core returns -1 (IGNORE), 0 (DENY), 1 (GRANT for all privs)
static int cedar_check_access_core(const mysql_authorization_event *event) {
  if (cedar_should_log_info()) {
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
    return -1;
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

  if (cedar_should_log_info()) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "Cedar authorization check: user=%s, resource=%s, "
                          "privileges=%lu, namespace=%s",
                          user_uid_value.c_str(), resource_identifier.c_str(),
                          event->privileges, ns.c_str());
  }

  // Get context information
  auto time_ctx = auth_common::auth_get_time_context();
  std::string day = time_ctx.day;
  uint32_t date = time_ctx.date;
  uint32_t fmt_time = time_ctx.time;
    std::string client_ip = auth_common::auth_get_client_ip_cached(event->thd);

  if (cedar_should_log_info()) {
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
    if (cedar_should_log_info()) {
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
        if (cedar_should_log_info()) {
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
        if (cedar_should_log_info()) {
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
    if (cedar_should_log_info()) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: Access GRANTED");
    }
    return 1;
  } else {
    if (cedar_should_log_info()) {
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
  if (cedar_authorization_collect_stats) {
    get_thread_stats().requests++;
  }

  if (cedar_should_log_info()) {
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
    if (cedar_should_log_info()) {
      my_plugin_log_message(
          &plugin_handle, MY_INFORMATION_LEVEL,
          "Cedar authorization: presence check -> GRANT (internal probe)");
    }
    return MYSQL_AUTHORIZATION_GRANT;
  }

  // Handle zero-privilege checks by allowing them (these are also internal
  // checks)
  if (event->privileges == 0) {
    if (cedar_should_log_info()) {
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
    if (cedar_should_log_info()) {
      my_plugin_log_message(
          &plugin_handle, MY_INFORMATION_LEVEL,
          "Cedar authorization: unsupported event type %s, returning IGNORE",
          auth_common::auth_event_type_to_string(event->event_subclass)
              .c_str());
    }
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  // Column access: skip unless explicitly enabled via sysvar
  if (!cedar_authorization_enable_column_access &&
      event->event_subclass == MYSQL_AUTHORIZATION_COLUMN_ACCESS) {
    if (cedar_authorization_collect_stats) {
      get_thread_stats().grants++;
    }
    return MYSQL_AUTHORIZATION_GRANT;
  }

  int result;
  if (cedar_authorization_collect_stats) {
    auto start_total = std::chrono::high_resolution_clock::now();
    result = cedar_check_access_core(event);
    auto end_total = std::chrono::high_resolution_clock::now();
    get_thread_stats().total_time_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(end_total -
                                                              start_total)
            .count();
  } else {
    result = cedar_check_access_core(event);
  }

  if (result == -1) {
    if (cedar_should_log_info()) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: IGNORE");
    }
    return MYSQL_AUTHORIZATION_IGNORE;
  } else if (result == 1) {
    if (cedar_should_log_info()) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Cedar authorization: GRANT");
    }
     if (cedar_authorization_collect_stats) {
       get_thread_stats().grants++;
     }
     return MYSQL_AUTHORIZATION_GRANT;
   } else {
     if (cedar_should_log_info()) {
       my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                             "Cedar authorization: DENY");
     }
     if (cedar_authorization_collect_stats) {
       get_thread_stats().denies++;
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

  if (cedar_should_log_info()) {
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

   // Initialize sharded cache
   g_sharded_auth_cache.init();

   // Initialize stats registry
   mysql_mutex_init(0, &LOCK_stats_registry, MY_MUTEX_INIT_FAST);
   g_stats_registry_initialized = true;

   plugin_initialized = true;

  if (cedar_should_log_info()) {
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
  if (cedar_should_log_info()) {
    my_plugin_log_message(
        &plugin_handle, MY_INFORMATION_LEVEL,
        "Cedar authorization plugin deinitialization starting...");
  }

   plugin_initialized = false;
   curl_global_cleanup();

   // Destroy sharded cache
   g_sharded_auth_cache.destroy();

   // Cleanup stats registry
#ifdef EXTRA_CODE_FOR_UNIT_TESTING
   // For unit tests, don't clear the registry to preserve thread registration.
   // Just reset the stats values.
   for (AuthStats* stats : g_stats_registry) {
     stats->requests = 0;
     stats->grants = 0;
     stats->denies = 0;
     stats->errors = 0;
     stats->cache_hits = 0;
     stats->cache_misses = 0;
     stats->cache_evictions = 0;
     stats->total_time_us = 0;
     stats->remote_time_us = 0;
   }
  // Don't set g_stats_registry_initialized = false;
  // Don't clear g_stats_registry;
  // Don't destroy the mutex;
#else
   g_stats_registry_initialized = false;
   g_stats_registry.clear();
   mysql_mutex_destroy(&LOCK_stats_registry);
#endif

   // Clear thread-local IP cache
   auth_common::auth_clear_all_client_ip_cache();

  if (cedar_should_log_info()) {
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

static MYSQL_SYSVAR_BOOL(
    ssl_verify_peer, cedar_authorization_ssl_verify_peer, PLUGIN_VAR_RQCMDARG,
    "Enable SSL peer certificate verification for HTTPS connections (default: "
    "disabled)",
    nullptr, nullptr, false);

static MYSQL_SYSVAR_BOOL(
    ssl_verify_host, cedar_authorization_ssl_verify_host, PLUGIN_VAR_RQCMDARG,
    "Enable SSL host name verification for HTTPS connections (default: "
    "disabled)",
    nullptr, nullptr, false);

static MYSQL_SYSVAR_STR(ssl_ca_file, cedar_authorization_ssl_ca_file,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
                        "Path to CA certificate file for SSL/TLS verification",
                        nullptr, nullptr, nullptr);

static MYSQL_SYSVAR_STR(
    ssl_cert_file, cedar_authorization_ssl_cert_file,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Path to client certificate file for mutual TLS authentication", nullptr,
    nullptr, nullptr);

static MYSQL_SYSVAR_STR(
    ssl_key_file, cedar_authorization_ssl_key_file,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Path to client private key file for mutual TLS authentication", nullptr,
    nullptr, nullptr);

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
    g_sharded_auth_cache.clear();

    if (cedar_should_log_info()) {
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

// Stats update callback
static void cedar_authorization_reset_stats_update(
    MYSQL_THD thd [[maybe_unused]], SYS_VAR *var [[maybe_unused]],
    void *var_ptr [[maybe_unused]], const void *save) {
  bool new_val = *static_cast<const bool *>(save);
  if (new_val) {
    mysql_mutex_lock(&LOCK_stats_registry);
    for (AuthStats* stats : g_stats_registry) {
      stats->requests = 0;
      stats->grants = 0;
      stats->denies = 0;
      stats->errors = 0;
      stats->cache_hits = 0;
      stats->cache_misses = 0;
      stats->cache_evictions = 0;
      stats->total_time_us = 0;
      stats->remote_time_us = 0;
    }
    mysql_mutex_unlock(&LOCK_stats_registry);

    if (cedar_should_log_info()) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "Authorization statistics reset");
    }

    // Reset the variable to false
    cedar_authorization_reset_stats = false;
  }
}

static MYSQL_SYSVAR_BOOL(collect_stats, cedar_authorization_collect_stats,
                         PLUGIN_VAR_RQCMDARG,
                         "Enable collection of authorization statistics",
                         nullptr, nullptr, true);

static MYSQL_SYSVAR_BOOL(
    reset_stats, cedar_authorization_reset_stats, PLUGIN_VAR_RQCMDARG,
    "Reset authorization statistics (automatically resets to 0)", nullptr,
    cedar_authorization_reset_stats_update, false);

static MYSQL_SYSVAR_BOOL(
    log_info, cedar_authorization_log_info, PLUGIN_VAR_RQCMDARG,
    "Enable info-level logging for cedar_authorization (default: disabled)",
    nullptr, nullptr, false);

static MYSQL_SYSVAR_BOOL(
    enable_column_access, cedar_authorization_enable_column_access,
    PLUGIN_VAR_RQCMDARG,
    "Enable Cedar authorization for column-level access (default: disabled)",
    nullptr, nullptr, false);

// System variables array
static SYS_VAR *cedar_authorization_system_vars[] = {
    MYSQL_SYSVAR(url),
    MYSQL_SYSVAR(namespace),
    MYSQL_SYSVAR(timeout),
    MYSQL_SYSVAR(ssl_verify_peer),
    MYSQL_SYSVAR(ssl_verify_host),
    MYSQL_SYSVAR(ssl_ca_file),
    MYSQL_SYSVAR(ssl_cert_file),
    MYSQL_SYSVAR(ssl_key_file),
    MYSQL_SYSVAR(cache_enabled),
    MYSQL_SYSVAR(cache_size),
    MYSQL_SYSVAR(cache_ttl),
    MYSQL_SYSVAR(cache_flush),
    MYSQL_SYSVAR(collect_stats),
    MYSQL_SYSVAR(reset_stats),
    MYSQL_SYSVAR(log_info),
    MYSQL_SYSVAR(enable_column_access),
    nullptr};

// Status variables - Macro to define show functions for each stat
#define DEF_SHOW_STAT(name, stat_member)                                      \
  static int show_auth_##name(MYSQL_THD, SHOW_VAR *var, char *buff) {         \
    int64_t value = aggregate_stat(&AuthStats::stat_member);                  \
    memcpy(buff, &value, sizeof(value));                                      \
    var->type = SHOW_LONGLONG;                                                \
    var->value = buff;                                                        \
    return 0;                                                                 \
  }

DEF_SHOW_STAT(requests, requests)
DEF_SHOW_STAT(grants, grants)
DEF_SHOW_STAT(denies, denies)
DEF_SHOW_STAT(errors, errors)
DEF_SHOW_STAT(cache_hits, cache_hits)
DEF_SHOW_STAT(cache_misses, cache_misses)
DEF_SHOW_STAT(cache_evictions, cache_evictions)
DEF_SHOW_STAT(total_time_us, total_time_us)
DEF_SHOW_STAT(remote_time_us, remote_time_us)

static SHOW_VAR cedar_status_vars[] = {
    {"cedar_authorization_requests", (char *)&show_auth_requests, SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"cedar_authorization_grants", (char *)&show_auth_grants, SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"cedar_authorization_denies", (char *)&show_auth_denies, SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"cedar_authorization_errors", (char *)&show_auth_errors, SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"cedar_authorization_cache_hits", (char *)&show_auth_cache_hits, SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"cedar_authorization_cache_misses", (char *)&show_auth_cache_misses,
     SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"cedar_authorization_cache_evictions", (char *)&show_auth_cache_evictions,
     SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"cedar_authorization_total_time_us", (char *)&show_auth_total_time_us,
     SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"cedar_authorization_remote_time_us", (char *)&show_auth_remote_time_us,
     SHOW_FUNC, SHOW_SCOPE_GLOBAL},

    {nullptr, nullptr, SHOW_UNDEF, SHOW_SCOPE_UNDEF}};

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
    cedar_status_vars,                // status vars
    cedar_authorization_system_vars,  // system vars
    nullptr,                          // config options
    0,                                // flags
} mysql_declare_plugin_end;

// No test-specific wrappers; tests include the public header and call directly

#ifdef EXTRA_CODE_FOR_UNIT_TESTING
void cedar_auth_cache_reset() {
  g_sharded_auth_cache.clear();
}

size_t cedar_auth_cache_size() {
  return g_sharded_auth_cache.size();
}

void cedar_set_authorization_url(const char *url) {
  if (cedar_authorization_url) free(cedar_authorization_url);
  cedar_authorization_url = url ? strdup(url) : nullptr;
}

void cedar_set_cache_enabled(bool enabled) {
  cedar_authorization_cache_enabled = enabled;
}

void cedar_set_cache_size_for_test(int size) {
  cedar_authorization_cache_size = size;
}

void cedar_set_cache_ttl_for_test(int ttl_seconds) {
  cedar_authorization_cache_ttl = ttl_seconds;
}

size_t cedar_cache_key_shard_index_for_test(const char *user,
                                            const char *resource,
                                            const char *action,
                                            const char *day, uint32_t date,
                                            const char *ip) {
  AuthCacheKey key{std::string(user ? user : ""),
                   std::string(resource ? resource : ""),
                   std::string(action ? action : ""),
                   std::string(day ? day : ""),
                   date,
                   std::string(ip ? ip : "")};
  uint64_t h = static_cast<uint64_t>(AuthCacheKeyHash{}(key));
  return static_cast<size_t>(h >> kShardShift);
}

bool cedar_cache_contains_for_test(const char *user, const char *resource,
                                  const char *action, const char *day,
                                  uint32_t date, const char *ip) {
  AuthCacheKey key{std::string(user ? user : ""),
                   std::string(resource ? resource : ""),
                   std::string(action ? action : ""),
                   std::string(day ? day : ""),
                   date,
                   std::string(ip ? ip : "")};
  AuthCacheEntry entry;
  return g_sharded_auth_cache.get(key, entry);
}

// Stats testing helpers
int64_t cedar_get_auth_stat_requests() {
  return aggregate_stat(&AuthStats::requests);
}
int64_t cedar_get_auth_stat_grants() {
  return aggregate_stat(&AuthStats::grants);
}
int64_t cedar_get_auth_stat_denies() {
  return aggregate_stat(&AuthStats::denies);
}
void cedar_reset_stats_for_test() {
  mysql_mutex_lock(&LOCK_stats_registry);
  for (AuthStats* stats : g_stats_registry) {
    stats->requests = 0;
    stats->grants = 0;
    stats->denies = 0;
    stats->errors = 0;
    stats->cache_hits = 0;
    stats->cache_misses = 0;
    stats->cache_evictions = 0;
    stats->total_time_us = 0;
    stats->remote_time_us = 0;
  }
  mysql_mutex_unlock(&LOCK_stats_registry);
}
void cedar_set_collect_stats(bool enable) {
  cedar_authorization_collect_stats = enable;
}

void cedar_set_enable_column_access_for_test(bool enable) {
  cedar_authorization_enable_column_access = enable;
}

size_t cedar_test_curl_pool_size() { return CurlHandlePool::pool_size_for_test(); }

uintptr_t cedar_test_curl_acquire_handle() {
  CURL *h = CurlHandlePool::acquire();
  return reinterpret_cast<uintptr_t>(h);
}

void cedar_test_curl_release_handle(uintptr_t handle) {
  CurlHandlePool::release(reinterpret_cast<CURL *>(handle));
}

void cedar_test_curl_cleanup_thread() { CurlHandlePool::cleanup_thread(); }
#endif
