-- Install and configure ddl_audit plugin, bootstrap ABAC demo schema/data
INSTALL PLUGIN ddl_audit SONAME 'ddl_audit.so';

SET GLOBAL ddl_audit_cedar_url = 'http://localhost:8280';
SET GLOBAL ddl_audit_cedar_timeout = 5000;
SET GLOBAL ddl_audit_enabled = ON;

-- Optional sanity checks
SELECT PLUGIN_NAME, PLUGIN_STATUS FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME='ddl_audit';
SHOW VARIABLES LIKE 'ddl_audit_%';

