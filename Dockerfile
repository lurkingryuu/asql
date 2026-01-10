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
# Copy build config and source directories explicitly. 
# This ensures that changes to 'docker-entrypoint.sh' do NOT trigger a full rebuild.
COPY CMakeLists.txt MYSQL_VERSION *.cmake ./
COPY sql/ include/ storage/ cmake/ libmysql/ mysys/ extra/ strings/ vio/ \
     components/ plugin/ share/ libservices/ libbinlogevents/ \
     libbinlogstandalone/ libchangestreams/ sql-common/ utilities/ \
     client/ router/ packaging/ man/ support-files/ ./

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

ENV MYSQL_DATADIR=/var/lib/mysql

# Copy entrypoint script at the very end to optimize build cache
COPY docker-entrypoint.sh /docker-entrypoint.sh
RUN chmod +x /docker-entrypoint.sh

ENTRYPOINT ["/docker-entrypoint.sh"]
