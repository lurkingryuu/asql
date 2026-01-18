/* See header for license */

#include "plugin/authorization/authorization_common.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <sstream>

#include <json/json.h>
#include "my_dbug.h"
#include "sql/protocol_classic.h"
#include "violite.h"

using namespace std;

namespace auth_common {

std::string auth_event_type_to_string(
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

std::string auth_build_user_uid(const mysql_authorization_event *event) {
  std::string user = event && event->user.str
                         ? std::string(event->user.str, event->user.length)
                         : std::string("unknown");
  return user;
}

std::string auth_make_db_id(const mysql_authorization_event *event) {
  return (event && event->database.str)
             ? std::string(event->database.str, event->database.length)
             : std::string("");
}

std::string auth_make_table_id(const mysql_authorization_event *event) {
  std::string db = auth_make_db_id(event);
  std::string tbl = (event && event->table.str)
                        ? std::string(event->table.str, event->table.length)
                        : std::string("");
  if (!db.empty() && !tbl.empty()) return db + "." + tbl;
  if (!tbl.empty()) return tbl;
  return db.empty() ? std::string("unknown") : db;
}

std::string auth_make_column_id(const mysql_authorization_event *event) {
  std::string tbl = auth_make_table_id(event);
  std::string col = (event && event->column.str)
                        ? std::string(event->column.str, event->column.length)
                        : std::string("");
  if (!col.empty()) return tbl + "." + col;
  return tbl;
}

std::string auth_create_resource_identifier(
    const mysql_authorization_event *event, const std::string &ns) {
  std::string resource;
  std::string prefix = ns.empty() ? "" : ns + "::";
  switch (event->event_subclass) {
    case MYSQL_AUTHORIZATION_DB_ACCESS: {
      std::string db = auth_make_db_id(event);
      resource = prefix + "Database::\"" +
                 (db.empty() ? std::string("unknown") : db) + "\"";
      break;
    }
    case MYSQL_AUTHORIZATION_TABLE_ACCESS: {
      std::string table_id = auth_make_table_id(event);
      if (!table_id.empty())
        resource = prefix + "Table::\"" + table_id + "\"";
      else
        resource = prefix + "Table::\"unknown\"";
      break;
    }
    case MYSQL_AUTHORIZATION_COLUMN_ACCESS: {
      std::string column_id = auth_make_column_id(event);
      resource = prefix + "Column::\"" + column_id + "\"";
      break;
    }
    case MYSQL_AUTHORIZATION_ROUTINE_ACCESS: {
      std::string db = auth_make_db_id(event);
      std::string routine = event->routine.str ? std::string(event->routine.str)
                                               : std::string("unknown");
      if (!db.empty()) routine = db + "." + routine;
      resource = prefix + "Routine::\"" + routine + "\"";
      break;
    }
    default:
      resource = prefix + "Unknown::\"unknown\"";
      break;
  }
  return resource;
}

std::string auth_get_day() {
  std::time_t currentTime = std::time(0);
  std::tm *now = std::localtime(&currentTime);
  char dayString[4];
  std::strftime(dayString, sizeof(dayString), "%a", now);
  std::string day(dayString);
  std::transform(day.begin(), day.end(), day.begin(), ::tolower);
  return day;
}

uint32_t auth_get_date() {
  std::time_t currentTime = std::time(0);
  std::tm *now = std::localtime(&currentTime);
  char dateString[9];
  std::strftime(dateString, sizeof(dateString), "%Y%m%d", now);
  return std::stoul(dateString);
}

uint32_t auth_get_time() {
  std::time_t currentTime = std::time(0);
  std::tm *now = std::localtime(&currentTime);
  char timeString[7];
  std::strftime(timeString, sizeof(timeString), "%H%M%S", now);
  return std::stoul(timeString);
}

std::string auth_get_client_ip(THD *thd) {
  if (!thd || !thd->get_protocol_classic()) {
    return "unknown";
  }
  Vio *vio = thd->get_protocol_classic()->get_vio();
  if (!vio) {
    return "unknown";
  }
  char ip[INET6_ADDRSTRLEN];
  uint16_t port;
  if (!vio_peer_addr(vio, ip, &port, sizeof(ip))) {
    return std::string(ip);
  }
  return "unknown";
}

std::string auth_get_primary_action(unsigned long privileges) {
  if (privileges & (1 << 0)) return "SELECT";           // SELECT_ACL
  if (privileges & (1 << 1)) return "INSERT";           // INSERT_ACL
  if (privileges & (1 << 2)) return "UPDATE";           // UPDATE_ACL
  if (privileges & (1 << 3)) return "DELETE";           // DELETE_ACL
  if (privileges & (1 << 4)) return "CREATE";           // CREATE_ACL
  if (privileges & (1 << 5)) return "DROP";             // DROP_ACL
  if (privileges & (1 << 13)) return "ALTER";           // ALTER_ACL
  if (privileges & (1 << 6)) return "RELOAD";           // RELOAD_ACL
  if (privileges & (1 << 7)) return "SHUTDOWN";         // SHUTDOWN_ACL
  if (privileges & (1 << 8)) return "PROCESS";          // PROCESS_ACL
  if (privileges & (1 << 9)) return "FILE";             // FILE_ACL
  if (privileges & (1 << 10)) return "GRANT";           // GRANT_ACL
  if (privileges & (1 << 11)) return "REFERENCES";      // REFERENCES_ACL
  if (privileges & (1 << 12)) return "INDEX";           // INDEX_ACL
  if (privileges & (1 << 14)) return "SHOW DATABASES";  // SHOW_DB_ACL
  if (privileges & (1 << 15)) return "SUPER";           // SUPER_ACL
  if (privileges & (1 << 16))
    return "CREATE TEMPORARY TABLES";                       // CREATE_TMP_ACL
  if (privileges & (1 << 17)) return "LOCK TABLES";         // LOCK_TABLES_ACL
  if (privileges & (1 << 18)) return "EXECUTE";             // EXECUTE_ACL
  if (privileges & (1 << 19)) return "REPLICATION SLAVE";   // REPL_SLAVE_ACL
  if (privileges & (1 << 20)) return "REPLICATION CLIENT";  // REPL_CLIENT_ACL
  if (privileges & (1 << 21)) return "CREATE VIEW";         // CREATE_VIEW_ACL
  if (privileges & (1 << 22)) return "SHOW VIEW";           // SHOW_VIEW_ACL
  if (privileges & (1 << 23)) return "CREATE ROUTINE";      // CREATE_PROC_ACL
  if (privileges & (1 << 24)) return "ALTER ROUTINE";       // ALTER_PROC_ACL
  if (privileges & (1 << 25)) return "CREATE USER";         // CREATE_USER_ACL
  if (privileges & (1 << 26)) return "EVENT";               // EVENT_ACL
  if (privileges & (1 << 27)) return "TRIGGER";             // TRIGGER_ACL
  if (privileges & (1 << 28))
    return "CREATE TABLESPACE";                      // CREATE_TABLESPACE_ACL
  if (privileges & (1 << 29)) return "CREATE ROLE";  // CREATE_ROLE_ACL
  if (privileges & (1 << 30)) return "DROP ROLE";    // DROP_ROLE_ACL
  for (const auto &[privilege, offset] : privs::global_acls_map) {
    if (privileges & (1 << offset)) {
      return privilege;
    }
  }
  return "UNKNOWN";
}

Json::Value auth_privileges_to_json(unsigned long privileges) {
  Json::Value privileges_value(Json::arrayValue);
  for (const auto &[privilege, offset] : privs::global_acls_map) {
    if (privileges & (1 << offset)) {
      privileges_value.append(privilege);
    }
  }
  return privileges_value;
}

Json::Value auth_build_cedar_payload(
    const std::string &user_uid, const std::string &resource_identifier,
    const std::string &privilege, const std::string &day, uint32_t date,
    uint32_t fmt_time, const std::string &client_ip, const std::string &ns) {
  Json::Value json_payload;
  std::string prefix = ns.empty() ? "" : ns + "::";

  json_payload["principal"] = prefix + "User::\"" + user_uid + "\"";
  json_payload["action"] = prefix + "Action::\"" + privilege + "\"";
  json_payload["resource"] = prefix + resource_identifier;
  json_payload["context"]["day"] = day;
  json_payload["context"]["date"] = date;
  json_payload["context"]["time"] = fmt_time;
  json_payload["context"]["ip"]["__extn"]["fn"] = "ip";
  json_payload["context"]["ip"]["__extn"]["arg"] = client_ip;
  return json_payload;
}

}  // namespace auth_common
