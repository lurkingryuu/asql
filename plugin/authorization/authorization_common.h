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

#pragma once

#include <cstdint>
#include <string>

// Forward declare JSON type to avoid requiring jsoncpp headers in public header
namespace Json {
class Value;
}

#if __has_include("mysql/plugin_authorization.h")
#include "mysql/plugin_authorization.h"
#else
#include "include/mysql/plugin_authorization.h"
#endif
#include "sql/sql_class.h"

namespace auth_common {

// Event helpers
std::string auth_event_type_to_string(mysql_authorization_event_subclass_t event_type);

// UID/identifier helpers
std::string auth_build_user_uid(const mysql_authorization_event *event);
std::string auth_make_db_id(const mysql_authorization_event *event);
std::string auth_make_table_id(const mysql_authorization_event *event);
std::string auth_make_column_id(const mysql_authorization_event *event);
std::string auth_create_resource_identifier(const mysql_authorization_event *event);

// Context helpers
std::string auth_get_day();
uint32_t auth_get_date();
uint32_t auth_get_time();
std::string auth_get_client_ip(THD *thd);

// Privilege helpers
std::string auth_get_primary_action(unsigned long privileges);
Json::Value auth_privileges_to_json(unsigned long privileges);

// Cedar payload helper (pure construction, no network)
Json::Value auth_build_cedar_payload(const std::string &user_uid,
                                const std::string &resource_identifier,
                                const std::string &privilege,
                                const std::string &day,
                                uint32_t date,
                                uint32_t fmt_time,
                                const std::string &client_ip);

}  // namespace auth_common


