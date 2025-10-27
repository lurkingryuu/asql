-- Check current verbosity
SELECT @@global.log_error_verbosity;

-- Set to level 3 (includes INFO messages)
SET GLOBAL log_error_verbosity = 3;

-- Verify the change
SELECT @@global.log_error_verbosity;

-- Install and configure ddl_audit plugin, bootstrap ABAC demo schema/data
INSTALL PLUGIN ddl_audit SONAME 'ddl_audit.so';

SET GLOBAL ddl_audit_cedar_url = 'http://localhost:8280';
SET GLOBAL ddl_audit_cedar_timeout = 5000;
SET GLOBAL ddl_audit_enabled = ON;

-- Optional sanity checks
SELECT PLUGIN_NAME, PLUGIN_STATUS FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME='ddl_audit';
SHOW VARIABLES LIKE 'ddl_audit_%';

-- --
INSTALL PLUGIN cedar_authorization SONAME 'cedar_authorization.so';

SET GLOBAL cedar_authorization_url = 'http://localhost:8280/v1/is_authorized';
SET GLOBAL cedar_authorization_timeout = 5000;

SELECT PLUGIN_NAME, PLUGIN_STATUS, PLUGIN_TYPE FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'cedar_authorization';
SHOW VARIABLES LIKE 'cedar_authorization%';