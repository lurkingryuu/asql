# Multi-stage Dockerfile for MySQL with Authorization Plugin Support
# =============================================================================
# Stage 1: Build Environment
# =============================================================================
FROM ubuntu:22.04 AS builder

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV MYSQL_BUILD_DIR=/build/mysql
ENV MYSQL_SOURCE_DIR=/mysql-source
ENV MYSQL_INSTALL_DIR=/usr/local/mysql
ENV MYSQL_BOOST_DIR=/usr/local/boost

# Install build dependencies
RUN apt-get update && apt-get install -y \
    # Basic build tools
    build-essential \
    cmake \
    git \
    ninja-build \
    # MySQL dependencies
    flex \
    bison \
    pkg-config \
    doxygen \
    libkrb5-dev \
    libncurses5-dev \
    libssl-dev \
    libsasl2-dev \
    libboost-all-dev \
    libcurl4-openssl-dev \
    libjsoncpp-dev \
    libaio-dev \
    libnuma-dev \
    libtirpc-dev \
    libldap2-dev \
    libedit-dev \
    libudev-dev \
    # Additional tools
    wget \
    curl \
    lld \
    && rm -rf /var/lib/apt/lists/*

# Create directories
RUN mkdir -p $MYSQL_BUILD_DIR $MYSQL_SOURCE_DIR $MYSQL_INSTALL_DIR

WORKDIR /tmp

# Copy local MySQL source code
WORKDIR $MYSQL_SOURCE_DIR
COPY . .

# Running cmake with: -DWITH_BOOST=/home/karthik/boost_1_77_0 -DWITH_DEBUG=0 -DMYSQL_UNIX_ADDR=/home/karthik/mtp/asql/build/mysql.sock -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DMYSQL_DYNAMIC_PLUGIN=true -DWITH_SSL=system -DCMAKE_LINKER=lld
# Configure and build MySQL
WORKDIR $MYSQL_BUILD_DIR
RUN cmake $MYSQL_SOURCE_DIR \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$MYSQL_INSTALL_DIR \
    -DWITH_BOOST=$MYSQL_BOOST_DIR \
    -DWITH_SSL=system \
    -DDOWNLOAD_BOOST=1 \
    -DWITH_UNIT_TESTS=OFF \
    -DENABLED_LOCAL_INFILE=1 \
    -DMYSQL_DATADIR=/var/lib/mysql-data \
    -DSYSCONFDIR=/etc \
    -DWITH_DEBUG=0 \
    -DMYSQL_UNIX_ADDR=/var/run/mysqld/mysqld.sock \
    -DMYSQL_DYNAMIC_PLUGIN=true \
    -DCMAKE_LINKER=lld \
    -G Ninja

# Build MySQL (this will take a while)
# Take only half of the cores to avoid overloading the system
RUN CORES=$(nproc) && CORES=$((CORES/2)) && ninja -j$CORES && ninja install

# =============================================================================
# Stage 2: Runtime Environment
# =============================================================================
FROM ubuntu:22.04

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV MYSQL_HOME=/usr/local/mysql
ENV PATH=$MYSQL_HOME/bin:$PATH
ENV MYSQL_DATA_DIR=/var/lib/mysql-data
ENV MYSQL_LOG_DIR=/var/log/mysql

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libssl3 \
    libcurl4 \
    libjsoncpp25 \
    libncurses6 \
    libaio1 \
    libnuma1 \
    libtirpc3 \
    libldap-2.5-0 \
    libkrb5-3 \
    libedit2 \
    libboost-system1.74.0 \
    libboost-filesystem1.74.0 \
    libboost-program-options1.74.0 \
    # Additional runtime tools
    curl \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Create MySQL user and directories
RUN groupadd -r mysql && useradd -r -g mysql -s /bin/false mysql && \
    mkdir -p $MYSQL_DATA_DIR $MYSQL_LOG_DIR /etc/mysql && \
    chown -R mysql:mysql $MYSQL_DATA_DIR $MYSQL_LOG_DIR

# Prepare runtime socket directory while still root
RUN mkdir -p /var/run/mysqld && \
    chown -R mysql:mysql /var/run/mysqld

# Copy MySQL installation from builder stage
COPY --from=builder /usr/local/mysql /usr/local/mysql

# Copy configuration files
# COPY --from=builder /mysql-source/docker/mysql/my.cnf /etc/mysql/my.cnf
COPY --from=builder /mysql-source/docker/mysql/docker-entrypoint.sh /usr/local/bin/

# Set proper permissions
RUN chmod +x /usr/local/bin/docker-entrypoint.sh && \
    chown -R mysql:mysql /usr/local/mysql && \
    chown -R mysql:mysql /etc/mysql

# Create symbolic links for MySQL commands
RUN ln -sf /usr/local/mysql/bin/mysql /usr/bin/mysql && \
    ln -sf /usr/local/mysql/bin/mysqld /usr/bin/mysqld && \
    ln -sf /usr/local/mysql/bin/mysqladmin /usr/bin/mysqladmin

# Expose MySQL port
EXPOSE 3306

# Set working directory
WORKDIR $MYSQL_DATA_DIR

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD mysqladmin ping -h localhost --silent
    
RUN mysqld --initialize-insecure --user=mysql --datadir=$MYSQL_DATA_DIR
    
# # Entry point
ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["init", "mysqld"]