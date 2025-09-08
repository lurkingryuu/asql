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

#define MYSQL_AUTHORIZATION_INTERFACE_VERSION 0x0101

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

#define MYSQL_AUTHORIZATION_ALL                                       \
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

  /**
    Requirement semantics for interpreting privileges at this call site.
    ALL_OF: all listed privileges are required.
    ANY_OF: any one of the listed privileges is sufficient.
    PRESENCE: this is a presence/visibility probe (not a concrete privilege request).
  */
  enum mysql_authorization_requirement_t {
    MYSQL_AUTHZ_REQ_UNSPECIFIED = 0,
    MYSQL_AUTHZ_REQ_ALL_OF = 1,
    MYSQL_AUTHZ_REQ_ANY_OF = 2,
    MYSQL_AUTHZ_REQ_PRESENCE = 3
  } requirement_mode;

  /** Optional: Bitmask of the missing privileges (when applicable). */
  unsigned long missing_privileges;

  /** SQL command being executed */
  int sql_command;

  /** Query text */
  MYSQL_LEX_CSTRING query;

  /** Whether this is a procedure (true) or function (false) for routine checks
   */
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
  /** Plugin doesn't handle this request - fall back to built-in authorization
   */
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

/**
  @namespace privs
  @brief Consts for static privileges, keep it in sync with
  sql/auth/auth_acls.{h,cc} files
*/
namespace privs {
/** Name of the static privileges */
const std::string SELECT("SELECT");
const std::string INSERT("INSERT");
const std::string UPDATE("UPDATE");
const std::string DELETE("DELETE");
const std::string CREATE("CREATE");
const std::string DROP("DROP");
const std::string RELOAD("RELOAD");
const std::string SHUTDOWN("SHUTDOWN");
const std::string PROCESS("PROCESS");
const std::string FILE("FILE");
const std::string GRANT("GRANT");
const std::string REFERENCES("REFERENCES");
const std::string INDEX("INDEX");
const std::string ALTER("ALTER");
const std::string SHOW_DATABASES("SHOW DATABASES");
const std::string SUPER("SUPER");
const std::string CREATE_TEMPORARY_TABLES("CREATE TEMPORARY TABLES");
const std::string LOCK_TABLES("LOCK TABLES");
const std::string EXECUTE("EXECUTE");
const std::string REPLICATION_SLAVE("REPLICATION SLAVE");
const std::string REPLICATION_CLIENT("REPLICATION CLIENT");
const std::string CREATE_VIEW("CREATE VIEW");
const std::string SHOW_VIEW("SHOW VIEW");
const std::string CREATE_ROUTINE("CREATE ROUTINE");
const std::string ALTER_ROUTINE("ALTER ROUTINE");
const std::string CREATE_USER("CREATE USER");
const std::string EVENT("EVENT");
const std::string TRIGGER("TRIGGER");
const std::string CREATE_TABLESPACE("CREATE TABLESPACE");
const std::string CREATE_ROLE("CREATE ROLE");
const std::string DROP_ROLE("DROP ROLE");

/// Bitmap offsets for static privileges, keep it in sync with
/// sql/auth/auth_acls.{h,cc} files
const std::unordered_map<std::string, int> global_acls_map{
    {privs::SELECT, 0},
    {privs::INSERT, 1},
    {privs::UPDATE, 2},
    {privs::DELETE, 3},
    {privs::CREATE, 4},
    {privs::DROP, 5},
    {privs::RELOAD, 6},
    {privs::SHUTDOWN, 7},
    {privs::PROCESS, 8},
    {privs::FILE, 9},
    {privs::GRANT, 10},
    {privs::REFERENCES, 11},
    {privs::INDEX, 12},
    {privs::ALTER, 13},
    {privs::SHOW_DATABASES, 14},
    {privs::SUPER, 15},
    {privs::CREATE_TEMPORARY_TABLES, 16},
    {privs::LOCK_TABLES, 17},
    {privs::EXECUTE, 18},
    {privs::REPLICATION_SLAVE, 19},
    {privs::REPLICATION_CLIENT, 20},
    {privs::CREATE_VIEW, 21},
    {privs::SHOW_VIEW, 22},
    {privs::CREATE_ROUTINE, 23},
    {privs::ALTER_ROUTINE, 24},
    {privs::CREATE_USER, 25},
    {privs::EVENT, 26},
    {privs::TRIGGER, 27},
    {privs::CREATE_TABLESPACE, 28},
    {privs::CREATE_ROLE, 29},
    {privs::DROP_ROLE, 30}};
}  // namespace privs

#endif /* _mysql_authorization_plugin_h */
