/* Copyright (c) 2025, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details. */

/**
  @file plugin/authorization/embedded_cedar.cc

  Embedded Cedar Authorization Plugin

  Evaluates Cedar policies in-process via libcedar - a C ABI wrapper around
  the Cedar Rust crate. No HTTP round-trip is required at query time.

  Policies, schema, and entities are loaded from files on disk at plugin
  initialization and on demand via the `embedded_cedar_reload` sysvar. This
  makes the plugin fully agent-independent after startup.

  Configuration:
    INSTALL PLUGIN embedded_cedar SONAME 'embedded_cedar.so';
    SET GLOBAL embedded_cedar_policy_file   = '/app/mysql_schemas/policies.cedar';
    SET GLOBAL embedded_cedar_schema_file   = '/app/mysql_schemas/schema.json';
    SET GLOBAL embedded_cedar_entities_file = '/app/mysql_schemas/data.json';
    SET GLOBAL embedded_cedar_namespace     = 'MySQL';
    SET GLOBAL embedded_cedar_enabled       = 1;

  To reload policies/schema/entities at runtime:
    SET GLOBAL embedded_cedar_reload = 1;

  Authorization model:
    - Each SQL privilege maps to a separate Cedar Action (e.g., Action::"SELECT").
    - For ALL_OF mode, every required privilege must be allowed.
    - Errors in the Cedar engine return IGNORE (fail-open to native MySQL ACL).
    - Presence checks and zero-privilege checks always return GRANT.

  Thread safety:
    A single global CedarEngine* is shared across all MySQL threads.
    Concurrent is_authorized calls hold a shared read lock; engine reload
    acquires an exclusive write lock. Cedar evaluation is fast enough that
    mutex contention is not a bottleneck on typical OLTP workloads.
*/

#include <mysql/plugin.h>
#include <mysql/plugin_authorization.h>
#include <mysql/service_my_plugin_log.h>
#include <mysql/service_mysql_alloc.h>

#include <json/json.h>
#include <json/value.h>

#include <new>
#include <pthread.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <list>
#include <mutex>
#include <unordered_map>
using namespace std;

extern "C" {
#include "libcedar.h"
}

#include "plugin/authorization/authorization_common.h"

#include "sql/auth/auth_acls.h"
#include "sql/sql_class.h"
#include "my_dbug.h"
#include "sql/protocol_classic.h"
#include "violite.h"

struct EmbeddedAuthStats {
  int64_t requests{0};
  int64_t grants{0};
  int64_t denies{0};
  int64_t errors{0};
  int64_t cache_hits{0};
  int64_t cache_misses{0};
  int64_t cache_evictions{0};
  int64_t total_time_us{0};
  int64_t eval_time_us{0};
};

static thread_local EmbeddedAuthStats *t_stats_ptr = nullptr;

static pthread_key_t g_stats_key;
static bool g_stats_key_initialized = false;

static mysql_mutex_t LOCK_stats_registry;
static std::vector<EmbeddedAuthStats*> g_stats_registry;
static bool g_stats_registry_initialized = false;

static void unregister_stats(EmbeddedAuthStats *stats) {
  if (!stats || !g_stats_registry_initialized) return;
  mysql_mutex_lock(&LOCK_stats_registry);
  for (auto it = g_stats_registry.begin(); it != g_stats_registry.end(); ++it) {
    if (*it == stats) { g_stats_registry.erase(it); break; }
  }
  mysql_mutex_unlock(&LOCK_stats_registry);
}

static void stats_tls_destructor(void *ptr) {
  auto *stats = static_cast<EmbeddedAuthStats *>(ptr);
  if (!stats) return;
  unregister_stats(stats);
  delete stats;
}

static EmbeddedAuthStats& get_thread_stats() {
  if (t_stats_ptr) return *t_stats_ptr;

  EmbeddedAuthStats *stats = nullptr;
  if (g_stats_key_initialized)
    stats = static_cast<EmbeddedAuthStats *>(pthread_getspecific(g_stats_key));

  if (!stats) {
    stats = new (std::nothrow) EmbeddedAuthStats();
    if (!stats) {
      static thread_local EmbeddedAuthStats fallback;
      return fallback;
    }
    if (g_stats_key_initialized)
      (void)pthread_setspecific(g_stats_key, stats);
    if (g_stats_registry_initialized) {
      mysql_mutex_lock(&LOCK_stats_registry);
      g_stats_registry.push_back(stats);
      mysql_mutex_unlock(&LOCK_stats_registry);
    }
  }
  t_stats_ptr = stats;
  return *stats;
}

static int64_t aggregate_stat(int64_t EmbeddedAuthStats::*member) {
  int64_t total = 0;
  if (!g_stats_registry_initialized) return 0;
  mysql_mutex_lock(&LOCK_stats_registry);
  for (EmbeddedAuthStats *s : g_stats_registry) total += s->*member;
  mysql_mutex_unlock(&LOCK_stats_registry);
  return total;
}

static MYSQL_PLUGIN plugin_handle = nullptr;
static bool plugin_initialized = false;

static CedarEngine *g_cedar_engine = nullptr;
static mysql_rwlock_t LOCK_cedar_engine;
static bool g_engine_lock_initialized = false;

static char *embedded_cedar_policy_file   = nullptr;
static char *embedded_cedar_schema_file   = nullptr;
static char *embedded_cedar_entities_file = nullptr;
static char *embedded_cedar_namespace     = nullptr;
static bool  embedded_cedar_enabled       = true;
static bool  embedded_cedar_cache_enabled = true;
static int   embedded_cedar_cache_size    = 1024;
static int   embedded_cedar_cache_ttl     = 300;
static bool  embedded_cedar_cache_flush   = false;
static bool  embedded_cedar_collect_stats = true;
static bool  embedded_cedar_reset_stats   = false;
static bool  embedded_cedar_log_info      = false;
static bool  embedded_cedar_enable_column_access = false;
static bool  embedded_cedar_reload        = false;

static inline bool should_log_info() {
  return embedded_cedar_log_info && plugin_handle;
}

struct AuthCacheKey {
  std::string user;
  std::string resource;
  std::string action;
  std::string day;
  uint32_t date;
  std::string ip;

  bool operator==(const AuthCacheKey &o) const {
    return user == o.user && resource == o.resource && action == o.action &&
           day == o.day && date == o.date && ip == o.ip;
  }
};

struct AuthCacheEntry {
  int result;
  std::time_t expires;
  std::list<AuthCacheKey>::iterator lru_iter;
};

struct AuthCacheKeyHash {
  std::size_t operator()(const AuthCacheKey &k) const {
    size_t h = std::hash<std::string>{}(k.user);
    h ^= std::hash<std::string>{}(k.resource) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(k.action)   + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(k.day)      + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(k.date)        + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(k.ip)       + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

static constexpr size_t kNumShards  = 64;
static constexpr size_t kShardShift = 64 - 6;

struct alignas(64) CacheShard {
  mysql_mutex_t mutex;
  std::unordered_map<AuthCacheKey, AuthCacheEntry, AuthCacheKeyHash> entries;
  std::list<AuthCacheKey> lru_list;
  char padding[64 - sizeof(mysql_mutex_t) % 64];
};

class ShardedAuthCache {
 public:
  void init() {
    for (size_t i = 0; i < kNumShards; ++i)
      mysql_mutex_init(0, &shards_[i].mutex, MY_MUTEX_INIT_FAST);
  }

  void destroy() {
    for (size_t i = 0; i < kNumShards; ++i)
      mysql_mutex_destroy(&shards_[i].mutex);
  }

  bool get(const AuthCacheKey &key, AuthCacheEntry &entry) {
    size_t idx = get_shard(key);
    CacheShard &s = shards_[idx];
    mysql_mutex_lock(&s.mutex);
    auto it = s.entries.find(key);
    if (it != s.entries.end()) {
      if (std::time(nullptr) < it->second.expires) {
        s.lru_list.splice(s.lru_list.begin(), s.lru_list, it->second.lru_iter);
        entry = it->second;
        mysql_mutex_unlock(&s.mutex);
        return true;
      }
      s.lru_list.erase(it->second.lru_iter);
      s.entries.erase(it);
    }
    mysql_mutex_unlock(&s.mutex);
    return false;
  }

  void put(const AuthCacheKey &key, const AuthCacheEntry &entry, int max_per_shard) {
    size_t idx = get_shard(key);
    CacheShard &s = shards_[idx];
    mysql_mutex_lock(&s.mutex);
    auto existing = s.entries.find(key);
    if (existing != s.entries.end()) {
      s.lru_list.splice(s.lru_list.begin(), s.lru_list, existing->second.lru_iter);
      existing->second.result  = entry.result;
      existing->second.expires = entry.expires;
      mysql_mutex_unlock(&s.mutex);
      return;
    }
    while (static_cast<int>(s.entries.size()) >= max_per_shard && !s.lru_list.empty()) {
      s.entries.erase(s.lru_list.back());
      s.lru_list.pop_back();
    }
    s.lru_list.push_front(key);
    AuthCacheEntry ne = entry;
    ne.lru_iter = s.lru_list.begin();
    s.entries[key] = ne;
    mysql_mutex_unlock(&s.mutex);
  }

  void clear() {
    for (size_t i = 0; i < kNumShards; ++i) {
      mysql_mutex_lock(&shards_[i].mutex);
      shards_[i].entries.clear();
      shards_[i].lru_list.clear();
      mysql_mutex_unlock(&shards_[i].mutex);
    }
  }

 private:
  size_t get_shard(const AuthCacheKey &key) {
    return AuthCacheKeyHash{}(key) >> kShardShift;
  }

  CacheShard shards_[kNumShards];
};

static ShardedAuthCache g_cache;

static std::string read_file(const char *path) {
  if (!path || !path[0]) return {};
  std::ifstream f(path);
  if (!f.is_open()) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static bool create_engine_from_files() {
  std::string policy_text   = read_file(embedded_cedar_policy_file);
  std::string schema_json   = read_file(embedded_cedar_schema_file);
  std::string entities_json = read_file(embedded_cedar_entities_file);

  if (policy_text.empty() && !embedded_cedar_policy_file) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "embedded_cedar: policy file not configured; "
                            "plugin will IGNORE all checks");
    return true;
  }

  CedarEngine *engine = cedar_engine_new();
  if (!engine) {
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_ERROR_LEVEL,
                            "embedded_cedar: cedar_engine_new() returned NULL");
    return false;
  }

  if (!schema_json.empty()) {
    if (cedar_engine_set_schema_json(engine, schema_json.c_str()) != 0) {
      const char *err = cedar_engine_last_error(engine);
      if (plugin_handle)
        my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                              "embedded_cedar: failed to load schema: %s",
                              err ? err : "(unknown)");
    } else if (should_log_info()) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "embedded_cedar: schema loaded from %s",
                            embedded_cedar_schema_file);
    }
  }

  if (!entities_json.empty()) {
    if (cedar_engine_set_entities_json(engine, entities_json.c_str()) != 0) {
      const char *err = cedar_engine_last_error(engine);
      if (plugin_handle)
        my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                              "embedded_cedar: failed to load entities: %s",
                              err ? err : "(unknown)");
    } else if (should_log_info()) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "embedded_cedar: entities loaded from %s",
                            embedded_cedar_entities_file);
    }
  }

  if (!policy_text.empty()) {
    if (cedar_engine_set_policies(engine, policy_text.c_str()) != 0) {
      const char *err = cedar_engine_last_error(engine);
      if (plugin_handle)
        my_plugin_log_message(&plugin_handle, MY_ERROR_LEVEL,
                              "embedded_cedar: failed to load policies: %s",
                              err ? err : "(unknown)");
      cedar_engine_free(engine);
      return false;
    } else if (should_log_info()) {
      my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                            "embedded_cedar: policies loaded from %s",
                            embedded_cedar_policy_file);
    }
  }

  CedarEngine *old_engine = g_cedar_engine;
  g_cedar_engine = engine;
  if (old_engine) cedar_engine_free(old_engine);

  if (plugin_handle)
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "embedded_cedar: engine (re)loaded successfully");
  return true;
}

static int check_single_privilege_embedded(
    const std::string &user_uid, const std::string &resource_id,
    const std::string &privilege, const std::string &day, uint32_t date,
    uint32_t fmt_time, const std::string &client_ip, const std::string &ns) {
  if (embedded_cedar_cache_enabled) {
    AuthCacheKey key{user_uid, resource_id, privilege, day, date, client_ip};
    AuthCacheEntry entry;
    if (g_cache.get(key, entry)) {
      if (embedded_cedar_collect_stats) get_thread_stats().cache_hits++;
      if (should_log_info())
        my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                              "embedded_cedar: cache hit for %s -> %d",
                              privilege.c_str(), entry.result);
      return entry.result;
    }
    if (embedded_cedar_collect_stats) get_thread_stats().cache_misses++;
  }

  std::string prefix    = ns.empty() ? "" : ns + "::";
  std::string principal = prefix + "User::\"" + user_uid + "\"";
  std::string action    = prefix + "Action::\"" + privilege + "\"";
  std::string resource  = prefix + resource_id;

  Json::Value ctx;
  ctx["day"] = day;
  ctx["date"] = date;
  ctx["time"] = fmt_time;
  ctx["ip"]["__extn"]["fn"] = "ip";
  ctx["ip"]["__extn"]["arg"] = client_ip;
  Json::StreamWriterBuilder wb;
  wb["indentation"] = "";
  std::string ctx_json = Json::writeString(wb, ctx);

  CedarDecision decision;
  {
    mysql_rwlock_rdlock(&LOCK_cedar_engine);
    if (!g_cedar_engine) {
      mysql_rwlock_unlock(&LOCK_cedar_engine);
      if (plugin_handle)
        my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                              "embedded_cedar: engine not loaded; returning IGNORE");
      return -1;
    }

    if (embedded_cedar_collect_stats) {
      auto t0 = std::chrono::high_resolution_clock::now();
      decision = cedar_engine_is_authorized(g_cedar_engine,
                                            principal.c_str(), action.c_str(),
                                            resource.c_str(), ctx_json.c_str());
      auto t1 = std::chrono::high_resolution_clock::now();
      get_thread_stats().eval_time_us +=
          std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    } else {
      decision = cedar_engine_is_authorized(g_cedar_engine,
                                            principal.c_str(), action.c_str(),
                                            resource.c_str(), ctx_json.c_str());
    }
    mysql_rwlock_unlock(&LOCK_cedar_engine);
  }

  if (decision == Error) {
    if (embedded_cedar_collect_stats) get_thread_stats().errors++;
    if (plugin_handle)
      my_plugin_log_message(&plugin_handle, MY_WARNING_LEVEL,
                            "embedded_cedar: evaluation error for %s",
                            privilege.c_str());
    return -1;
  }

  int result = (decision == Allow) ? 1 : 0;

  if (embedded_cedar_cache_enabled) {
    AuthCacheKey key{user_uid, resource_id, privilege, day, date, client_ip};
    std::time_t now = std::time(nullptr);
    AuthCacheEntry entry{result, now + embedded_cedar_cache_ttl, {}};
    int max_per_shard = embedded_cedar_cache_size / static_cast<int>(kNumShards);
    g_cache.put(key, entry, max_per_shard);
  }

  return result;
}

static int embedded_check_access_core(const mysql_authorization_event *event) {
  if (!plugin_initialized) return -1;

  std::string user_uid = auth_common::auth_build_user_uid(event);
  std::string ns = embedded_cedar_namespace ? embedded_cedar_namespace : "MySQL";
  std::string resource_id = auth_common::auth_create_resource_identifier(event, "");

  auto time_ctx = auth_common::auth_get_time_context();
  std::string ip = auth_common::auth_get_client_ip_cached(event->thd);

  static const std::pair<const char *, int> kStdPrivs[] = {
      {"SELECT", 0}, {"INSERT", 1}, {"UPDATE", 2}, {"DELETE", 3},
      {"CREATE", 4}, {"DROP", 5}, {"RELOAD", 6}, {"SHUTDOWN", 7},
      {"PROCESS", 8}, {"FILE", 9}, {"GRANT", 10}, {"REFERENCES", 11},
      {"INDEX", 12}, {"ALTER", 13}, {"SHOW DATABASES", 14}, {"SUPER", 15},
      {"CREATE TEMPORARY TABLES", 16}, {"LOCK TABLES", 17}, {"EXECUTE", 18},
      {"REPLICATION SLAVE", 19}, {"REPLICATION CLIENT", 20},
      {"CREATE VIEW", 21}, {"SHOW VIEW", 22},
      {"CREATE ROUTINE", 23}, {"ALTER ROUTINE", 24},
      {"CREATE USER", 25}, {"EVENT", 26},
      {"TRIGGER", 27}, {"CREATE TABLESPACE", 28},
      {"CREATE ROLE", 29}, {"DROP ROLE", 30}};

  std::vector<std::pair<std::string, int>> privs_to_check;
  for (const auto &p : kStdPrivs) {
    if (event->privileges & (1UL << p.second))
      privs_to_check.push_back({p.first, p.second});
  }
  for (const auto &[priv, offset] : privs::global_acls_map) {
    if (!(event->privileges & (1UL << offset))) continue;
    bool dup = false;
    for (const auto &e : privs_to_check) {
      if (e.second == offset) { dup = true; break; }
    }
    if (!dup) privs_to_check.push_back({priv, offset});
  }

  bool is_any_of = (event->requirement_mode ==
                    mysql_authorization_event::MYSQL_AUTHZ_REQ_ANY_OF);
  bool authorized = !is_any_of;
  bool any_checked = false;

  for (const auto &p : privs_to_check) {
    any_checked = true;
    int r = check_single_privilege_embedded(
        user_uid, resource_id, p.first,
        time_ctx.day, time_ctx.date, time_ctx.time, ip, ns);

    if (r == -1) return -1;

    if (is_any_of) {
      if (r == 1) { authorized = true; break; }
    } else {
      if (r == 0) { authorized = false; break; }
    }
  }

  if (!any_checked) return 1;
  return authorized ? 1 : 0;
}

static mysql_authorization_result_t embedded_cedar_check(
    const mysql_authorization_event *event) {
  if (embedded_cedar_collect_stats) get_thread_stats().requests++;

  if (!plugin_initialized || !embedded_cedar_enabled)
    return MYSQL_AUTHORIZATION_IGNORE;

  if (event->requirement_mode ==
      mysql_authorization_event::MYSQL_AUTHZ_REQ_PRESENCE)
    return MYSQL_AUTHORIZATION_GRANT;

  if (event->privileges == 0)
    return MYSQL_AUTHORIZATION_GRANT;

  if (event->event_subclass != MYSQL_AUTHORIZATION_DB_ACCESS &&
      event->event_subclass != MYSQL_AUTHORIZATION_TABLE_ACCESS &&
      event->event_subclass != MYSQL_AUTHORIZATION_COLUMN_ACCESS &&
      event->event_subclass != MYSQL_AUTHORIZATION_ROUTINE_ACCESS)
    return MYSQL_AUTHORIZATION_IGNORE;

  if (!embedded_cedar_enable_column_access &&
      event->event_subclass == MYSQL_AUTHORIZATION_COLUMN_ACCESS) {
    if (embedded_cedar_collect_stats) get_thread_stats().grants++;
    return MYSQL_AUTHORIZATION_GRANT;
  }

  int result;
  if (embedded_cedar_collect_stats) {
    auto t0 = std::chrono::high_resolution_clock::now();
    result = embedded_check_access_core(event);
    auto t1 = std::chrono::high_resolution_clock::now();
    get_thread_stats().total_time_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  } else {
    result = embedded_check_access_core(event);
  }

  if (result == -1) return MYSQL_AUTHORIZATION_IGNORE;
  if (result == 1) {
    if (embedded_cedar_collect_stats) get_thread_stats().grants++;
    return MYSQL_AUTHORIZATION_GRANT;
  }
  if (embedded_cedar_collect_stats) get_thread_stats().denies++;
  return MYSQL_AUTHORIZATION_DENY;
}

static st_mysql_authorization embedded_cedar_descriptor = {
    MYSQL_AUTHORIZATION_INTERFACE_VERSION, embedded_cedar_check};

static void on_reload_update(MYSQL_THD, SYS_VAR *, void *, const void *save) {
  bool new_val = *static_cast<const bool *>(save);
  if (!new_val) return;

  mysql_rwlock_wrlock(&LOCK_cedar_engine);
  bool ok = create_engine_from_files();
  mysql_rwlock_unlock(&LOCK_cedar_engine);

  g_cache.clear();

  if (!ok && plugin_handle)
    my_plugin_log_message(&plugin_handle, MY_ERROR_LEVEL,
                          "embedded_cedar: reload failed");

  embedded_cedar_reload = false;
}

static void on_cache_flush_update(MYSQL_THD, SYS_VAR *, void *, const void *save) {
  bool new_val = *static_cast<const bool *>(save);
  if (!new_val) return;
  g_cache.clear();
  embedded_cedar_cache_flush = false;
}

static void on_reset_stats_update(MYSQL_THD, SYS_VAR *, void *, const void *save) {
  bool new_val = *static_cast<const bool *>(save);
  if (!new_val) return;
  if (!g_stats_registry_initialized) {
    embedded_cedar_reset_stats = false;
    return;
  }
  mysql_mutex_lock(&LOCK_stats_registry);
  for (EmbeddedAuthStats *s : g_stats_registry) {
    s->requests = s->grants = s->denies = s->errors = 0;
    s->cache_hits = s->cache_misses = s->cache_evictions = 0;
    s->total_time_us = s->eval_time_us = 0;
  }
  mysql_mutex_unlock(&LOCK_stats_registry);
  embedded_cedar_reset_stats = false;
}

int embedded_cedar_init(MYSQL_PLUGIN plugin_info) {
  plugin_handle = plugin_info;

  mysql_rwlock_init(0, &LOCK_cedar_engine);
  g_engine_lock_initialized = true;

  g_cache.init();

  mysql_mutex_init(0, &LOCK_stats_registry, MY_MUTEX_INIT_FAST);
  g_stats_registry_initialized = true;

  if (!g_stats_key_initialized) {
    if (pthread_key_create(&g_stats_key, stats_tls_destructor) == 0)
      g_stats_key_initialized = true;
  }

  plugin_initialized = true;

  if (embedded_cedar_policy_file && embedded_cedar_policy_file[0]) {
    mysql_rwlock_wrlock(&LOCK_cedar_engine);
    create_engine_from_files();
    mysql_rwlock_unlock(&LOCK_cedar_engine);
  } else if (plugin_handle) {
    my_plugin_log_message(&plugin_handle, MY_INFORMATION_LEVEL,
                          "embedded_cedar: no policy file configured at startup; "
                          "use SET GLOBAL embedded_cedar_reload=1 after configuring files");
  }

  return 0;
}

int embedded_cedar_deinit(MYSQL_PLUGIN plugin_info [[maybe_unused]]) {
  plugin_initialized = false;

  if (g_engine_lock_initialized) {
    mysql_rwlock_wrlock(&LOCK_cedar_engine);
    if (g_cedar_engine) { cedar_engine_free(g_cedar_engine); g_cedar_engine = nullptr; }
    mysql_rwlock_unlock(&LOCK_cedar_engine);
    mysql_rwlock_destroy(&LOCK_cedar_engine);
    g_engine_lock_initialized = false;
  }

  g_cache.destroy();

  g_stats_registry_initialized = false;
  auth_common::auth_clear_all_client_ip_cache();

  plugin_handle = nullptr;
  return 0;
}

static MYSQL_SYSVAR_BOOL(enabled, embedded_cedar_enabled, PLUGIN_VAR_RQCMDARG,
    "Enable embedded Cedar authorization enforcement", nullptr, nullptr, true);
static MYSQL_SYSVAR_STR(policy_file, embedded_cedar_policy_file,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Path to Cedar policy text file (Cedar policy language syntax)",
    nullptr, nullptr, nullptr);
static MYSQL_SYSVAR_STR(schema_file, embedded_cedar_schema_file,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Path to Cedar schema JSON file",
    nullptr, nullptr, nullptr);
static MYSQL_SYSVAR_STR(entities_file, embedded_cedar_entities_file,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Path to Cedar entities JSON file",
    nullptr, nullptr, nullptr);
static MYSQL_SYSVAR_STR(namespace, embedded_cedar_namespace,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Cedar namespace prefix for entity UIDs (e.g. MySQL)", nullptr, nullptr, "MySQL");
static MYSQL_SYSVAR_BOOL(cache_enabled, embedded_cedar_cache_enabled,
    PLUGIN_VAR_RQCMDARG, "Enable authorization decision cache",
    nullptr, nullptr, true);
static MYSQL_SYSVAR_INT(cache_size, embedded_cedar_cache_size,
    PLUGIN_VAR_RQCMDARG, "Maximum number of cached authorization decisions",
    nullptr, nullptr, 1024, 64, 100000, 0);
static MYSQL_SYSVAR_INT(cache_ttl, embedded_cedar_cache_ttl,
    PLUGIN_VAR_RQCMDARG, "Cache entry TTL in seconds",
    nullptr, nullptr, 300, 1, 86400, 0);
static MYSQL_SYSVAR_BOOL(cache_flush, embedded_cedar_cache_flush,
    PLUGIN_VAR_RQCMDARG,
    "Flush the authorization cache (resets to 0 automatically)",
    nullptr, on_cache_flush_update, false);
static MYSQL_SYSVAR_BOOL(collect_stats, embedded_cedar_collect_stats,
    PLUGIN_VAR_RQCMDARG, "Collect authorization statistics",
    nullptr, nullptr, true);
static MYSQL_SYSVAR_BOOL(reset_stats, embedded_cedar_reset_stats,
    PLUGIN_VAR_RQCMDARG,
    "Reset authorization statistics (resets to 0 automatically)",
    nullptr, on_reset_stats_update, false);
static MYSQL_SYSVAR_BOOL(log_info, embedded_cedar_log_info,
    PLUGIN_VAR_RQCMDARG, "Enable info-level logging (default: disabled)",
    nullptr, nullptr, false);
static MYSQL_SYSVAR_BOOL(enable_column_access, embedded_cedar_enable_column_access,
    PLUGIN_VAR_RQCMDARG,
    "Enable Cedar checks for column-level access (default: disabled)",
    nullptr, nullptr, false);
static MYSQL_SYSVAR_BOOL(reload, embedded_cedar_reload,
    PLUGIN_VAR_RQCMDARG,
    "Reload Cedar engine from configured files (resets to 0 automatically)",
    nullptr, on_reload_update, false);

static SYS_VAR *embedded_cedar_system_vars[] = {
    MYSQL_SYSVAR(enabled),
    MYSQL_SYSVAR(policy_file),
    MYSQL_SYSVAR(schema_file),
    MYSQL_SYSVAR(entities_file),
    MYSQL_SYSVAR(namespace),
    MYSQL_SYSVAR(cache_enabled),
    MYSQL_SYSVAR(cache_size),
    MYSQL_SYSVAR(cache_ttl),
    MYSQL_SYSVAR(cache_flush),
    MYSQL_SYSVAR(collect_stats),
    MYSQL_SYSVAR(reset_stats),
    MYSQL_SYSVAR(log_info),
    MYSQL_SYSVAR(enable_column_access),
    MYSQL_SYSVAR(reload),
    nullptr};

#define DEF_SHOW_STAT(name, member)                                         \
  static int show_##name(MYSQL_THD, SHOW_VAR *var, char *buff) {           \
    int64_t v = aggregate_stat(&EmbeddedAuthStats::member);                 \
    memcpy(buff, &v, sizeof(v));                                            \
    var->type = SHOW_LONGLONG;                                              \
    var->value = buff;                                                      \
    return 0;                                                               \
  }

DEF_SHOW_STAT(ec_requests, requests)
DEF_SHOW_STAT(ec_grants, grants)
DEF_SHOW_STAT(ec_denies, denies)
DEF_SHOW_STAT(ec_errors, errors)
DEF_SHOW_STAT(ec_cache_hits, cache_hits)
DEF_SHOW_STAT(ec_cache_misses, cache_misses)
DEF_SHOW_STAT(ec_cache_evictions, cache_evictions)
DEF_SHOW_STAT(ec_total_time_us, total_time_us)
DEF_SHOW_STAT(ec_eval_time_us, eval_time_us)

static SHOW_VAR embedded_cedar_status_vars[] = {
    {"embedded_cedar_requests",        (char *)&show_ec_requests,        SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"embedded_cedar_grants",          (char *)&show_ec_grants,          SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"embedded_cedar_denies",          (char *)&show_ec_denies,          SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"embedded_cedar_errors",          (char *)&show_ec_errors,          SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"embedded_cedar_cache_hits",      (char *)&show_ec_cache_hits,      SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"embedded_cedar_cache_misses",    (char *)&show_ec_cache_misses,    SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"embedded_cedar_cache_evictions", (char *)&show_ec_cache_evictions, SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"embedded_cedar_total_time_us",   (char *)&show_ec_total_time_us,   SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"embedded_cedar_eval_time_us",    (char *)&show_ec_eval_time_us,    SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {nullptr, nullptr, SHOW_UNDEF, SHOW_SCOPE_UNDEF}};

mysql_declare_plugin(embedded_cedar){
    MYSQL_AUTHORIZATION_PLUGIN,
    &embedded_cedar_descriptor,
    "embedded_cedar",
    PLUGIN_AUTHOR_ORACLE,
    "Embedded Cedar Authorization Plugin (in-process via libcedar)",
    PLUGIN_LICENSE_GPL,
    embedded_cedar_init,
    nullptr,
    embedded_cedar_deinit,
    0x0100,
    embedded_cedar_status_vars,
    embedded_cedar_system_vars,
    nullptr,
    0,
} mysql_declare_plugin_end;
