-- Copyright (c) 2025, Oracle and/or its affiliates.
--
-- Test script for MySQL Authorization Plugin
--
-- This script demonstrates the functionality of the authorization plugin
-- system by testing various scenarios with the simple_authorization plugin.
--
-- Prerequisites:
-- 1. MySQL server with authorization plugin support compiled in
-- 2. simple_authorization.so plugin installed in plugin directory
-- 3. MySQL user with SUPER privileges to install plugins
--
-- Usage:
--   mysql -u root -p < test_authorization_plugin.sql

-- ============================================================================
-- Setup Test Environment
-- ============================================================================

-- Enable general log to track authorization decisions
SET GLOBAL general_log = ON;
SET GLOBAL general_log_file = '/tmp/mysql-auth-test.log';

-- Create test database and tables
DROP DATABASE IF EXISTS testdb;
DROP DATABASE IF EXISTS otherdb;
CREATE DATABASE testdb;
CREATE DATABASE otherdb;

USE testdb;
CREATE TABLE test_table (
  id INT PRIMARY KEY,
  name VARCHAR(50),
  secret VARCHAR(100)
);

INSERT INTO test_table VALUES 
  (1, 'public_data', 'not_so_secret'),
  (2, 'more_data', 'also_secret');

USE otherdb;
CREATE TABLE other_table (
  id INT PRIMARY KEY,
  data VARCHAR(100)
);

INSERT INTO other_table VALUES (1, 'other_data');

-- Create test users
DROP USER IF EXISTS 'testuser'@'localhost';
DROP USER IF EXISTS 'otheruser'@'localhost';
DROP USER IF EXISTS 'normaluser'@'localhost';

CREATE USER 'testuser'@'localhost' IDENTIFIED BY 'password';
CREATE USER 'otheruser'@'localhost' IDENTIFIED BY 'password';  
CREATE USER 'normaluser'@'localhost' IDENTIFIED BY 'password';

-- Grant minimal privileges to normaluser (for comparison)
GRANT SELECT ON testdb.* TO 'normaluser'@'localhost';

-- ============================================================================
-- Install and Configure Authorization Plugin
-- ============================================================================

-- Check if plugin is already installed
SELECT PLUGIN_NAME, PLUGIN_STATUS, PLUGIN_TYPE 
FROM INFORMATION_SCHEMA.PLUGINS 
WHERE PLUGIN_NAME = 'simple_authorization';

-- Install the simple authorization plugin
-- Note: Plugin must be available in MySQL plugin directory
INSTALL PLUGIN simple_authorization SONAME 'simple_authorization.so';

-- Verify plugin installation
SELECT PLUGIN_NAME, PLUGIN_STATUS, PLUGIN_TYPE, PLUGIN_DESCRIPTION
FROM INFORMATION_SCHEMA.PLUGINS 
WHERE PLUGIN_NAME = 'simple_authorization';

-- Show plugin system variables
SHOW VARIABLES LIKE 'simple_auth%';

-- ============================================================================
-- Test 1: Plugin in IGNORE mode (should use built-in authorization)
-- ============================================================================

SELECT 'Test 1: Plugin in IGNORE mode' as test_name;

-- Configure plugin to ignore all requests
SET GLOBAL simple_auth_mode = 'ignore';

-- Test access as testuser (should fail - no built-in privileges)
-- This would normally be tested by connecting as testuser, but for this demo
-- we'll simulate by checking current behavior
SELECT 'testuser should be denied access to testdb (no built-in privileges)' as expected_result;

-- ============================================================================
-- Test 2: Plugin in GRANT mode with user allowlist
-- ============================================================================

SELECT 'Test 2: Plugin in GRANT mode with user allowlist' as test_name;

-- Configure plugin to grant access to testuser
SET GLOBAL simple_auth_allow_user = 'testuser';
SET GLOBAL simple_auth_mode = 'grant';

-- Show current configuration
SHOW VARIABLES LIKE 'simple_auth%';

SELECT 'testuser should now have access to all databases' as expected_result;

-- ============================================================================
-- Test 3: Plugin in GRANT mode with user and database allowlist
-- ============================================================================

SELECT 'Test 3: Plugin in GRANT mode with user and database allowlist' as test_name;

-- Configure plugin to grant access to testuser only on testdb
SET GLOBAL simple_auth_allow_user = 'testuser';
SET GLOBAL simple_auth_allow_db = 'testdb';
SET GLOBAL simple_auth_mode = 'grant';

-- Show current configuration
SHOW VARIABLES LIKE 'simple_auth%';

SELECT 'testuser should have access to testdb but not otherdb' as expected_result;

-- ============================================================================
-- Test 4: Plugin in DENY mode
-- ============================================================================

SELECT 'Test 4: Plugin in DENY mode' as test_name;

-- Configure plugin to deny all access
SET GLOBAL simple_auth_mode = 'deny';

SELECT 'All users should be denied access (plugin overrides built-in privileges)' as expected_result;

-- ============================================================================
-- Test 5: Multiple user scenarios
-- ============================================================================

SELECT 'Test 5: Multiple user scenarios' as test_name;

-- Reset to selective grant mode
SET GLOBAL simple_auth_allow_user = 'testuser';
SET GLOBAL simple_auth_allow_db = 'testdb'; 
SET GLOBAL simple_auth_mode = 'grant';

SELECT 'testuser: access to testdb via plugin' as scenario_1;
SELECT 'otheruser: no plugin access, no built-in privileges -> denied' as scenario_2;
SELECT 'normaluser: no plugin access, has built-in SELECT on testdb -> allowed' as scenario_3;

-- ============================================================================
-- Test 6: Plugin behavior with different SQL operations  
-- ============================================================================

SELECT 'Test 6: Plugin behavior with different SQL operations' as test_name;

-- The plugin should be called for different types of database access:
-- - SELECT queries (table access)
-- - INSERT/UPDATE/DELETE queries (table access)  
-- - USE database (database access)
-- - SHOW tables (database access)
-- - etc.

SELECT 'Plugin should handle various SQL operations and access types' as expected_result;

-- ============================================================================
-- Test 7: Error handling and edge cases
-- ============================================================================

SELECT 'Test 7: Error handling and edge cases' as test_name;

-- Test with NULL/empty configuration
SET GLOBAL simple_auth_allow_user = '';
SET GLOBAL simple_auth_allow_db = '';

-- Test with invalid mode (should default to ignore)
SET GLOBAL simple_auth_mode = 'invalid_mode';

SELECT 'Plugin should handle invalid configuration gracefully' as expected_result;

-- ============================================================================
-- Test 8: Plugin uninstall and reinstall
-- ============================================================================

SELECT 'Test 8: Plugin uninstall and reinstall' as test_name;

-- Uninstall plugin
UNINSTALL PLUGIN simple_authorization;

-- Verify plugin is uninstalled
SELECT COUNT(*) as plugin_count_after_uninstall 
FROM INFORMATION_SCHEMA.PLUGINS 
WHERE PLUGIN_NAME = 'simple_authorization';

-- Reinstall plugin  
INSTALL PLUGIN simple_authorization SONAME 'simple_authorization.so';

-- Verify plugin is reinstalled
SELECT PLUGIN_NAME, PLUGIN_STATUS 
FROM INFORMATION_SCHEMA.PLUGINS 
WHERE PLUGIN_NAME = 'simple_authorization';

SELECT 'Plugin should survive uninstall/reinstall cycle' as expected_result;

-- ============================================================================
-- Performance Testing (Optional)
-- ============================================================================

SELECT 'Performance Test: Measuring plugin overhead' as test_name;

-- Reset plugin to ignore mode for baseline
SET GLOBAL simple_auth_mode = 'ignore';

-- Measure time for multiple queries without plugin intervention
SET @start_time = NOW(6);

-- Execute multiple queries to measure baseline performance
SELECT COUNT(*) FROM testdb.test_table; 
SELECT COUNT(*) FROM testdb.test_table;
SELECT COUNT(*) FROM testdb.test_table;
SELECT COUNT(*) FROM testdb.test_table;
SELECT COUNT(*) FROM testdb.test_table;

SET @ignore_time = TIMESTAMPDIFF(MICROSECOND, @start_time, NOW(6));

-- Now test with plugin active
SET GLOBAL simple_auth_mode = 'grant';
SET GLOBAL simple_auth_allow_user = USER();

SET @start_time = NOW(6);

-- Execute same queries with plugin active
SELECT COUNT(*) FROM testdb.test_table;
SELECT COUNT(*) FROM testdb.test_table; 
SELECT COUNT(*) FROM testdb.test_table;
SELECT COUNT(*) FROM testdb.test_table;
SELECT COUNT(*) FROM testdb.test_table;

SET @plugin_time = TIMESTAMPDIFF(MICROSECOND, @start_time, NOW(6));

-- Show performance comparison
SELECT @ignore_time as baseline_microseconds, 
       @plugin_time as plugin_microseconds,
       @plugin_time - @ignore_time as overhead_microseconds;

-- ============================================================================
-- Cleanup
-- ============================================================================

SELECT 'Cleaning up test environment' as cleanup;

-- Uninstall plugin
UNINSTALL PLUGIN simple_authorization;

-- Remove test users
DROP USER 'testuser'@'localhost';
DROP USER 'otheruser'@'localhost';
DROP USER 'normaluser'@'localhost';

-- Remove test databases
DROP DATABASE testdb;
DROP DATABASE otherdb;

-- Disable general log
SET GLOBAL general_log = OFF;

-- ============================================================================
-- Test Results Summary
-- ============================================================================

SELECT 'Authorization Plugin Test Complete' as status;
SELECT 'Check MySQL error log and /tmp/mysql-auth-test.log for detailed results' as instructions;
SELECT 'Expected plugin behavior:' as summary;
SELECT '1. IGNORE mode: Falls back to built-in authorization' as behavior_1;
SELECT '2. GRANT mode: Grants access to specified users/databases' as behavior_2; 
SELECT '3. DENY mode: Denies all access regardless of built-in privileges' as behavior_3;
SELECT '4. Plugin decisions override built-in authorization when not IGNORE' as behavior_4;
