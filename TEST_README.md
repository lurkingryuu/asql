# MySQL Authorization Plugin Test Suite

This directory contains comprehensive test scripts for the MySQL Authorization Plugin system.

## Files Overview

### Test Scripts
- **`test_authorization_plugin.sh`** - Main CLI test script for authorization plugins
- **`build_and_test.sh`** - Build and basic test script for MySQL server
- **`plugin/authorization/test_authorization_plugin.sql`** - SQL-based test script

### Plugin Files
- **`plugin/authorization/simple_authorization.cc`** - Simple authorization plugin example
- **`plugin/authorization/external_authorization.cc`** - External HTTP-based authorization plugin
- **`plugin/authorization/CMakeLists.txt`** - Plugin build configuration
- **`plugin/authorization/README.md`** - Plugin documentation
- **`plugin/authorization/BUILD.md`** - Plugin build guide

### Core Implementation Files
- **`include/mysql/plugin_authorization.h`** - Plugin interface header
- **`sql/sql_authorization_plugin.h/.cc`** - Plugin management
- **`sql/auth/sql_authorization.cc`** - Authorization hooks

## Quick Start

### 1. Build MySQL with Authorization Plugin Support

```bash
# Run the build script
./build_and_test.sh

# Or build manually
mkdir build && cd build
cmake .. -DWITH_BOOST=$HOME/boost_1_77_0 -DWITH_AUTHORIZATION_PLUGIN=ON
make -j$(nproc)
sudo make install
```

### 2. Start MySQL Server

```bash
# Initialize and start MySQL
cd build
bin/mysqld --initialize-insecure --user=mysql --datadir=./data
bin/mysqld --user=mysql --datadir=./data --socket=/tmp/mysql.sock &
```

### 3. Run Authorization Plugin Tests

```bash
# Basic test with socket connection
./test_authorization_plugin.sh --mysql-socket /tmp/mysql.sock

# Test with TCP connection
./test_authorization_plugin.sh --mysql-port 3306 --mysql-user root --mysql-password yourpassword

# Test only simple plugin with verbose output
./test_authorization_plugin.sh --simple-only --verbose --mysql-socket /tmp/mysql.sock

# Just cleanup test data
./test_authorization_plugin.sh --cleanup-only --mysql-socket /tmp/mysql.sock
```

## Test Script Options

### Connection Options
- `--mysql-socket PATH` - MySQL socket path (default: none, uses TCP)
- `--mysql-port PORT` - MySQL port for TCP connections (default: 3306)
- `--mysql-user USER` - MySQL user to connect as (default: root)
- `--mysql-password PASS` - MySQL password (default: empty)

### Test Selection Options
- `--simple-only` - Test only simple_authorization plugin
- `--external-only` - Test only external_authorization plugin
- `--cleanup-only` - Only cleanup test environment and exit

### Output Options
- `--verbose` - Enable verbose output
- `--debug` - Enable debug output with shell tracing
- `-h, --help` - Show help message

## Test Scenarios

The test script covers comprehensive authorization scenarios:

### Simple Authorization Plugin Tests

1. **IGNORE Mode**: Plugin falls back to built-in MySQL authorization
2. **GRANT Mode with User Allowlist**: Specific users get access via plugin
3. **GRANT Mode with User+DB Allowlist**: User gets access only to specific databases
4. **DENY Mode**: Plugin explicitly denies all access
5. **SQL Operations**: Tests SELECT, INSERT, UPDATE operations
6. **User Lifecycle**: Tests user creation, privilege changes, deletion

### External Authorization Plugin Tests

1. **No External Service**: Plugin ignores when no URL configured
2. **Service Configuration**: Tests URL and timeout settings
3. **Error Handling**: Tests plugin behavior with service failures

## Test Output

The script provides colored, clear output:

```bash
[INFO] === MYSQL AUTHORIZATION PLUGIN TEST SCRIPT ===
[INFO] Connection args: -u root --socket=/tmp/mysql.sock
[INFO] Checking MySQL connection...
[SUCCESS] Connected to MySQL server: 8.0.27
[INFO] Checking plugin files...
[SUCCESS] Simple authorization plugin found: /usr/local/mysql/lib/plugin/simple_authorization.so
[INFO] === TESTING SIMPLE AUTHORIZATION PLUGIN ===
[TEST] User should be denied access (no built-in privileges)
[PASS] User should be denied access (no built-in privileges)
...
[SUCCESS] All authorization plugin tests passed!
```

## Troubleshooting

### Common Issues

**1. Plugin Not Found**
```bash
[WARNING] Simple authorization plugin not found: /usr/local/mysql/lib/plugin/simple_authorization.so
```
**Solution**: Ensure MySQL was built with authorization plugin support and plugins are in the correct directory.

**2. MySQL Connection Failed**
```bash
[ERROR] Cannot connect to MySQL server
```
**Solution**: Check MySQL server is running and connection parameters are correct.

**3. Test Failures**
- Enable `--verbose` or `--debug` for detailed output
- Check MySQL error log: `tail -f /var/log/mysql/error.log`
- Check general query log: `tail -f /tmp/mysql-auth-plugin-test.log`

### Debug Mode

Enable debug output to see all MySQL commands executed:

```bash
./test_authorization_plugin.sh --debug --mysql-socket /tmp/mysql.sock
```

This will show:
- All MySQL queries being executed
- Connection details
- Plugin installation commands
- Test setup and cleanup operations

### Verbose Mode

Enable verbose output for detailed test progress:

```bash
./test_authorization_plugin.sh --verbose --mysql-socket /tmp/mysql.sock
```

## Integration with CI/CD

The test script can be integrated into CI/CD pipelines:

```yaml
# GitHub Actions example
- name: Test Authorization Plugin
  run: |
    ./test_authorization_plugin.sh \
      --mysql-socket /tmp/mysql.sock \
      --verbose \
      --simple-only
```

```bash
# Jenkins example
./test_authorization_plugin.sh \
  --mysql-port 3306 \
  --mysql-user root \
  --mysql-password $MYSQL_PASSWORD \
  --verbose
```

## Expected Test Results

### Successful Run
```
[INFO] Simple Authorization Plugin Test Results: 8/8 tests passed
[SUCCESS] All authorization plugin tests passed!
```

### Failed Run
```
[FAIL] User should be denied access (no built-in privileges)
[ERROR] 1 plugin test(s) failed
```

## Manual Testing

You can also run individual SQL commands manually:

```sql
-- Install plugin
INSTALL PLUGIN simple_authorization SONAME 'simple_authorization.so';

-- Configure plugin
SET GLOBAL simple_authorization_mode = 'grant';
SET GLOBAL simple_authorization_allow_user = 'testuser';

-- Create test user
CREATE USER 'testuser'@'localhost' IDENTIFIED BY 'password';

-- Test access (should succeed due to plugin)
-- Connect as testuser and try: USE testdb; SELECT * FROM test_table;
```

## Performance Considerations

- **Plugin Overhead**: Authorization plugins add minimal overhead (~1-5ms per query)
- **Caching**: Consider implementing result caching in production plugins
- **Async Operations**: For external services, consider async patterns
- **Timeout Handling**: Set reasonable timeouts to avoid blocking queries

## Security Notes

- **Plugin Permissions**: Plugins run with MySQL server privileges
- **Input Validation**: Always validate inputs in plugin code
- **Logging**: Enable appropriate logging for security auditing
- **Network Security**: Use HTTPS for external authorization services
- **Configuration Security**: Protect plugin configuration variables

## Support

For issues with the authorization plugin system:

1. Check the [Plugin Documentation](plugin/authorization/README.md)
2. Review the [Build Guide](plugin/authorization/BUILD.md)
3. Enable debug logging and check MySQL error logs
4. Test with simple scenarios first, then complex ones

## Contributing

When adding new tests:

1. Add test functions following the naming pattern `test_*_plugin()`
2. Use the provided helper functions for consistent output
3. Include both positive and negative test cases
4. Document test scenarios clearly
5. Handle cleanup properly to avoid test interference
