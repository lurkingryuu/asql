#!/bin/bash
set -e

# Initialize database if data directory is empty
if [ ! -d "$MYSQL_DATADIR/mysql" ]; then
    echo "Initializing MySQL database..."
    
    # Determine initialization method based on root password
    if [ -n "$MYSQL_ROOT_PASSWORD" ]; then
        echo "Initializing with root password..."
        mysqld --initialize --user=mysql --datadir=$MYSQL_DATADIR
        INIT_WITH_PASSWORD=1
    else
        echo "Initializing without root password (insecure)..."
        mysqld --initialize-insecure --user=mysql --datadir=$MYSQL_DATADIR
        INIT_WITH_PASSWORD=0
    fi
    
    chown -R mysql:mysql $MYSQL_DATADIR
    
    # Start MySQL temporarily to set root password and create database/user
    echo "Starting MySQL for initial setup..."
    mysqld --user=mysql --datadir=$MYSQL_DATADIR --skip-networking &
    MYSQL_PID=$!
    
    # Wait for MySQL to be ready
    for i in {30..0}; do
        if mysqladmin ping --silent; then
            break
        fi
        echo "Waiting for MySQL to start... ($i)"
        sleep 1
    done
    
    if [ $i -eq 0 ]; then
        echo "MySQL failed to start" >&2
        exit 1
    fi
    
    # Set root password if provided
    if [ -n "$MYSQL_ROOT_PASSWORD" ]; then
        if [ "$INIT_WITH_PASSWORD" -eq 1 ]; then
            echo "MySQL 8.0 initialization: fixing authentication..."
            
            # Extract temporary password reliably
            TEMP_PASS=$(grep -h "temporary password" $MYSQL_DATADIR/*.log $MYSQL_DATADIR/*.err 2>/dev/null | sed -n 's/.*root@localhost: //p' | tail -1)
            
            if [ -n "$TEMP_PASS" ]; then
                echo "Applying fix with temporary password..."
                # Use the method that worked: FLUSH PRIVILEGES first, then ALTER
                # We use MYSQL_PWD to avoid shell injection issues with special characters
                MYSQL_PWD="$TEMP_PASS" mysql --connect-expired-password -uroot <<EOF
FLUSH PRIVILEGES;
ALTER USER 'root'@'localhost' IDENTIFIED BY '$MYSQL_ROOT_PASSWORD';
CREATE USER IF NOT EXISTS 'root'@'%' IDENTIFIED BY '$MYSQL_ROOT_PASSWORD';
GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' WITH GRANT OPTION;
FLUSH PRIVILEGES;
EOF
                echo "✓ Authentication fixed successfully"
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
