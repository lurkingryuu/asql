# DDL Audit Plugin

This plugin captures DDL (Data Definition Language) statements and sends them to a Cedar server for data population. It triggers only after successful execution of DDL statements.

## Features

- Captures all DDL statements (CREATE, ALTER, DROP, etc.)
- Triggers only after successful execution (post-execution)
- Sends structured data to Cedar server via HTTP
- Configurable Cedar service URL and timeout
- Detailed logging for debugging
- Extracts database, table, user, and context information

## Supported DDL Commands

The plugin captures the following DDL commands:

- **Table Operations**: CREATE TABLE, ALTER TABLE, DROP TABLE, RENAME TABLE
- **Index Operations**: CREATE INDEX, DROP INDEX
- **Database Operations**: CREATE DATABASE, ALTER DATABASE, DROP DATABASE
- **User Operations**: CREATE USER, DROP USER, RENAME USER, ALTER USER
- **Function/Procedure Operations**: CREATE/DROP/ALTER FUNCTION, CREATE/DROP/ALTER PROCEDURE
- **View Operations**: CREATE VIEW, DROP VIEW
- **Trigger Operations**: CREATE TRIGGER, DROP TRIGGER
- **Event Operations**: CREATE EVENT, ALTER EVENT, DROP EVENT
- **Server Operations**: CREATE SERVER, DROP SERVER, ALTER SERVER
- **Role Operations**: CREATE ROLE, DROP ROLE
- **Resource Group Operations**: CREATE/ALTER/DROP RESOURCE GROUP
- **Spatial Reference System**: CREATE SRS, DROP SRS
- **Tablespace Operations**: ALTER TABLESPACE

## Installation

1. Build the plugin:
```bash
cd /path/to/mysql/source
mkdir -p plugin/ddl_audit
# Copy the plugin files to plugin/ddl_audit/
make
```

2. Install the plugin:
```sql
INSTALL PLUGIN ddl_audit SONAME 'ddl_audit.so';
```

3. Configure the plugin:
```sql
SET GLOBAL ddl_audit_cedar_url = 'http://localhost:8180';
SET GLOBAL ddl_audit_cedar_timeout = 5000;  -- milliseconds
SET GLOBAL ddl_audit_enabled = ON;
```

## Configuration Variables

- `ddl_audit_cedar_url`: Cedar server URL (default: http://localhost:8180)
- `ddl_audit_cedar_timeout`: Request timeout in milliseconds (default: 5000)
- `ddl_audit_enabled`: Enable/disable the plugin (default: ON)

## Cedar Server API

The plugin sends POST requests to the Cedar server with the following JSON payload:

```json
{
  "ddl_type": "CREATE_TABLE",
  "sql_command_id": 1,
  "query": "CREATE TABLE test (id INT PRIMARY KEY, name VARCHAR(50))",
  "database": "test_db",
  "table": "test",
  "user": "root",
  "host": "localhost",
  "timestamp": "2025-01-01T12:00:00Z",
  "context": {
    "ip_address": "127.0.0.1",
    "connection_id": 123
  }
}
```

### Payload Fields

- `ddl_type`: Human-readable DDL command type
- `sql_command_id`: MySQL internal SQL command ID
- `query`: The complete DDL statement
- `database`: Database name (extracted from context or query)
- `table`: Table name (extracted from query, if applicable)
- `user`: MySQL user who executed the statement
- `host`: Host from which the user connected
- `timestamp`: ISO 8601 timestamp of execution
- `context`: Additional context information
  - `ip_address`: Client IP address
  - `connection_id`: MySQL connection ID

## Cedar Server Implementation

Your Cedar server should implement an endpoint (e.g., `/v1/ddl_audit`) that:

1. Receives the DDL audit data
2. Parses the DDL statement to extract schema information
3. Updates Cedar entities and policies accordingly
4. Returns appropriate HTTP status code

Example Cedar server endpoint:
```javascript
app.post('/v1/ddl_audit', (req, res) => {
  const ddlData = req.body;
  
  // Process the DDL statement
  if (ddlData.ddl_type === 'CREATE_TABLE') {
    // Create Cedar entities for the new table
    // Update policies if needed
  } else if (ddlData.ddl_type === 'DROP_TABLE') {
    // Remove Cedar entities for the dropped table
  }
  
  res.status(200).json({ status: 'success' });
});
```

## Logging

The plugin logs its activities to the MySQL error log:

- Plugin initialization/deinitialization
- Successful DDL captures
- Errors when communicating with Cedar server
- Configuration issues

## Uninstallation

```sql
UNINSTALL PLUGIN ddl_audit;
```

## Dependencies

- libcurl (for HTTP communication)
- jsoncpp (for JSON processing)
- MySQL 8.0+ audit plugin interface

## Troubleshooting

1. **Plugin fails to load**: Check that all dependencies are installed
2. **No DDL events captured**: Verify `ddl_audit_enabled` is ON
3. **Cedar server communication fails**: Check URL and network connectivity
4. **Performance impact**: Monitor query execution times and adjust timeout if needed

## Security Considerations

- The plugin sends DDL statements to external servers
- Ensure Cedar server is properly secured
- Consider using HTTPS for production deployments
- Review what information is being sent to Cedar server
