-- Remove demo data/users

DROP TABLE IF EXISTS abac_test.employees;
DROP TABLE IF EXISTS abac_test.projects;
DROP TABLE IF EXISTS abac_test.sensitive_data;

-- Remove demo data and schema
DROP DATABASE IF EXISTS abac_test;

-- Remove demo users
DROP USER IF EXISTS 'user_alice'@'localhost';
DROP USER IF EXISTS 'user_bob'@'localhost';
DROP USER IF EXISTS 'user_charlie'@'localhost';
DROP USER IF EXISTS 'user_charlie'@'198.19.249.3';

