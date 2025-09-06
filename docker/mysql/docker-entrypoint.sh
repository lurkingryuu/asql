#!/bin/bash
set -e

# MySQL Docker Entrypoint Script for Authorization Plugin Support
# =============================================================================

# Set proper permissions when running as root
if [ "$(id -u)" = "0" ]; then
  chown -R mysql:mysql /var/run/mysqld || true
  chown -R mysql:mysql /var/lib/mysql-data || true
  chown -R mysql:mysql /var/log/mysql || true
fi

# Initialize MySQL data directory if it doesn't exist or is incomplete
DATADIR="/var/lib/mysql-data"

needs_init=false
if [ ! -d "$DATADIR/mysql" ]; then
  needs_init=true
elif [ ! -f "$DATADIR/mysql.ibd" ]; then
  # Likely incomplete/corrupt init or leftover empty volume
  needs_init=true
fi

if [ "$needs_init" = true ]; then
  echo "Initializing MySQL data directory..."
  # If directory is non-empty but missing the mysql schema dir, either reinit (if allowed) or abort with instructions
  if [ -d "$DATADIR" ] && [ "$(ls -A "$DATADIR" 2>/dev/null)" ]; then
    if [ "${AUTO_REINIT:-0}" = "1" ]; then
      echo "AUTO_REINIT=1 detected; purging $DATADIR for clean initialization"
      # Remove contents but keep the directory (tolerate read-only/system entries)
      if [ "$(id -u)" = "0" ]; then
        find "$DATADIR" -mindepth 1 -maxdepth 1 -exec rm -rf {} + || true
      else
        # Fallback for non-root
        rm -rf "$DATADIR"/* "$DATADIR"/.[!.]* "$DATADIR"/..?* 2>/dev/null || true
      fi
    else
      echo "Data directory $DATADIR exists but is missing the 'mysql' directory." >&2
      echo "To reset: docker compose down -v && docker compose up --build" >&2
      echo "Alternatively, set AUTO_REINIT=1 to auto-purge the data dir on next start." >&2
      exit 1
    fi
  fi
  mysqld --initialize-insecure --user=mysql --datadir="$DATADIR"
fi

# Set environment variables for plugins
export MYSQL_PLUGIN_DIR="/usr/local/mysql/lib/plugin"

# Handle different command modes
if [ "$1" = 'mysqld' ]; then
    # Start MySQL server
    echo "Starting MySQL server..."

    # Set ownership for data directory
    chown -R mysql:mysql /var/lib/mysql-data

    # Stream error log to container stdout for easier debugging
    touch /var/log/mysql/error.log || true
    # Follow the error log in background and send to stdout
    tail -n0 -F /var/log/mysql/error.log &

    # Execute MySQL server
    shift
    exec mysqld --user=mysql --datadir=/var/lib/mysql-data --socket=/var/run/mysqld/mysqld.sock --console "$@"
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
        SELECT PLUGIN_STATUS FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME LIKE '%authorization%';
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
