# Cedar Authorization Plugin for MySQL

This plugin integrates Amazon Cedar policy engine with MySQL's authorization system, providing fine-grained access control based on policies.

## Features

- **Policy-based authorization**: Uses Cedar's powerful policy language
- **Rich context**: Includes time, date, IP address, and other contextual information
- **Flexible resource model**: Supports Database, Table, Column, and Routine resources
- **Action-based permissions**: Maps MySQL privileges to Cedar actions
- **Real-time decisions**: Queries Cedar service for each authorization request

## Installation

### Prerequisites

- MySQL server with plugin support
- Cedar authorization service running
- Required libraries: curl, jsoncpp

### Building the Plugin

```bash
# In your MySQL build directory
cmake -DWITH_AUTHORIZATION_PLUGINS=ON .
make cedar_authorization
```

### Installing the Plugin

```sql
-- Install the plugin
INSTALL PLUGIN cedar_authorization SONAME 'cedar_authorization.so';

-- Configure Cedar service URL
SET GLOBAL cedar_authorization_url = 'http://localhost:8180/v1/is_authorized';

-- Optional: Set timeout (default 5000ms)
SET GLOBAL cedar_authorization_timeout = 5000;

-- Verify installation
SHOW PLUGINS;
```

## Configuration

### System Variables

- `cedar_authorization_url`: URL of the Cedar authorization service
- `cedar_authorization_timeout`: Request timeout in milliseconds (1000-60000, default 5000)

### Cedar Service Setup

The plugin expects a Cedar authorization service running at the configured URL with the following endpoint:

```
POST /v1/is_authorized
Content-Type: application/json
```

## Request Format

The plugin sends requests in the following JSON format:

```json
{
  "principal": "User::\"username\"",
  "action": "Action::\"Select\"",
  "resource": "Table::\"tablename\"",
  "context": {
    "day": "mon",
    "date": 20250101,
    "time": 120000,
    "ip": {
      "__extn": {
        "fn": "ip",
        "arg": "192.168.1.1"
      }
    }
  }
}
```

### Fields Explained

- **principal**: User entity in Cedar format (`User::"username"`)
- **action**: Requested action (Select, Insert, Update, Delete, Create, Drop, etc.)
- **resource**: Target resource (Database, Table, Column, or Routine)
- **context**: Additional information including:
  - `day`: Day of week (lowercase, e.g., "mon", "tue")
  - `date`: Date as YYYYMMDD integer
  - `time`: Time as HHMMSS integer
  - `ip`: Client IP address as Cedar extension function

## Response Format

Expected response from Cedar service:

```json
{
  "decision": "Allow",
  "diagnostics": {
    "errors": []
  }
}
```

- **decision**: "Allow" or "Deny"
- **diagnostics**: Optional error information

## Resource Types

The plugin maps MySQL objects to Cedar resources:

- **Database access**: `Database::"dbname"`
- **Table access**: `Table::"tablename"`
- **Column access**: `Column::"columnname"`
- **Routine access**: `Routine::"routinename"`

## Actions

MySQL privileges are mapped to Cedar actions:

| MySQL Privilege | Cedar Action |
|----------------|--------------|
| SELECT_ACL | Select |
| INSERT_ACL | Insert |
| UPDATE_ACL | Update |
| DELETE_ACL | Delete |
| CREATE_ACL | Create |
| DROP_ACL | Drop |
| ALTER_ACL | Alter |
| CREATE_VIEW_ACL | CreateView |
| SHOW_VIEW_ACL | ShowView |
| CREATE_PROC_ACL | CreateRoutines |
| ALTER_PROC_ACL | AlterRoutines |
| EXECUTE_ACL | Execute |

## Sample Cedar Policies

### Basic Time-Based Access

```cedar
permit (
    principal == User::"alice",
    action == Action::"Select",
    resource == Table::"employees"
) when {
    context.time >= 90000 && context.time <= 170000  // 9:00 AM to 5:00 PM
};
```

### IP-Based Restrictions

```cedar
permit (
    principal == User::"bob",
    action == Action::"Insert",
    resource == Table::"orders"
) when {
    ip("192.168.1.0/24").contains(context.ip)
};
```

### Day-of-Week Restrictions

```cedar
permit (
    principal == User::"maintenance",
    action == Action::"Drop",
    resource == Database::"test_db"
) when {
    context.day == "sat" || context.day == "sun"
};
```

### Role-Based Access

```cedar
permit (
    principal in Role::"analysts",
    action == Action::"Select",
    resource
) when {
    resource.type == "Table" && resource.schema == "analytics"
};
```

## Testing

### Basic Functionality Test

```sql
-- Create test user
CREATE USER 'testuser'@'%' IDENTIFIED BY 'password';

-- Try to access a table (should be evaluated by Cedar)
USE test_db;
SELECT * FROM test_table;
```

### Monitor Plugin Activity

Check the MySQL error log for Cedar plugin messages:

```bash
tail -f /var/log/mysql/error.log | grep -i cedar
```

### Debug Mode

Enable additional logging by setting log level:

```sql
SET GLOBAL log_error_verbosity = 3;
```

## Troubleshooting

### Common Issues

1. **Plugin not loading**
   - Check library dependencies (httplib, nlohmann/json)
   - Verify plugin file permissions
   - Check MySQL error log

2. **Cedar service unreachable**
   - Verify service URL configuration
   - Check network connectivity
   - Increase timeout if needed

3. **Authorization always denied**
   - Verify Cedar policies are correctly defined
   - Check request format in logs
   - Ensure Cedar service is responding correctly

### Log Analysis

The plugin logs detailed information about:
- Authorization requests and responses
- Cedar service communication
- Context information (time, IP, etc.)
- Decision outcomes

Example log entries:
```
[Note] Cedar authorization callback invoked for user: alice@localhost, event: table_access
[Note] Checking Cedar authorization for action: Select
[Note] Context: day=mon, date=20250101, time=120000, client_ip=192.168.1.100
[Note] Cedar request: {"principal":"User::\"alice\"","action":"Action::\"Select\"","resource":"Table::\"employees\"","context":{"day":"mon","date":20250101,"time":120000,"ip":{"__extn":{"fn":"ip","arg":"192.168.1.100"}}}}
[Note] Cedar response: {"decision":"Allow","diagnostics":{"errors":[]}}
[Note] Cedar authorization: GRANT
```

## Security Considerations

1. **Secure Communication**: Use HTTPS for Cedar service communication in production
2. **Policy Management**: Implement proper policy versioning and rollback mechanisms
3. **Monitoring**: Set up alerts for authorization failures and service errors
4. **Fallback Behavior**: The plugin denies access on Cedar service errors
5. **Resource Naming**: Use consistent resource naming conventions in policies

## Performance Notes

- Each SQL operation may result in multiple authorization checks
- Consider Cedar service performance and scaling
- Monitor latency impact on query execution
- Use appropriate timeout values

## Integration Examples

### With Cedar CLI

```bash
# Validate policies
cedar validate --policies policies.cedar --schema schema.cedarschema

# Evaluate specific authorization
cedar authorize \
  --policies policies.cedar \
  --principal 'User::"alice"' \
  --action 'Action::"Select"' \
  --resource 'Table::"employees"' \
  --context '{"day":"mon","time":120000}'
```

### With Cedar Agent

Configure Cedar Agent to serve the authorization endpoint and manage policies dynamically.

## Version Compatibility

- MySQL 8.0+
- Cedar 2.0+
- C++17 or later
- CMake 3.10+

## License

This plugin is licensed under the GNU General Public License, version 2.0.
