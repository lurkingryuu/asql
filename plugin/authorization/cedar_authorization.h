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

#include <string>

// Export minimal surface for unit tests (no network in tests)

// Access check core returns -1 (IGNORE), 0 (DENY), 1 (GRANT for all privs)
int cedar_check_access_core(const mysql_authorization_event *event);

// Main callback exposed for completeness
mysql_authorization_result_t cedar_check(const mysql_authorization_event *event);

// Expose resource builder to validate mapping
std::string cedar_create_resource_identifier(const mysql_authorization_event *event);

// Privilege string helper for logging
std::string privileges_to_string(unsigned long privileges);

// Init/deinit for tests
int cedar_authorization_init(MYSQL_PLUGIN plugin_info);
int cedar_authorization_deinit(MYSQL_PLUGIN plugin_info);


