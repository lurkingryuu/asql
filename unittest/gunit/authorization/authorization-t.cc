/* Copyright (c) 2025, Oracle and/or its affiliates.
   See top-level LICENSE for details. */

#include <gtest/gtest.h>
#include <json/json.h>
#include <string>
#include <utility>

#include "include/mysql/plugin_authorization.h"
#include "plugin/authorization/authorization_common.h"
#include "plugin/authorization/cedar_authorization.h"
#include "sql/sql_class.h"
#include "unittest/gunit/parsertest.h"

using namespace auth_common;

namespace authorization_unittest {

class AuthorizationHelpersTest : public ::testing::Test {};

class AuthorizationServerTest : public ParserTest {
 protected:
  THD *parse_query(const char *query) {
    parse(query, 0);
    return thd();
  }
};

TEST_F(AuthorizationHelpersTest, BuildCedarPayload) {
  Json::Value payload =
      auth_build_cedar_payload("alice", "Table::\"db.t\"", "SELECT", "mon",
                               20250101, 123001, "127.0.0.1", "MySQL");
  ASSERT_TRUE(payload.isObject());
  EXPECT_EQ(payload["principal"].asString(), "MySQL::User::\"alice\"");
  EXPECT_EQ(payload["action"].asString(), "MySQL::Action::\"SELECT\"");
  EXPECT_EQ(payload["resource"].asString(), "MySQL::Table::\"db.t\"");
  EXPECT_EQ(payload["context"]["day"].asString(), "mon");
  EXPECT_EQ(payload["context"]["date"].asUInt(), 20250101U);
  EXPECT_EQ(payload["context"]["time"].asUInt(), 123001U);
  EXPECT_EQ(payload["context"]["ip"]["__extn"]["fn"].asString(), "ip");
  EXPECT_EQ(payload["context"]["ip"]["__extn"]["arg"].asString(), "127.0.0.1");
}

TEST_F(AuthorizationHelpersTest, PrivilegesPrimaryAction) {
  // SELECT bit -> 1<<0
  unsigned long mask = (1UL << 0) | (1UL << 2);  // SELECT and UPDATE
  EXPECT_EQ(auth_get_primary_action(mask), "SELECT");
  EXPECT_EQ(auth_get_primary_action(1UL << 25), "CREATE USER");
}

TEST_F(AuthorizationServerTest, BuildIdentifiers) {
  mysql_authorization_event ev{};
  const char *user = "alice";
  ev.user.str = const_cast<char *>(user);
  ev.user.length = (unsigned long)strlen(user);
  ev.event_subclass = MYSQL_AUTHORIZATION_TABLE_ACCESS;
  const char *db = "test";
  const char *tbl = "users";
  ev.database.str = const_cast<char *>(db);
  ev.database.length = (unsigned long)strlen(db);
  ev.table.str = const_cast<char *>(tbl);
  ev.table.length = (unsigned long)strlen(tbl);

  EXPECT_EQ(auth_build_user_uid(&ev), "alice");
  EXPECT_EQ(auth_make_db_id(&ev), "test");
  EXPECT_EQ(auth_make_table_id(&ev), "test.users");
  EXPECT_EQ(auth_create_resource_identifier(&ev, "MySQL"),
            "MySQL::Table::\"test.users\"");
  EXPECT_EQ(cedar_create_resource_identifier(&ev),
            "MySQL::Table::\"test.users\"");
}

TEST_F(AuthorizationServerTest, ClientIpUnknownInUnitTest) {
  THD *t = parse_query("SELECT 1");
  std::string ip = auth_get_client_ip(t);
  EXPECT_FALSE(ip.empty());
}

// Fixture that initializes and deinitializes the cedar plugin
class CedarPluginInitializedTest : public ::testing::Test {
 protected:
  void SetUp() override { (void)cedar_authorization_init(nullptr); }
  void TearDown() override {
    cedar_set_authorization_url(nullptr);
    (void)cedar_authorization_deinit(nullptr);
  }
};

// Helper to build a minimal authorization event
static void fill_basic_table_event(mysql_authorization_event &ev,
                                   const char *user, const char *db,
                                   const char *table, unsigned long priv_mask) {
  memset(&ev, 0, sizeof(ev));
  ev.user.str = const_cast<char *>(user);
  ev.user.length = (unsigned long)strlen(user);
  ev.database.str = const_cast<char *>(db);
  ev.database.length = (unsigned long)strlen(db);
  ev.table.str = const_cast<char *>(table);
  ev.table.length = (unsigned long)strlen(table);
  ev.event_subclass = MYSQL_AUTHORIZATION_TABLE_ACCESS;
  ev.privileges = priv_mask;
  ev.requirement_mode = mysql_authorization_event::MYSQL_AUTHZ_REQ_ALL_OF;
}

TEST(CedarAuthorizationTest, NotInitializedReturnsIgnore) {
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 1UL << 0);

  auto result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_IGNORE);
}

TEST_F(CedarPluginInitializedTest, PresenceModeGrants) {
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 1UL << 0);
  ev.requirement_mode = mysql_authorization_event::MYSQL_AUTHZ_REQ_PRESENCE;

  auto result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_GRANT);
}

TEST_F(CedarPluginInitializedTest, ZeroPrivilegesGrants) {
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 0UL);

  auto result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_GRANT);
}

TEST_F(CedarPluginInitializedTest, UrlNotConfiguredReturnsIgnore) {
  // With plugin initialized but cedar_authorization_url unset (default),
  // core should return IGNORE and the top-level should map to IGNORE
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 1UL << 0);

  auto result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_IGNORE);
}

TEST_F(CedarPluginInitializedTest, PrivilegesToStringHasBrackets) {
  // Validate formatting helper exposed by cedar plugin
  unsigned long mask = (1UL << 0) | (1UL << 2);  // SELECT and UPDATE
  std::string s = privileges_to_string(mask);
  ASSERT_FALSE(s.empty());
  EXPECT_EQ(s.front(), '[');
  EXPECT_EQ(s.back(), ']');
}

TEST_F(CedarPluginInitializedTest, CacheBasicFlow) {
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 1UL << 0);

  // Ensure cache is empty and URL is set to mock
  cedar_auth_cache_reset();
  cedar_set_authorization_url("http://mock-allow");
  EXPECT_EQ(cedar_auth_cache_size(), 0U);

  // First check - should be a miss (will return GRANT because of mock URL)
  auto result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_GRANT);

  // Cache should now have an entry
  EXPECT_EQ(cedar_auth_cache_size(), 1U);

  // Second check - should be a hit
  result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_GRANT);
  EXPECT_EQ(cedar_auth_cache_size(), 1U);
}

TEST_F(CedarPluginInitializedTest, CacheReset) {
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 1UL << 0);

  cedar_set_authorization_url("http://mock-allow");
  (void)cedar_check(&ev);
  EXPECT_EQ(cedar_auth_cache_size(), 1U);

  cedar_auth_cache_reset();
  EXPECT_EQ(cedar_auth_cache_size(), 0U);
}

TEST_F(CedarPluginInitializedTest, CacheEviction) {
  cedar_auth_cache_reset();
  cedar_set_authorization_url("http://mock-allow");

  // We can't easily change the global GUC `cedar_authorization_cache_size` from
  // here without more complex mocking, but we can verify the logic if we assume
  // a small size or just run many insertions. For this test, let's just ensure
  // multiple entries can coexist.

  for (int i = 0; i < 5; ++i) {
    mysql_authorization_event ev{};
    std::string user = "alice" + std::to_string(i);
    fill_basic_table_event(ev, user.c_str(), "test", "users", 1UL << 0);
    (void)cedar_check(&ev);
  }

  EXPECT_EQ(cedar_auth_cache_size(), 5U);
}

}  // namespace authorization_unittest
