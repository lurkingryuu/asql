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

#ifndef PLUGIN_DDL_AUDIT_INCLUDED
#define PLUGIN_DDL_AUDIT_INCLUDED

#include <string>
#include <vector>
#include <utility>

// Ensure MYSQL_THD and THD are available to this header's declarations, while
// avoiding hard dependency on MySQL include search paths for standalone lints.
#ifndef MYSQL_THD
#ifdef __cplusplus
class THD;
#define MYSQL_THD THD *
#else
#define MYSQL_THD void *
#endif
#endif

/**
  Check if a given SQL command is a DDL command.

  @param sql_command_id  MySQL SQL command ID

  @retval true if the command is a DDL command
*/
bool is_ddl_command(int sql_command_id);

/**
  Get the human-readable name of a DDL command.

  @param sql_command_id  MySQL SQL command ID

  @return String name of the command (e.g., "CREATE_TABLE")
*/
const char* get_command_name(int sql_command_id);

// Note: Non-LEX parsing helpers have been removed. Use LEX-based helpers below.

/**
  Generate a unique identifier for a user entity.

  For consistency with the Cedar authorization plugin, this creates
  a uniform UID that can be used across both plugins.

  @param user  Username
  @param host  Hostname (not included in UID per plugin design)

  @return User UID string
*/
std::string make_user_uid(const std::string &user, const std::string &host);

/**
  Generate a unique identifier for a database entity.

  @param db  Database name

  @return Database UID string
*/
std::string make_db_uid(const std::string &db);

/**
  Generate a unique identifier for a table entity.

  Combines database and table name into a qualified identifier.

  @param db     Database name
  @param table  Table name

  @return Table UID string (format: "db.table")
*/
std::string make_table_uid(const std::string &db, const std::string &table);

/**
  Extract database name from LEX structure.

  @param thd            MySQL thread context
  @param sql_command_id SQL command ID

  @return Database name or empty string
*/
std::string extract_database_name_from_lex(MYSQL_THD thd, int sql_command_id);

/**
  Extract user information from LEX structure.

  @param thd            MySQL thread context
  @param sql_command_id SQL command ID
  @param users          Output vector for user/host pairs
*/
void extract_users_from_lex(MYSQL_THD thd, int sql_command_id,
                           std::vector<std::pair<std::string, std::string>>& users);

/**
  Extract table information from LEX structure.

  @param thd            MySQL thread context
  @param sql_command_id SQL command ID
  @param out_db         Output database name
  @param out_table      Output table name
*/
void extract_table_from_lex(MYSQL_THD thd, int sql_command_id,
                           std::string &out_db, std::string &out_table);

/**
  Get current timestamp in ISO 8601 format.

  @return Timestamp string
*/
std::string get_current_timestamp();

/**
  Get client IP address from thread context.

  @param thd MySQL thread context

  @return IP address string or "unknown"
*/
std::string get_client_ip(MYSQL_THD thd);

#endif  // PLUGIN_DDL_AUDIT_INCLUDED

