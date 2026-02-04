/* Copyright (c) 2025, Oracle and/or its affiliates.
   See top-level LICENSE for details. */

#include <gtest/gtest.h>
#include <json/json.h>
#include <array>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

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

TEST_F(AuthorizationHelpersTest, TimeContextFormats) {
  AuthTimeContext ctx = auth_get_time_context();
  ASSERT_EQ(ctx.day.size(), 3U);
  for (char c : ctx.day) {
    EXPECT_TRUE(std::islower(static_cast<unsigned char>(c)));
  }
  EXPECT_GE(ctx.date, 19700101U);
  EXPECT_LE(ctx.date, 29991231U);
  EXPECT_LE(ctx.time, 235959U);
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

TEST_F(AuthorizationServerTest, ClientIpCachedCallsRawOncePerTHD) {
  THD *t = parse_query("SELECT 1");
  auth_clear_all_client_ip_cache();
  auth_reset_client_ip_raw_calls_for_test();

  std::string ip1 = auth_get_client_ip_cached(t);
  std::string ip2 = auth_get_client_ip_cached(t);
  EXPECT_EQ(ip1, ip2);
  EXPECT_EQ(auth_get_client_ip_raw_calls_for_test(), 1);

  auth_clear_client_ip_cache(t);
  (void)auth_get_client_ip_cached(t);
  EXPECT_EQ(auth_get_client_ip_raw_calls_for_test(), 2);
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

static std::vector<std::string> find_users_same_shard(
    size_t want_count, const std::string &resource, const std::string &action,
    const std::string &day, uint32_t date, const std::string &ip) {
  std::array<std::vector<std::string>, 64> per_shard;
  for (int i = 0; i < 200000; ++i) {
    std::string user = "user_" + std::to_string(i);
    size_t shard = cedar_cache_key_shard_index_for_test(
        user.c_str(), resource.c_str(), action.c_str(), day.c_str(), date,
        ip.c_str());
    if (shard >= per_shard.size()) continue;
    auto &bucket = per_shard[shard];
    if (bucket.size() < want_count) bucket.push_back(user);
    if (bucket.size() == want_count) return bucket;
  }
  return {};
}

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

TEST_F(CedarPluginInitializedTest, CacheLRUEvictionWithinShard) {
  cedar_auth_cache_reset();
  cedar_set_authorization_url("http://mock-allow");

  cedar_set_cache_enabled(true);
  cedar_set_cache_size_for_test(128);  // 2 entries per shard
  cedar_set_cache_ttl_for_test(3600);

  AuthTimeContext time_ctx = auth_get_time_context();
  const std::string day = time_ctx.day;
  const uint32_t date = time_ctx.date;
  const std::string ip = "unknown";

  mysql_authorization_event base{};
  fill_basic_table_event(base, "seed", "test", "users", 1UL << 0);
  const std::string resource = auth_create_resource_identifier(&base, "");
  const std::string action = "SELECT";

  std::vector<std::string> users =
      find_users_same_shard(3, resource, action, day, date, ip);
  ASSERT_EQ(users.size(), 3U);

  const std::string user_a = users[0];
  const std::string user_b = users[1];
  const std::string user_c = users[2];

  mysql_authorization_event ev{};

  // Insert A then B into the same shard.
  fill_basic_table_event(ev, user_a.c_str(), "test", "users", 1UL << 0);
  EXPECT_EQ(cedar_check(&ev), MYSQL_AUTHORIZATION_GRANT);
  fill_basic_table_event(ev, user_b.c_str(), "test", "users", 1UL << 0);
  EXPECT_EQ(cedar_check(&ev), MYSQL_AUTHORIZATION_GRANT);
  EXPECT_EQ(cedar_auth_cache_size(), 2U);

  // Touch A to make it most-recently-used.
  fill_basic_table_event(ev, user_a.c_str(), "test", "users", 1UL << 0);
  EXPECT_EQ(cedar_check(&ev), MYSQL_AUTHORIZATION_GRANT);
  EXPECT_EQ(cedar_auth_cache_size(), 2U);

  // Insert C; shard capacity is 2, so B should be evicted.
  fill_basic_table_event(ev, user_c.c_str(), "test", "users", 1UL << 0);
  EXPECT_EQ(cedar_check(&ev), MYSQL_AUTHORIZATION_GRANT);
  EXPECT_EQ(cedar_auth_cache_size(), 2U);

  EXPECT_TRUE(cedar_cache_contains_for_test(user_a.c_str(), resource.c_str(),
                                           action.c_str(), day.c_str(), date,
                                           ip.c_str()));
  EXPECT_FALSE(cedar_cache_contains_for_test(user_b.c_str(), resource.c_str(),
                                            action.c_str(), day.c_str(), date,
                                            ip.c_str()));
  EXPECT_TRUE(cedar_cache_contains_for_test(user_c.c_str(), resource.c_str(),
                                           action.c_str(), day.c_str(), date,
                                           ip.c_str()));
}

TEST_F(CedarPluginInitializedTest, CurlPoolReusesHandleWithinThread) {
  cedar_test_curl_cleanup_thread();
  EXPECT_EQ(cedar_test_curl_pool_size(), 0U);

  uintptr_t h1 = cedar_test_curl_acquire_handle();
  ASSERT_NE(h1, 0U);
  EXPECT_EQ(cedar_test_curl_pool_size(), 0U);

  cedar_test_curl_release_handle(h1);
  EXPECT_EQ(cedar_test_curl_pool_size(), 1U);

  uintptr_t h2 = cedar_test_curl_acquire_handle();
  EXPECT_EQ(h2, h1);
  cedar_test_curl_release_handle(h2);
  EXPECT_EQ(cedar_test_curl_pool_size(), 1U);

  cedar_test_curl_cleanup_thread();
  EXPECT_EQ(cedar_test_curl_pool_size(), 0U);
}

TEST_F(CedarPluginInitializedTest, DenyFlow) {
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 1UL << 0);

  cedar_set_authorization_url("http://mock-deny");

  auto result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_DENY);
}

TEST_F(CedarPluginInitializedTest, AnyOfRequirement) {
  // Test ANY_OF where we have multiple privileges
  // We'll mock one "Allow" and logic usually requires us to just have ONE
  // allow. However, our simple mock "http://mock-allow" allows ALL, and
  // "http://mock-deny" denies ALL. We can't easily test mixed results without a
  // more complex mock in C++ code or by extending the mock-url parsing. For
  // now, let's test that ANY_OF with mock-allow returns GRANT.

  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users",
                         (1UL << 0) | (1UL << 2));  // SELECT | UPDATE
  ev.requirement_mode = mysql_authorization_event::MYSQL_AUTHZ_REQ_ANY_OF;

  cedar_set_authorization_url("http://mock-allow");
  auto result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_GRANT);

  // Test ANY_OF with mock-deny returns DENY
  // Use cache reset to ensure we don't hit the cache from the previous Allow
  // check
  cedar_auth_cache_reset();
  cedar_set_authorization_url("http://mock-deny");
  result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_DENY);
}

TEST_F(CedarPluginInitializedTest, ServerErrorReturnsIgnore) {
  // We don't have a "mock-error" URL handler in the C++ code yet,
  // checking `cedar_authorization.cc`'s `check_single_privilege_cedar` function
  // for EXTRA_CODE_FOR_UNIT_TESTING. It only handles "mock-allow" and
  // "mock-deny". Everything else falls through to real curl. In unit test
  // environment, real curl to invalid URL should fail and return 0 (IGNORE -
  // mapped to -1 in core).

  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 1UL << 0);

  cedar_auth_cache_reset();
  cedar_set_authorization_url("http://invalid-url-should-fail");

  // The code returns -1 for error, which `cedar_check` maps to
  // MYSQL_AUTHORIZATION_IGNORE
  auto result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_IGNORE);
}

TEST_F(CedarPluginInitializedTest, StatsIncrements) {
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 1UL << 0);

  cedar_auth_cache_reset();
  cedar_reset_stats_for_test();
  cedar_set_collect_stats(true);
  cedar_set_authorization_url("http://mock-allow");

  // Perform request
  auto result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_GRANT);

  EXPECT_EQ(cedar_get_auth_stat_requests(), 1);
  EXPECT_EQ(cedar_get_auth_stat_grants(), 1);
  EXPECT_EQ(cedar_get_auth_stat_denies(), 0);
}

TEST_F(CedarPluginInitializedTest, StatsGating) {
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 1UL << 0);

  cedar_auth_cache_reset();
  cedar_reset_stats_for_test();
  cedar_set_collect_stats(false);
  cedar_set_authorization_url("http://mock-allow");

  // Perform request with stats disabled
  auto result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_GRANT);

  EXPECT_EQ(cedar_get_auth_stat_requests(), 0);
  EXPECT_EQ(cedar_get_auth_stat_grants(), 0);

  // Re-enable and verify it works again
  cedar_set_collect_stats(true);
  result = cedar_check(&ev);
  EXPECT_EQ(result, MYSQL_AUTHORIZATION_GRANT);

  EXPECT_EQ(cedar_get_auth_stat_requests(), 1);
}

TEST_F(CedarPluginInitializedTest, StatsReset) {
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", 1UL << 0);

  cedar_auth_cache_reset();
  cedar_reset_stats_for_test();
  cedar_set_collect_stats(true);
  cedar_set_authorization_url("http://mock-allow");

  // Increment stats
  (void)cedar_check(&ev);
  EXPECT_EQ(cedar_get_auth_stat_requests(), 1);

  // Reset
  cedar_reset_stats_for_test();
  EXPECT_EQ(cedar_get_auth_stat_requests(), 0);
  EXPECT_EQ(cedar_get_auth_stat_grants(), 0);
}

// TODO: Add complex ANY_OF mixed test when mock infrastructure supports
// fine-grained control

}  // namespace authorization_unittest
