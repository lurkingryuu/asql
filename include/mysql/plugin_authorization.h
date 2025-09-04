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

#ifndef _mysql_authorization_plugin_h
#define _mysql_authorization_plugin_h

/**
  @file include/mysql/plugin_authorization.h
  API for Authorization plugin. (MYSQL_AUTHORIZATION_PLUGIN)
*/

#include "mysql/mysql_lex_string.h"
#include "plugin.h"
#ifndef MYSQL_ABI_CHECK
#include "m_string.h"
#endif

#define MYSQL_AUTHORIZATION_INTERFACE_VERSION 0x0100

/**
  @enum mysql_authorization_event_subclass_t
  
  Events for authorization plugin.
*/
typedef enum {
  /** Database level access check */
  MYSQL_AUTHORIZATION_DB_ACCESS = 1 << 0,
  /** Table level access check */
  MYSQL_AUTHORIZATION_TABLE_ACCESS = 1 << 1,
  /** Column level access check */
  MYSQL_AUTHORIZATION_COLUMN_ACCESS = 1 << 2,
  /** Procedure/Function access check */
  MYSQL_AUTHORIZATION_ROUTINE_ACCESS = 1 << 3
} mysql_authorization_event_subclass_t;

#define MYSQL_AUTHORIZATION_ALL \
  (MYSQL_AUTHORIZATION_DB_ACCESS | MYSQL_AUTHORIZATION_TABLE_ACCESS | \
   MYSQL_AUTHORIZATION_COLUMN_ACCESS | MYSQL_AUTHORIZATION_ROUTINE_ACCESS)

/**
  @struct mysql_authorization_event
  
  Structure passed to authorization plugin callback.
*/
struct mysql_authorization_event {
  /** Event subclass indicating the type of access check */
  mysql_authorization_event_subclass_t event_subclass;
  
  /** THD connection context */
  MYSQL_THD thd;
  
  /** User requesting access */
  MYSQL_LEX_CSTRING user;
  
  /** Host of the user */
  MYSQL_LEX_CSTRING host;
  
  /** Database name (can be NULL) */
  MYSQL_LEX_CSTRING database;
  
  /** Table name (can be NULL for DB-level checks) */
  MYSQL_LEX_CSTRING table;
  
  /** Column name (can be NULL for non-column checks) */
  MYSQL_LEX_CSTRING column;
  
  /** Routine name (for procedure/function checks) */
  MYSQL_LEX_CSTRING routine;
  
  /** Requested privileges bitmask */
  unsigned long privileges;
  
  /** SQL command being executed */
  int sql_command;
  
  /** Query text */
  MYSQL_LEX_CSTRING query;
  
  /** Whether this is a procedure (true) or function (false) for routine checks */
  bool is_procedure;
};

/**
  @enum mysql_authorization_result_t
  
  Result returned by authorization plugin callback.
*/
typedef enum {
  /** Plugin grants access - allow the operation */
  MYSQL_AUTHORIZATION_GRANT = 0,
  /** Plugin denies access - deny the operation */
  MYSQL_AUTHORIZATION_DENY = 1,
  /** Plugin doesn't handle this request - fall back to built-in authorization */
  MYSQL_AUTHORIZATION_IGNORE = 2
} mysql_authorization_result_t;

/**
  @struct st_mysql_authorization
  
  The descriptor structure that is referred from st_mysql_plugin.
*/
struct st_mysql_authorization {
  /**
    Interface version.
  */
  int interface_version;
  
  /**
    Authorization check callback.
    
    This function is called by MySQL server whenever it needs to check
    if a user has privileges to perform an operation.
    
    @param event Pointer to authorization event structure containing
                 all necessary information about the access request
    
    @return MYSQL_AUTHORIZATION_GRANT to allow access
    @return MYSQL_AUTHORIZATION_DENY to deny access  
    @return MYSQL_AUTHORIZATION_IGNORE to fall back to built-in checks
  */
  mysql_authorization_result_t (*check_authorization)(
      const struct mysql_authorization_event *event);
};

#endif /* _mysql_authorization_plugin_h */
