#!/usr/bin/env node

/**
 * MySQL External Authorization Service
 *
 * This service demonstrates how to implement an external authorization
 * server that works with the MySQL Authorization Plugin.
 *
 * API Endpoint: POST /auth
 * Request Body: JSON authorization request with privileges as array
 * Response: JSON with authorization decision
 *
 * Expected Request Format:
 * {
 *   "user": "username",
 *   "host": "hostname", 
 *   "database": "dbname",
 *   "table": "tablename",
 *   "column": "columnname",
 *   "routine": "routinename",
 *   "privileges": ["SELECT", "INSERT", "UPDATE"], // Array of privilege names
 *   "event_type": "db_access|table_access|column_access|routine_access",
 *   "sql_command": "SELECT",
 *   "query": "SELECT * FROM table1",
 *   "is_procedure": false
 * }
 */

const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');

const app = express();
const PORT = process.env.PORT || 8080;

// Request counter for debugging
let requestCounter = 0;

// Middleware
app.use(cors());
app.use(bodyParser.json());

// Request logging middleware
app.use((req, res, next) => {
  requestCounter++;
  const timestamp = new Date().toISOString();
  console.log(`[${timestamp}] HTTP ${req.method} ${req.url} - Request #${requestCounter}`);
  if (req.method === 'POST' && req.url === '/auth') {
    console.log(`[${timestamp}] Content-Type: ${req.headers['content-type']}`);
    console.log(`[${timestamp}] Content-Length: ${req.headers['content-length']}`);
    console.log(`[${timestamp}] User-Agent: ${req.headers['user-agent']}`);
  }
  next();
});

// Authorization policies
const POLICIES = {
  // Grant access to specific users on specific databases
  userDbPolicy: {
    'testuser': ['testdb'],
    'admin': ['testdb', 'otherdb', 'mysql']
  },

  // Time-based access control (example)
  timePolicy: {
    allowHours: [9, 10, 11, 12, 13, 14, 15, 16, 17], // 9 AM to 5 PM
  },

  // IP-based access control (example)
  ipPolicy: {
    allowedIPs: ['127.0.0.1', 'localhost', '172.16.0.0/12']
  },

  // Privilege-based access control
  privilegePolicy: {
    // Deny sensitive operations for certain users
    deniedPrivileges: {
      'testuser': ['DROP', 'DELETE', 'CREATE USER', 'SUPER'],
      'readonly_user': ['INSERT', 'UPDATE', 'DELETE', 'DROP', 'CREATE', 'ALTER']
    },
    // Allow specific privileges for users
    allowedPrivileges: {
      'testuser': ['SELECT', 'INSERT', 'UPDATE'],
      'admin': ['*'] // admin can do everything
    }
  }
};

// Enhanced logging functions
function logRequest(req, result, reason) {
  const timestamp = new Date().toISOString();
  console.log(`[${timestamp}] AUTH ${result.toUpperCase()}: ${req.body.user}@${req.body.host} -> ${req.body.database}.${req.body.table} (${reason})`);
}

function logDebug(message, data = null) {
  const timestamp = new Date().toISOString();
  console.log(`[${timestamp}] DEBUG: ${message}`);
  if (data) {
    console.log(`[${timestamp}] DEBUG DATA:`, JSON.stringify(data, null, 2));
  }
}

function logPolicyCheck(policyName, input, result) {
  const timestamp = new Date().toISOString();
  console.log(`[${timestamp}] POLICY CHECK [${policyName}]:`, 
    `Input: ${JSON.stringify(input)} -> Result: ${result.result} (${result.reason})`);
}

// Check user-database policy
function checkUserDatabasePolicy(user, database) {
  const input = { user, database };
  const allowedDatabases = POLICIES.userDbPolicy[user];
  
  logDebug(`Checking user-database policy for ${user} on ${database}`, {
    user,
    database,
    allowedDatabases,
    allPolicyUsers: Object.keys(POLICIES.userDbPolicy)
  });
  
  if (!allowedDatabases) {
    const result = { result: 'ignore', reason: 'user not in policy' };
    logPolicyCheck('USER_DB', input, result);
    return result;
  }

  if (allowedDatabases.includes(database) || allowedDatabases.includes('*')) {
    const result = { result: 'grant', reason: 'user has access to database' };
    logPolicyCheck('USER_DB', input, result);
    return result;
  }

  const result = { result: 'deny', reason: 'user not allowed on database' };
  logPolicyCheck('USER_DB', input, result);
  return result;
}

// Check time-based policy
function checkTimePolicy() {
  const now = new Date();
  const currentHour = now.getHours();

  if (POLICIES.timePolicy.allowHours.includes(currentHour)) {
    return { result: 'grant', reason: 'access allowed during business hours' };
  }

  return { result: 'deny', reason: 'access denied outside business hours' };
}

// Check IP-based policy
function checkIPPolicy(ip) {
  // Simple IP check (in production, use proper IP range checking)
  if (POLICIES.ipPolicy.allowedIPs.includes(ip) || ip.startsWith('172.')) {
    return { result: 'grant', reason: 'IP address allowed' };
  }

  return { result: 'ignore', reason: 'IP policy check skipped' };
}

// Check privilege-based policy
function checkPrivilegePolicy(user, privileges, mode = 'all') {
  const input = { user, privileges, mode };
  
  logDebug(`Checking privilege policy for ${user}`, {
    user,
    privileges,
    privilegesType: typeof privileges,
    isArray: Array.isArray(privileges),
    deniedPrivileges: POLICIES.privilegePolicy.deniedPrivileges[user],
    allowedPrivileges: POLICIES.privilegePolicy.allowedPrivileges[user]
  });
  
  if (mode === 'presence') {
    const result = { result: 'ignore', reason: 'presence probe; defer to user/db policy' };
    logPolicyCheck('PRIVILEGE', input, result);
    return result;
  }

  if (!Array.isArray(privileges)) {
    const result = { result: 'ignore', reason: 'privileges not provided as array' };
    logPolicyCheck('PRIVILEGE', input, result);
    return result;
  }

  // Check denied privileges first
  const deniedPrivs = POLICIES.privilegePolicy.deniedPrivileges[user];
  if (deniedPrivs && mode === 'all') {
    logDebug(`Checking denied privileges for ${user}`, { deniedPrivs, requestedPrivs: privileges });
    for (const privilege of privileges) {
      if (deniedPrivs.includes(privilege)) {
        const result = { result: 'deny', reason: `denied privilege: ${privilege}` };
        logPolicyCheck('PRIVILEGE', input, result);
        return result;
      }
    }
  }

  // Check allowed privileges
  const allowedPrivs = POLICIES.privilegePolicy.allowedPrivileges[user];
  if (allowedPrivs) {
    logDebug(`Checking allowed privileges for ${user}`, { allowedPrivs, requestedPrivs: privileges });
    
    // Admin wildcard check
    if (allowedPrivs.includes('*')) {
      const result = { result: 'grant', reason: 'admin has all privileges' };
      logPolicyCheck('PRIVILEGE', input, result);
      return result;
    }
    
    if (mode === 'all') {
      for (const privilege of privileges) {
        if (!allowedPrivs.includes(privilege)) {
          const result = { result: 'deny', reason: `privilege not allowed: ${privilege}` };
          logPolicyCheck('PRIVILEGE', input, result);
          return result;
        }
      }
      const result = { result: 'grant', reason: `all privileges allowed: [${privileges.join(', ')}]` };
      logPolicyCheck('PRIVILEGE', input, result);
      return result;
    } else if (mode === 'any') {
      const anyAllowed = privileges.some(p => allowedPrivs.includes(p));
      if (anyAllowed) {
        const result = { result: 'grant', reason: 'at least one requested privilege allowed' };
        logPolicyCheck('PRIVILEGE', input, result);
        return result;
      }
      if (deniedPrivs && privileges.every(p => deniedPrivs.includes(p))) {
        const result = { result: 'deny', reason: 'all requested privileges denied' };
        logPolicyCheck('PRIVILEGE', input, result);
        return result;
      }
      const result = { result: 'ignore', reason: 'no allowed privileges matched' };
      logPolicyCheck('PRIVILEGE', input, result);
      return result;
    }
  }

  const result = { result: 'ignore', reason: 'user not in privilege policy' };
  logPolicyCheck('PRIVILEGE', input, result);
  return result;
}

// Main authorization endpoint
app.post('/auth', (req, res) => {
  const requestStart = Date.now();
  const requestId = Math.random().toString(36).substr(2, 9);
  
  try {
    logDebug(`=== NEW AUTHORIZATION REQUEST [${requestId}] ===`);
    logDebug(`Full request body:`, req.body);
    logDebug(`Request headers:`, req.headers);
    logDebug(`Request method: ${req.method}, URL: ${req.url}`);
    
    const {
      user,
      host,
      database,
      table,
      column,
      routine,
      privileges,
      event_type,
      sql_command,
      query,
      is_procedure,
      requirement_mode,
      missing_privileges
    } = req.body;

    console.log(`\n=== Authorization Request [${requestId}] ===`);
    console.log(`User: ${user}@${host}`);
    console.log(`Database: ${database}, Table: ${table}, Column: ${column}`);
    console.log(`Privileges: ${Array.isArray(privileges) ? `[${privileges.join(', ')}]` : privileges}, Event: ${event_type}`);
    console.log(`SQL Command: ${sql_command}`);
    console.log(`Query: ${query}`);
    
    // Validate required fields
    logDebug(`Request validation:`, {
      hasUser: !!user,
      hasHost: !!host,
      hasDatabase: !!database,
      hasEventType: !!event_type,
      hasPrivileges: !!privileges,
      privilegesType: typeof privileges,
      privilegesLength: Array.isArray(privileges) ? privileges.length : 'not array'
    });

    let decision = { result: 'ignore', reason: 'no policy matched' };

    logDebug(`Starting policy evaluation for event type: ${event_type}`);

    // Apply policies based on event type
    switch (event_type) {
      case 'db_access':
        logDebug(`Evaluating db_access policies`);
        {
          const mode = requirement_mode || 'unspecified';
          if (mode === 'presence') {
            decision = checkUserDatabasePolicy(user, database);
            logDebug(`After user-database policy: ${decision.result} - ${decision.reason}`);
            if (decision.result === 'ignore') {
              decision = checkTimePolicy();
              logDebug(`After time policy: ${decision.result} - ${decision.reason}`);
            }
          } else if (mode === 'any') {
            const privs = Array.isArray(privileges) ? privileges : [];
            decision = checkPrivilegePolicy(user, privs, 'any');
            logDebug(`After privilege policy(any): ${decision.result} - ${decision.reason}`);
            if (decision.result === 'ignore') {
              decision = checkUserDatabasePolicy(user, database);
              logDebug(`After user-database policy: ${decision.result} - ${decision.reason}`);
            }
          } else {
            const privs = Array.isArray(privileges) ? privileges : [];
            decision = checkPrivilegePolicy(user, privs, 'all');
            logDebug(`After privilege policy(all): ${decision.result} - ${decision.reason}`);
            if (decision.result === 'ignore') {
              decision = checkUserDatabasePolicy(user, database);
              logDebug(`After user-database policy: ${decision.result} - ${decision.reason}`);
              if (decision.result === 'ignore') {
                decision = checkTimePolicy();
                logDebug(`After time policy: ${decision.result} - ${decision.reason}`);
              }
            }
          }
        }
        break;

      case 'table_access':
        logDebug(`Evaluating table_access policies`);
        // For table access, check missing privileges with ALL semantics
        {
          const privs = Array.isArray(missing_privileges) && missing_privileges.length > 0
            ? missing_privileges
            : (Array.isArray(privileges) ? privileges : []);
          decision = checkPrivilegePolicy(user, privs, 'all');
        }
        logDebug(`After privilege policy: ${decision.result} - ${decision.reason}`);
        
        if (decision.result === 'ignore') {
          // Fall back to user-database policy
          decision = checkUserDatabasePolicy(user, database);
          logDebug(`After user-database policy: ${decision.result} - ${decision.reason}`);
          
          if (decision.result === 'grant') {
            // Additional table-specific logic can go here
            decision.reason += ` (table: ${table})`;
            logDebug(`Updated reason for table access: ${decision.reason}`);
          }
        }
        break;

      case 'column_access':
        logDebug(`Evaluating column_access policies for column: ${column}`);
        // Column-level access control - use missing_privileges if present
        {
          const privs = Array.isArray(missing_privileges) && missing_privileges.length > 0
            ? missing_privileges
            : (Array.isArray(privileges) ? privileges : []);
          decision = checkPrivilegePolicy(user, privs, 'all');
        }
        logDebug(`After privilege policy: ${decision.result} - ${decision.reason}`);
        
        if (decision.result === 'ignore') {
          // Fall back to user-database policy
          decision = checkUserDatabasePolicy(user, database);
          logDebug(`After user-database policy: ${decision.result} - ${decision.reason}`);
        }
        
        if (decision.result === 'grant' && column) {
          // Deny access to sensitive columns for non-admin users
          logDebug(`Checking sensitive column access`, { 
            column, 
            columnLower: column.toLowerCase(), 
            includesSecret: column.toLowerCase().includes('secret'),
            user,
            isAdmin: user === 'admin'
          });
          
          if (column.toLowerCase().includes('secret') && user !== 'admin') {
            decision = { result: 'deny', reason: 'access denied to sensitive column' };
            logDebug(`Denied access to sensitive column: ${column}`);
          }
        }
        break;

      case 'routine_access':
        logDebug(`Evaluating routine_access policies`);
        if (requirement_mode === 'presence') {
          decision = checkUserDatabasePolicy(user, database);
        } else {
          const privs = Array.isArray(privileges) ? privileges : [];
          decision = checkPrivilegePolicy(user, privs, 'all');
        }
        logDebug(`After privilege policy: ${decision.result} - ${decision.reason}`);
        
        if (decision.result === 'ignore') {
          // Fall back to user-database policy
          decision = checkUserDatabasePolicy(user, database);
          logDebug(`After user-database policy: ${decision.result} - ${decision.reason}`);
        }
        break;

      default:
        logDebug(`Unknown event type: ${event_type}`);
        decision = { result: 'ignore', reason: 'unknown event type' };
    }

    logDebug(`Final decision for ${requestId}: ${decision.result} - ${decision.reason}`);

    // Log the decision
    logRequest(req, decision.result, decision.reason);

    const responseBody = {
      result: decision.result,
      reason: decision.reason,
      timestamp: new Date().toISOString(),
      requestId: requestId,
      processingTime: Date.now() - requestStart
    };

    logDebug(`Sending response for ${requestId}:`, responseBody);

    // Send response
    res.json(responseBody);

    logDebug(`Request ${requestId} completed in ${Date.now() - requestStart}ms`);

  } catch (error) {
    const errorInfo = {
      message: error.message,
      stack: error.stack,
      requestId: requestId,
      processingTime: Date.now() - requestStart
    };
    
    console.error(`Authorization error for request ${requestId}:`, errorInfo);
    logDebug(`Error details for ${requestId}:`, errorInfo);
    
    res.status(500).json({
      result: 'ignore',
      reason: 'internal server error',
      error: error.message,
      requestId: requestId,
      timestamp: new Date().toISOString()
    });
  }
});

// Health check endpoint
app.get('/health', (req, res) => {
  res.json({
    status: 'healthy',
    service: 'MySQL External Authorization Service',
    version: '1.0.0',
    timestamp: new Date().toISOString()
  });
});

// Policies endpoint
app.get('/policies', (req, res) => {
  res.json({
    policies: POLICIES,
    description: 'Current authorization policies',
    timestamp: new Date().toISOString()
  });
});

// Root endpoint
app.get('/', (req, res) => {
  res.json({
    service: 'MySQL External Authorization Service',
    description: 'External authorization service for MySQL Authorization Plugin',
    endpoints: {
      'POST /auth': 'Main authorization endpoint',
      'GET /health': 'Health check',
      'GET /policies': 'View current policies',
      'GET /': 'This information'
    },
    version: '1.1.0',
    features: [
      'Array-based privilege handling',
      'Multi-level authorization policies',
      'Privilege-based access control',
      'Sensitive column protection',
      'Time-based access control',
      'IP-based access control'
    ],
    requestFormat: {
      user: 'username',
      host: 'hostname',
      database: 'dbname',
      table: 'tablename',
      column: 'columnname',
      routine: 'routinename',
      privileges: ['SELECT', 'INSERT', 'UPDATE'], // Array format
      event_type: 'db_access|table_access|column_access|routine_access',
      sql_command: 'SELECT',
      query: 'SELECT * FROM table1',
      is_procedure: false
    }
  });
});

// Error handling middleware
app.use((error, req, res, next) => {
  console.error('Unhandled error:', error);
  res.status(500).json({
    result: 'ignore',
    reason: 'unhandled error',
    error: error.message
  });
});

// Debug endpoint
app.get('/debug', (req, res) => {
  res.json({
    service: 'MySQL External Authorization Service - Debug Info',
    timestamp: new Date().toISOString(),
    requestCounter: requestCounter,
    policies: POLICIES,
    currentHour: new Date().getHours(),
    environment: {
      nodeVersion: process.version,
      platform: process.platform,
      port: PORT,
      pid: process.pid,
      memory: process.memoryUsage()
    },
    recentLogs: 'Check console output for detailed request logs'
  });
});

// Start server
app.listen(PORT, () => {
  const timestamp = new Date().toISOString();
  console.log(`\n[${timestamp}] 🚀 MySQL External Authorization Service - ENHANCED DEBUG MODE`);
  console.log(`[${timestamp}] ==========================================`);
  console.log(`[${timestamp}] Server starting on port ${PORT}`);
  console.log(`[${timestamp}] Node.js version: ${process.version}`);
  console.log(`[${timestamp}] Platform: ${process.platform}`);
  console.log(`[${timestamp}] Process ID: ${process.pid}`);
  console.log(`[${timestamp}] `);
  console.log(`[${timestamp}] 🔍 DEBUG ENDPOINTS:`);
  console.log(`[${timestamp}] - Health check: http://localhost:${PORT}/health`);
  console.log(`[${timestamp}] - API documentation: http://localhost:${PORT}/`);
  console.log(`[${timestamp}] - Current policies: http://localhost:${PORT}/policies`);
  console.log(`[${timestamp}] - Debug info: http://localhost:${PORT}/debug`);
  console.log(`[${timestamp}] `);
  console.log(`[${timestamp}] 📋 AUTHORIZATION POLICIES:`);
  console.log(`[${timestamp}] - Users with DB access: ${Object.keys(POLICIES.userDbPolicy).join(', ')}`);
  console.log(`[${timestamp}] - Business hours: ${POLICIES.timePolicy.allowHours.join(', ')}`);
  console.log(`[${timestamp}] - Allowed IP ranges: ${POLICIES.ipPolicy.allowedIPs.join(', ')}`);
  console.log(`[${timestamp}] - Privilege-based users: ${Object.keys(POLICIES.privilegePolicy.allowedPrivileges).join(', ')}`);
  console.log(`[${timestamp}] - Users with denied privileges: ${Object.keys(POLICIES.privilegePolicy.deniedPrivileges).join(', ')}`);
  console.log(`[${timestamp}] `);
  console.log(`[${timestamp}] 🎯 DEBUG FEATURES ENABLED:`);
  console.log(`[${timestamp}] - ✅ Enhanced request logging with unique IDs`);
  console.log(`[${timestamp}] - ✅ Full request/response body logging`);
  console.log(`[${timestamp}] - ✅ Policy decision flow tracing`);
  console.log(`[${timestamp}] - ✅ Request timing and performance metrics`);
  console.log(`[${timestamp}] - ✅ Error stack traces and detailed error info`);
  console.log(`[${timestamp}] - ✅ Array-based privilege handling (MySQL plugin v1.1)`);
  console.log(`[${timestamp}] - ✅ Multi-level authorization policies (privilege -> user/db -> time)`);
  console.log(`[${timestamp}] - ✅ Sensitive column protection`);
  console.log(`[${timestamp}] `);
  console.log(`[${timestamp}] 🚀 Ready to receive authorization requests from MySQL!`);
  console.log(`[${timestamp}] 📝 All requests will be logged in detail for debugging.`);
  console.log(`[${timestamp}] `);
});

// Graceful shutdown
process.on('SIGTERM', () => {
  console.log('Shutting down authorization service...');
  process.exit(0);
});

process.on('SIGINT', () => {
  console.log('Shutting down authorization service...');
  process.exit(0);
});
