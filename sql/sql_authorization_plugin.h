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

#ifndef SQL_AUTHORIZATION_PLUGIN_INCLUDED
#define SQL_AUTHORIZATION_PLUGIN_INCLUDED

#include "mysql/plugin_authorization.h"

class THD;
struct st_plugin_int;

/**
  Initialize authorization plugin.
  
  @param plugin Plugin to initialize
  @return 0 on success, non-zero on error
*/
int initialize_authorization_plugin(st_plugin_int *plugin);

/**
  Finalize authorization plugin.
  
  @param plugin Plugin to finalize
  @return 0 on success, non-zero on error
*/
int finalize_authorization_plugin(st_plugin_int *plugin);

/**
  Check if user has authorization to access a resource.
  
  This function calls all registered authorization plugins to check if the
  user has permission to access the specified resource. If any plugin
  returns MYSQL_AUTHORIZATION_GRANT, access is granted. If all plugins
  return MYSQL_AUTHORIZATION_IGNORE, the built-in authorization system
  is used. If any plugin returns MYSQL_AUTHORIZATION_DENY, access is denied.
  
  @param thd              Thread context
  @param event_subclass   Type of access check
  @param user            User name
  @param host            Host name  
  @param database        Database name (can be NULL)
  @param table           Table name (can be NULL)
  @param column          Column name (can be NULL) 
  @param routine         Routine name (can be NULL)
  @param privileges      Requested privileges bitmask
  @param is_procedure    True if routine is a procedure, false if function
  
  @return MYSQL_AUTHORIZATION_GRANT to allow access
  @return MYSQL_AUTHORIZATION_DENY to deny access
  @return MYSQL_AUTHORIZATION_IGNORE to use built-in authorization
*/
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
    bool is_procedure);

#endif /* SQL_AUTHORIZATION_PLUGIN_INCLUDED */
