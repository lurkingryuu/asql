/* Copyright (c) 2026.
   See top-level LICENSE for details. */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include "include/mysql/plugin_authorization.h"
#include "plugin/authorization/authorization_common.h"
#include "plugin/authorization/embedded_cedar.h"

using namespace auth_common;

namespace authorization_unittest {

namespace {

std::string make_temp_dir() {
  char tmpl[] = "/tmp/embedded-cedar-test-XXXXXX";
  char *dir = mkdtemp(tmpl);
  EXPECT_NE(dir, nullptr);
  return dir ? std::string(dir) : std::string("/tmp");
}

void write_text(const std::string &path, const std::string &text) {
  std::ofstream out(path);
  ASSERT_TRUE(out.is_open()) << path;
  out << text;
  out.close();
}

void fill_basic_table_event(mysql_authorization_event &ev,
                            const char *user,
                            const char *db,
                            const char *table,
                            unsigned long priv_mask) {
  memset(&ev, 0, sizeof(ev));
  ev.user.str = const_cast<char *>(user);
  ev.user.length = static_cast<unsigned long>(strlen(user));
  ev.database.str = const_cast<char *>(db);
  ev.database.length = static_cast<unsigned long>(strlen(db));
  ev.table.str = const_cast<char *>(table);
  ev.table.length = static_cast<unsigned long>(strlen(table));
  ev.event_subclass = MYSQL_AUTHORIZATION_TABLE_ACCESS;
  ev.privileges = priv_mask;
  ev.requirement_mode = mysql_authorization_event::MYSQL_AUTHZ_REQ_ALL_OF;
}

void fill_basic_column_event(mysql_authorization_event &ev,
                             const char *user,
                             const char *db,
                             const char *table,
                             const char *column,
                             unsigned long priv_mask) {
  memset(&ev, 0, sizeof(ev));
  ev.user.str = const_cast<char *>(user);
  ev.user.length = static_cast<unsigned long>(strlen(user));
  ev.database.str = const_cast<char *>(db);
  ev.database.length = static_cast<unsigned long>(strlen(db));
  ev.table.str = const_cast<char *>(table);
  ev.table.length = static_cast<unsigned long>(strlen(table));
  ev.column.str = const_cast<char *>(column);
  ev.column.length = static_cast<unsigned long>(strlen(column));
  ev.event_subclass = MYSQL_AUTHORIZATION_COLUMN_ACCESS;
  ev.privileges = priv_mask;
  ev.requirement_mode = mysql_authorization_event::MYSQL_AUTHZ_REQ_ALL_OF;
}

void fill_basic_db_event(mysql_authorization_event &ev,
                         const char *user,
                         const char *db,
                         unsigned long priv_mask) {
  memset(&ev, 0, sizeof(ev));
  ev.user.str = const_cast<char *>(user);
  ev.user.length = static_cast<unsigned long>(strlen(user));
  ev.database.str = const_cast<char *>(db);
  ev.database.length = static_cast<unsigned long>(strlen(db));
  ev.event_subclass = MYSQL_AUTHORIZATION_DB_ACCESS;
  ev.privileges = priv_mask;
  ev.requirement_mode = mysql_authorization_event::MYSQL_AUTHZ_REQ_ALL_OF;
}

void fill_basic_routine_event(mysql_authorization_event &ev,
                              const char *user,
                              const char *db,
                              const char *routine,
                              unsigned long priv_mask) {
  memset(&ev, 0, sizeof(ev));
  ev.user.str = const_cast<char *>(user);
  ev.user.length = static_cast<unsigned long>(strlen(user));
  ev.database.str = const_cast<char *>(db);
  ev.database.length = static_cast<unsigned long>(strlen(db));
  ev.routine.str = const_cast<char *>(routine);
  ev.routine.length = static_cast<unsigned long>(strlen(routine));
  ev.event_subclass = MYSQL_AUTHORIZATION_ROUTINE_ACCESS;
  ev.privileges = priv_mask;
  ev.requirement_mode = mysql_authorization_event::MYSQL_AUTHZ_REQ_ALL_OF;
}

std::vector<std::string> find_users_same_shard(size_t want_count,
                                               const std::string &resource,
                                               const std::string &action,
                                               const std::string &day,
                                               uint32_t date,
                                               const std::string &ip) {
  std::vector<std::string> by_shard[64];
  for (int i = 0; i < 200000; ++i) {
    std::string user = "user_" + std::to_string(i);
    size_t shard = embedded_cedar_cache_key_shard_index_for_test(
        user.c_str(), resource.c_str(), action.c_str(), day.c_str(), date,
        ip.c_str());
    if (shard >= 64) continue;
    if (by_shard[shard].size() < want_count) by_shard[shard].push_back(user);
    if (by_shard[shard].size() == want_count) return by_shard[shard];
  }
  return {};
}

constexpr unsigned long kSelectAcl = 1UL << 0;
constexpr unsigned long kUpdateAcl = 1UL << 2;
constexpr unsigned long kExecuteAcl = 1UL << 18;

const char *kBaseSchema = R"JSON({
  "MySQL": {
    "entityTypes": {
      "User": { "shape": { "type": "Record", "attributes": {} } },
      "Group": { "shape": { "type": "Record", "attributes": {} } },
      "Database": { "shape": { "type": "Record", "attributes": {} } },
      "Table": { "shape": { "type": "Record", "attributes": {} } },
      "Column": { "shape": { "type": "Record", "attributes": {} } },
      "Routine": { "shape": { "type": "Record", "attributes": {} } }
    },
    "actions": {
      "SELECT": {
        "appliesTo": {
          "principalTypes": ["User"],
          "resourceTypes": ["Database", "Table", "Column"],
          "context": {
            "type": "Record",
            "attributes": {
              "day": { "type": "String" },
              "date": { "type": "Long" },
              "time": { "type": "Long" },
              "ip": { "type": "Extension", "name": "ipaddr" }
            }
          }
        }
      },
      "UPDATE": {
        "appliesTo": {
          "principalTypes": ["User"],
          "resourceTypes": ["Table"],
          "context": {
            "type": "Record",
            "attributes": {
              "day": { "type": "String" },
              "date": { "type": "Long" },
              "time": { "type": "Long" },
              "ip": { "type": "Extension", "name": "ipaddr" }
            }
          }
        }
      },
      "EXECUTE": {
        "appliesTo": {
          "principalTypes": ["User"],
          "resourceTypes": ["Routine"],
          "context": {
            "type": "Record",
            "attributes": {
              "day": { "type": "String" },
              "date": { "type": "Long" },
              "time": { "type": "Long" },
              "ip": { "type": "Extension", "name": "ipaddr" }
            }
          }
        }
      }
    }
  }
})JSON";

const char *kGroupEntities = R"JSON([
  {
    "uid": {"type": "MySQL::User", "id": "alice"},
    "attrs": {},
    "parents": [{"type": "MySQL::Group", "id": "readers"}]
  },
  {
    "uid": {"type": "MySQL::Group", "id": "readers"},
    "attrs": {},
    "parents": []
  }
])JSON";

void expect_embedded_result(const mysql_authorization_event &ev,
                            mysql_authorization_result_t expected) {
  auto actual = embedded_cedar_check(&ev);
  EXPECT_EQ(actual, expected)
      << (embedded_cedar_last_error_for_test()
              ? embedded_cedar_last_error_for_test()
              : "(no cedar error)");
}

}  // namespace

class EmbeddedCedarInitializedTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = make_temp_dir();
    policy_path_ = temp_dir_ + "/policy.cedar";
    schema_path_ = temp_dir_ + "/schema.json";
    entities_path_ = temp_dir_ + "/entities.json";

    ASSERT_EQ(embedded_cedar_init(nullptr), 0);
    embedded_cedar_set_namespace_for_test("MySQL");
    embedded_cedar_set_enabled_for_test(true);
    embedded_cedar_set_collect_stats_for_test(true);
    embedded_cedar_set_enable_column_access_for_test(false);
    embedded_cedar_set_cache_enabled_for_test(true);
    embedded_cedar_set_cache_size_for_test(1024);
    embedded_cedar_set_cache_ttl_for_test(300);
    embedded_cedar_set_policy_file_for_test(nullptr);
    embedded_cedar_set_schema_file_for_test(nullptr);
    embedded_cedar_set_entities_file_for_test(nullptr);
    embedded_cedar_cache_flush_for_test();
    embedded_cedar_reset_stats_for_test();
  }

  void TearDown() override {
    embedded_cedar_set_policy_file_for_test(nullptr);
    embedded_cedar_set_schema_file_for_test(nullptr);
    embedded_cedar_set_entities_file_for_test(nullptr);
    embedded_cedar_set_namespace_for_test("MySQL");
    embedded_cedar_set_enabled_for_test(true);
    embedded_cedar_set_collect_stats_for_test(true);
    embedded_cedar_set_enable_column_access_for_test(false);
    embedded_cedar_set_cache_enabled_for_test(true);
    embedded_cedar_set_cache_size_for_test(1024);
    embedded_cedar_set_cache_ttl_for_test(300);
    embedded_cedar_cache_flush_for_test();
    embedded_cedar_reset_stats_for_test();
    ASSERT_EQ(embedded_cedar_deinit(nullptr), 0);

    std::remove(policy_path_.c_str());
    std::remove(schema_path_.c_str());
    std::remove(entities_path_.c_str());
    ::rmdir(temp_dir_.c_str());
  }

  void write_policy(const std::string &policy) { write_text(policy_path_, policy); }
  void write_schema(const std::string &schema) { write_text(schema_path_, schema); }
  void write_entities(const std::string &entities) {
    write_text(entities_path_, entities);
  }

  bool reload_from_files(bool include_schema = true, bool include_entities = false) {
    embedded_cedar_set_policy_file_for_test(policy_path_.c_str());
    embedded_cedar_set_schema_file_for_test(include_schema ? schema_path_.c_str()
                                                           : nullptr);
    embedded_cedar_set_entities_file_for_test(include_entities ? entities_path_.c_str()
                                                               : nullptr);
    return embedded_cedar_reload_for_test();
  }

  std::string temp_dir_;
  std::string policy_path_;
  std::string schema_path_;
  std::string entities_path_;
};

TEST(EmbeddedCedarTest, NotInitializedReturnsIgnore) {
  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", kSelectAcl);
  expect_embedded_result(ev, MYSQL_AUTHORIZATION_IGNORE);
}

TEST_F(EmbeddedCedarInitializedTest, ReloadedPoliciesAuthorizeTableDatabaseAndRoutine) {
  write_schema(kBaseSchema);
  write_policy(R"CEDAR(
permit(principal == MySQL::User::"alice", action == MySQL::Action::"SELECT", resource == MySQL::Database::"test");
permit(principal == MySQL::User::"alice", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test.users");
permit(principal == MySQL::User::"alice", action == MySQL::Action::"EXECUTE", resource == MySQL::Routine::"test.calculate_bonus");
)CEDAR");

  ASSERT_TRUE(reload_from_files());

  mysql_authorization_event db_ev{};
  mysql_authorization_event table_ev{};
  mysql_authorization_event routine_ev{};

  fill_basic_db_event(db_ev, "alice", "test", kSelectAcl);
  fill_basic_table_event(table_ev, "alice", "test", "users", kSelectAcl);
  fill_basic_routine_event(routine_ev, "alice", "test", "calculate_bonus",
                           kExecuteAcl);

  expect_embedded_result(db_ev, MYSQL_AUTHORIZATION_GRANT);
  expect_embedded_result(table_ev, MYSQL_AUTHORIZATION_GRANT);
  expect_embedded_result(routine_ev, MYSQL_AUTHORIZATION_GRANT);

  fill_basic_table_event(table_ev, "bob", "test", "users", kSelectAcl);
  expect_embedded_result(table_ev, MYSQL_AUTHORIZATION_DENY);
}

TEST_F(EmbeddedCedarInitializedTest, ColumnAccessToggleControlsEnforcement) {
  write_schema(kBaseSchema);
  write_policy(R"CEDAR(
permit(principal == MySQL::User::"alice", action == MySQL::Action::"SELECT", resource == MySQL::Column::"test.users.id");
)CEDAR");

  ASSERT_TRUE(reload_from_files());

  mysql_authorization_event col_ev{};
  fill_basic_column_event(col_ev, "alice", "test", "users", "name", kSelectAcl);

  embedded_cedar_set_enable_column_access_for_test(false);
  expect_embedded_result(col_ev, MYSQL_AUTHORIZATION_GRANT);

  embedded_cedar_reset_stats_for_test();
  embedded_cedar_set_enable_column_access_for_test(true);
  expect_embedded_result(col_ev, MYSQL_AUTHORIZATION_DENY);
  EXPECT_EQ(embedded_cedar_get_auth_stat_requests(), 1);
  EXPECT_EQ(embedded_cedar_get_auth_stat_denies(), 1);
}

TEST_F(EmbeddedCedarInitializedTest, ReloadFailureKeepsPreviousEngineActive) {
  write_schema(kBaseSchema);
  write_policy(R"CEDAR(
permit(principal == MySQL::User::"alice", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test.users");
)CEDAR");

  ASSERT_TRUE(reload_from_files());

  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", kSelectAcl);
  expect_embedded_result(ev, MYSQL_AUTHORIZATION_GRANT);

  write_policy("this is not valid cedar");
  EXPECT_FALSE(reload_from_files());

  expect_embedded_result(ev, MYSQL_AUTHORIZATION_GRANT);
}

TEST_F(EmbeddedCedarInitializedTest, ReloadPicksUpUpdatedPolicyFiles) {
  write_schema(kBaseSchema);
  write_policy(R"CEDAR(
permit(principal == MySQL::User::"alice", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test.users");
)CEDAR");

  ASSERT_TRUE(reload_from_files());

  mysql_authorization_event alice_ev{};
  mysql_authorization_event bob_ev{};
  fill_basic_table_event(alice_ev, "alice", "test", "users", kSelectAcl);
  fill_basic_table_event(bob_ev, "bob", "test", "users", kSelectAcl);

  expect_embedded_result(alice_ev, MYSQL_AUTHORIZATION_GRANT);
  expect_embedded_result(bob_ev, MYSQL_AUTHORIZATION_DENY);

  write_policy(R"CEDAR(
permit(principal == MySQL::User::"bob", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test.users");
)CEDAR");
  ASSERT_TRUE(reload_from_files());

  expect_embedded_result(alice_ev, MYSQL_AUTHORIZATION_DENY);
  expect_embedded_result(bob_ev, MYSQL_AUTHORIZATION_GRANT);
}

TEST_F(EmbeddedCedarInitializedTest, GroupEntitiesAndNamespaceDriveAuthorization) {
  write_schema(R"JSON({
  "MySQL": {
    "entityTypes": {
      "User": { "shape": { "type": "Record", "attributes": {} }, "memberOfTypes": ["Group"] },
      "Group": { "shape": { "type": "Record", "attributes": {} } },
      "Table": { "shape": { "type": "Record", "attributes": {} } }
    },
    "actions": {
      "SELECT": {
        "appliesTo": {
          "principalTypes": ["User"],
          "resourceTypes": ["Table"],
          "context": {
            "type": "Record",
            "attributes": {
              "day": { "type": "String" },
              "date": { "type": "Long" },
              "time": { "type": "Long" },
              "ip": { "type": "Extension", "name": "ipaddr" }
            }
          }
        }
      }
    }
  }
})JSON");
  write_entities(kGroupEntities);
  write_policy(R"CEDAR(
permit(principal in MySQL::Group::"readers", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test.users");
)CEDAR");

  ASSERT_TRUE(reload_from_files(true, true));

  mysql_authorization_event alice_ev{};
  mysql_authorization_event bob_ev{};
  fill_basic_table_event(alice_ev, "alice", "test", "users", kSelectAcl);
  fill_basic_table_event(bob_ev, "bob", "test", "users", kSelectAcl);

  expect_embedded_result(alice_ev, MYSQL_AUTHORIZATION_GRANT);
  expect_embedded_result(bob_ev, MYSQL_AUTHORIZATION_DENY);
}

TEST_F(EmbeddedCedarInitializedTest, AnyOfUsesMixedPrivilegeResults) {
  write_schema(kBaseSchema);
  write_policy(R"CEDAR(
permit(principal == MySQL::User::"alice", action == MySQL::Action::"UPDATE", resource == MySQL::Table::"test.users");
)CEDAR");

  ASSERT_TRUE(reload_from_files());

  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", kSelectAcl | kUpdateAcl);
  ev.requirement_mode = mysql_authorization_event::MYSQL_AUTHZ_REQ_ANY_OF;

  expect_embedded_result(ev, MYSQL_AUTHORIZATION_GRANT);

  ev.requirement_mode = mysql_authorization_event::MYSQL_AUTHZ_REQ_ALL_OF;
  expect_embedded_result(ev, MYSQL_AUTHORIZATION_DENY);
}

TEST_F(EmbeddedCedarInitializedTest, SchemaValidationErrorFailsOpenAndCountsError) {
  write_schema(R"JSON({
  "MySQL": {
    "entityTypes": {
      "User": { "shape": { "type": "Record", "attributes": {} } },
      "Table": { "shape": { "type": "Record", "attributes": {} } }
    },
    "actions": {
      "SELECT": {
        "appliesTo": {
          "principalTypes": ["User"],
          "resourceTypes": ["Table"],
          "context": {
            "type": "Record",
            "attributes": {
              "day": { "type": "String" },
              "date": { "type": "Long" },
              "time": { "type": "Long" },
              "ip": { "type": "Extension", "name": "ipaddr" }
            }
          }
        }
      }
    }
  }
})JSON");
  write_policy(R"CEDAR(
permit(principal == MySQL::User::"alice", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test.users");
)CEDAR");

  ASSERT_TRUE(reload_from_files());

  mysql_authorization_event db_ev{};
  fill_basic_db_event(db_ev, "alice", "test", kSelectAcl);

  expect_embedded_result(db_ev, MYSQL_AUTHORIZATION_IGNORE);
  EXPECT_EQ(embedded_cedar_get_auth_stat_requests(), 1);
  EXPECT_EQ(embedded_cedar_get_auth_stat_errors(), 1);
}

TEST_F(EmbeddedCedarInitializedTest, CacheTracksHitsMissesAndExpiry) {
  write_schema(kBaseSchema);
  write_policy(R"CEDAR(
permit(principal == MySQL::User::"alice", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test.users");
)CEDAR");

  ASSERT_TRUE(reload_from_files());

  embedded_cedar_set_cache_ttl_for_test(1);
  embedded_cedar_reset_stats_for_test();

  mysql_authorization_event ev{};
  fill_basic_table_event(ev, "alice", "test", "users", kSelectAcl);

  expect_embedded_result(ev, MYSQL_AUTHORIZATION_GRANT);
  expect_embedded_result(ev, MYSQL_AUTHORIZATION_GRANT);
  EXPECT_EQ(embedded_cedar_get_auth_stat_cache_misses(), 1);
  EXPECT_EQ(embedded_cedar_get_auth_stat_cache_hits(), 1);

  mysql_authorization_event probe{};
  fill_basic_table_event(probe, "alice", "test", "users", kSelectAcl);
  AuthTimeContext time_ctx = auth_get_time_context();
  const std::string resource = auth_create_resource_identifier(&probe, "");
  EXPECT_TRUE(embedded_cedar_cache_contains_for_test(
      "alice", resource.c_str(), "SELECT", time_ctx.day.c_str(), time_ctx.date,
      "0.0.0.0"));

  ::sleep(2);
  EXPECT_FALSE(embedded_cedar_cache_contains_for_test(
      "alice", resource.c_str(), "SELECT", time_ctx.day.c_str(), time_ctx.date,
      "0.0.0.0"));
}

TEST_F(EmbeddedCedarInitializedTest, CacheEvictsLeastRecentlyUsedWithinShard) {
  write_schema(kBaseSchema);
  write_policy(R"CEDAR(
permit(principal == MySQL::User::"user_0", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test.users");
permit(principal == MySQL::User::"user_1", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test.users");
permit(principal == MySQL::User::"user_2", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test.users");
)CEDAR");

  ASSERT_TRUE(reload_from_files());

  embedded_cedar_set_cache_enabled_for_test(true);
  embedded_cedar_set_cache_size_for_test(128);
  embedded_cedar_set_cache_ttl_for_test(3600);
  embedded_cedar_cache_flush_for_test();
  embedded_cedar_reset_stats_for_test();

  mysql_authorization_event base{};
  fill_basic_table_event(base, "seed", "test", "users", kSelectAcl);
  AuthTimeContext time_ctx = auth_get_time_context();
  const std::string resource = auth_create_resource_identifier(&base, "");
  const std::string action = "SELECT";
  const std::string ip = "0.0.0.0";

  std::vector<std::string> users =
      find_users_same_shard(3, resource, action, time_ctx.day, time_ctx.date, ip);
  ASSERT_EQ(users.size(), 3U);

  std::string policy =
      "permit(principal == MySQL::User::\"" + users[0] +
      "\", action == MySQL::Action::\"SELECT\", resource == "
      "MySQL::Table::\"test.users\");\n"
      "permit(principal == MySQL::User::\"" + users[1] +
      "\", action == MySQL::Action::\"SELECT\", resource == "
      "MySQL::Table::\"test.users\");\n"
      "permit(principal == MySQL::User::\"" + users[2] +
      "\", action == MySQL::Action::\"SELECT\", resource == "
      "MySQL::Table::\"test.users\");\n";
  write_policy(policy);
  ASSERT_TRUE(reload_from_files());

  mysql_authorization_event ev{};
  fill_basic_table_event(ev, users[0].c_str(), "test", "users", kSelectAcl);
  expect_embedded_result(ev, MYSQL_AUTHORIZATION_GRANT);
  fill_basic_table_event(ev, users[1].c_str(), "test", "users", kSelectAcl);
  expect_embedded_result(ev, MYSQL_AUTHORIZATION_GRANT);
  fill_basic_table_event(ev, users[0].c_str(), "test", "users", kSelectAcl);
  expect_embedded_result(ev, MYSQL_AUTHORIZATION_GRANT);
  fill_basic_table_event(ev, users[2].c_str(), "test", "users", kSelectAcl);
  expect_embedded_result(ev, MYSQL_AUTHORIZATION_GRANT);

  EXPECT_TRUE(embedded_cedar_cache_contains_for_test(
      users[0].c_str(), resource.c_str(), action.c_str(), time_ctx.day.c_str(),
      time_ctx.date, ip.c_str()));
  EXPECT_FALSE(embedded_cedar_cache_contains_for_test(
      users[1].c_str(), resource.c_str(), action.c_str(), time_ctx.day.c_str(),
      time_ctx.date, ip.c_str()));
  EXPECT_TRUE(embedded_cedar_cache_contains_for_test(
      users[2].c_str(), resource.c_str(), action.c_str(), time_ctx.day.c_str(),
      time_ctx.date, ip.c_str()));
}

}  // namespace authorization_unittest
