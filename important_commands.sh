# Set the log error verbosity to 3 (includes INFO messages)
mysql -u root --socket=build/mysql.sock -e "-- Check current verbosity
SELECT @@global.log_error_verbosity;

-- Set to level 3 (includes INFO messages)
SET GLOBAL log_error_verbosity = 3;

-- Verify the change
SELECT @@global.log_error_verbosity;"

# Way to run a sql file in the mysql client
mysql -u root --socket=build/mysql.sock < plugin/ddl_audit/cedar_create.sql

# Way to execute a command in the mysql client
mysql -u root --socket=build/mysql.sock -e "SHOW VARIABLES LIKE 'log_error_verbosity';"
