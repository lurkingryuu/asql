-- Test Script for Cedar Authorization Plugin
-- This script demonstrates how to test the Cedar authorization functionality

-- Step 1: Install the plugin
INSTALL PLUGIN cedar_authorization SONAME 'cedar_authorization.so';

-- Step 2: Configure the plugin
SET GLOBAL cedar_authorization_url = 'http://localhost:8280/v1/is_authorized';
SET GLOBAL cedar_authorization_timeout = 5000;

-- Step 3: Verify plugin installation
SHOW PLUGINS;
SELECT PLUGIN_NAME, PLUGIN_STATUS, PLUGIN_TYPE 
FROM INFORMATION_SCHEMA.PLUGINS 
WHERE PLUGIN_NAME = 'cedar_authorization';

-- Step 4: Check system variables
SHOW VARIABLES LIKE 'cedar_authorization%';

-- Step 5: Create test database and tables
CREATE DATABASE IF NOT EXISTS test_cedar_db;
USE test_cedar_db;

CREATE TABLE IF NOT EXISTS employees (
    id INT PRIMARY KEY AUTO_INCREMENT,
    name VARCHAR(100) NOT NULL,
    department VARCHAR(50),
    salary DECIMAL(10,2),
    hire_date DATE
);

CREATE TABLE IF NOT EXISTS orders (
    order_id INT PRIMARY KEY AUTO_INCREMENT,
    customer_id INT,
    product_name VARCHAR(100),
    quantity INT,
    order_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS payroll (
    employee_id INT PRIMARY KEY,
    base_salary DECIMAL(10,2),
    bonus DECIMAL(10,2),
    tax_deductions DECIMAL(10,2),
    net_pay DECIMAL(10,2)
);

CREATE TABLE IF NOT EXISTS public_data (
    id INT PRIMARY KEY AUTO_INCREMENT,
    title VARCHAR(100),
    description TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Step 6: Create test users (run as admin)
CREATE USER IF NOT EXISTS 'alice'@'%' IDENTIFIED BY 'password123';
CREATE USER IF NOT EXISTS 'bob'@'%' IDENTIFIED BY 'password123';
CREATE USER IF NOT EXISTS 'charlie'@'%' IDENTIFIED BY 'password123';
CREATE USER IF NOT EXISTS 'maintenance'@'%' IDENTIFIED BY 'password123';
CREATE USER IF NOT EXISTS 'admin'@'%' IDENTIFIED BY 'password123';
CREATE USER IF NOT EXISTS 'hr_user'@'%' IDENTIFIED BY 'password123';
CREATE USER IF NOT EXISTS 'developer'@'%' IDENTIFIED BY 'password123';
CREATE USER IF NOT EXISTS 'temp_contractor'@'%' IDENTIFIED BY 'password123';
CREATE USER IF NOT EXISTS 'auditor'@'%' IDENTIFIED BY 'password123';
CREATE USER IF NOT EXISTS 'emergency'@'%' IDENTIFIED BY 'password123';

-- Step 7: Insert test data
INSERT INTO employees (name, department, salary, hire_date) VALUES
('John Doe', 'Engineering', 75000.00, '2023-01-15'),
('Jane Smith', 'Marketing', 65000.00, '2023-02-20'),
('Bob Johnson', 'Sales', 55000.00, '2023-03-10'),
('Alice Williams', 'HR', 70000.00, '2023-04-05');

INSERT INTO orders (customer_id, product_name, quantity) VALUES
(1, 'Widget A', 10),
(2, 'Widget B', 5),
(3, 'Widget C', 8),
(1, 'Widget D', 12);

INSERT INTO payroll (employee_id, base_salary, bonus, tax_deductions, net_pay) VALUES
(1, 75000.00, 5000.00, 15000.00, 65000.00),
(2, 65000.00, 3000.00, 12000.00, 56000.00),
(3, 55000.00, 2000.00, 10000.00, 47000.00),
(4, 70000.00, 4000.00, 14000.00, 60000.00);

INSERT INTO public_data (title, description) VALUES
('Public Announcement 1', 'This is public information available to all users.'),
('Public Announcement 2', 'Another piece of public information.'),
('Public FAQ', 'Frequently asked questions and answers.');

-- Step 8: Create a test stored procedure
DELIMITER //
CREATE PROCEDURE IF NOT EXISTS calculate_bonus(IN emp_id INT)
BEGIN
    DECLARE bonus_amount DECIMAL(10,2);
    SELECT salary * 0.1 INTO bonus_amount FROM employees WHERE id = emp_id;
    SELECT CONCAT('Bonus for employee ', emp_id, ' is: $', bonus_amount) AS result;
END//
DELIMITER ;

-- Step 9: Test queries (these will be evaluated by Cedar)
-- Note: Run these as different users to test various policies

-- Test 1: Alice accessing employees table (should be allowed by Policy 1)
-- Run as alice:
-- SELECT * FROM employees;

-- Test 2: Bob accessing orders during business hours (Policy 2)
-- Run as bob during 9 AM - 5 PM:
-- SELECT * FROM orders;
-- INSERT INTO orders (customer_id, product_name, quantity) VALUES (4, 'Test Widget', 1);

-- Test 3: Charlie from internal IP (Policy 3)
-- Run as charlie from 192.168.1.x or 10.x.x.x:
-- SELECT * FROM public_data;

-- Test 4: Maintenance operations on weekends (Policy 4)
-- Run as maintenance on Saturday/Sunday:
-- CREATE TABLE maintenance_log (id INT, operation VARCHAR(100), timestamp TIMESTAMP);

-- Test 5: Admin access during weekdays (Policy 5)
-- Run as admin Monday-Friday:
-- CREATE DATABASE test_admin_db;
-- DROP DATABASE test_admin_db;

-- Test 6: Payroll access restrictions (Policy 7)
-- This should be denied outside 8 AM - 6 PM:
-- SELECT * FROM payroll;

-- Test 7: Delete operations (Policy 9)
-- Run as alice or admin:
-- DELETE FROM orders WHERE order_id = 1;

-- Test 8: Column-level access (Policy 10)
-- Run as hr_user during business hours from 192.168.100.x:
-- SELECT salary FROM employees;

-- Test 9: Procedure execution (Policy 11)
-- Run as developer during weekday business hours:
-- CALL calculate_bonus(1);

-- Test 10: Audit user read access (Policy 15)
-- Run as auditor:
-- SELECT * FROM employees;
-- SELECT * FROM orders;
-- SELECT * FROM public_data;

-- Step 10: Monitor Cedar plugin logs
-- Check MySQL error log for Cedar authorization messages:
-- tail -f /var/log/mysql/error.log | grep -i cedar

-- Step 11: Test error scenarios

-- -- Test with invalid Cedar URL
-- SET GLOBAL cedar_authorization_url = 'http://invalid-url:9999';
-- -- Try any query - should result in DENY

-- -- Test with Cedar service down
-- SET GLOBAL cedar_authorization_url = 'http://localhost:9999';
-- -- Try any query - should result in DENY

-- -- Restore correct URL
-- SET GLOBAL cedar_authorization_url = 'http://localhost:8180';

-- -- Step 12: Performance testing queries
-- -- These can be used to test the performance impact of Cedar authorization

-- SELECT COUNT(*) FROM employees;
-- SELECT AVG(salary) FROM employees WHERE department = 'Engineering';
-- SELECT o.order_id, o.product_name, e.name 
-- FROM orders o 
-- JOIN employees e ON o.customer_id = e.id;

-- Step 13: Cleanup (optional)
-- DROP DATABASE test_cedar_db;
-- DROP USER 'alice'@'%';
-- DROP USER 'bob'@'%';
-- DROP USER 'charlie'@'%';
-- DROP USER 'maintenance'@'%';
-- DROP USER 'admin'@'%';
-- DROP USER 'hr_user'@'%';
-- DROP USER 'developer'@'%';
-- DROP USER 'temp_contractor'@'%';
-- DROP USER 'auditor'@'%';
-- DROP USER 'emergency'@'%';
-- UNINSTALL PLUGIN cedar_authorization;

-- Notes for testing:
-- 1. Ensure Cedar service is running at localhost:8180 with the sample policies loaded
-- 2. Test from different IP addresses to verify IP-based policies
-- 3. Test at different times of day to verify time-based policies
-- 4. Test on different days of the week to verify day-based policies
-- 5. Monitor MySQL error log for detailed Cedar plugin activity
-- 6. Use different MySQL client connections for different users
-- 7. Consider testing with concurrent connections to verify thread safety
