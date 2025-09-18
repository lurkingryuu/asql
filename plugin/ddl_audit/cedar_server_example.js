#!/usr/bin/env node

/**
 * Example Cedar Server for DDL Audit Plugin
 * 
 * This is a simple Node.js server that receives DDL audit data from the MySQL
 * DDL Audit Plugin and processes it to maintain Cedar entities and policies.
 * 
 * Usage:
 *   npm install express
 *   node cedar_server_example.js
 * 
 * The server will listen on port 8180 and provide an endpoint at /v1/ddl_audit
 * to receive DDL audit data from the MySQL plugin.
 */

const express = require('express');
const app = express();
const port = 8180;

// Middleware to parse JSON
app.use(express.json());

// In-memory storage for entities (in production, use a proper database)
const entities = {
  users: new Set(),
  databases: new Set(),
  tables: new Set(),
  columns: new Set(),
  views: new Set(),
  indices: new Set(),
  triggers: new Set(),
  procedures: new Set(),
  functions: new Set()
};

// Helper function to log with timestamp
function log(message) {
  const timestamp = new Date().toISOString();
  console.log(`[${timestamp}] ${message}`);
}

// Helper function to extract table name from CREATE TABLE statement
function extractTableInfo(query) {
  const createTableMatch = query.match(/CREATE\s+TABLE\s+(?:IF\s+NOT\s+EXISTS\s+)?`?([^`\s(]+)`?\s*\(/i);
  if (createTableMatch) {
    return {
      tableName: createTableMatch[1],
      columns: extractColumns(query)
    };
  }
  return null;
}

// Helper function to extract column information from CREATE TABLE statement
function extractColumns(query) {
  const columns = [];
  
  // Find the part between parentheses
  const tableDefMatch = query.match(/\(\s*(.*)\s*\)(?:\s*ENGINE|$)/is);
  if (!tableDefMatch) {
    return columns;
  }
  
  const tableDefinition = tableDefMatch[1];
  
  // Split by comma, but be careful about nested parentheses and functions
  const columnDefs = [];
  let currentDef = '';
  let parenLevel = 0;
  let inQuotes = false;
  let quoteChar = '';
  
  for (let i = 0; i < tableDefinition.length; i++) {
    const char = tableDefinition[i];
    
    if (!inQuotes && (char === '"' || char === "'" || char === '`')) {
      inQuotes = true;
      quoteChar = char;
      currentDef += char;
    } else if (inQuotes && char === quoteChar) {
      inQuotes = false;
      quoteChar = '';
      currentDef += char;
    } else if (!inQuotes && char === '(') {
      parenLevel++;
      currentDef += char;
    } else if (!inQuotes && char === ')') {
      parenLevel--;
      currentDef += char;
    } else if (!inQuotes && char === ',' && parenLevel === 0) {
      columnDefs.push(currentDef.trim());
      currentDef = '';
    } else {
      currentDef += char;
    }
  }
  
  if (currentDef.trim()) {
    columnDefs.push(currentDef.trim());
  }
  
  // Parse each column definition
  columnDefs.forEach(def => {
    def = def.trim();
    
    // Skip constraints and keys
    if (def.match(/^\s*(PRIMARY\s+KEY|FOREIGN\s+KEY|KEY|INDEX|UNIQUE|CONSTRAINT)/i)) {
      return;
    }
    
    // Extract column name and type
    const columnMatch = def.match(/^`?([^`\s]+)`?\s+([^\s(]+)(?:\([^)]*\))?/i);
    if (columnMatch) {
      columns.push({
        name: columnMatch[1],
        type: columnMatch[2].toUpperCase()
      });
    }
  });
  
  return columns;
}

// Helper function to create Cedar entity
function createCedarEntity(type, id, attributes = {}) {
  return {
    type: type,
    id: id,
    attributes: attributes
  };
}

// Main DDL audit endpoint
app.post('/v1/ddl_audit', (req, res) => {
  try {
    const ddlData = req.body;
    
    log(`Received DDL audit: ${ddlData.ddl_type} - ${ddlData.query.substring(0, 100)}...`);
    
    // Process based on DDL type
    switch (ddlData.ddl_type) {
      case 'CREATE_TABLE':
        handleCreateTable(ddlData);
        break;
        
      case 'ALTER_TABLE':
        handleAlterTable(ddlData);
        break;
        
      case 'DROP_TABLE':
        handleDropTable(ddlData);
        break;
        
      case 'CREATE_DATABASE':
        handleCreateDatabase(ddlData);
        break;
        
      case 'DROP_DATABASE':
        handleDropDatabase(ddlData);
        break;
        
      case 'CREATE_USER':
        handleCreateUser(ddlData);
        break;
        
      case 'DROP_USER':
        handleDropUser(ddlData);
        break;
        
      case 'CREATE_INDEX':
        handleCreateIndex(ddlData);
        break;
        
      case 'DROP_INDEX':
        handleDropIndex(ddlData);
        break;
        
      case 'CREATE_VIEW':
        handleCreateView(ddlData);
        break;
        
      case 'DROP_VIEW':
        handleDropView(ddlData);
        break;
        
      case 'CREATE_TRIGGER':
        handleCreateTrigger(ddlData);
        break;
        
      case 'DROP_TRIGGER':
        handleDropTrigger(ddlData);
        break;
        
      case 'CREATE_PROCEDURE':
        handleCreateProcedure(ddlData);
        break;
        
      case 'DROP_PROCEDURE':
        handleDropProcedure(ddlData);
        break;
        
      case 'CREATE_FUNCTION':
        handleCreateFunction(ddlData);
        break;
        
      case 'DROP_FUNCTION':
        handleDropFunction(ddlData);
        break;
        
      case 'RENAME_TABLE':
        handleRenameTable(ddlData);
        break;
        
      case 'ALTER_DATABASE':
        handleAlterDatabase(ddlData);
        break;
        
      case 'RENAME_USER':
        handleRenameUser(ddlData);
        break;
        
      case 'ALTER_USER':
        handleAlterUser(ddlData);
        break;
        
      case 'ALTER_FUNCTION':
        handleAlterFunction(ddlData);
        break;
        
      case 'ALTER_PROCEDURE':
        handleAlterProcedure(ddlData);
        break;
        
      case 'CREATE_EVENT':
        handleCreateEvent(ddlData);
        break;
        
      case 'ALTER_EVENT':
        handleAlterEvent(ddlData);
        break;
        
      case 'DROP_EVENT':
        handleDropEvent(ddlData);
        break;
        
      case 'CREATE_SERVER':
        handleCreateServer(ddlData);
        break;
        
      case 'DROP_SERVER':
        handleDropServer(ddlData);
        break;
        
      case 'ALTER_SERVER':
        handleAlterServer(ddlData);
        break;
        
      case 'CREATE_ROLE':
        handleCreateRole(ddlData);
        break;
        
      case 'DROP_ROLE':
        handleDropRole(ddlData);
        break;
        
      case 'ALTER_TABLESPACE':
        handleAlterTablespace(ddlData);
        break;
        
      case 'CREATE_RESOURCE_GROUP':
        handleCreateResourceGroup(ddlData);
        break;
        
      case 'ALTER_RESOURCE_GROUP':
        handleAlterResourceGroup(ddlData);
        break;
        
      case 'DROP_RESOURCE_GROUP':
        handleDropResourceGroup(ddlData);
        break;
        
      case 'CREATE_SRS':
        handleCreateSRS(ddlData);
        break;
        
      case 'DROP_SRS':
        handleDropSRS(ddlData);
        break;
        
      default:
        log(`Unhandled DDL type: ${ddlData.ddl_type}`);
    }
    
    res.status(200).json({ 
      status: 'success',
      message: `Processed ${ddlData.ddl_type} successfully`
    });
    
  } catch (error) {
    log(`Error processing DDL audit: ${error.message}`);
    res.status(500).json({ 
      status: 'error',
      message: error.message
    });
  }
});

// Handle CREATE TABLE
function handleCreateTable(ddlData) {
  const tableInfo = extractTableInfo(ddlData.query);
  
  if (tableInfo) {
    // Add database entity
    if (ddlData.database) {
      entities.databases.add(ddlData.database);
      log(`Added database entity: ${ddlData.database}`);
    }
    
    // Add table entity
    const tableId = `${ddlData.database}.${tableInfo.tableName}`;
    entities.tables.add(tableId);
    log(`Added table entity: ${tableId}`);
    
    // Add column entities
    tableInfo.columns.forEach(column => {
      const columnId = `${tableId}.${column.name}`;
      entities.columns.add(columnId);
      log(`Added column entity: ${columnId} (${column.type})`);
    });
    
    // In a real implementation, you would:
    // 1. Create Cedar entities using the Cedar SDK
    // 2. Update policies if needed
    // 3. Store in a persistent database
  }
}

// Handle ALTER TABLE
function handleAlterTable(ddlData) {
  const tableId = `${ddlData.database}.${ddlData.table}`;
  log(`Processing ALTER TABLE on: ${tableId}`);
  
  // Basic ALTER TABLE processing
  const query = ddlData.query.toUpperCase();
  
  if (query.includes('ADD COLUMN')) {
    log(`Adding column to table: ${tableId}`);
    // In production: parse the new column and add to entities
  } else if (query.includes('DROP COLUMN')) {
    log(`Dropping column from table: ${tableId}`);
    // In production: remove column from entities
  } else if (query.includes('MODIFY COLUMN') || query.includes('CHANGE COLUMN')) {
    log(`Modifying column in table: ${tableId}`);
    // In production: update column definition
  } else {
    log(`Other ALTER TABLE operation on: ${tableId}`);
  }
}

// Handle DROP TABLE
function handleDropTable(ddlData) {
  const tableId = `${ddlData.database}.${ddlData.table}`;
  
  // Remove table and its columns
  entities.tables.delete(tableId);
  
  // Remove all columns for this table
  for (const columnId of entities.columns) {
    if (columnId.startsWith(tableId + '.')) {
      entities.columns.delete(columnId);
    }
  }
  
  log(`Removed table entity: ${tableId}`);
}

// Handle CREATE DATABASE
function handleCreateDatabase(ddlData) {
  if (ddlData.database) {
    entities.databases.add(ddlData.database);
    log(`Added database entity: ${ddlData.database}`);
  }
}

// Handle DROP DATABASE
function handleDropDatabase(ddlData) {
  if (ddlData.database) {
    entities.databases.delete(ddlData.database);
    
    // Remove all tables and columns in this database
    for (const tableId of entities.tables) {
      if (tableId.startsWith(ddlData.database + '.')) {
        entities.tables.delete(tableId);
      }
    }
    
    for (const columnId of entities.columns) {
      if (columnId.startsWith(ddlData.database + '.')) {
        entities.columns.delete(columnId);
      }
    }
    
    log(`Removed database entity: ${ddlData.database}`);
  }
}

// Handle CREATE USER
function handleCreateUser(ddlData) {
  const userId = ddlData.user;
  if (userId && userId !== 'unknown') {
    entities.users.add(userId);
    log(`Added user entity: ${userId}`);
  }
}

// Handle DROP USER
function handleDropUser(ddlData) {
  const userId = ddlData.user;
  if (userId && userId !== 'unknown') {
    entities.users.delete(userId);
    log(`Removed user entity: ${userId}`);
  }
}

// Handle CREATE INDEX
function handleCreateIndex(ddlData) {
  const tableId = `${ddlData.database}.${ddlData.table}`;
  log(`Created index on table: ${tableId}`);
  
  // In production: track index entities and their policies
}

// Handle DROP INDEX
function handleDropIndex(ddlData) {
  const tableId = `${ddlData.database}.${ddlData.table}`;
  log(`Dropped index from table: ${tableId}`);
  
  // In production: remove index entities
}

// Handle CREATE VIEW
function handleCreateView(ddlData) {
  const viewMatch = ddlData.query.match(/CREATE\s+VIEW\s+`?([^`\s]+)`?/i);
  if (viewMatch) {
    const viewId = `${ddlData.database}.${viewMatch[1]}`;
    entities.views.add(viewId);
    log(`Created view entity: ${viewId}`);
  }
}

// Handle DROP VIEW
function handleDropView(ddlData) {
  const viewMatch = ddlData.query.match(/DROP\s+VIEW\s+(?:IF\s+EXISTS\s+)?`?([^`\s]+)`?/i);
  if (viewMatch) {
    const viewId = `${ddlData.database}.${viewMatch[1]}`;
    entities.views.delete(viewId);
    log(`Removed view entity: ${viewId}`);
  }
}

// Handle CREATE TRIGGER
function handleCreateTrigger(ddlData) {
  const triggerMatch = ddlData.query.match(/CREATE\s+TRIGGER\s+`?([^`\s]+)`?\s+(?:BEFORE|AFTER)\s+(?:INSERT|UPDATE|DELETE)\s+ON\s+`?([^`\s]+)`?/i);
  if (triggerMatch) {
    const triggerName = triggerMatch[1];
    const tableName = triggerMatch[2];
    const triggerId = `${ddlData.database}.${triggerName}`;
    const tableId = `${ddlData.database}.${tableName}`;
    entities.triggers.add(triggerId);
    log(`Created trigger '${triggerName}' on table: ${tableId}`);
  }
}

// Handle DROP TRIGGER
function handleDropTrigger(ddlData) {
  const triggerMatch = ddlData.query.match(/DROP\s+TRIGGER\s+(?:IF\s+EXISTS\s+)?`?([^`\s]+)`?/i);
  if (triggerMatch) {
    const triggerName = triggerMatch[1];
    const triggerId = `${ddlData.database}.${triggerName}`;
    entities.triggers.delete(triggerId);
    log(`Dropped trigger: ${triggerId}`);
  }
}

// Handle CREATE PROCEDURE
function handleCreateProcedure(ddlData) {
  const procMatch = ddlData.query.match(/CREATE\s+PROCEDURE\s+`?([^`\s(]+)`?\s*\(/i);
  if (procMatch) {
    const procName = procMatch[1];
    const procId = `${ddlData.database}.${procName}`;
    entities.procedures.add(procId);
    log(`Created procedure: ${procId}`);
  }
}

// Handle DROP PROCEDURE
function handleDropProcedure(ddlData) {
  const procMatch = ddlData.query.match(/DROP\s+PROCEDURE\s+(?:IF\s+EXISTS\s+)?`?([^`\s]+)`?/i);
  if (procMatch) {
    const procName = procMatch[1];
    const procId = `${ddlData.database}.${procName}`;
    entities.procedures.delete(procId);
    log(`Dropped procedure: ${procId}`);
  }
}

// Handle CREATE FUNCTION
function handleCreateFunction(ddlData) {
  const funcMatch = ddlData.query.match(/CREATE\s+FUNCTION\s+`?([^`\s(]+)`?\s*\(/i);
  if (funcMatch) {
    const funcName = funcMatch[1];
    const funcId = `${ddlData.database}.${funcName}`;
    entities.functions.add(funcId);
    log(`Created function: ${funcId}`);
  }
}

// Handle DROP FUNCTION
function handleDropFunction(ddlData) {
  const funcMatch = ddlData.query.match(/DROP\s+FUNCTION\s+(?:IF\s+EXISTS\s+)?`?([^`\s]+)`?/i);
  if (funcMatch) {
    const funcName = funcMatch[1];
    const funcId = `${ddlData.database}.${funcName}`;
    entities.functions.delete(funcId);
    log(`Dropped function: ${funcId}`);
  }
}

// Handle RENAME TABLE
function handleRenameTable(ddlData) {
  const renameMatch = ddlData.query.match(/RENAME\s+TABLE\s+`?([^`\s]+)`?\s+TO\s+`?([^`\s]+)`?/i);
  if (renameMatch) {
    const oldName = renameMatch[1];
    const newName = renameMatch[2];
    const oldTableId = `${ddlData.database}.${oldName}`;
    const newTableId = `${ddlData.database}.${newName}`;
    
    // Update entities
    if (entities.tables.has(oldTableId)) {
      entities.tables.delete(oldTableId);
      entities.tables.add(newTableId);
      
      // Update column entities
      for (const columnId of entities.columns) {
        if (columnId.startsWith(oldTableId + '.')) {
          const columnName = columnId.substring(oldTableId.length + 1);
          entities.columns.delete(columnId);
          entities.columns.add(`${newTableId}.${columnName}`);
        }
      }
      
      log(`Renamed table from ${oldTableId} to ${newTableId}`);
    }
  }
}

// Handle ALTER DATABASE
function handleAlterDatabase(ddlData) {
  if (ddlData.database) {
    log(`Altering database: ${ddlData.database}`);
    // In production: update database attributes in Cedar
  }
}

// Handle RENAME USER
function handleRenameUser(ddlData) {
  const oldUser = ddlData.context?.target_user || 'unknown';
  const newUser = ddlData.context?.new_user || 'unknown';
  
  if (entities.users.has(oldUser)) {
    entities.users.delete(oldUser);
    entities.users.add(newUser);
    log(`Renamed user from ${oldUser} to ${newUser}`);
  }
}

// Handle ALTER USER
function handleAlterUser(ddlData) {
  const user = ddlData.context?.target_user || ddlData.user || 'unknown';
  log(`Altering user: ${user}`);
  // In production: update user attributes in Cedar
}

// Handle ALTER FUNCTION
function handleAlterFunction(ddlData) {
  const funcMatch = ddlData.query.match(/ALTER\s+FUNCTION\s+`?([^`\s(]+)`?/i);
  if (funcMatch) {
    const funcName = funcMatch[1];
    const funcId = `${ddlData.database}.${funcName}`;
    log(`Altering function: ${funcId}`);
    // In production: update function definition in Cedar
  }
}

// Handle ALTER PROCEDURE
function handleAlterProcedure(ddlData) {
  const procMatch = ddlData.query.match(/ALTER\s+PROCEDURE\s+`?([^`\s(]+)`?/i);
  if (procMatch) {
    const procName = procMatch[1];
    const procId = `${ddlData.database}.${procName}`;
    log(`Altering procedure: ${procId}`);
    // In production: update procedure definition in Cedar
  }
}

// Handle CREATE EVENT
function handleCreateEvent(ddlData) {
  const eventMatch = ddlData.query.match(/CREATE\s+EVENT\s+`?([^`\s]+)`?/i);
  if (eventMatch) {
    const eventName = eventMatch[1];
    const eventId = `${ddlData.database}.${eventName}`;
    entities.events = entities.events || new Set();
    entities.events.add(eventId);
    log(`Created event: ${eventId}`);
  }
}

// Handle ALTER EVENT
function handleAlterEvent(ddlData) {
  const eventMatch = ddlData.query.match(/ALTER\s+EVENT\s+`?([^`\s]+)`?/i);
  if (eventMatch) {
    const eventName = eventMatch[1];
    const eventId = `${ddlData.database}.${eventName}`;
    log(`Altering event: ${eventId}`);
    // In production: update event definition in Cedar
  }
}

// Handle DROP EVENT
function handleDropEvent(ddlData) {
  const eventMatch = ddlData.query.match(/DROP\s+EVENT\s+(?:IF\s+EXISTS\s+)?`?([^`\s]+)`?/i);
  if (eventMatch) {
    const eventName = eventMatch[1];
    const eventId = `${ddlData.database}.${eventName}`;
    if (entities.events) {
      entities.events.delete(eventId);
    }
    log(`Dropped event: ${eventId}`);
  }
}

// Handle CREATE SERVER
function handleCreateServer(ddlData) {
  const serverMatch = ddlData.query.match(/CREATE\s+SERVER\s+`?([^`\s]+)`?/i);
  if (serverMatch) {
    const serverName = serverMatch[1];
    entities.servers = entities.servers || new Set();
    entities.servers.add(serverName);
    log(`Created server: ${serverName}`);
  }
}

// Handle DROP SERVER
function handleDropServer(ddlData) {
  const serverMatch = ddlData.query.match(/DROP\s+SERVER\s+(?:IF\s+EXISTS\s+)?`?([^`\s]+)`?/i);
  if (serverMatch) {
    const serverName = serverMatch[1];
    if (entities.servers) {
      entities.servers.delete(serverName);
    }
    log(`Dropped server: ${serverName}`);
  }
}

// Handle ALTER SERVER
function handleAlterServer(ddlData) {
  const serverMatch = ddlData.query.match(/ALTER\s+SERVER\s+`?([^`\s]+)`?/i);
  if (serverMatch) {
    const serverName = serverMatch[1];
    log(`Altering server: ${serverName}`);
    // In production: update server configuration in Cedar
  }
}

// Handle CREATE ROLE
function handleCreateRole(ddlData) {
  const roleMatch = ddlData.query.match(/CREATE\s+ROLE\s+`?([^`\s@]+)`?/i);
  if (roleMatch) {
    const roleName = roleMatch[1];
    entities.roles = entities.roles || new Set();
    entities.roles.add(roleName);
    log(`Created role: ${roleName}`);
  }
}

// Handle DROP ROLE
function handleDropRole(ddlData) {
  const roleMatch = ddlData.query.match(/DROP\s+ROLE\s+(?:IF\s+EXISTS\s+)?`?([^`\s@]+)`?/i);
  if (roleMatch) {
    const roleName = roleMatch[1];
    if (entities.roles) {
      entities.roles.delete(roleName);
    }
    log(`Dropped role: ${roleName}`);
  }
}

// Handle ALTER TABLESPACE
function handleAlterTablespace(ddlData) {
  const tablespaceMatch = ddlData.query.match(/ALTER\s+TABLESPACE\s+`?([^`\s]+)`?/i);
  if (tablespaceMatch) {
    const tablespaceName = tablespaceMatch[1];
    log(`Altering tablespace: ${tablespaceName}`);
    // In production: update tablespace configuration in Cedar
  }
}

// Handle CREATE RESOURCE GROUP
function handleCreateResourceGroup(ddlData) {
  const rgMatch = ddlData.query.match(/CREATE\s+RESOURCE\s+GROUP\s+`?([^`\s]+)`?/i);
  if (rgMatch) {
    const rgName = rgMatch[1];
    entities.resourceGroups = entities.resourceGroups || new Set();
    entities.resourceGroups.add(rgName);
    log(`Created resource group: ${rgName}`);
  }
}

// Handle ALTER RESOURCE GROUP
function handleAlterResourceGroup(ddlData) {
  const rgMatch = ddlData.query.match(/ALTER\s+RESOURCE\s+GROUP\s+`?([^`\s]+)`?/i);
  if (rgMatch) {
    const rgName = rgMatch[1];
    log(`Altering resource group: ${rgName}`);
    // In production: update resource group configuration in Cedar
  }
}

// Handle DROP RESOURCE GROUP
function handleDropResourceGroup(ddlData) {
  const rgMatch = ddlData.query.match(/DROP\s+RESOURCE\s+GROUP\s+(?:IF\s+EXISTS\s+)?`?([^`\s]+)`?/i);
  if (rgMatch) {
    const rgName = rgMatch[1];
    if (entities.resourceGroups) {
      entities.resourceGroups.delete(rgName);
    }
    log(`Dropped resource group: ${rgName}`);
  }
}

// Handle CREATE SRS (Spatial Reference System)
function handleCreateSRS(ddlData) {
  const srsMatch = ddlData.query.match(/CREATE\s+SRS\s+`?([^`\s]+)`?/i);
  if (srsMatch) {
    const srsName = srsMatch[1];
    entities.srs = entities.srs || new Set();
    entities.srs.add(srsName);
    log(`Created SRS: ${srsName}`);
  }
}

// Handle DROP SRS
function handleDropSRS(ddlData) {
  const srsMatch = ddlData.query.match(/DROP\s+SRS\s+(?:IF\s+EXISTS\s+)?`?([^`\s]+)`?/i);
  if (srsMatch) {
    const srsName = srsMatch[1];
    if (entities.srs) {
      entities.srs.delete(srsName);
    }
    log(`Dropped SRS: ${srsName}`);
  }
}

// Status endpoint to view current entities
app.get('/v1/status', (req, res) => {
  res.json({
    status: 'running',
    entities: {
      users: Array.from(entities.users),
      databases: Array.from(entities.databases),
      tables: Array.from(entities.tables),
      columns: Array.from(entities.columns),
      views: Array.from(entities.views),
      indices: Array.from(entities.indices),
      triggers: Array.from(entities.triggers),
      procedures: Array.from(entities.procedures),
      functions: Array.from(entities.functions),
      events: entities.events ? Array.from(entities.events) : [],
      servers: entities.servers ? Array.from(entities.servers) : [],
      roles: entities.roles ? Array.from(entities.roles) : [],
      resourceGroups: entities.resourceGroups ? Array.from(entities.resourceGroups) : [],
      srs: entities.srs ? Array.from(entities.srs) : []
    }
  });
});

// Health check endpoint
app.get('/health', (req, res) => {
  res.json({ status: 'healthy' });
});

// Start the server
app.listen(port, () => {
  log(`Cedar DDL Audit Server running on port ${port}`);
  log(`DDL Audit endpoint: http://localhost:${port}/v1/ddl_audit`);
  log(`Status endpoint: http://localhost:${port}/v1/status`);
  log(`Health check: http://localhost:${port}/health`);
});

// Graceful shutdown
process.on('SIGINT', () => {
  log('Shutting down Cedar DDL Audit Server...');
  process.exit(0);
});
