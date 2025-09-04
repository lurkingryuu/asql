#!/usr/bin/env node

/**
 * MySQL External Authorization Service
 *
 * This service demonstrates how to implement an external authorization
 * server that works with the MySQL Authorization Plugin.
 *
 * API Endpoint: POST /auth
 * Request Body: JSON authorization request
 * Response: JSON with authorization decision
 */

const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');

const app = express();
const PORT = process.env.PORT || 8080;

// Middleware
app.use(cors());
app.use(bodyParser.json());

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
  }
};

// Logging function
function logRequest(req, result, reason) {
  const timestamp = new Date().toISOString();
  console.log(`[${timestamp}] AUTH ${result.toUpperCase()}: ${req.body.user}@${req.body.host} -> ${req.body.database}.${req.body.table} (${reason})`);
}

// Check user-database policy
function checkUserDatabasePolicy(user, database) {
  const allowedDatabases = POLICIES.userDbPolicy[user];
  if (!allowedDatabases) {
    return { result: 'ignore', reason: 'user not in policy' };
  }

  if (allowedDatabases.includes(database) || allowedDatabases.includes('*')) {
    return { result: 'grant', reason: 'user has access to database' };
  }

  return { result: 'deny', reason: 'user not allowed on database' };
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

// Main authorization endpoint
app.post('/auth', (req, res) => {
  try {
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
      is_procedure
    } = req.body;

    console.log(`\n=== Authorization Request ===`);
    console.log(`User: ${user}@${host}`);
    console.log(`Database: ${database}, Table: ${table}, Column: ${column}`);
    console.log(`Privileges: ${privileges}, Event: ${event_type}`);
    console.log(`SQL Command: ${sql_command}`);
    console.log(`Query: ${query}`);

    let decision = { result: 'ignore', reason: 'no policy matched' };

    // Apply policies based on event type
    switch (event_type) {
      case 'db_access':
        // Check user-database policy first
        decision = checkUserDatabasePolicy(user, database);
        if (decision.result === 'ignore') {
          // Fall back to time-based policy
          decision = checkTimePolicy();
        }
        break;

      case 'table_access':
        // For table access, check user permissions
        decision = checkUserDatabasePolicy(user, database);
        if (decision.result === 'grant') {
          // Additional table-specific logic can go here
          decision.reason += ` (table: ${table})`;
        }
        break;

      case 'column_access':
        // Column-level access control
        decision = checkUserDatabasePolicy(user, database);
        if (decision.result === 'grant' && column) {
          // Deny access to sensitive columns for non-admin users
          if (column.toLowerCase().includes('secret') && user !== 'admin') {
            decision = { result: 'deny', reason: 'access denied to sensitive column' };
          }
        }
        break;

      case 'routine_access':
        // Routine access control
        decision = checkUserDatabasePolicy(user, database);
        break;

      default:
        decision = { result: 'ignore', reason: 'unknown event type' };
    }

    // Log the decision
    logRequest(req, decision.result, decision.reason);

    // Send response
    res.json({
      result: decision.result,
      reason: decision.reason,
      timestamp: new Date().toISOString()
    });

  } catch (error) {
    console.error('Authorization error:', error);
    res.status(500).json({
      result: 'ignore',
      reason: 'internal server error',
      error: error.message
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
    version: '1.0.0'
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

// Start server
app.listen(PORT, () => {
  console.log(`\n🚀 MySQL External Authorization Service`);
  console.log(`=====================================`);
  console.log(`Server running on port ${PORT}`);
  console.log(`Health check: http://localhost:${PORT}/health`);
  console.log(`API documentation: http://localhost:${PORT}/`);
  console.log(`Current policies: http://localhost:${PORT}/policies`);
  console.log(`\nAuthorization Policies:`);
  console.log(`- Users with DB access: ${Object.keys(POLICIES.userDbPolicy).join(', ')}`);
  console.log(`- Business hours: ${POLICIES.timePolicy.allowHours.join(', ')}`);
  console.log(`- Allowed IP ranges: ${POLICIES.ipPolicy.allowedIPs.join(', ')}`);
  console.log(`\nReady to receive authorization requests from MySQL!`);
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
