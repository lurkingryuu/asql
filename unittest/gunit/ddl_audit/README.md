# DDL Audit Plugin Unit Tests

This directory contains unit tests for the `ddl_audit` plugin's helper functions and utilities.

## Overview

The DDL Audit plugin is a MySQL audit plugin that captures DDL (Data Definition Language) statements and sends them to a Cedar server for authorization and data population. These unit tests focus on testing the core helper functions that don't require full MySQL server infrastructure.

## What is Tested

The unit tests cover the following components:

### 1. DDL Command Detection (`is_ddl_command`)
- Verifies that all supported DDL commands are correctly identified
- Tests table operations (CREATE, ALTER, DROP, RENAME TABLE)
- Tests database operations (CREATE, ALTER, DROP DATABASE)
- Tests user operations (CREATE, DROP, RENAME, ALTER USER)
- Tests stored program operations (functions, procedures, views, triggers, events)
- Tests role operations (CREATE, DROP ROLE)
- Tests other DDL commands (indexes, servers, tablespaces, resource groups, SRS)
- Ensures non-DDL commands (SELECT, INSERT, UPDATE, DELETE) are not identified as DDL

### 2. Command Name Mapping (`get_command_name`)
- Tests conversion of MySQL SQL command IDs to human-readable names
- Examples: `SQLCOM_CREATE_TABLE` → "CREATE_TABLE"
- Ensures unknown commands return "UNKNOWN"

### 3. UID Generation Functions

#### User UIDs (`make_user_uid`)
- Creates uniform user identifiers
- Note: Host information is intentionally NOT included in the UID
- This matches the Cedar authorization plugin's UID scheme
- Examples:
  - `("root", "localhost")` → `"root"`
  - `("admin", "192.168.1.1")` → `"admin"`

#### Database UIDs (`make_db_uid`)
- Creates database identifiers
- Examples:
  - `"test_db"` → `"test_db"`
  - `"mysql"` → `"mysql"`

#### Table UIDs (`make_table_uid`)
- Creates qualified table identifiers
- Combines database and table names
- Examples:
  - `("test_db", "users")` → `"test_db.users"`
  - `("", "standalone_table")` → `"standalone_table"`

### 4. Server Context Functions (Using ParserTest)

#### Database Name Extraction (`extract_database_name_from_lex`)
- Extracts database names from CREATE/ALTER/DROP DATABASE statements
- Handles `IF NOT EXISTS` clauses
- Returns current database context for table operations

#### User Extraction (`extract_users_from_lex`)
- Extracts user/host pairs from CREATE/ALTER/DROP USER statements
- Handles single and multiple users
- Supports host wildcards (%)
- Works with various user statement formats

#### Table Extraction (`extract_table_from_lex`)
- Extracts table names from table-related DDL statements
- Handles quoted table names
- Works with CREATE/ALTER/DROP TABLE, RENAME TABLE, CREATE/DROP INDEX
- Returns empty for non-table commands

#### Context Helpers (`get_current_timestamp`, `get_client_ip`)
- Timestamp generation in ISO 8601 format
- Client IP address extraction (returns "unknown" in test environment)

## What is NOT Tested

Due to the nature of unit testing and the complexity of the MySQL server environment, the following components are NOT tested here:

1. **Cedar Server communication**
   - HTTP requests to Cedar server
   - curl operations
   - JSON payload construction and sending

2. **Full Event handlers**
   - Complete audit event handling (only helper functions are tested)
   - Plugin lifecycle operations
   - System variable handling

3. **Thread safety**
   - Mutex operations
   - Counter updates

4. **Production network operations**
   - Real client IP detection (test environment returns "unknown")

These components require integration testing or MySQL test framework (MTR) tests rather than unit tests.

## Building and Running Tests

### Build the Tests

From the build directory:

```bash
cd build
cmake ..
make ddl_audit-t
```

### Run the Tests

Run the specific test executable:

```bash
./unittest/gunit/ddl_audit/ddl_audit-t
```

Or run with ctest:

```bash
ctest -R ddl_audit -V
```

### Run Tests with Detailed Output

```bash
./unittest/gunit/ddl_audit/ddl_audit-t --gtest_print_time=1
```

### Run Specific Test Cases

```bash
# Run only the DDL command detection tests
./unittest/gunit/ddl_audit/ddl_audit-t --gtest_filter="*IsDDLCommand*"

# Run only the LEX-based extraction tests
./unittest/gunit/ddl_audit/ddl_audit-t --gtest_filter="*Extract*FromLex*"

# Run only the UID generation tests
./unittest/gunit/ddl_audit/ddl_audit-t --gtest_filter="*UID*"
```

## Test Structure

The tests follow Google Test (gtest) framework conventions:

```cpp
TEST_F(DDL_audit_test, TestName) {
  // Test implementation
  EXPECT_EQ(expected, actual);
  EXPECT_TRUE(condition);
  EXPECT_STREQ(expected_str, actual_str);
}
```

## Adding New Tests

When adding new helper functions to the DDL Audit plugin:

1. **Add the function signature to `plugin/ddl_audit/ddl_audit.h`**
   - Use the naming convention: `ddl_audit_<function_name>`
   - Add comprehensive documentation

2. **Add the wrapper function to `plugin/ddl_audit/ddl_audit.cc`**
   - At the end of the file, add a wrapper that calls the static helper function
   - This exposes the function for testing without modifying the implementation

3. **Add test cases to `unittest/gunit/ddl_audit/ddl_audit-t.cc`**
   - Create a new `TEST_F` for your function
   - Test normal cases, edge cases, and error conditions
   - Use descriptive test names

4. **Rebuild and run tests**
   ```bash
   make ddl_audit-t
   ./unittest/gunit/ddl_audit/ddl_audit-t
   ```

## Dependencies

The unit tests depend on:

- **Google Test (gtest)**: Testing framework
- **Google Mock (gmock)**: Mocking framework (not heavily used in these tests)
- **MySQL headers**: For SQL command definitions (`my_sqlcommand.h`)
- **Standard C++**: String manipulation, vectors, etc.

## Continuous Integration

These tests should be included in the CI pipeline:

```bash
# In your CI script
cd build
make ddl_audit-t
ctest -R ddl_audit --output-on-failure
```

## Troubleshooting

### Compilation Errors

If you encounter compilation errors:

1. **Missing headers**: Ensure MySQL source is properly configured
   ```bash
   cmake .. -DWITH_UNIT_TESTS=ON
   ```

2. **Link errors**: Check that all dependencies are available
   - libcurl
   - jsoncpp
   - MySQL libraries

### Test Failures

If tests fail:

1. **Run with verbose output**:
   ```bash
   ./unittest/gunit/ddl_audit/ddl_audit-t --gtest_print_time=1
   ```

2. **Run specific failing test**:
   ```bash
   ./unittest/gunit/ddl_audit/ddl_audit-t --gtest_filter="*FailingTestName*"
   ```

3. **Check for recent code changes**: The tests are sensitive to changes in helper function implementations

## Code Coverage

To generate code coverage for these tests:

```bash
# Build with coverage flags
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_GCOV=ON
make ddl_audit-t

# Run tests
./unittest/gunit/ddl_audit/ddl_audit-t

# Generate coverage report
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

## Related Files

- **Plugin source**: `plugin/ddl_audit/ddl_audit.cc`
- **Plugin header**: `plugin/ddl_audit/ddl_audit.h`
- **Plugin CMakeLists**: `plugin/ddl_audit/CMakeLists.txt`
- **Test source**: `unittest/gunit/ddl_audit/ddl_audit-t.cc`
- **Test CMakeLists**: `unittest/gunit/ddl_audit/CMakeLists.txt`

## See Also

- DDL Rewriter unit tests: `unittest/gunit/ddl_rewriter/`
- MySQL Test Framework (MTR) tests: `mysql-test/`
- Plugin README: `plugin/ddl_audit/README.md`

