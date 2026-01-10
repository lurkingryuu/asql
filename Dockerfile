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
RUN echo '#!/bin/bash\n\
set -e\n\
\n\
# Initialize database if data directory is empty\n\
if [ ! -d "$MYSQL_DATADIR/mysql" ]; then\n\
    echo "Initializing MySQL database..."\n\
    \n\
    # Determine initialization method based on root password\n\
    if [ -n "$MYSQL_ROOT_PASSWORD" ]; then\n\
        echo "Initializing with root password..."\n\
        mysqld --initialize --user=mysql --datadir=$MYSQL_DATADIR\n\
        INIT_WITH_PASSWORD=1\n\
    else\n\
        echo "Initializing without root password (insecure)..."\n\
        mysqld --initialize-insecure --user=mysql --datadir=$MYSQL_DATADIR\n\
        INIT_WITH_PASSWORD=0\n\
    fi\n\
    \n\
    chown -R mysql:mysql $MYSQL_DATADIR\n\
    \n\
    # Start MySQL temporarily to set root password and create database/user\n\
    echo "Starting MySQL for initial setup..."\n\
    mysqld --user=mysql --datadir=$MYSQL_DATADIR --skip-networking &\n\
    MYSQL_PID=$!\n\
    \n\
    # Wait for MySQL to be ready\n\
    for i in {30..0}; do\n\
        if mysqladmin ping --silent; then\n\
            break\n\
        fi\n\
        echo "Waiting for MySQL to start... ($i)"\n\
        sleep 1\n\
    done\n\
    \n\
    if [ $i -eq 0 ]; then\n\
        echo "MySQL failed to start" >&2\n\
        exit 1\n\
    fi\n\
    \n\
    # Set root password if provided\n\
    if [ -n "$MYSQL_ROOT_PASSWORD" ]; then\n\
            # For MySQL 8.0, we need to handle the temporary password more carefully\n\
        if [ "$INIT_WITH_PASSWORD" -eq 1 ]; then\n\
            echo "MySQL 8.0 initialization: fixing authentication..."\n\
            \n\
            # Extract temporary password reliably\n\
            TEMP_PASS=$(grep -h "temporary password" $MYSQL_DATADIR/*.log $MYSQL_DATADIR/*.err 2>/dev/null | sed -n '\''s/.*root@localhost: //p'\'' | tail -1)\n\
            \n\
            if [ -n "$TEMP_PASS" ]; then\n\
                echo "Applying fix with temporary password..."\n\
                # Use the method that worked: FLUSH PRIVILEGES first, then ALTER\n\
                # We use MYSQL_PWD to avoid shell injection issues with special characters\n\
                MYSQL_PWD="$TEMP_PASS" mysql --connect-expired-password -uroot <<EOF\n\
FLUSH PRIVILEGES;\n\
ALTER USER '\''root'\''@'\''localhost'\'' IDENTIFIED BY '\''$MYSQL_ROOT_PASSWORD'\'';\n\
CREATE USER IF NOT EXISTS '\''root'\''@'\''%'\'' IDENTIFIED BY '\''$MYSQL_ROOT_PASSWORD'\'';\n\
GRANT ALL PRIVILEGES ON *.* TO '\''root'\''@'\''%'\'' WITH GRANT OPTION;\n\
FLUSH PRIVILEGES;\n\
EOF\n\
                echo "✓ Authentication fixed successfully"\n\
            fi\n\
        fi\n\
        \n\
        # Create root@'\''%'\'' user\n\
        echo "Authentication setup complete"
    fi\n\
    \n\
    # Create database if specified\n\
    if [ -n "$MYSQL_DATABASE" ]; then\n\
        echo "Creating database: $MYSQL_DATABASE"\n\
        if [ -n "$MYSQL_ROOT_PASSWORD" ]; then\n\
            mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "CREATE DATABASE IF NOT EXISTS \`$MYSQL_DATABASE\`;" 2>/dev/null || true\n\
        else\n\
            mysql -uroot -e "CREATE DATABASE IF NOT EXISTS \`$MYSQL_DATABASE\`;" 2>/dev/null || true\n\
        fi\n\
    fi\n\
    \n\
    # Create user if specified\n\
    if [ -n "$MYSQL_USER" ] && [ -n "$MYSQL_PASSWORD" ]; then\n\
        echo "Creating user: $MYSQL_USER"\n\
        if [ -n "$MYSQL_ROOT_PASSWORD" ]; then\n\
            mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "CREATE USER IF NOT EXISTS '\''$MYSQL_USER'\''@'\''%'\'' IDENTIFIED BY '\''$MYSQL_PASSWORD'\'';" 2>/dev/null || true\n\
            if [ -n "$MYSQL_DATABASE" ]; then\n\
                mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "GRANT ALL PRIVILEGES ON \`$MYSQL_DATABASE\`.* TO '\''$MYSQL_USER'\''@'\''%'\'';" 2>/dev/null || true\n\
            else\n\
                mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "GRANT ALL PRIVILEGES ON *.* TO '\''$MYSQL_USER'\''@'\''%'\'';" 2>/dev/null || true\n\
            fi\n\
            mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "FLUSH PRIVILEGES;" 2>/dev/null || true\n\
        else\n\
            mysql -uroot -e "CREATE USER IF NOT EXISTS '\''$MYSQL_USER'\''@'\''%'\'' IDENTIFIED BY '\''$MYSQL_PASSWORD'\'';" 2>/dev/null || true\n\
            if [ -n "$MYSQL_DATABASE" ]; then\n\
                mysql -uroot -e "GRANT ALL PRIVILEGES ON \`$MYSQL_DATABASE\`.* TO '\''$MYSQL_USER'\''@'\''%'\'';" 2>/dev/null || true\n\
            else\n\
                mysql -uroot -e "GRANT ALL PRIVILEGES ON *.* TO '\''$MYSQL_USER'\''@'\''%'\'';" 2>/dev/null || true\n\
            fi\n\
            mysql -uroot -e "FLUSH PRIVILEGES;" 2>/dev/null || true\n\
        fi\n\
    fi\n\
    \n\
    # Stop temporary MySQL instance\n\
    echo "Stopping temporary MySQL instance..."\n\
    kill $MYSQL_PID 2>/dev/null || true\n\
    wait $MYSQL_PID 2>/dev/null || true\n\
    \n\
    echo "Initialization complete!"\n\
fi\n\
\n\
# Start MySQL server\n\
# Use any additional command line arguments passed to the container\n\
exec mysqld --user=mysql --datadir=$MYSQL_DATADIR "$@"\n\
' > /docker-entrypoint.sh && chmod +x /docker-entrypoint.sh

ENV MYSQL_DATADIR=/var/lib/mysql

ENTRYPOINT ["/docker-entrypoint.sh"]
