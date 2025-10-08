# Integrated Testing Guide: DDL Audit + Cedar Authorization Plugins

This guide explains how to test the DDL Audit and Cedar Authorization plugins working together with a **real Cedar Agent server** in a comprehensive, multi-stage workflow.

## Overview

The integrated test suite validates that both plugins work correctly together by:

1. **Stage 1**: Loading both plugins and configuring system variables
2. **Stage 2**: Creating databases, tables, and users
3. **Stage 3**: Defining authorization policies in Cedar Agent
4. **Stage 4**: Testing access control enforcement
5. **Stage 5**: Cleaning up all resources and unloading plugins

## Architecture

```
┌─────────────────────┐
│   MySQL Server      │
│  ┌──────────────┐   │
│  │ DDL Audit    │───┼──→ Cedar Agent Server
│  │ Plugin       │   │    http://localhost:8280/v1/data/single/<entity_id>
│  └──────────────┘   │
│  ┌──────────────┐   │
│  │ Cedar Auth   │───┼──→ Cedar Agent Server
│  │ Plugin       │   │    http://localhost:8280/v1/is_authorized
│  └──────────────┘   │
└─────────────────────┘
```

## Prerequisites

### 1. Cedar Agent Server

You must have a real Cedar Agent server built and ready to run. The Cedar Agent is a Rust-based HTTP server that manages Cedar policies and authorization decisions.

**Building Cedar Agent** (if not already built):
```bash
cd /path/to/cedar-agent
cargo build --release
```

### 2. MySQL Server

Either local MySQL or Docker-based MySQL with both plugins compiled.

## Quick Start

### Step 1: Start Cedar Agent

Use the provided startup script with pre-configured schema, data, and policies:

```bash
# Make the script executable
chmod +x scripts/start_cedar_agent.sh

# Start Cedar Agent with test configuration
./scripts/start_cedar_agent.sh

# Or manually:
./target/release/cedar-agent \
  -l debug \
  -s scripts/cedar_agent_schema.json \
  -d scripts/cedar_agent_data.json \
  --policies scripts/cedar_agent_policies.json \
  --addr 0.0.0.0 --port 8280
```

**Expected output:**
```
Cedar Agent is running at http://0.0.0.0:8280
```

### Step 2: Run the Integrated Test

In a new terminal:

```bash
# Make the test script executable
chmod +x test_integrated_plugins.sh

# Run with Docker MySQL
./test_integrated_plugins.sh --docker

# Or with local MySQL
./test_integrated_plugins.sh \
  --mysql-port 3306 \
  --mysql-user root \
  --mysql-password yourpassword

# With custom Cedar Agent URL
./test_integrated_plugins.sh \
  --docker \
  --cedar-url http://localhost:8280/v1

# With authentication token (if Cedar Agent requires it)
./test_integrated_plugins.sh \
  --docker \
  --cedar-url http://localhost:8280/v1 \
  --cedar-auth-token your-api-key
```

## Cedar Agent Configuration Files

The test suite includes pre-configured Cedar Agent files:

### 1. Schema (`scripts/cedar_agent_schema.json`)

Defines the entity types and actions for MySQL:
- **Entity Types**: User, Role, Group, Table, Database, TableGroup, ActionGroup
- **Actions**: Select, Insert, Update, Delete, Create, Drop, Alter, Execute
- **Attributes**: department, role, database, classification, etc.

### 2. Data (`scripts/cedar_agent_data.json`)

Contains test entities:
- **Users**: alice_user, bob_user, charlie_user, hr_manager, developer, analyst, admin_user
- **Tables**: employees, projects, payroll, audit_log, public_reports
- **Databases**: company_db, reporting_db
- **Groups**: HRTables, ProjectModify, DataModify

### 3. Policies (`scripts/cedar_agent_policies.json`)

Pre-defined authorization policies:
- alice_user: SELECT on employees table only
- bob_user: SELECT/INSERT/UPDATE on projects table
- hr_manager: Full access to HR tables (employees, payroll)
- developer: Read-only (SELECT) access to all tables
- analyst: SELECT access to reporting_db only
- admin_user: Full access to everything
- Deny DELETE on payroll (except admin)

## Test Stages Explained

### Stage 1: Load Plugins and Configure

This stage:
- Checks Cedar Agent connectivity
- Installs the `ddl_audit` plugin and enables it
- Configures DDL audit settings for Cedar Agent integration:
  ```sql
  SET GLOBAL ddl_audit_cedar_url = 'http://localhost:8280';
  SET GLOBAL ddl_audit_cedar_timeout = 5000;
  SET GLOBAL ddl_audit_enabled = ON;
  ```
- Installs the `cedar_authorization` plugin
- Configures Cedar authorization:
  ```sql
  SET GLOBAL cedar_authorization_url = 'http://localhost:8280/v1/is_authorized';
  SET GLOBAL cedar_authorization_timeout = 5000;
  ```

**Success Criteria**: Both plugins are ACTIVE, Cedar Agent is accessible, and DDL audit is enabled.

**DDL Audit Integration**: The DDL audit plugin automatically detects Cedar Agent and uses the `/v1/data/single/<entity_id>` endpoint to populate entity data when DDL operations occur (CREATE TABLE, CREATE USER, etc.).

### Stage 2: Create Test Resources

Creates a realistic test environment:

**Databases**:
- `company_db` - Main company database
- `reporting_db` - Reporting/analytics database

**Tables in company_db**:
- `employees` - Employee information
- `projects` - Project tracking
- `payroll` - Sensitive payroll data
- `audit_log` - Audit trail

**Tables in reporting_db**:
- `public_reports` - Public reporting data

**Test Users**:
- `alice_user` - Limited SELECT access
- `bob_user` - Project manager
- `charlie_user` - IT support
- `hr_manager` - HR department access
- `developer` - Read-only access
- `analyst` - Reporting access only
- `admin_user` - Full admin access

**Success Criteria**: All databases, tables, and users created successfully.

### Stage 3: Define Cedar Policies

**NOTE**: If Cedar Agent is started with `--policies` flag, policies are already loaded.

The test attempts to define additional policies via the Cedar Agent API, but will continue if policies already exist.

Policies enforce:
1. **alice_user**: Can SELECT from employees table only
2. **bob_user**: Can SELECT, INSERT, UPDATE on projects table
3. **hr_manager**: Full access to employees and payroll tables
4. **developer**: Read-only (SELECT) access to all tables
5. **analyst**: SELECT access limited to reporting_db
6. **admin_user**: Unrestricted access to everything
7. **DELETE restriction**: No one can DELETE from payroll (except admin)

**Success Criteria**: Policies are available in Cedar Agent.

### Stage 4: Test Access Control Enforcement

Runs 15 comprehensive tests:

| # | User | Action | Expected | Description |
|---|------|--------|----------|-------------|
| 1 | alice_user | SELECT employees | ✓ PASS | Allowed by policy |
| 2 | alice_user | INSERT employees | ✓ PASS (denied) | No permission |
| 3 | bob_user | SELECT projects | ✓ PASS | Allowed by policy |
| 4 | bob_user | INSERT projects | ✓ PASS | Allowed by policy |
| 5 | bob_user | SELECT employees | ✓ PASS (denied) | No permission |
| 6 | hr_manager | SELECT employees | ✓ PASS | Allowed by policy |
| 7 | hr_manager | SELECT payroll | ✓ PASS | Allowed by policy |
| 8 | hr_manager | UPDATE employees | ✓ PASS | Allowed by policy |
| 9 | developer | SELECT employees | ✓ PASS | Read-only access |
| 10 | developer | INSERT employees | ✓ PASS (denied) | Read-only only |
| 11 | analyst | SELECT reporting_db | ✓ PASS | Allowed by policy |
| 12 | analyst | SELECT company_db | ✓ PASS (denied) | No permission |
| 13 | admin_user | CREATE TABLE | ✓ PASS | Full access |
| 14 | admin_user | DROP TABLE | ✓ PASS | Full access |
| 15 | hr_manager | DELETE payroll | ✓ PASS (denied) | Explicitly denied |

**Success Criteria**: All 15 tests pass correctly.

### Stage 5: Cleanup

Thorough cleanup:
- Disable plugins
- Drop test databases
- Drop test users
- Uninstall plugins
- **Note**: Cedar Agent policies/data are preserved (restart Cedar Agent to reset)

## Advanced Usage

### Custom Cedar Agent Configuration

You can provide your own Cedar Agent configuration files:

```bash
# Start with custom files
./scripts/start_cedar_agent.sh \
  --schema ~/my-schema.json \
  --data ~/my-data.json \
  --policies ~/my-policies.json \
  --port 8280
```

### Testing with Authentication

If your Cedar Agent requires authentication:

```bash
# Set authentication token in Cedar Agent
export CEDAR_AGENT_AUTHENTICATION=my-secret-token

# Start Cedar Agent
./target/release/cedar-agent -a my-secret-token --port 8280 ...

# Run tests with token
./test_integrated_plugins.sh \
  --docker \
  --cedar-auth-token my-secret-token
```

### Viewing Cedar Agent State

Check policies, entities, and schema:

```bash
# List all policies
curl http://localhost:8280/v1/policies | jq

# List all entities
curl http://localhost:8280/v1/data | jq

# Get schema
curl http://localhost:8280/v1/schema | jq

# Health check
curl http://localhost:8280/
```

### Manual Authorization Testing

Test authorization directly with Cedar Agent:

```bash
curl -X POST http://localhost:8280/v1/is_authorized \
  -H "Content-Type: application/json" \
  -d '{
    "principal": {
      "type": "User",
      "id": "alice_user"
    },
    "action": {
      "type": "Action",
      "id": "Select"
    },
    "resource": {
      "type": "Table",
      "id": "employees"
    },
    "context": {}
  }'
```

## Troubleshooting

### Cedar Agent Not Starting

**Problem**: Cedar Agent fails to start

**Solutions**:
1. Check port availability:
   ```bash
   lsof -i :8280
   ```

2. Validate configuration files:
   ```bash
   jq . scripts/cedar_agent_schema.json
   jq . scripts/cedar_agent_data.json
   jq . scripts/cedar_agent_policies.json
   ```

3. Check Cedar Agent logs for errors

### Plugin Connection Errors

**Problem**: `cedar_authorization_url` connection timeout

**Solutions**:
1. Verify Cedar Agent is running:
   ```bash
   curl http://localhost:8280/
   ```

2. Check MySQL configuration:
   ```sql
   SHOW VARIABLES LIKE 'cedar_authorization_url';
   ```

3. Ensure network connectivity between MySQL and Cedar Agent

### Policy Not Working

**Problem**: Authorization always denied

**Solutions**:
1. Check if policies are loaded:
   ```bash
   curl http://localhost:8280/v1/policies | jq
   ```

2. Verify entity exists:
   ```bash
   curl http://localhost:8280/v1/data | jq '.[] | select(.uid.id == "alice_user")'
   ```

3. Test authorization directly (see Manual Authorization Testing above)

4. Enable debug logging:
   ```bash
   ./target/release/cedar-agent -l debug ...
   ```

### Tests Failing

**Problem**: Some tests fail unexpectedly

**Solutions**:
1. Run with debug mode:
   ```bash
   ./test_integrated_plugins.sh --debug --verbose --docker
   ```

2. Check Cedar Agent is using the correct policy file

3. Verify schema matches the test requirements

4. Check MySQL error log for authorization plugin errors

## Files Reference

### Configuration Files
- `scripts/cedar_agent_schema.json` - Cedar schema for MySQL entities
- `scripts/cedar_agent_data.json` - Test entities and relationships
- `scripts/cedar_agent_policies.json` - Authorization policies

### Scripts
- `test_integrated_plugins.sh` - Main integrated test script
- `scripts/start_cedar_agent.sh` - Cedar Agent startup helper
- `scripts/define_cedar_policies.sh` - Policy management (optional)

### Legacy Files (for mock service)
- `scripts/cedar_policies.json` - Example policies (not for Cedar Agent)

## Complete Testing Workflow

Here's the complete workflow from start to finish:

```bash
# 1. Start Cedar Agent
./scripts/start_cedar_agent.sh
# Wait for "Cedar Agent is running at http://0.0.0.0:8280"

# 2. In another terminal, run the integrated test
./test_integrated_plugins.sh --docker

# 3. Review test results
# Expected: All 5 stages PASS

# 4. Optional: Run cleanup only
./test_integrated_plugins.sh --docker --cleanup-only

# 5. Stop Cedar Agent (Ctrl+C in first terminal)
```

## Next Steps

After successful testing:

1. **Production Deployment**: Adapt the configuration for your production environment
2. **Custom Policies**: Create policies specific to your use case
3. **Performance Testing**: Test with realistic workloads
4. **Monitoring**: Set up monitoring for both plugins and Cedar Agent
5. **HA Setup**: Configure Cedar Agent for high availability

## Related Documentation

- [Cedar Agent API Documentation](plugin/ddl_audit/CEDAR_AGENT_ALL_API_DOCUMENTATION.md)
- [DDL Audit Plugin](plugin/ddl_audit/README.md)
- [Cedar Authorization Plugin](plugin/authorization/README.md)
- [Cedar Policy Language](https://www.cedarpolicy.com/)

## Support

For issues:
1. Check troubleshooting section
2. Enable debug logging on both MySQL plugins and Cedar Agent
3. Verify Cedar Agent configuration files
4. Check network connectivity
5. Review Cedar Agent and MySQL error logs
