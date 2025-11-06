FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

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

RUN cmake /mysql-source \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local/mysql \
    -DDOWNLOAD_BOOST=1 \
    -DWITH_BOOST=/tmp/boost \
    -DWITH_UNIT_TESTS=OFF \
    -DENABLED_LOCAL_INFILE=1 \
    -DMYSQL_DATADIR=/var/lib/mysql \
    -DSYSCONFDIR=/etc/mysql \
    -DWITH_SSL=system \
    -G Ninja

RUN ninja -j$(nproc) && ninja install

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
    && rm -rf /var/lib/apt/lists/*

RUN groupadd -r mysql && useradd -r -g mysql mysql

RUN mkdir -p /var/lib/mysql /var/run/mysqld \
    && chown -R mysql:mysql /var/lib/mysql /var/run/mysqld

COPY --from=builder /usr/local/mysql /usr/local/mysql

ENV PATH=$PATH:/usr/local/mysql/bin

EXPOSE 3306

# Create entrypoint script for database initialization
RUN echo '#!/bin/bash\n\
set -e\n\
if [ ! -d "$MYSQL_DATADIR/mysql" ]; then\n\
    echo "Initializing MySQL database..."\n\
    mysqld --initialize-insecure --user=mysql --datadir=$MYSQL_DATADIR\n\
    chown -R mysql:mysql $MYSQL_DATADIR\n\
fi\n\
exec mysqld --user=mysql --datadir=$MYSQL_DATADIR\n\
' > /docker-entrypoint.sh && chmod +x /docker-entrypoint.sh

ENV MYSQL_DATADIR=/var/lib/mysql

ENTRYPOINT ["/docker-entrypoint.sh"]
