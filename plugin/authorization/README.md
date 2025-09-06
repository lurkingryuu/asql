# MySQL Authorization Plugins

This directory contains MySQL authorization plugins that demonstrate how to extend MySQL's authorization system with custom logic.

## Overview

MySQL's authorization system traditionally relies on:
- **DAC (Discretionary Access Control)**: User privileges in mysql.user, mysql.db, mysql.tables_priv, etc.
- **RBAC (Role-Based Access Control)**: User roles and role grants

The authorization plugin system extends this with:
- **Plugin-based Authorization**: Custom authorization logic through loadable plugins
- **External Authorization**: Delegation to external authorization services
- **Hybrid Authorization**: Combination of built-in and plugin-based authorization

## Plugin Architecture

### Authorization Plugin Interface

Authorization plugins implement the `st_mysql_authorization` interface defined in `include/mysql/plugin_authorization.h`:

```c
struct st_mysql_authorization {
  int interface_version;
  mysql_authorization_result_t (*check_authorization)(
      const struct mysql_authorization_event *event);
};
```

### Authorization Events

The plugin system supports different types of authorization events:

- `MYSQL_AUTHORIZATION_DB_ACCESS`: Database-level access checks
- `MYSQL_AUTHORIZATION_TABLE_ACCESS`: Table-level access checks  
- `MYSQL_AUTHORIZATION_COLUMN_ACCESS`: Column-level access checks
- `MYSQL_AUTHORIZATION_ROUTINE_ACCESS`: Stored procedure/function access checks

### Plugin Results

Authorization plugins can return:

- `MYSQL_AUTHORIZATION_GRANT`: Explicitly grant access
- `MYSQL_AUTHORIZATION_DENY`: Explicitly deny access
- `MYSQL_AUTHORIZATION_IGNORE`: Fall back to built-in authorization

### Decision Logic

The MySQL server combines plugin results with built-in authorization:

1. If **any** plugin returns `GRANT`, access is granted
2. If **any** plugin returns `DENY`, access is denied
3. If **all** plugins return `IGNORE`, built-in authorization is used

This allows plugins to:
- Grant access that would normally be denied
- Deny access that would normally be granted
- Selectively handle only certain authorization requests

## Example Plugins

### Simple Authorization Plugin

`simple_authorization.cc` - A basic example that demonstrates:

- Simple rule-based authorization
- User and database allow-lists
- Configurable grant/deny/ignore modes
- System variable configuration

**Usage:**
```sql
INSTALL PLUGIN simple_authorization SONAME 'simple_authorization.so';

-- Configure plugin
SET GLOBAL simple_authorization_allow_user = 'testuser';
SET GLOBAL simple_authorization_allow_db = 'testdb';
SET GLOBAL simple_authorization_mode = 'grant';

-- Test access
-- User 'testuser' will now have access to 'testdb' even without built-in privileges
```

### External Authorization Plugin

`external_authorization.cc` - An advanced example that demonstrates:

- HTTP-based external authorization service integration
- JSON request/response handling
- Timeout and error handling
- Comprehensive logging

**Usage:**
```sql
INSTALL PLUGIN external_authorization SONAME 'external_authorization.so';

-- Configure external service
SET GLOBAL external_authorization_url = 'http://localhost:8080/auth';
SET GLOBAL external_authorization_timeout = 5000;
```

**External Service API:**

The plugin sends POST requests with JSON payload:
```json
{
  "user": "username",
  "host": "hostname",
  "database": "dbname", 
  "table": "tablename",
  "column": "columnname",
  "routine": "routinename",
  "privileges": 123,
  "event_type": "db_access|table_access|column_access|routine_access",
  "sql_command": "SELECT",
  "query": "SELECT * FROM table1",
  "is_procedure": false
}
```

Expected response:
```json
{
  "result": "grant|deny|ignore"
}
```

## Building Plugins

### Prerequisites

- MySQL 8.0+ source code
- C++ compiler with C++11 support
- CMake 3.5+
- For external plugin: libcurl, jsoncpp

### Build Process

1. **Add to MySQL build:**
   ```bash
   # Copy plugin directory to MySQL source
   cp -r plugin/authorization /path/to/mysql-source/plugin/
   ```

2. **Configure and build:**
   ```bash
   cd /path/to/mysql-source
   mkdir build && cd build
   cmake .. -DWITH_AUTHORIZATION_PLUGIN=ON
   make
   ```

3. **Install plugins:**
   ```bash
   # Copy plugin library to MySQL plugin directory
   cp plugin/authorization/simple_authorization.so /usr/local/mysql/lib/plugin/
   ```

### Manual Build

You can also build plugins manually:

```bash
# Simple authorization plugin
gcc -fPIC -shared -o simple_authorization.so \
  -I/usr/local/mysql/include \
  simple_authorization.cc

# External authorization plugin (requires dependencies)
gcc -fPIC -shared -o external_authorization.so \
  -I/usr/local/mysql/include \
  -lcurl -ljsoncpp \
  external_authorization.cc
```

## Testing

### Basic Functionality Test

```sql
-- Create test user without privileges
CREATE USER 'testuser'@'localhost' IDENTIFIED BY 'password';

-- Create test database
CREATE DATABASE testdb;

-- Install and configure plugin
INSTALL PLUGIN simple_authorization SONAME 'simple_authorization.so';
SET GLOBAL simple_authorization_allow_user = 'testuser';
SET GLOBAL simple_authorization_allow_db = 'testdb';  
SET GLOBAL simple_authorization_mode = 'grant';

-- Test access (should succeed due to plugin)
-- Connect as testuser
USE testdb;  -- Should work due to plugin authorization
```

### Plugin Status Check

```sql
-- Check plugin status
SHOW PLUGINS;
SELECT * FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME LIKE '%authorization%';

-- Check plugin variables
SHOW VARIABLES LIKE 'simple_authorization%';
```

### Debugging

Enable general log to see authorization decisions:
```sql
SET GLOBAL general_log = ON;
SET GLOBAL general_log_file = '/tmp/mysql-general.log';
```

Plugin debug output goes to MySQL error log:
```bash
tail -f /var/log/mysql/error.log | grep -i authorization
```

## Security Considerations

### Plugin Security

- **Validate all inputs**: Check all event parameters for NULL and bounds
- **Handle errors gracefully**: Return appropriate results on failures
- **Log security events**: Track authorization decisions for auditing
- **Secure configuration**: Protect plugin system variables
- **Network security**: Use HTTPS for external authorization services

### Performance Considerations

- **Minimize latency**: Authorization checks happen on every query
- **Cache results**: Cache authorization decisions when appropriate
- **Handle timeouts**: Set reasonable timeouts for external services
- **Async processing**: Consider async patterns for complex authorization logic

### Operational Considerations

- **Monitor plugin health**: Track plugin errors and performance
- **Backup configuration**: Include plugin settings in backup procedures
- **Rolling updates**: Plan for plugin updates without downtime
- **Failover behavior**: Define behavior when external services are unavailable

## Troubleshooting

### Common Issues

1. **Plugin won't load**
   - Check plugin library path and permissions
   - Verify MySQL plugin directory configuration
   - Check MySQL error log for initialization errors

2. **Authorization not working**
   - Verify plugin is loaded: `SHOW PLUGINS`
   - Check plugin variables: `SHOW VARIABLES LIKE 'simple_authorization%'`
   - Enable debug logging to see authorization decisions

3. **Performance issues**
   - Check plugin execution time in performance schema
   - Monitor external service response times
   - Consider caching strategies

### Debug Mode

For development, compile plugins with debug symbols:
```bash
gcc -g -DDEBUG -fPIC -shared -o plugin.so plugin.cc
```

Enable debug output in plugin code and check MySQL error log.

## Advanced Usage

### Multiple Plugins

You can install multiple authorization plugins:

```sql
INSTALL PLUGIN simple_authorization SONAME 'simple_authorization.so';
INSTALL PLUGIN external_authorization SONAME 'external_authorization.so';
```

The server will consult all plugins in installation order.

### Custom Plugin Development

To create your own authorization plugin:

1. **Implement the interface:**
   ```c
   #include <mysql/plugin_authorization.h>
   
   mysql_authorization_result_t my_check_authorization(
       const mysql_authorization_event *event) {
     // Your custom logic here
     return MYSQL_AUTHORIZATION_IGNORE;
   }
   ```

2. **Create plugin descriptor:**
   ```c
   static st_mysql_authorization my_authorization_descriptor = {
     MYSQL_AUTHORIZATION_INTERFACE_VERSION,
     my_check_authorization
   };
   ```

3. **Declare plugin:**
   ```c
   mysql_declare_plugin(my_authorization) {
     MYSQL_AUTHORIZATION_PLUGIN,
     &my_authorization_descriptor,
     "my_authorization",
     // ... other fields
   }
   mysql_declare_plugin_end;
   ```

## References

- [MySQL Plugin API Documentation](https://dev.mysql.com/doc/refman/8.0/en/plugin-api.html)
- [MySQL Security Guide](https://dev.mysql.com/doc/refman/8.0/en/security.html)
- [Writing Custom Plugins](https://dev.mysql.com/doc/extending-mysql/8.0/en/plugin-services.html)
