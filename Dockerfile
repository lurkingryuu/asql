FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Build argument for parallel jobs (defaults to all available CPUs)
# If not provided, will use nproc in the RUN command
ARG PARALLEL_JOBS

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    flex \
    bison \
    pkg-config \
    doxygen \
    libkrb5-dev \
    libncurses5-dev \
    libssl-dev \
    libsasl2-dev \
    libaio-dev \
    libnuma-dev \
    libtirpc-dev \
    libldap2-dev \
    libedit-dev \
    libudev-dev \
    libcurl4-openssl-dev \
    libjsoncpp-dev \
    wget \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /mysql-source
COPY . .

RUN mkdir -p /tmp/boost /mysql-build
WORKDIR /mysql-build

# Pre-download Boost to avoid timeout issues during cmake
RUN wget -q -O /tmp/boost.tar.bz2 https://archives.boost.io/release/1.77.0/source/boost_1_77_0.tar.bz2 && \
    tar -xjf /tmp/boost.tar.bz2 -C /tmp/boost --strip-components=1

RUN cmake /mysql-source \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local/mysql \
    -DCMAKE_C_FLAGS="-O3 -march=native -mtune=native" \
    -DCMAKE_CXX_FLAGS="-O3 -march=native -mtune=native" \
    -DDOWNLOAD_BOOST=0 \
    -DWITH_BOOST=/tmp/boost \
    -DWITH_UNIT_TESTS=OFF \
    -DWITH_DEBUG=OFF \
    -DENABLED_LOCAL_INFILE=1 \
    -DMYSQL_DATADIR=/var/lib/mysql \
    -DSYSCONFDIR=/etc/mysql \
    -DWITH_SSL=system \
    -G Ninja

RUN if [ -n "${PARALLEL_JOBS}" ]; then \
        ninja -j${PARALLEL_JOBS} && ninja install; \
    else \
        ninja -j$(nproc) && ninja install; \
    fi

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libssl3 \
    libncurses6 \
    libaio1 \
    libnuma1 \
    libtirpc3 \
    libldap-2.5-0 \
    libkrb5-3 \
    libedit2 \
    curl \
    libcurl4-openssl-dev \
    libjsoncpp-dev \
    && rm -rf /var/lib/apt/lists/* \
    && ldconfig \
    && echo "Verifying required libraries are available:" \
    && ldconfig -p | grep -q libcurl.so.4 && echo "✓ libcurl.so.4 found" \
    && ldconfig -p | grep -q libjsoncpp && echo "✓ libjsoncpp found"

RUN groupadd -r mysql && useradd -r -g mysql mysql

RUN mkdir -p /var/lib/mysql /var/run/mysqld \
    && chown -R mysql:mysql /var/lib/mysql /var/run/mysqld

COPY --from=builder /usr/local/mysql /usr/local/mysql

ENV PATH=$PATH:/usr/local/mysql/bin

EXPOSE 3306

# Create entrypoint script for database initialization
RUN echo '#!/bin/bash
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
            TEMP_PASS=$(grep -h "temporary password" $MYSQL_DATADIR/*.log $MYSQL_DATADIR/*.err 2>/dev/null | sed -n '\''s/.*root@localhost: //p'\'' | tail -1)
            
            if [ -n "$TEMP_PASS" ]; then
                echo "Applying fix with temporary password..."
                # Use the method that worked: FLUSH PRIVILEGES first, then ALTER
                # We use MYSQL_PWD to avoid shell injection issues with special characters
                MYSQL_PWD="$TEMP_PASS" mysql --connect-expired-password -uroot <<EOF
FLUSH PRIVILEGES;
ALTER USER '\''root'\''@'\''localhost'\'' IDENTIFIED BY '\''$MYSQL_ROOT_PASSWORD'\'';
CREATE USER IF NOT EXISTS '\''root'\''@'\''%'\'' IDENTIFIED BY '\''$MYSQL_ROOT_PASSWORD'\'';
GRANT ALL PRIVILEGES ON *.* TO '\''root'\''@'\''%'\'' WITH GRANT OPTION;
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
            mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "CREATE USER IF NOT EXISTS '\''$MYSQL_USER'\''@'\''%'\'' IDENTIFIED BY '\''$MYSQL_PASSWORD'\'';" 2>/dev/null || true
            if [ -n "$MYSQL_DATABASE" ]; then
                mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "GRANT ALL PRIVILEGES ON \`$MYSQL_DATABASE\`.* TO '\''$MYSQL_USER'\''@'\''%'\'';" 2>/dev/null || true
            else
                mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "GRANT ALL PRIVILEGES ON *.* TO '\''$MYSQL_USER'\''@'\''%'\'';" 2>/dev/null || true
            fi
            mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "FLUSH PRIVILEGES;" 2>/dev/null || true
        else
            mysql -uroot -e "CREATE USER IF NOT EXISTS '\''$MYSQL_USER'\''@'\''%'\'' IDENTIFIED BY '\''$MYSQL_PASSWORD'\'';" 2>/dev/null || true
            if [ -n "$MYSQL_DATABASE" ]; then
                mysql -uroot -e "GRANT ALL PRIVILEGES ON \`$MYSQL_DATABASE\`.* TO '\''$MYSQL_USER'\''@'\''%'\'';" 2>/dev/null || true
            else
                mysql -uroot -e "GRANT ALL PRIVILEGES ON *.* TO '\''$MYSQL_USER'\''@'\''%'\'';" 2>/dev/null || true
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
' > /docker-entrypoint.sh && chmod +x /docker-entrypoint.sh

ENV MYSQL_DATADIR=/var/lib/mysql

ENTRYPOINT ["/docker-entrypoint.sh"]
