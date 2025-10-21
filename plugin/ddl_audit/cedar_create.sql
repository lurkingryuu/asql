CREATE USER 'user_alice'@'localhost' IDENTIFIED BY '';
CREATE USER 'user_bob'@'localhost' IDENTIFIED BY '';
CREATE USER 'user_charlie'@'localhost' IDENTIFIED BY '';
CREATE USER 'user_charlie'@'198.19.249.3' IDENTIFIED BY '';
CREATE DATABASE IF NOT EXISTS abac_test;
CREATE TABLE IF NOT EXISTS abac_test.employees ( id INT PRIMARY KEY, name VARCHAR(100), department VARCHAR(50) );
CREATE TABLE IF NOT EXISTS abac_test.projects ( id INT PRIMARY KEY, name VARCHAR(100), classification VARCHAR(50) );
CREATE TABLE IF NOT EXISTS abac_test.sensitive_data ( id INT PRIMARY KEY, info TEXT );