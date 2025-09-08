# External Authorization Plugin Testing

This document describes the comprehensive testing framework for the MySQL External Authorization Plugin.

## Overview

The external authorization plugin delegates authorization decisions to an external HTTP service, allowing for flexible, policy-based access control. Our testing framework validates:

- **Privilege Array Handling**: Tests the new array-based privilege format
- **Multi-level Authorization Policies**: User/DB, privilege-based, time-based, and IP-based policies
- **Sensitive Column Protection**: Column-level access control
- **Error Handling**: External service failures and timeout scenarios
- **Real-world Scenarios**: Business hours access, admin privileges, etc.

## Test Components

### 1. Authorization Service (`docker/auth-service/server.js`)

A Node.js service that implements the external authorization API:

- **Endpoint**: `POST /auth`
- **Input**: JSON with user, database, table, column, privileges array, etc.
- **Output**: JSON with `result: "grant|deny|ignore"`

**Features**:
- ✅ Array-based privilege handling (updated for plugin v1.1)
- ✅ Multi-level authorization policies 
- ✅ Sensitive column protection
- ✅ Time-based access control
- ✅ User/database access policies

### 2. Test Script (`test_authorization_plugin.sh`)

Enhanced test script with comprehensive external authorization testing:

**New Options**:
- `--start-auth-service`: Automatically start the auth service
- `--auth-service-port PORT`: Specify auth service port (default: 8080)

**Test Coverage**:
1. **Basic Plugin Tests**: No external URL (ignore mode)
2. **User/Database Policies**: testuser→testdb, admin→all
3. **Unauthorized Access**: Denied for unknown users  
4. **Privilege-based Control**: Array privilege validation
5. **Sensitive Columns**: Deny access to `secret*` columns for non-admin
6. **Admin Access**: Full access including sensitive data
7. **Time-based Control**: Business hours (9 AM - 5 PM) validation
8. **Error Handling**: External service unreachable scenarios

### 3. Test Runner (`run_tests.sh`)

Enhanced with external authorization support:

**New Command**:
- `external-full`: Run comprehensive external auth tests with service startup

## Usage Examples

### Basic External Auth Tests (No Service)
```bash
# Test plugin behavior without external service
./run_tests.sh external
```

### Comprehensive Tests with Auth Service
```bash
# Automatically start auth service and run full tests
./run_tests.sh external-full --verbose

# Or manually with test script
./test_authorization_plugin.sh --external-only --start-auth-service --verbose
```

### Docker Environment
```bash
# Run in Docker environment
./test_authorization_plugin.sh --docker --external-only --start-auth-service
```

### Manual Service Management
```bash
# Start auth service manually
cd docker/auth-service
PORT=8080 node server.js &

# Run tests (will detect running service)
./test_authorization_plugin.sh --external-only --verbose
```

## Test Scenarios

### 1. User Database Policies
- `testuser` → access to `testdb` ✅
- `admin` → access to all databases ✅  
- `unauthorizeduser` → denied access ❌

### 2. Privilege-based Access Control
Tests the new array format: `privileges: ["SELECT", "INSERT", "UPDATE"]`

- Validates individual privilege requests
- Denies operations based on user privilege policies
- Supports admin wildcard access (`"*"`)

### 3. Sensitive Data Protection
- Column names containing "secret" are protected
- Non-admin users denied access to sensitive columns
- Admin users have full access

### 4. Time-based Access Control
- Business hours: 9 AM - 5 PM allowed
- Outside hours: May deny or fall back to other policies
- Dynamic testing based on current time

### 5. Error Handling
- Network failures (service unreachable)
- Invalid response formats
- Timeout scenarios
- Graceful fallback to `ignore` mode

## Service Configuration

The auth service supports multiple policy types:

```javascript
const POLICIES = {
  // User-database mapping
  userDbPolicy: {
    'testuser': ['testdb'],
    'admin': ['testdb', 'otherdb', 'mysql']
  },
  
  // Time-based access (business hours)
  timePolicy: {
    allowHours: [9, 10, 11, 12, 13, 14, 15, 16, 17]
  },
  
  // Privilege-based control
  privilegePolicy: {
    deniedPrivileges: {
      'testuser': ['DROP', 'DELETE', 'CREATE USER', 'SUPER']
    },
    allowedPrivileges: {
      'testuser': ['SELECT', 'INSERT', 'UPDATE'],
      'admin': ['*']  // wildcard access
    }
  }
};
```

## Monitoring and Debugging

### Service Logs
```bash
# View auth service logs during tests
tail -f docker/auth-service/auth_service.log
```

### Debug Mode
```bash
# Enable debug output
./test_authorization_plugin.sh --external-only --start-auth-service --debug
```

### Manual API Testing
```bash
# Test auth service directly
curl -X POST http://localhost:8080/auth \
  -H "Content-Type: application/json" \
  -d '{
    "user": "testuser",
    "host": "localhost", 
    "database": "testdb",
    "privileges": ["SELECT", "INSERT"],
    "event_type": "db_access"
  }'
```

## Signal Handling

The test script includes graceful cleanup:
- `SIGINT` (Ctrl+C): Stops auth service and cleans up
- `SIGTERM`: Graceful termination
- `EXIT`: Cleanup on script exit

## Requirements

- **Node.js**: Required for auth service
- **curl**: For health checks and API testing  
- **lsof**: For port checking
- **MySQL**: With authorization plugin support

## Integration with CI/CD

The test framework is designed for automated testing:

```bash
# CI-friendly command (no interactive prompts)
./run_tests.sh external-full --verbose 2>&1 | tee test_results.log
```

Exit codes:
- `0`: All tests passed
- `1`: One or more tests failed
- Non-zero: Setup or configuration errors
