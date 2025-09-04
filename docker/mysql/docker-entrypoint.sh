#!/bin/bash
set -e

# MySQL Docker Entrypoint Script for Authorization Plugin Support
# =============================================================================

# Create necessary directories
mkdir -p /var/run/mysqld
mkdir -p /var/lib/mysql-data
mkdir -p /var/log/mysql

# Set proper permissions
chown -R mysql:mysql /var/run/mysqld
chown -R mysql:mysql /var/lib/mysql-data
chown -R mysql:mysql /var/log/mysql

# Initialize MySQL data directory if it doesn't exist
if [ ! -d "/var/lib/mysql-data/mysql" ]; then
    echo "Initializing MySQL data directory..."
    mysqld --initialize-insecure --user=mysql --datadir=/var/lib/mysql-data
fi

# Set environment variables for plugins
export MYSQL_PLUGIN_DIR="/usr/local/mysql/lib/plugin"

# Configure authorization plugins if environment variables are set
if [ -n "$SIMPLE_AUTH_MODE" ]; then
    echo "Setting simple authorization mode to: $SIMPLE_AUTH_MODE"
    sed -i "s/simple_auth_mode.*/simple_auth_mode = $SIMPLE_AUTH_MODE/" /etc/mysql/my.cnf
fi

if [ -n "$SIMPLE_AUTH_ALLOW_USER" ]; then
    echo "Setting simple authorization allow user to: $SIMPLE_AUTH_ALLOW_USER"
    sed -i "s/simple_auth_allow_user.*/simple_auth_allow_user = $SIMPLE_AUTH_ALLOW_USER/" /etc/mysql/my.cnf
fi

if [ -n "$SIMPLE_AUTH_ALLOW_DB" ]; then
    echo "Setting simple authorization allow database to: $SIMPLE_AUTH_ALLOW_DB"
    sed -i "s/simple_auth_allow_db.*/simple_auth_allow_db = $SIMPLE_AUTH_ALLOW_DB/" /etc/mysql/my.cnf
fi

if [ -n "$EXTERNAL_AUTH_URL" ]; then
    echo "Setting external authorization URL to: $EXTERNAL_AUTH_URL"
    # Enable external authorization plugin
    sed -i "s/plugin_load_add.*/plugin_load_add = simple_authorization.so,external_authorization.so/" /etc/mysql/my.cnf
    sed -i "/\[mysqld\]/a external_authorization_url = $EXTERNAL_AUTH_URL" /etc/mysql/my.cnf
fi

if [ -n "$EXTERNAL_AUTH_TIMEOUT" ]; then
    echo "Setting external authorization timeout to: $EXTERNAL_AUTH_TIMEOUT"
    sed -i "/\[mysqld\]/a external_authorization_timeout = $EXTERNAL_AUTH_TIMEOUT" /etc/mysql/my.cnf
fi

# Handle different command modes
if [ "$1" = 'mysqld' ]; then
    # Start MySQL server
    echo "Starting MySQL server..."

    # Set ownership for data directory
    chown -R mysql:mysql /var/lib/mysql-data

    # Execute MySQL server
    exec mysqld --user=mysql --datadir=/var/lib/mysql-data --socket=/var/run/mysqld/mysqld.sock "$@"
elif [ "$1" = 'mysql' ]; then
    # Start MySQL client
    shift
    exec mysql --socket=/var/run/mysqld/mysqld.sock "$@"
elif [ "$1" = 'mysqladmin' ]; then
    # MySQL admin commands
    shift
    exec mysqladmin --socket=/var/run/mysqld/mysqld.sock "$@"
elif [ "$1" = 'init' ]; then
    # Initialize database and create users
    echo "Initializing MySQL database..."

    # Start MySQL in background for initialization
    mysqld --user=mysql --datadir=/var/lib/mysql-data --socket=/var/run/mysqld/mysqld.sock --skip-networking &
    MYSQL_PID=$!

    # Wait for MySQL to be ready
    echo "Waiting for MySQL to start..."
    for i in {1..30}; do
        if mysqladmin ping --socket=/var/run/mysqld/mysqld.sock >/dev/null 2>&1; then
            break
        fi
        sleep 1
    done

    if ! mysqladmin ping --socket=/var/run/mysqld/mysqld.sock >/dev/null 2>&1; then
        echo "MySQL failed to start"
        exit 1
    fi

    # Create authorization plugin test database and users
    echo "Setting up authorization plugin test environment..."

    mysql --socket=/var/run/mysqld/mysqld.sock -e "
        -- Create test database
        CREATE DATABASE IF NOT EXISTS testdb;
        CREATE DATABASE IF NOT EXISTS otherdb;

        -- Create test tables
        USE testdb;
        CREATE TABLE IF NOT EXISTS test_table (
            id INT PRIMARY KEY,
            name VARCHAR(50),
            secret VARCHAR(100)
        );

        INSERT INTO test_table VALUES
            (1, 'public_data', 'not_so_secret'),
            (2, 'more_data', 'also_secret');

        USE otherdb;
        CREATE TABLE IF NOT EXISTS other_table (
            id INT PRIMARY KEY,
            data VARCHAR(100)
        );

        INSERT INTO other_table VALUES (1, 'other_data');

        -- Create test users
        CREATE USER IF NOT EXISTS 'testuser'@'%' IDENTIFIED BY 'password';
        CREATE USER IF NOT EXISTS 'otheruser'@'%' IDENTIFIED BY 'password';
        CREATE USER IF NOT EXISTS 'normaluser'@'%' IDENTIFIED BY 'password';

        -- Grant minimal privileges to normaluser
        GRANT SELECT ON testdb.* TO 'normaluser'@'%';

        -- Enable general log for debugging
        SET GLOBAL general_log = ON;
        SET GLOBAL general_log_file = '/var/log/mysql/general.log';

        -- Show plugin status
        SHOW PLUGINS LIKE '%authorization%';
    "

    # Stop MySQL
    kill $MYSQL_PID
    wait $MYSQL_PID

    echo "MySQL initialization complete!"
    echo ""
    echo "Authorization Plugin Test Environment Ready!"
    echo "=============================================="
    echo "Test users created:"
    echo "  testuser/password   - No built-in privileges (for plugin testing)"
    echo "  otheruser/password  - No built-in privileges (for plugin testing)"
    echo "  normaluser/password - Has SELECT on testdb (for comparison)"
    echo ""
    echo "Test databases:"
    echo "  testdb  - Test database with sample data"
    echo "  otherdb - Additional test database"
    echo ""
    echo "To start MySQL: docker-compose up mysql"
    echo "To connect: docker-compose exec mysql mysql -u root"
    echo "To test: docker-compose exec mysql mysql -u testuser -ppassword testdb"

else
    # Execute any other command
    exec "$@"
fi
