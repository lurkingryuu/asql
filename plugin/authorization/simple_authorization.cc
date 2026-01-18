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
  @file plugin/authorization/simple_authorization.cc

  Simple Authorization Plugin Example

  This plugin demonstrates how to implement a basic authorization plugin
  that provides custom authorization logic without external dependencies.

  Features:
  - Simple rule-based authorization
  - Grants access to specific users on specific databases
  - Configurable allow/deny lists
  - Educational example for plugin development

  Configuration:
  INSTALL PLUGIN simple_authorization SONAME 'simple_authorization.so';
  SET GLOBAL simple_authorization_allow_user = 'testuser';
  SET GLOBAL simple_authorization_allow_db = 'testdb';
  SET GLOBAL simple_authorization_mode = 'grant';  -- 'grant', 'deny', or
  'ignore'
*/

#include <mysql/plugin.h>
#include <mysql/plugin_authorization.h>
#include <mysql/service_mysql_alloc.h>

#include "sql/auth/auth_acls.h"  // SELECT_ACL

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define PLUGIN_SIMPLE_AUTHORIZATION_NAME "simple_authorization"
#define PLUGIN_SIMPLE_AUTHORIZATION_AUTHOR "Karthikeya"
#define PLUGIN_SIMPLE_AUTHORIZATION_DESCRIPTION "Simple Authorization Plugin"
#define PLUGIN_SIMPLE_AUTHORIZATION_VERSION 0x0001

// Helper function to get thread and process info
[[maybe_unused]] static const char *get_thread_id() {
  static __thread char tid_str[32];
  snprintf(tid_str, sizeof(tid_str), "%lu", (unsigned long)pthread_self());
  return tid_str;
}

[[maybe_unused]] static const char *get_process_id() {
  static __thread char pid_str[32];
  snprintf(pid_str, sizeof(pid_str), "%d", getpid());
  return pid_str;
}

// Plugin system variables
static char *simple_authorization_allow_user = nullptr;
static char *simple_authorization_allow_db = nullptr;
static char *simple_authorization_mode = nullptr;

// Plugin initialization flag
static bool plugin_initialized = false;

// Convert event subclass to string for logging
[[maybe_unused]] static const char *event_type_to_string(
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

// Simple authorization logic
static mysql_authorization_result_t simple_authorization_logic(
    const mysql_authorization_event *event) {
  if (!plugin_initialized) {
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  // Check plugin mode
  if (!simple_authorization_mode) {
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  if (strcmp(simple_authorization_mode, "ignore") == 0) {
    return MYSQL_AUTHORIZATION_IGNORE;
  } else if (strcmp(simple_authorization_mode, "deny") == 0) {
    return MYSQL_AUTHORIZATION_DENY;
  } else if (strcmp(simple_authorization_mode, "grant") == 0) {
    // Check if user is in allow list
    if (!simple_authorization_allow_user || !event->user.str) {
      return MYSQL_AUTHORIZATION_IGNORE;
    }

    if (strcmp(simple_authorization_allow_user, event->user.str) == 0) {
      // Check if database is in allow list (if specified)
      if (simple_authorization_allow_db &&
          strlen(simple_authorization_allow_db) > 0) {
        // Database restriction is set
        if (event->database.str &&
            strcmp(simple_authorization_allow_db, event->database.str) == 0) {
          // For DB_ACCESS, grant full access to the allowed database
          if (event->event_subclass == MYSQL_AUTHORIZATION_DB_ACCESS) {
            return MYSQL_AUTHORIZATION_GRANT;
          }
          // For table/column/routine access, only grant SELECT privilege
          if ((event->privileges & ~SELECT_ACL) != 0) {
            return MYSQL_AUTHORIZATION_IGNORE;  // Let built-in handle
                                                // non-SELECT
          }
          return MYSQL_AUTHORIZATION_GRANT;
        } else {
          return MYSQL_AUTHORIZATION_IGNORE;  // Not our target database
        }
      } else {
        // No database restriction
        // For DB_ACCESS, grant access to any database
        if (event->event_subclass == MYSQL_AUTHORIZATION_DB_ACCESS) {
          return MYSQL_AUTHORIZATION_GRANT;
        }
        // For table/column/routine access, only grant SELECT privilege
        if ((event->privileges & ~SELECT_ACL) != 0) {
          return MYSQL_AUTHORIZATION_IGNORE;  // Let built-in handle non-SELECT
        }
        return MYSQL_AUTHORIZATION_GRANT;
      }
    }
  }

  return MYSQL_AUTHORIZATION_IGNORE;
}

// Main authorization callback function
static mysql_authorization_result_t simple_authorization_check(
    const mysql_authorization_event *event) {
  // Basic validation
  if (!event) {
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  mysql_authorization_result_t result = simple_authorization_logic(event);

  // Simple logging to error log (could be enhanced)
  [[maybe_unused]] const char *result_str =
      (result == MYSQL_AUTHORIZATION_GRANT)  ? "GRANT"
      : (result == MYSQL_AUTHORIZATION_DENY) ? "DENY"
                                             : "IGNORE";

  [[maybe_unused]] const char *user_str =
      event->user.str ? event->user.str : "unknown";
  [[maybe_unused]] const char *db_str =
      event->database.str ? event->database.str : "unknown";
  [[maybe_unused]] const char *table_str =
      event->table.str ? event->table.str : "";

  return result;
}

// Plugin descriptor
static st_mysql_authorization simple_authorization_descriptor = {
    MYSQL_AUTHORIZATION_INTERFACE_VERSION, simple_authorization_check};

// Plugin initialization
static int simple_authorization_init(MYSQL_PLUGIN plugin_info
                                     [[maybe_unused]]) {
  // Store plugin reference
  plugin_initialized = true;
  return 0;
}

// Plugin deinitialization
static int simple_authorization_deinit(MYSQL_PLUGIN plugin_info
                                       [[maybe_unused]]) {
  plugin_initialized = false;
  return 0;
}

// System variables
static MYSQL_SYSVAR_STR(allow_user,                                 // name
                        simple_authorization_allow_user,            // var
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,  // flags
                        "User name to grant access to",             // comment
                        nullptr,                                    // check
                        nullptr,                                    // update
                        nullptr                                     // default
);

static MYSQL_SYSVAR_STR(allow_db,                                   // name
                        simple_authorization_allow_db,              // var
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,  // flags
                        "Database name to grant access to",         // comment
                        nullptr,                                    // check
                        nullptr,                                    // update
                        nullptr                                     // default
);

static MYSQL_SYSVAR_STR(
    mode,                                          // name
    simple_authorization_mode,                     // var
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,     // flags
    "Authorization mode: grant, deny, or ignore",  // comment
    nullptr,                                       // check
    nullptr,                                       // update
    nullptr                                        // default
);

// System variables array
static SYS_VAR *simple_authorization_system_vars[] = {
    MYSQL_SYSVAR(allow_user), MYSQL_SYSVAR(allow_db), MYSQL_SYSVAR(mode),
    nullptr};

// Plugin declaration
mysql_declare_plugin(simple_authorization){
    MYSQL_AUTHORIZATION_PLUGIN,               // type
    &simple_authorization_descriptor,         // descriptor
    PLUGIN_SIMPLE_AUTHORIZATION_NAME,         // name
    PLUGIN_SIMPLE_AUTHORIZATION_AUTHOR,       // author
    PLUGIN_SIMPLE_AUTHORIZATION_DESCRIPTION,  // description
    PLUGIN_LICENSE_GPL,                       // license
    simple_authorization_init,                // init function
    nullptr,                                  // check_uninstall
    simple_authorization_deinit,              // deinit function
    PLUGIN_SIMPLE_AUTHORIZATION_VERSION,      // version
    nullptr,                                  // status vars
    simple_authorization_system_vars,         // system vars
    nullptr,                                  // config options
    0,                                        // flags
} mysql_declare_plugin_end;
