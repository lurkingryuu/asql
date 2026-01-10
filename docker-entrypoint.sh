#!/bin/bash
set -e

# Initialize database if data directory is empty
if [ ! -d "$MYSQL_DATADIR/mysql" ]; then
    echo "Initializing MySQL database..."
    
    # Clean up any leftover files from failed initialization attempts
    # mysqld --initialize requires a completely empty directory
    # If the 'mysql' directory is missing, any other files are likely junk
    # from a previous failed attempt.
    echo "Cleaning up data directory for initialization..."
    find "$MYSQL_DATADIR" -mindepth 1 -delete 2>/dev/null || true
    
    # Ensure the data directory is owned by the mysql user BEFORE initialization
    chown -R mysql:mysql $MYSQL_DATADIR

    # IMPORTANT: Do NOT write any files into $MYSQL_DATADIR before mysqld --initialize,
    # otherwise initialization will fail with:
    #   "--initialize specified but the data directory has files in it"
    INIT_LOG="/tmp/mysql-init.log"
    rm -f "$INIT_LOG" 2>/dev/null || true
    
    # Determine initialization method based on root password
    if [ -n "$MYSQL_ROOT_PASSWORD" ]; then
        echo "Initializing with root password..."
        # Force logging to a file outside the datadir so we can extract the temporary password
        if ! mysqld --initialize --user=mysql --datadir=$MYSQL_DATADIR > "$INIT_LOG" 2>&1; then
            echo "ERROR: MySQL initialization failed. Logs:"
            cat "$INIT_LOG" || true
            exit 1
        fi
        INIT_WITH_PASSWORD=1
    else
        echo "Initializing without root password (insecure)..."
        if ! mysqld --initialize-insecure --user=mysql --datadir=$MYSQL_DATADIR > "$INIT_LOG" 2>&1; then
            echo "ERROR: MySQL initialization failed. Logs:"
            cat "$INIT_LOG" || true
            exit 1
        fi
        INIT_WITH_PASSWORD=0
    fi
    
    # Start MySQL temporarily to set root password and create database/user
    echo "Starting MySQL for initial setup..."
    mysqld --user=mysql --datadir=$MYSQL_DATADIR --skip-networking &
    MYSQL_PID=$!
    
    # Wait for MySQL to be ready
    echo "Waiting for MySQL to be ready for configuration..."
    for i in {60..0}; do
        if mysqladmin -u root ping >/dev/null 2>&1 || \
           mysql -u root -e "SELECT 1" 2>&1 | grep -q "Access denied"; then
            echo "✓ MySQL is up"
            break
        fi
        if [ "$i" -eq 0 ]; then
            echo "Error: MySQL failed to start for setup" >&2
            exit 1
        fi
        sleep 1
    done
    
    # Set root password if provided
    if [ -n "$MYSQL_ROOT_PASSWORD" ]; then
        if [ "$INIT_WITH_PASSWORD" -eq 1 ]; then
            echo "MySQL 8.0 initialization: fixing authentication..."
            
            # Extract temporary password reliably from the init log
            # Use sync to ensure logs are written to disk
            sync
            TEMP_PASS=$(grep "temporary password" "$INIT_LOG" 2>/dev/null | awk -F': ' '{print $NF}' | tr -d '[:space:]' | tail -1)
            
            if [ -n "$TEMP_PASS" ]; then
                echo "Applying fix with temporary password (length: ${#TEMP_PASS})..."
                # Put ALTER USER first because MySQL 8.0 requires it before any other command (Error 1820)
                MYSQL_PWD="$TEMP_PASS" mysql --connect-expired-password -uroot <<EOF
ALTER USER 'root'@'localhost' IDENTIFIED BY '$MYSQL_ROOT_PASSWORD';
FLUSH PRIVILEGES;
CREATE USER IF NOT EXISTS 'root'@'%' IDENTIFIED BY '$MYSQL_ROOT_PASSWORD';
GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' WITH GRANT OPTION;
FLUSH PRIVILEGES;
EOF
                if [ $? -eq 0 ]; then
                    echo "✓ Authentication fixed successfully"
                else
                    echo "⚠️  Authentication fix failed"
                fi
            else
                echo "⚠️  Temporary password not found in init.log. Logs:"
                cat "$INIT_LOG" || true
                # Fallback: maybe it's already set or doesn't need one?
                mysql -uroot -e "ALTER USER 'root'@'localhost' IDENTIFIED BY '$MYSQL_ROOT_PASSWORD'; CREATE USER IF NOT EXISTS 'root'@'%' IDENTIFIED BY '$MYSQL_ROOT_PASSWORD'; GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' WITH GRANT OPTION; FLUSH PRIVILEGES;" 2>/dev/null && echo "✓ Set password without temp password"
            fi
        fi
        echo "Authentication setup complete"
    fi
    
    # Create database if specified
    if [ -n "$MYSQL_DATABASE" ]; then
        echo "Creating database: $MYSQL_DATABASE"
        if [ -n "$MYSQL_ROOT_PASSWORD" ]; then
            mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "CREATE DATABASE IF NOT EXISTS \`$MYSQL_DATABASE\`;" 2>/dev/null || true
        else
            mysql -uroot -e "CREATE DATABASE IF NOT EXISTS \`$MYSQL_DATABASE\`;" 2>/dev/null || true
        fi
    fi
    
    # Create user if specified
    if [ -n "$MYSQL_USER" ] && [ -n "$MYSQL_PASSWORD" ]; then
        echo "Creating user: $MYSQL_USER"
        if [ -n "$MYSQL_ROOT_PASSWORD" ]; then
            mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "CREATE USER IF NOT EXISTS '$MYSQL_USER'@'%' IDENTIFIED BY '$MYSQL_PASSWORD';" 2>/dev/null || true
            if [ -n "$MYSQL_DATABASE" ]; then
                mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "GRANT ALL PRIVILEGES ON \`$MYSQL_DATABASE\`.* TO '$MYSQL_USER'@'%';" 2>/dev/null || true
            else
                mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "GRANT ALL PRIVILEGES ON *.* TO '$MYSQL_USER'@'%';" 2>/dev/null || true
            fi
            mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "FLUSH PRIVILEGES;" 2>/dev/null || true
        else
            mysql -uroot -e "CREATE USER IF NOT EXISTS '$MYSQL_USER'@'%' IDENTIFIED BY '$MYSQL_PASSWORD';" 2>/dev/null || true
            if [ -n "$MYSQL_DATABASE" ]; then
                mysql -uroot -e "GRANT ALL PRIVILEGES ON \`$MYSQL_DATABASE\`.* TO '$MYSQL_USER'@'%';" 2>/dev/null || true
            else
                mysql -uroot -e "GRANT ALL PRIVILEGES ON *.* TO '$MYSQL_USER'@'%';" 2>/dev/null || true
            fi
            mysql -uroot -e "FLUSH PRIVILEGES;" 2>/dev/null || true
        fi
    fi
    
    # Stop temporary MySQL instance
    echo "Stopping temporary MySQL instance..."
    kill $MYSQL_PID 2>/dev/null || true
    wait $MYSQL_PID 2>/dev/null || true
    
    echo "Initialization complete!"
fi

# Start MySQL server
# Use any additional command line arguments passed to the container
exec mysqld --user=mysql --datadir=$MYSQL_DATADIR "$@"
