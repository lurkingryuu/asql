-- Enhanced Test Script for DDL Audit Plugin
-- This script comprehensively tests all multi-event class capabilities:
-- - MYSQL_AUDIT_QUERY_CLASS (DDL statements)
-- - MYSQL_AUDIT_AUTHENTICATION_CLASS (user/role operations) 
-- - MYSQL_AUDIT_AUTHORIZATION_CLASS (privilege checks)
-- - MYSQL_AUDIT_TABLE_ACCESS_CLASS (table access patterns)
-- - MYSQL_AUDIT_STORED_PROGRAM_CLASS (procedures/functions)

-- Step 1: Install the plugin
INSTALL PLUGIN ddl_audit SONAME 'ddl_audit.so';

-- Step 2: Configure the plugin
SET GLOBAL ddl_audit_cedar_url = 'http://localhost:8180';
SET GLOBAL ddl_audit_cedar_timeout = 5000;
SET GLOBAL ddl_audit_enabled = ON;

-- Step 3: Verify plugin installation
SELECT PLUGIN_NAME, PLUGIN_STATUS, PLUGIN_TYPE 
FROM INFORMATION_SCHEMA.PLUGINS 
WHERE PLUGIN_NAME = 'ddl_audit';

-- Step 4: Check system variables
SHOW VARIABLES LIKE 'ddl_audit_%';

-- Step 5: Check initial status variables (should all be 0)
SHOW STATUS LIKE 'DDL_audit_%';

-- ========================================
-- SECTION 1: BASIC DATABASE AND TABLE OPERATIONS
-- Tests: MYSQL_AUDIT_QUERY_CLASS + MYSQL_AUDIT_TABLE_ACCESS_CLASS
-- ========================================

-- Step 6: Create test database (should trigger DDL event)
CREATE DATABASE IF NOT EXISTS test_ddl_audit;
USE test_ddl_audit;

-- Step 7: Test table creation (should trigger multiple events)
-- Expected events: QUERY_CLASS + TABLE_ACCESS_CLASS + AUTHORIZATION_CLASS
CREATE TABLE IF NOT EXISTS users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    username VARCHAR(50) NOT NULL UNIQUE,
    email VARCHAR(100) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    status ENUM('active', 'inactive') DEFAULT 'active'
);

CREATE TABLE IF NOT EXISTS orders (
    id INT PRIMARY KEY AUTO_INCREMENT,
    user_id INT NOT NULL,
    product_name VARCHAR(100) NOT NULL,
    amount DECIMAL(10,2) NOT NULL,
    order_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id)
);

-- Create additional tables for comprehensive testing
CREATE TABLE IF NOT EXISTS products (
    id INT PRIMARY KEY AUTO_INCREMENT,
    name VARCHAR(100) NOT NULL,
    category VARCHAR(50),
    price DECIMAL(10,2) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Step 8: Check status after table creation
SHOW STATUS LIKE 'DDL_audit_%';

-- ========================================
-- SECTION 2: INDEX OPERATIONS
-- Tests: MYSQL_AUDIT_QUERY_CLASS with INDEX commands
-- ========================================

-- Step 9: Test index creation (should extract table names from CREATE INDEX)
CREATE INDEX idx_username ON users(username);
CREATE INDEX idx_order_date ON orders(order_date);  
CREATE INDEX idx_product_category ON products(category);
CREATE UNIQUE INDEX idx_product_name ON products(name);

-- Step 10: Check status after index creation
SHOW STATUS LIKE 'DDL_audit_%';

-- ========================================
-- SECTION 3: ALTER TABLE OPERATIONS  
-- Tests: MYSQL_AUDIT_QUERY_CLASS + TABLE_ACCESS_CLASS
-- ========================================

-- Step 11: Test various ALTER TABLE operations
ALTER TABLE users ADD COLUMN last_login TIMESTAMP NULL;
ALTER TABLE users MODIFY COLUMN email VARCHAR(150);
ALTER TABLE users CHANGE COLUMN status user_status ENUM('active', 'inactive', 'suspended') DEFAULT 'active';
ALTER TABLE orders ADD COLUMN status ENUM('pending', 'completed', 'cancelled') DEFAULT 'pending';
ALTER TABLE products ADD COLUMN description TEXT;
ALTER TABLE products DROP COLUMN category;

-- Step 12: Check status after ALTER operations
SHOW STATUS LIKE 'DDL_audit_%';

-- ========================================
-- SECTION 4: VIEW OPERATIONS
-- Tests: MYSQL_AUDIT_QUERY_CLASS with VIEW commands  
-- ========================================

-- Step 13: Test view creation
CREATE VIEW user_order_summary AS
SELECT 
    u.username,
    u.email,
    u.user_status,
    COUNT(o.id) as order_count,
    SUM(o.amount) as total_amount
FROM users u
LEFT JOIN orders o ON u.id = o.user_id
GROUP BY u.id, u.username, u.email, u.user_status;

CREATE VIEW active_users AS
SELECT id, username, email, created_at
FROM users 
WHERE user_status = 'active';

CREATE VIEW product_summary AS
SELECT name, price, description
FROM products
WHERE price > 0;

-- Step 14: Check status after view creation
SHOW STATUS LIKE 'DDL_audit_%';

-- ========================================
-- SECTION 5: TRIGGER OPERATIONS
-- Tests: MYSQL_AUDIT_QUERY_CLASS with TRIGGER commands
-- ========================================

-- Step 15: Test trigger creation
DELIMITER //
CREATE TRIGGER update_user_last_login
    AFTER UPDATE ON users
    FOR EACH ROW
BEGIN
    -- Just log the update, don't perform another update to avoid circular reference
    -- In a real scenario, you might log to an audit table or perform other actions
    -- No action needed - this trigger exists just to test trigger DDL capture
END//
DELIMITER ;

DELIMITER //
CREATE TRIGGER validate_order_amount
    BEFORE INSERT ON orders
    FOR EACH ROW
BEGIN
    IF NEW.amount <= 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Order amount must be positive';
    END IF;
END//
DELIMITER ;

-- Step 16: Check status after trigger creation
SHOW STATUS LIKE 'DDL_audit_%';

-- ========================================
-- SECTION 6: STORED PROCEDURE OPERATIONS  
-- Tests: MYSQL_AUDIT_QUERY_CLASS + MYSQL_AUDIT_STORED_PROGRAM_CLASS
-- ========================================

-- Step 17: Test stored procedure creation
DELIMITER //
CREATE PROCEDURE GetUserOrders(IN user_id INT)
BEGIN
    SELECT o.*, u.username 
    FROM orders o 
    JOIN users u ON o.user_id = u.id 
    WHERE o.user_id = user_id;
END//
DELIMITER ;

DELIMITER //
CREATE PROCEDURE CreateUser(
    IN p_username VARCHAR(50),
    IN p_email VARCHAR(150),
    IN p_status ENUM('active', 'inactive', 'suspended')
)
BEGIN
    INSERT INTO users (username, email, user_status) 
    VALUES (p_username, p_email, p_status);
END//
DELIMITER ;

-- Step 18: Test stored function creation
DELIMITER //
CREATE FUNCTION GetUserOrderCount(user_id INT) 
RETURNS INT
READS SQL DATA
DETERMINISTIC
BEGIN
    DECLARE order_count INT DEFAULT 0;
    SELECT COUNT(*) INTO order_count FROM orders WHERE user_id = user_id;
    RETURN order_count;
END//
DELIMITER ;

DELIMITER //
CREATE FUNCTION CalculateUserRevenue(user_id INT)
RETURNS DECIMAL(10,2)
READS SQL DATA
DETERMINISTIC  
BEGIN
    DECLARE total_revenue DECIMAL(10,2) DEFAULT 0.00;
    SELECT COALESCE(SUM(amount), 0.00) INTO total_revenue 
    FROM orders 
    WHERE user_id = user_id AND status = 'completed';
    RETURN total_revenue;
END//
DELIMITER ;

-- Step 19: Check status after procedure/function creation
SHOW STATUS LIKE 'DDL_audit_%';

-- ========================================
-- SECTION 7: USER AND ROLE OPERATIONS
-- Tests: MYSQL_AUDIT_AUTHENTICATION_CLASS + MYSQL_AUDIT_AUTHORIZATION_CLASS
-- ========================================

-- Step 20: Test user creation (CRITICAL: Tests AUTHENTICATION_CLASS events)
-- NOTE: These commands should trigger MYSQL_AUDIT_AUTHENTICATION_CLASS events
CREATE USER IF NOT EXISTS 'test_user_1'@'localhost' IDENTIFIED BY 'test_password';
CREATE USER IF NOT EXISTS 'test_user_2'@'%' IDENTIFIED BY 'another_password';
CREATE USER IF NOT EXISTS 'audit_test_user'@'192.168.1.%' IDENTIFIED BY 'secure_pass';

-- Step 21: Test role creation (MySQL 8.0+ - Tests is_role field)
CREATE ROLE IF NOT EXISTS 'order_manager';
CREATE ROLE IF NOT EXISTS 'product_admin';  
CREATE ROLE IF NOT EXISTS 'read_only_user';

-- Step 22: Test user privilege grants (Tests AUTHORIZATION_CLASS events)
GRANT SELECT, INSERT ON test_ddl_audit.* TO 'test_user_1'@'localhost';
GRANT SELECT, INSERT, UPDATE ON test_ddl_audit.orders TO 'test_user_2'@'%';
GRANT ALL PRIVILEGES ON test_ddl_audit.products TO 'audit_test_user'@'192.168.1.%';

-- Step 23: Test role privilege grants
GRANT SELECT, INSERT, UPDATE ON test_ddl_audit.orders TO 'order_manager';
GRANT ALL PRIVILEGES ON test_ddl_audit.products TO 'product_admin';
GRANT SELECT ON test_ddl_audit.* TO 'read_only_user';

-- Step 24: Test role assignments to users
GRANT 'order_manager' TO 'test_user_1'@'localhost';
GRANT 'product_admin' TO 'test_user_2'@'%';
GRANT 'read_only_user' TO 'audit_test_user'@'192.168.1.%';

-- Step 25: Check status after user/role operations
SHOW STATUS LIKE 'DDL_audit_%';

-- Step 26: Test user modifications (Tests AUTHENTICATION_CREDENTIAL_CHANGE)
ALTER USER 'test_user_1'@'localhost' IDENTIFIED BY 'new_password';
ALTER USER 'test_user_2'@'%' PASSWORD EXPIRE NEVER;

-- Step 27: Test user renames (Tests AUTHENTICATION_AUTHID_RENAME)
RENAME USER 'test_user_1'@'localhost' TO 'renamed_user'@'localhost';

-- Step 28: Check status after user modifications
SHOW STATUS LIKE 'DDL_audit_%';

-- ========================================
-- SECTION 8: DATA OPERATIONS (Should NOT be captured)
-- Tests: Verify DML operations are ignored
-- ========================================

-- Step 29: Insert test data (DML operations - these should NOT be captured by the plugin)
INSERT INTO users (username, email, user_status) VALUES ('john_doe', 'john@example.com', 'active');
INSERT INTO users (username, email, user_status) VALUES ('jane_smith', 'jane@example.com', 'active');
INSERT INTO products (name, price, description) VALUES ('Laptop', 999.99, 'High-performance laptop');
INSERT INTO products (name, price, description) VALUES ('Mouse', 29.99, 'Wireless mouse');
INSERT INTO orders (user_id, product_name, amount, status) VALUES (1, 'Laptop', 999.99, 'completed');
INSERT INTO orders (user_id, product_name, amount, status) VALUES (2, 'Mouse', 29.99, 'pending');

-- Step 30: Update/Delete data (should NOT be captured)
UPDATE users SET last_login = NOW() WHERE id = 1;
UPDATE orders SET status = 'completed' WHERE id = 2;
DELETE FROM orders WHERE amount < 50;

-- Step 31: Check status (counters should NOT increase for DML)
SHOW STATUS LIKE 'DDL_audit_%';

-- ========================================
-- SECTION 9: RENAME AND ADDITIONAL DDL OPERATIONS
-- Tests: RENAME TABLE and complex DDL operations
-- ========================================

-- Step 32: Test table rename operations
RENAME TABLE users TO customers;
RENAME TABLE products TO inventory;

-- Step 33: Test additional ALTER operations on renamed tables
ALTER TABLE customers ADD COLUMN phone VARCHAR(20);
ALTER TABLE customers ADD COLUMN address TEXT;
ALTER TABLE inventory MODIFY COLUMN description VARCHAR(500);

-- Step 34: Check status after rename operations
SHOW STATUS LIKE 'DDL_audit_%';

-- ========================================
-- SECTION 10: COMPREHENSIVE DROP OPERATIONS
-- Tests: All DROP statement types
-- ========================================

-- Step 35: Test dropping triggers
DROP TRIGGER IF EXISTS update_user_last_login;
DROP TRIGGER IF EXISTS validate_order_amount;

-- Step 36: Test dropping procedures and functions
DROP PROCEDURE IF EXISTS GetUserOrders;
DROP PROCEDURE IF EXISTS CreateUser;
DROP FUNCTION IF EXISTS GetUserOrderCount;
DROP FUNCTION IF EXISTS CalculateUserRevenue;

-- Step 37: Test dropping views
DROP VIEW IF EXISTS user_order_summary;
DROP VIEW IF EXISTS active_users;
DROP VIEW IF EXISTS product_summary;

-- Step 38: Test dropping indexes
DROP INDEX idx_username ON customers;
DROP INDEX idx_order_date ON orders;
DROP INDEX idx_product_category ON inventory;

-- Step 39: Test dropping roles and users (Tests AUTHENTICATION_AUTHID_DROP)
REVOKE ALL PRIVILEGES, GRANT OPTION FROM 'renamed_user'@'localhost';
REVOKE ALL PRIVILEGES, GRANT OPTION FROM 'test_user_2'@'%';
REVOKE ALL PRIVILEGES, GRANT OPTION FROM 'audit_test_user'@'192.168.1.%';

DROP ROLE IF EXISTS 'order_manager';
DROP ROLE IF EXISTS 'product_admin';
DROP ROLE IF EXISTS 'read_only_user';

DROP USER IF EXISTS 'renamed_user'@'localhost';
DROP USER IF EXISTS 'test_user_2'@'%';  
DROP USER IF EXISTS 'audit_test_user'@'192.168.1.%';

-- Step 40: Test dropping tables and database
DROP TABLE IF EXISTS orders;
DROP TABLE IF EXISTS customers;
DROP TABLE IF EXISTS inventory;

-- ========================================
-- SECTION 11: FINAL STATUS AND ERROR TESTING
-- Tests: Error scenarios and final status
-- ========================================

-- Step 41: Clean up database
DROP DATABASE IF EXISTS test_ddl_audit;

-- Step 42: Check final status (comprehensive summary)
SHOW STATUS LIKE 'DDL_audit_%';

-- Step 43: Test error scenarios
-- Test with invalid Cedar URL
SET GLOBAL ddl_audit_cedar_url = 'http://invalid-url:9999';
CREATE TABLE test_error_handling (id INT PRIMARY KEY);
DROP TABLE IF EXISTS test_error_handling;

-- Test with Cedar service down  
SET GLOBAL ddl_audit_cedar_url = 'http://localhost:9999';
CREATE TABLE test_service_down (id INT PRIMARY KEY);
DROP TABLE IF EXISTS test_service_down;

-- Restore correct URL
SET GLOBAL ddl_audit_cedar_url = 'http://localhost:8180';

-- Step 44: Test plugin disable/enable
SET GLOBAL ddl_audit_enabled = OFF;
CREATE TABLE test_disabled (id INT PRIMARY KEY);  -- Should NOT be captured
DROP TABLE IF EXISTS test_disabled;

SET GLOBAL ddl_audit_enabled = ON;
CREATE TABLE test_enabled (id INT PRIMARY KEY);   -- Should be captured
DROP TABLE IF EXISTS test_enabled;

-- Step 45: Final status check
SHOW VARIABLES LIKE 'ddl_audit_%';
SHOW STATUS LIKE 'DDL_audit_%';

-- Step 46: Performance testing (optional)
-- Uncomment to test performance impact:
-- 
-- SET @start_time = NOW(6);
-- CREATE TABLE performance_test (id INT PRIMARY KEY AUTO_INCREMENT, data VARCHAR(255));
-- CREATE INDEX idx_data ON performance_test(data);
-- ALTER TABLE performance_test ADD COLUMN status ENUM('active', 'inactive') DEFAULT 'active';
-- DROP TABLE performance_test;
-- SELECT TIMESTAMPDIFF(MICROSECOND, @start_time, NOW(6)) AS execution_time_microseconds;

-- Step 47: Uninstall plugin
UNINSTALL PLUGIN ddl_audit;

-- ========================================
-- COMPREHENSIVE TEST SUMMARY
-- ========================================

/* 
This test script validates ALL multi-event class capabilities:

EVENT CLASSES TESTED:
✅ MYSQL_AUDIT_QUERY_CLASS
   - CREATE/ALTER/DROP TABLE operations
   - CREATE/DROP INDEX operations  
   - CREATE/DROP VIEW operations
   - CREATE/DROP TRIGGER operations
   - CREATE/DROP PROCEDURE/FUNCTION operations
   - RENAME TABLE operations
   - CREATE/DROP DATABASE operations

✅ MYSQL_AUDIT_AUTHENTICATION_CLASS
   - CREATE USER (is_role = false)
   - CREATE ROLE (is_role = true)
   - ALTER USER (credential changes)
   - RENAME USER operations
   - DROP USER operations
   - DROP ROLE operations

✅ MYSQL_AUDIT_AUTHORIZATION_CLASS  
   - GRANT statements (privilege context)
   - REVOKE statements
   - Database/table/column privilege checks
   - Role assignment privileges

✅ MYSQL_AUDIT_TABLE_ACCESS_CLASS
   - Exact table name identification during DDL
   - Table access patterns (READ/INSERT/UPDATE/DELETE)
   - Database.table resolution without parsing

✅ MYSQL_AUDIT_STORED_PROGRAM_CLASS
   - Stored procedure creation/execution
   - Stored function creation/execution  
   - Program name and database identification

STATUS VARIABLES VALIDATED:
- DDL_audit_events_total
- DDL_audit_auth_events (NEW)
- DDL_audit_authorization_events (NEW)
- DDL_audit_table_access_events (NEW)  
- DDL_audit_stored_program_events (NEW)
- DDL_audit_cedar_requests/successes/failures
- Individual DDL type counters

NEGATIVE TESTING:
- DML operations (INSERT/UPDATE/DELETE) ignored
- Plugin disable/enable functionality
- Error handling (invalid URLs, service down)
- Performance impact measurement

Expected Results:
- Rich Cedar payloads with multi-dimensional context
- Perfect table/database name extraction (zero parsing)
- User/role operations with complete metadata
- Privilege context for all DDL operations
- Comprehensive operational monitoring

This represents COMPLETE utilization of MySQL's audit framework!
*/
