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

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <json/json.h>
#include "lex_string.h"
#include "mysql/plugin.h"
#include "mysql/plugin_audit.h"
#include "plugin/ddl_audit/ddl_audit.h"

// Forward declarations for non-header symbols from ddl_audit.cc
extern Json::Value create_ddl_data(
    MYSQL_THD thd, const std::string &ddl_type,
    enum_sql_command_t sql_command_id, const std::string &query,
    const std::string &database, const std::string &table,
    const std::string &event_class_name, const std::string &event_subclass_name,
    const Json::Value &extra_context = Json::Value());

extern int ddl_audit_plugin_init(MYSQL_PLUGIN plugin_info);
extern int ddl_audit_plugin_deinit(MYSQL_PLUGIN plugin_info);
extern int ddl_audit_notify(MYSQL_THD thd, mysql_event_class_t event_class,
                            const void *event);
#include "my_sqlcommand.h"
#include "sql/sql_class.h"
#include "sql/sql_lex.h"
#include "unittest/gunit/parsertest.h"

namespace ddl_audit_unittest {

class DDL_audit_test : public ::testing::Test {
 public:
  DDL_audit_test() = default;
  ~DDL_audit_test() = default;
};

// Test class for functions that require server context (THD, LEX structures)
class DDL_audit_server_test : public ParserTest {
 public:
  DDL_audit_server_test() = default;
  ~DDL_audit_server_test() = default;

 protected:
  // Helper to parse a query and get the THD
  THD *parse_query(const char *query) {
    // Parse the query using the base class method
    parse(query, 0);  // 0 = no expected error
    return thd();
  }

  // Helper to get LEX structure after parsing
  LEX *get_lex() { return thd()->lex; }
};

// Test DDL command detection
TEST_F(DDL_audit_test, IsDDLCommand) {
  // Table DDL commands
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_TABLE));
  EXPECT_TRUE(is_ddl_command(SQLCOM_ALTER_TABLE));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_TABLE));
  EXPECT_TRUE(is_ddl_command(SQLCOM_RENAME_TABLE));

  // Index DDL commands
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_INDEX));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_INDEX));

  // Database DDL commands
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_DB));
  EXPECT_TRUE(is_ddl_command(SQLCOM_ALTER_DB));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_DB));

  // User DDL commands
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_USER));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_USER));
  EXPECT_TRUE(is_ddl_command(SQLCOM_RENAME_USER));
  EXPECT_TRUE(is_ddl_command(SQLCOM_ALTER_USER));

  // Function/Procedure DDL commands
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_FUNCTION));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_FUNCTION));
  EXPECT_TRUE(is_ddl_command(SQLCOM_ALTER_FUNCTION));
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_PROCEDURE));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_PROCEDURE));
  EXPECT_TRUE(is_ddl_command(SQLCOM_ALTER_PROCEDURE));

  // View DDL commands
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_VIEW));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_VIEW));

  // Trigger DDL commands
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_TRIGGER));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_TRIGGER));

  // Event DDL commands
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_EVENT));
  EXPECT_TRUE(is_ddl_command(SQLCOM_ALTER_EVENT));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_EVENT));

  // Role DDL commands
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_ROLE));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_ROLE));

  // Other DDL commands
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_SERVER));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_SERVER));
  EXPECT_TRUE(is_ddl_command(SQLCOM_ALTER_SERVER));
  EXPECT_TRUE(is_ddl_command(SQLCOM_ALTER_TABLESPACE));
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_RESOURCE_GROUP));
  EXPECT_TRUE(is_ddl_command(SQLCOM_ALTER_RESOURCE_GROUP));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_RESOURCE_GROUP));
  EXPECT_TRUE(is_ddl_command(SQLCOM_CREATE_SRS));
  EXPECT_TRUE(is_ddl_command(SQLCOM_DROP_SRS));

  // Non-DDL commands should return false
  EXPECT_FALSE(is_ddl_command(SQLCOM_SELECT));
  EXPECT_FALSE(is_ddl_command(SQLCOM_INSERT));
  EXPECT_FALSE(is_ddl_command(SQLCOM_UPDATE));
  EXPECT_FALSE(is_ddl_command(SQLCOM_DELETE));
  EXPECT_FALSE(is_ddl_command(SQLCOM_SHOW_DATABASES));
  EXPECT_FALSE(is_ddl_command(SQLCOM_SHOW_TABLES));
}

// Test command name retrieval
TEST_F(DDL_audit_test, GetCommandName) {
  // Test major DDL command names
  EXPECT_STREQ(get_command_name(SQLCOM_CREATE_TABLE), "CREATE_TABLE");
  EXPECT_STREQ(get_command_name(SQLCOM_ALTER_TABLE), "ALTER_TABLE");
  EXPECT_STREQ(get_command_name(SQLCOM_DROP_TABLE), "DROP_TABLE");
  EXPECT_STREQ(get_command_name(SQLCOM_CREATE_DB), "CREATE_DATABASE");
  EXPECT_STREQ(get_command_name(SQLCOM_DROP_DB), "DROP_DATABASE");
  EXPECT_STREQ(get_command_name(SQLCOM_CREATE_USER), "CREATE_USER");
  EXPECT_STREQ(get_command_name(SQLCOM_DROP_USER), "DROP_USER");
  EXPECT_STREQ(get_command_name(SQLCOM_RENAME_USER), "RENAME_USER");
  EXPECT_STREQ(get_command_name(SQLCOM_CREATE_INDEX), "CREATE_INDEX");
  EXPECT_STREQ(get_command_name(SQLCOM_DROP_INDEX), "DROP_INDEX");
  EXPECT_STREQ(get_command_name(SQLCOM_CREATE_VIEW), "CREATE_VIEW");
  EXPECT_STREQ(get_command_name(SQLCOM_DROP_VIEW), "DROP_VIEW");
  EXPECT_STREQ(get_command_name(SQLCOM_CREATE_TRIGGER), "CREATE_TRIGGER");
  EXPECT_STREQ(get_command_name(SQLCOM_DROP_TRIGGER), "DROP_TRIGGER");
  EXPECT_STREQ(get_command_name(SQLCOM_RENAME_TABLE), "RENAME_TABLE");

  // Test unknown command
  EXPECT_STREQ(get_command_name(9999), "UNKNOWN");
}

// Test UID generation functions
TEST_F(DDL_audit_test, MakeUserUID) {
  // Basic user UID (note: host is not included per plugin design)
  EXPECT_EQ(make_user_uid("root", "localhost"), "User::\"root\"");
  EXPECT_EQ(make_user_uid("admin", "192.168.1.1"), "User::\"admin\"");
  EXPECT_EQ(make_user_uid("testuser", "%"), "User::\"testuser\"");

  // User UID with special characters
  EXPECT_EQ(make_user_uid("user_123", "host.domain.com"), "User::\"user_123\"");
  EXPECT_EQ(make_user_uid("user@domain", "localhost"), "User::\"user@domain\"");

  // Empty values
  EXPECT_EQ(make_user_uid("", "localhost"), "User::\"\"");
  EXPECT_EQ(make_user_uid("user", ""), "User::\"user\"");
}

TEST_F(DDL_audit_test, MakeDatabaseUID) {
  // Basic database UID
  EXPECT_EQ(make_db_uid("test_db"), "Database::\"test_db\"");
  EXPECT_EQ(make_db_uid("mysql"), "Database::\"mysql\"");
  EXPECT_EQ(make_db_uid("information_schema"),
            "Database::\"information_schema\"");

  // Database with special characters
  EXPECT_EQ(make_db_uid("db-with-dashes"), "Database::\"db-with-dashes\"");
  EXPECT_EQ(make_db_uid("db_with_underscores"),
            "Database::\"db_with_underscores\"");

  // Empty database
  EXPECT_EQ(make_db_uid(""), "Database::\"\"");
}

TEST_F(DDL_audit_test, MakeTableUID) {
  // Basic table UID with database
  EXPECT_EQ(make_table_uid("test_db", "users"), "Table::\"test_db.users\"");
  EXPECT_EQ(make_table_uid("mysql", "user"), "Table::\"mysql.user\"");

  // Table UID without database
  EXPECT_EQ(make_table_uid("", "standalone_table"),
            "Table::\"standalone_table\"");

  // Table UID with only database
  EXPECT_EQ(make_table_uid("only_db", ""), "Table::\"only_db\"");

  // Table UID with special characters
  EXPECT_EQ(make_table_uid("test-db", "my_table"),
            "Table::\"test-db.my_table\"");
  EXPECT_EQ(make_table_uid("db_123", "table_456"),
            "Table::\"db_123.table_456\"");

  // Empty values
  EXPECT_EQ(make_table_uid("", ""), "Table::\"\"");
}

// Test UID generation for real-world scenarios
TEST_F(DDL_audit_test, UIDGeneration_RealWorldScenarios) {
  // Scenario: Multi-tenant application
  EXPECT_EQ(make_db_uid("tenant_1"), "Database::\"tenant_1\"");
  EXPECT_EQ(make_table_uid("tenant_1", "customers"),
            "Table::\"tenant_1.customers\"");
  EXPECT_EQ(make_table_uid("tenant_2", "customers"),
            "Table::\"tenant_2.customers\"");

  // Scenario: System users
  EXPECT_EQ(make_user_uid("root", "localhost"), "User::\"root\"");
  EXPECT_EQ(make_user_uid("mysql.sys", "localhost"), "User::\"mysql.sys\"");
  EXPECT_EQ(make_user_uid("mysql.infoschema", "localhost"),
            "User::\"mysql.infoschema\"");

  // Scenario: Cross-database operations
  EXPECT_EQ(make_table_uid("source_db", "data"), "Table::\"source_db.data\"");
  EXPECT_EQ(make_table_uid("target_db", "data"), "Table::\"target_db.data\"");
}

// Test DDL command categorization
TEST_F(DDL_audit_test, DDLCommandCategories) {
  // All supported DDL commands should return true
  std::vector<int> all_ddl_commands = {SQLCOM_CREATE_TABLE,
                                       SQLCOM_ALTER_TABLE,
                                       SQLCOM_DROP_TABLE,
                                       SQLCOM_RENAME_TABLE,
                                       SQLCOM_CREATE_INDEX,
                                       SQLCOM_DROP_INDEX,
                                       SQLCOM_CREATE_DB,
                                       SQLCOM_ALTER_DB,
                                       SQLCOM_DROP_DB,
                                       SQLCOM_CREATE_USER,
                                       SQLCOM_DROP_USER,
                                       SQLCOM_RENAME_USER,
                                       SQLCOM_ALTER_USER,
                                       SQLCOM_CREATE_FUNCTION,
                                       SQLCOM_DROP_FUNCTION,
                                       SQLCOM_ALTER_FUNCTION,
                                       SQLCOM_CREATE_PROCEDURE,
                                       SQLCOM_DROP_PROCEDURE,
                                       SQLCOM_ALTER_PROCEDURE,
                                       SQLCOM_CREATE_VIEW,
                                       SQLCOM_DROP_VIEW,
                                       SQLCOM_CREATE_TRIGGER,
                                       SQLCOM_DROP_TRIGGER,
                                       SQLCOM_CREATE_EVENT,
                                       SQLCOM_ALTER_EVENT,
                                       SQLCOM_DROP_EVENT,
                                       SQLCOM_CREATE_SERVER,
                                       SQLCOM_DROP_SERVER,
                                       SQLCOM_ALTER_SERVER,
                                       SQLCOM_CREATE_ROLE,
                                       SQLCOM_DROP_ROLE,
                                       SQLCOM_ALTER_TABLESPACE,
                                       SQLCOM_CREATE_RESOURCE_GROUP,
                                       SQLCOM_ALTER_RESOURCE_GROUP,
                                       SQLCOM_DROP_RESOURCE_GROUP,
                                       SQLCOM_CREATE_SRS,
                                       SQLCOM_DROP_SRS};

  for (int cmd : all_ddl_commands) {
    EXPECT_TRUE(is_ddl_command(cmd)) << "Command " << get_command_name(cmd)
                                     << " should be recognized as DDL";
  }

  // Common non-DDL commands should return false
  std::vector<int> non_ddl_commands = {
      SQLCOM_SELECT,      SQLCOM_INSERT,         SQLCOM_UPDATE,
      SQLCOM_DELETE,      SQLCOM_SHOW_DATABASES, SQLCOM_SHOW_TABLES,
      SQLCOM_SHOW_CREATE, SQLCOM_SET_OPTION,     SQLCOM_BEGIN,
      SQLCOM_COMMIT,      SQLCOM_ROLLBACK};

  for (int cmd : non_ddl_commands) {
    EXPECT_FALSE(is_ddl_command(cmd))
        << "Command " << cmd << " should NOT be recognized as DDL";
  }
}

// ===== SERVER CONTEXT TESTS =====
// These tests require THD and LEX structures, so they inherit from ParserTest

TEST_F(DDL_audit_server_test, ExtractDatabaseNameFromLex) {
  // Test CREATE DATABASE
  THD *thd = parse_query("CREATE DATABASE test_db");
  EXPECT_EQ(extract_database_name_from_lex(thd, SQLCOM_CREATE_DB), "test_db");

  // Test CREATE DATABASE IF NOT EXISTS
  thd = parse_query("CREATE DATABASE IF NOT EXISTS another_db");
  EXPECT_EQ(extract_database_name_from_lex(thd, SQLCOM_CREATE_DB),
            "another_db");

  // Test DROP DATABASE
  thd = parse_query("DROP DATABASE test_db");
  EXPECT_EQ(extract_database_name_from_lex(thd, SQLCOM_DROP_DB), "test_db");

  // Test ALTER DATABASE
  thd = parse_query("ALTER DATABASE test_db CHARACTER SET utf8mb4");
  EXPECT_EQ(extract_database_name_from_lex(thd, SQLCOM_ALTER_DB), "test_db");

  // Test table operations (should return current database context, not from
  // LEX)
  thd = parse_query("CREATE TABLE users (id INT)");
  // For table operations, it should return the current database, not extract
  // from LEX
  std::string db_name =
      extract_database_name_from_lex(thd, SQLCOM_CREATE_TABLE);
  // The result depends on the test setup, but it should be non-empty
  EXPECT_FALSE(db_name.empty());
}

TEST_F(DDL_audit_server_test, ExtractUsersFromLex) {
  // Test CREATE USER single user
  THD *thd = parse_query(
      "CREATE USER 'testuser'@'localhost' IDENTIFIED BY 'password'");
  std::vector<std::pair<std::string, std::string>> users;
  extract_users_from_lex(thd, SQLCOM_CREATE_USER, users);
  ASSERT_EQ(users.size(), 1);
  EXPECT_EQ(users[0].first, "testuser");
  EXPECT_EQ(users[0].second, "localhost");

  // Test CREATE USER with host wildcard
  thd = parse_query("CREATE USER 'admin'@'%' IDENTIFIED BY 'password'");
  users.clear();
  extract_users_from_lex(thd, SQLCOM_CREATE_USER, users);
  ASSERT_EQ(users.size(), 1);
  EXPECT_EQ(users[0].first, "admin");
  EXPECT_EQ(users[0].second, "%");

  // Test CREATE USER multiple users
  thd = parse_query(
      "CREATE USER 'user1'@'host1', 'user2'@'host2' IDENTIFIED BY 'password'");
  users.clear();
  extract_users_from_lex(thd, SQLCOM_CREATE_USER, users);
  ASSERT_EQ(users.size(), 2);
  EXPECT_EQ(users[0].first, "user1");
  EXPECT_EQ(users[0].second, "host1");
  EXPECT_EQ(users[1].first, "user2");
  EXPECT_EQ(users[1].second, "host2");

  // Test DROP USER
  thd = parse_query("DROP USER 'testuser'@'localhost'");
  users.clear();
  extract_users_from_lex(thd, SQLCOM_DROP_USER, users);
  ASSERT_EQ(users.size(), 1);
  EXPECT_EQ(users[0].first, "testuser");
  EXPECT_EQ(users[0].second, "localhost");

  // Test ALTER USER
  thd = parse_query(
      "ALTER USER 'testuser'@'localhost' IDENTIFIED BY 'newpassword'");
  users.clear();
  extract_users_from_lex(thd, SQLCOM_ALTER_USER, users);
  ASSERT_EQ(users.size(), 1);
  EXPECT_EQ(users[0].first, "testuser");
  EXPECT_EQ(users[0].second, "localhost");

  // Test non-user command (should return empty)
  thd = parse_query("CREATE TABLE test (id INT)");
  users.clear();
  extract_users_from_lex(thd, SQLCOM_CREATE_TABLE, users);
  EXPECT_EQ(users.size(), 0);
}

TEST_F(DDL_audit_server_test, ExtractTableFromLex) {
  // Test CREATE TABLE
  THD *thd = parse_query("CREATE TABLE users (id INT, name VARCHAR(100))");
  std::string out_db, out_table;
  extract_table_from_lex(thd, SQLCOM_CREATE_TABLE, out_db, out_table);
  EXPECT_EQ(out_table, "users");
  // out_db may be empty or contain the current database

  // Test CREATE TABLE with IF NOT EXISTS
  thd = parse_query("CREATE TABLE IF NOT EXISTS products (id INT)");
  out_db.clear();
  out_table.clear();
  extract_table_from_lex(thd, SQLCOM_CREATE_TABLE, out_db, out_table);
  EXPECT_EQ(out_table, "products");

  // Test CREATE TABLE with quoted name
  thd = parse_query("CREATE TABLE `my_table` (id INT)");
  out_db.clear();
  out_table.clear();
  extract_table_from_lex(thd, SQLCOM_CREATE_TABLE, out_db, out_table);
  EXPECT_EQ(out_table, "my_table");

  // Test ALTER TABLE
  thd = parse_query("ALTER TABLE users ADD COLUMN email VARCHAR(255)");
  out_db.clear();
  out_table.clear();
  extract_table_from_lex(thd, SQLCOM_ALTER_TABLE, out_db, out_table);
  EXPECT_EQ(out_table, "users");

  // Test DROP TABLE
  thd = parse_query("DROP TABLE products");
  out_db.clear();
  out_table.clear();
  extract_table_from_lex(thd, SQLCOM_DROP_TABLE, out_db, out_table);
  EXPECT_EQ(out_table, "products");

  // Test RENAME TABLE
  thd = parse_query("RENAME TABLE old_table TO new_table");
  out_db.clear();
  out_table.clear();
  extract_table_from_lex(thd, SQLCOM_RENAME_TABLE, out_db, out_table);
  EXPECT_EQ(out_table, "old_table");

  // Test CREATE INDEX
  thd = parse_query("CREATE INDEX idx_name ON users (name)");
  out_db.clear();
  out_table.clear();
  extract_table_from_lex(thd, SQLCOM_CREATE_INDEX, out_db, out_table);
  EXPECT_EQ(out_table, "users");

  // Test DROP INDEX
  thd = parse_query("DROP INDEX idx_name ON users");
  out_db.clear();
  out_table.clear();
  extract_table_from_lex(thd, SQLCOM_DROP_INDEX, out_db, out_table);
  EXPECT_EQ(out_table, "users");

  // Test non-table command (should return empty)
  thd = parse_query("CREATE DATABASE test_db");
  out_db.clear();
  out_table.clear();
  extract_table_from_lex(thd, SQLCOM_CREATE_DB, out_db, out_table);
  EXPECT_EQ(out_table, "");
}

TEST_F(DDL_audit_server_test, GetCurrentTimestamp) {
  // Test that we get a valid timestamp
  std::string timestamp = get_current_timestamp();
  EXPECT_FALSE(timestamp.empty());

  // Should be in ISO 8601 format: YYYY-MM-DDTHH:MM:SSZ
  EXPECT_EQ(timestamp.length(), 20);  // Length of ISO 8601 format
  EXPECT_EQ(timestamp[10], 'T');      // T separator
  EXPECT_EQ(timestamp.back(), 'Z');   // Z suffix

  // Should contain valid date/time components
  // This is a basic format check - in a real test suite you might want more
  // validation
  for (size_t i = 0; i < timestamp.length(); ++i) {
    if (i != 10 && i != timestamp.length() - 1) {
      EXPECT_TRUE(std::isdigit(timestamp[i]) || timestamp[i] == '-' ||
                  timestamp[i] == ':');
    }
  }
}

TEST_F(DDL_audit_server_test, GetClientIP) {
  // Test with THD context
  THD *thd = parse_query("SELECT 1");

  // In unit test environment, this will likely return "unknown"
  // since there's no real network connection
  std::string ip = get_client_ip(thd);
  EXPECT_FALSE(ip.empty());

  // The result should be "unknown" in test environment
  // In real usage it would return an IP address
  EXPECT_TRUE(ip == "unknown" || ip.find('.') != std::string::npos ||
              ip.find(':') != std::string::npos);
}

TEST_F(DDL_audit_server_test, ComplexDDLExtraction) {
  // Test complex CREATE TABLE with various options
  THD *thd = parse_query(
      "CREATE TABLE IF NOT EXISTS `test_db`.`orders` ("
      "  id INT AUTO_INCREMENT PRIMARY KEY,"
      "  customer_id INT NOT NULL,"
      "  order_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
      "  total DECIMAL(10,2) NOT NULL,"
      "  INDEX idx_customer (customer_id),"
      "  FOREIGN KEY (customer_id) REFERENCES customers(id)"
      ") DEFAULT CHARSET=utf8mb4");

  std::string out_db, out_table;
  extract_table_from_lex(thd, SQLCOM_CREATE_TABLE, out_db, out_table);
  EXPECT_EQ(out_table, "orders");

  // Test database name extraction for database operations
  thd = parse_query(
      "CREATE DATABASE `complex_db` CHARACTER SET utf8mb4 COLLATE "
      "utf8mb4_unicode_ci");
  std::string db_name = extract_database_name_from_lex(thd, SQLCOM_CREATE_DB);
  EXPECT_EQ(db_name, "complex_db");

  // Test multiple user operations
  thd = parse_query(
      "CREATE USER 'app_user'@'app.example.com' IDENTIFIED BY 'password1',"
      "           'admin'@'localhost' IDENTIFIED BY 'password2',"
      "           'readonly'@'%' IDENTIFIED BY 'password3'");
  std::vector<std::pair<std::string, std::string>> users;
  extract_users_from_lex(thd, SQLCOM_CREATE_USER, users);
  ASSERT_EQ(users.size(), 3);
  EXPECT_EQ(users[0].first, "app_user");
  EXPECT_EQ(users[0].second, "app.example.com");
  EXPECT_EQ(users[1].first, "admin");
  EXPECT_EQ(users[1].second, "localhost");
  EXPECT_EQ(users[2].first, "readonly");
  EXPECT_EQ(users[2].second, "%");
}

TEST_F(DDL_audit_server_test, RenameTableLogic) {
  // Clear any previous calls
  ddl_audit_test_reset();

  // Create a fake RENAME statement
  // We need to construct a scenario where extract_rename_tables_from_lex works
  // This is tricky with ParserTest as we need the full LEX populated correctly
  // for RENAME

  THD *thd = parse_query("RENAME TABLE db1.t1 TO db2.t2, t3 TO t4");

  std::vector<std::pair<std::pair<std::string, std::string>,
                        std::pair<std::string, std::string>>>
      tables;

  extract_rename_tables_from_lex(thd, SQLCOM_RENAME_TABLE, tables);

  ASSERT_EQ(tables.size(), 2);

  // First pair: db1.t1 -> db2.t2
  EXPECT_EQ(tables[0].first.first, "db1");
  EXPECT_EQ(tables[0].first.second, "t1");
  EXPECT_EQ(tables[0].second.first, "db2");
  EXPECT_EQ(tables[0].second.second, "t2");

  // Second pair: t3 -> t4 (uses current db from THD, which might be default or
  // empty) In ParserTest default DB is usually empty unless set Let's assume
  // empty for now or check what thd->db() is. Actually table extraction falls
  // back to thd->db() if db part is empty.

  EXPECT_EQ(tables[1].first.second, "t3");
  EXPECT_EQ(tables[1].second.second, "t4");

  // Now verify the actual event handling logic triggers the right Cedar calls
  // We need to simulate the event
  mysql_event_query ev{};
  ev.event_subclass = MYSQL_AUDIT_QUERY_STATUS_END;
  ev.sql_command_id = SQLCOM_RENAME_TABLE;
  ev.query.str = (const char *)"RENAME TABLE ...";  // content doesn't matter
                                                    // for logic, LEX does
  ev.query.length = 16;

  // We need to ensure the plugin sees the THD with our LEX
  // handle_query_event takes THD

  // ddl_audit_notify calls handle_query_event
  // We'll call ddl_audit_notify directly with our THD

  // Ensure plugin is "installed" state for the test
  ddl_audit_plugin_init(nullptr);
  ddl_audit_test_reset();

  ddl_audit_notify(thd, MYSQL_AUDIT_QUERY_CLASS, &ev);

  const auto &calls = ddl_audit_test_get_calls();
  // We expect:
  // Pair 1: delete db1.t1, upsert db2.t2, upsert Database db2
  // Pair 2: delete t3, upsert t4
  // Plus potentially "Database" upserts for the new DBs

  // Verify calls exist
  ASSERT_GE(calls.size(), 4);

  // Verify specific sequence or existence
  bool found_delete_t1 = false;
  bool found_upsert_t2 = false;
  bool found_delete_t3 = false;
  bool found_upsert_t4 = false;

  for (const auto &call : calls) {
    if (call.action == "delete" && call.entity_type == "Table" &&
        call.entity_id == "db1.t1")
      found_delete_t1 = true;
    if (call.action == "upsert" && call.entity_type == "Table" &&
        call.entity_id == "db2.t2")
      found_upsert_t2 = true;

    // For t3/t4, the DB part depends on thd->db(). If it's empty/null, it might
    // just be the table name header checks. In extracted logic: src.first =
    // (src_db && src_db[0]) ? src_db : (thd->db().str ? ... : ""); if empty, id
    // is just table name. So check for "t3" and "t4" or ".t3" ".t4"?
    // make_table_uid: if (!db.empty() && !table.empty()) table_id = db + "." +
    // table; else ... table_id = table; So it should be just "t3" and "t4".

    // Check for t3, possibly with db prefix
    if (call.action == "delete" && call.entity_type == "Table") {
       size_t pos = call.entity_id.rfind("t3");
       if (pos != std::string::npos && pos + 2 == call.entity_id.length()) {
         if (pos == 0 || call.entity_id[pos-1] == '.') {
           found_delete_t3 = true;
         }
       }
    }
    
    // Check for t4, possibly with db prefix
    if (call.action == "upsert" && call.entity_type == "Table") {
       size_t pos = call.entity_id.rfind("t4");
       if (pos != std::string::npos && pos + 2 == call.entity_id.length()) {
         if (pos == 0 || call.entity_id[pos-1] == '.') {
           found_upsert_t4 = true;
         }
       }
    }
  }

  EXPECT_TRUE(found_delete_t1) << "Should delete old table db1.t1";
  EXPECT_TRUE(found_upsert_t2) << "Should upsert new table db2.t2";
  EXPECT_TRUE(found_delete_t3) << "Should delete old table t3";
  EXPECT_TRUE(found_upsert_t4) << "Should upsert new table t4";

  ddl_audit_plugin_deinit(nullptr);
}

// ===== Additional function coverage tests =====

TEST_F(DDL_audit_server_test, CreateDDLData_PopulatesFields) {
  // Prepare a parsed statement to have a valid THD
  THD *t = parse_query("CREATE TABLE t1 (id INT)");
  int cmd = SQLCOM_CREATE_TABLE;
  Json::Value doc = create_ddl_data(t, "CREATE_TABLE", (enum_sql_command_t)cmd,
                                    "CREATE TABLE t1 (id INT)", "test_db", "t1",
                                    "MYSQL_AUDIT_QUERY_CLASS",
                                    "MYSQL_AUDIT_QUERY_STATUS_END");

  ASSERT_TRUE(doc.isObject());
  EXPECT_EQ(doc["ddl_type"].asString(), "CREATE_TABLE");
  EXPECT_EQ(doc["sql_command_id"].asInt(), cmd);
  EXPECT_EQ(doc["query"].asString(), "CREATE TABLE t1 (id INT)");
  EXPECT_EQ(doc["database"].asString(), "test_db");
  EXPECT_EQ(doc["table"].asString(), "t1");
  ASSERT_TRUE(doc["timestamp"].isString());
  EXPECT_EQ(doc["timestamp"].asString().size(), 20U);
  ASSERT_TRUE(doc["context"].isObject());
  EXPECT_EQ(doc["context"]["event_class"].asString(),
            "MYSQL_AUDIT_QUERY_CLASS");
  EXPECT_EQ(doc["context"]["event_subclass"].asString(),
            "MYSQL_AUDIT_QUERY_STATUS_END");
}

TEST_F(DDL_audit_test, PluginInitAndDeinit_ReturnsZero) {
  // Initialize and deinitialize should succeed
  EXPECT_EQ(ddl_audit_plugin_init(nullptr), 0);
  EXPECT_EQ(ddl_audit_plugin_deinit(nullptr), 0);
}

TEST_F(DDL_audit_test, Notify_ReturnsZero_WhenPluginDisabledOrNotInstalled) {
  // When not installed, notify should no-op and return 0
  EXPECT_EQ(ddl_audit_notify(nullptr, MYSQL_AUDIT_QUERY_CLASS, nullptr), 0);
}

TEST_F(DDL_audit_server_test, Notify_QueryEvent_Minimal) {
  ASSERT_EQ(ddl_audit_plugin_init(nullptr), 0);

  // Build a simple CREATE DATABASE event
  THD *t = parse_query("CREATE DATABASE qdb");
  mysql_event_query ev{};
  ev.event_subclass = MYSQL_AUDIT_QUERY_STATUS_END;
  ev.query.str = const_cast<char *>("CREATE DATABASE qdb");
  ev.query.length = (unsigned long)strlen(ev.query.str);
  ev.sql_command_id = SQLCOM_CREATE_DB;

  // Should process and return 0
  EXPECT_EQ(ddl_audit_notify(t, MYSQL_AUDIT_QUERY_CLASS, &ev), 0);

  EXPECT_EQ(ddl_audit_plugin_deinit(nullptr), 0);
}

TEST_F(DDL_audit_test, Notify_AuthEvent_Minimal) {
  ASSERT_EQ(ddl_audit_plugin_init(nullptr), 0);

  mysql_event_authentication ev{};
  ev.event_subclass = MYSQL_AUDIT_AUTHENTICATION_AUTHID_CREATE;
  ev.user.str = const_cast<char *>("alice");
  ev.user.length = 5;
  ev.host.str = const_cast<char *>("localhost");
  ev.host.length = 9;
  ev.is_role = false;

  EXPECT_EQ(ddl_audit_notify(nullptr, MYSQL_AUDIT_AUTHENTICATION_CLASS, &ev),
            0);

  EXPECT_EQ(ddl_audit_plugin_deinit(nullptr), 0);
}

TEST_F(DDL_audit_test, Notify_TableAccessEvent_Minimal) {
  ASSERT_EQ(ddl_audit_plugin_init(nullptr), 0);

  mysql_event_table_access ev{};
  ev.event_subclass = MYSQL_AUDIT_TABLE_ACCESS_READ;
  ev.sql_command_id = SQLCOM_CREATE_TABLE;  // any DDL command to pass filter
  ev.table_database.str = const_cast<char *>("db");
  ev.table_database.length = 2;
  ev.table_name.str = const_cast<char *>("t");
  ev.table_name.length = 1;
  ev.query.str = const_cast<char *>("SELECT 1");
  ev.query.length = 8;

  EXPECT_EQ(ddl_audit_notify(nullptr, MYSQL_AUDIT_TABLE_ACCESS_CLASS, &ev), 0);

  EXPECT_EQ(ddl_audit_plugin_deinit(nullptr), 0);
}

TEST_F(DDL_audit_test, Notify_StoredProgramEvent_Minimal) {
  ASSERT_EQ(ddl_audit_plugin_init(nullptr), 0);

  mysql_event_stored_program ev{};
  ev.sql_command_id = SQLCOM_CREATE_PROCEDURE;
  ev.database.str = const_cast<char *>("db");
  ev.database.length = 2;
  ev.name.str = const_cast<char *>("proc");
  ev.name.length = 4;
  ev.query.str = const_cast<char *>("CREATE PROCEDURE proc() BEGIN END");
  ev.query.length = (unsigned long)strlen(ev.query.str);

  EXPECT_EQ(ddl_audit_notify(nullptr, MYSQL_AUDIT_STORED_PROGRAM_CLASS, &ev),
            0);

  EXPECT_EQ(ddl_audit_plugin_deinit(nullptr), 0);
}

}  // namespace ddl_audit_unittest
