/* Public interface for embedded_cedar plugin functions used by tests */

#pragma once

#if __has_include("mysql/plugin_authorization.h")
#include <mysql/plugin_authorization.h>
#else
#include "include/mysql/plugin_authorization.h"
#endif

#if __has_include("mysql/plugin.h")
#include <mysql/plugin.h>
#else
#include "include/mysql/plugin.h"
#endif

#include <cstddef>
#include <cstdint>

#ifdef EXTRA_CODE_FOR_UNIT_TESTING
mysql_authorization_result_t embedded_cedar_check(
    const mysql_authorization_event *event);

int embedded_cedar_init(MYSQL_PLUGIN plugin_info);
int embedded_cedar_deinit(MYSQL_PLUGIN plugin_info);

void embedded_cedar_set_policy_file_for_test(const char *path);
void embedded_cedar_set_schema_file_for_test(const char *path);
void embedded_cedar_set_entities_file_for_test(const char *path);
void embedded_cedar_set_namespace_for_test(const char *ns);

void embedded_cedar_set_enabled_for_test(bool enabled);
void embedded_cedar_set_collect_stats_for_test(bool enabled);
void embedded_cedar_set_enable_column_access_for_test(bool enabled);
void embedded_cedar_set_cache_enabled_for_test(bool enabled);
void embedded_cedar_set_cache_size_for_test(int size);
void embedded_cedar_set_cache_ttl_for_test(int ttl_seconds);

bool embedded_cedar_reload_for_test();
void embedded_cedar_cache_flush_for_test();
void embedded_cedar_reset_stats_for_test();

size_t embedded_cedar_cache_size_for_test();
size_t embedded_cedar_cache_key_shard_index_for_test(const char *user,
                                                     const char *resource,
                                                     const char *action,
                                                     const char *day,
                                                     uint32_t date,
                                                     const char *ip);
bool embedded_cedar_cache_contains_for_test(const char *user,
                                            const char *resource,
                                            const char *action,
                                            const char *day,
                                            uint32_t date,
                                            const char *ip);

int64_t embedded_cedar_get_auth_stat_requests();
int64_t embedded_cedar_get_auth_stat_grants();
int64_t embedded_cedar_get_auth_stat_denies();
int64_t embedded_cedar_get_auth_stat_errors();
int64_t embedded_cedar_get_auth_stat_cache_hits();
int64_t embedded_cedar_get_auth_stat_cache_misses();
int64_t embedded_cedar_get_auth_stat_cache_evictions();
#endif
