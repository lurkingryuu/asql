CREATE USER 'user_alice'@'%' IDENTIFIED BY '';
CREATE USER 'user_bob'@'%' IDENTIFIED BY '';
CREATE USER 'user_charlie'@'%' IDENTIFIED BY '';
CREATE DATABASE IF NOT EXISTS abac_test;
CREATE TABLE IF NOT EXISTS abac_test.employees ( id INT PRIMARY KEY, name VARCHAR(100), department VARCHAR(50) );
INSERT INTO abac_test.employees (id, name, department) VALUES (1, 'Alice', 'HR');
INSERT INTO abac_test.employees (id, name, department) VALUES (2, 'Bob', 'IT');
INSERT INTO abac_test.employees (id, name, department) VALUES (3, 'Charlie', 'Finance');

CREATE TABLE IF NOT EXISTS abac_test.projects ( id INT PRIMARY KEY, name VARCHAR(100), classification VARCHAR(50) );
INSERT INTO abac_test.projects (id, name, classification) VALUES (1, 'Project 1', 'Public');
INSERT INTO abac_test.projects (id, name, classification) VALUES (2, 'Project 2', 'Internal');
INSERT INTO abac_test.projects (id, name, classification) VALUES (3, 'Project 3', 'Confidential');

CREATE TABLE IF NOT EXISTS abac_test.sensitive_data ( id INT PRIMARY KEY, info TEXT );
INSERT INTO abac_test.sensitive_data (id, info) VALUES (1, 'Sensitive data 1');
INSERT INTO abac_test.sensitive_data (id, info) VALUES (2, 'Sensitive data 2');
INSERT INTO abac_test.sensitive_data (id, info) VALUES (3, 'Sensitive data 3');