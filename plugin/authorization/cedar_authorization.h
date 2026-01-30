/* Public interface for cedar_authorization plugin functions used by tests */

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

#include <ctime>
#include <string>
#include <vector>

#ifdef EXTRA_CODE_FOR_UNIT_TESTING
// Export minimal surface for unit tests (no network in tests)

// Main callback exposed for completeness
mysql_authorization_result_t cedar_check(
    const mysql_authorization_event *event);

// Expose resource builder to validate mapping
std::string cedar_create_resource_identifier(
    const mysql_authorization_event *event);

// Privilege string helper for logging
std::string privileges_to_string(unsigned long privileges);

// Init/deinit for tests
int cedar_authorization_init(MYSQL_PLUGIN plugin_info);
int cedar_authorization_deinit(MYSQL_PLUGIN plugin_info);

// Cache management for tests
void cedar_auth_cache_reset();
size_t cedar_auth_cache_size();
void cedar_set_authorization_url(const char *url);
void cedar_set_cache_enabled(bool enabled);

// Stats testing helpers
int64_t cedar_get_auth_stat_requests();
int64_t cedar_get_auth_stat_grants();
int64_t cedar_get_auth_stat_denies();
void cedar_reset_stats_for_test();
void cedar_set_collect_stats(bool enable);
#endif
