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

#include "sql/sql_authorization_plugin.h"

#include "mysql/plugin_authorization.h"
#include "sql/sql_class.h"
#include "sql/sql_lex.h"
#include "sql/sql_plugin.h"
#include "sql/current_thd.h"

#include <vector>
#include <mutex>

// Global list of registered authorization plugins
static std::vector<st_plugin_int*> authorization_plugins;
static std::mutex authorization_plugins_mutex;

int initialize_authorization_plugin(st_plugin_int *plugin) {
  if (!plugin || !plugin->plugin->info) {
    return 1;
  }

  plugin->plugin->init(plugin);

  st_mysql_authorization *authorization_plugin = 
      static_cast<st_mysql_authorization*>(plugin->plugin->info);
  
  if (authorization_plugin->interface_version != MYSQL_AUTHORIZATION_INTERFACE_VERSION) {
    return 1;
  }

  if (!authorization_plugin->check_authorization) {
    return 1;
  }

  // Add plugin to the global list
  std::lock_guard<std::mutex> lock(authorization_plugins_mutex);
  authorization_plugins.push_back(plugin);

  return 0;
}

int finalize_authorization_plugin(st_plugin_int *plugin) {
  if (!plugin) {
    return 1;
  }

  plugin->plugin->deinit(plugin);

  // Remove plugin from the global list
  std::lock_guard<std::mutex> lock(authorization_plugins_mutex);
  authorization_plugins.erase(
      std::remove(authorization_plugins.begin(), authorization_plugins.end(), plugin),
      authorization_plugins.end());

  return 0;
}

mysql_authorization_result_t mysql_authorization_plugin_check(
    THD *thd,
    mysql_authorization_event_subclass_t event_subclass,
    const char *user,
    const char *host,
    const char *database,
    const char *table,
    const char *column,
    const char *routine,
    unsigned long privileges,
    bool is_procedure,
    mysql_authorization_event::mysql_authorization_requirement_t requirement_mode,
    unsigned long missing_privileges) {

  std::lock_guard<std::mutex> lock(authorization_plugins_mutex);
  
  if (authorization_plugins.empty()) {
    return MYSQL_AUTHORIZATION_IGNORE;
  }

  // Create the event structure
  mysql_authorization_event event;
  event.event_subclass = event_subclass;
  event.thd = thd;
  
  // Set user and host
  event.user.str = user;
  event.user.length = user ? strlen(user) : 0;
  event.host.str = host;
  event.host.length = host ? strlen(host) : 0;
  
  // Set database, table, column, routine
  event.database.str = database;
  event.database.length = database ? strlen(database) : 0;
  event.table.str = table;
  event.table.length = table ? strlen(table) : 0;
  event.column.str = column;
  event.column.length = column ? strlen(column) : 0;
  event.routine.str = routine;
  event.routine.length = routine ? strlen(routine) : 0;
  
  event.privileges = privileges;
  event.is_procedure = is_procedure;
  event.requirement_mode = requirement_mode;
  event.missing_privileges = missing_privileges;
  
  // Get SQL command and query from THD
  event.sql_command = thd ? static_cast<int>(thd->lex->sql_command) : -1;
  if (thd && thd->query().str) {
    event.query.str = thd->query().str;
    event.query.length = thd->query().length;
  } else {
    event.query.str = nullptr;
    event.query.length = 0;
  }

  // Check with all authorization plugins
  for (auto* plugin : authorization_plugins) {
    if (!plugin || !plugin->plugin->info) {
      continue;
    }
    
    st_mysql_authorization *auth_plugin = 
        static_cast<st_mysql_authorization*>(plugin->plugin->info);
    
    if (!auth_plugin->check_authorization) {
      continue;
    }
    
    mysql_authorization_result_t result = auth_plugin->check_authorization(&event);
    
    // If any plugin explicitly grants access, allow it
    if (result == MYSQL_AUTHORIZATION_GRANT) {
      return MYSQL_AUTHORIZATION_GRANT;
    }
    
    // If any plugin explicitly denies access, deny it
    if (result == MYSQL_AUTHORIZATION_DENY) {
      return MYSQL_AUTHORIZATION_DENY;
    }
    
    // Continue checking other plugins if result is IGNORE
  }

  // If all plugins returned IGNORE, fall back to built-in authorization
  return MYSQL_AUTHORIZATION_IGNORE;
}
