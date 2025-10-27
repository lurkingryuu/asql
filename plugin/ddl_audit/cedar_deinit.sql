SET GLOBAL ddl_audit_enabled = OFF;
UNINSTALL PLUGIN ddl_audit;

SET GLOBAL cedar_authorization_url = DEFAULT;
UNINSTALL PLUGIN cedar_authorization;

-- Check current verbosity
SELECT @@global.log_error_verbosity;

-- Set to level 2 (includes WARNING messages)
SET GLOBAL log_error_verbosity = 2;

-- Verify the change
SELECT @@global.log_error_verbosity;