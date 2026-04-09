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

# ---- Build libcedar (Rust → C ABI) -----------------------------------------
# libcedar.a is statically linked into embedded_cedar.so at MySQL build time.
# We clone from GitHub and build it here so the MySQL cmake step can find
# libcedar.a and libcedar.h.  No runtime dependency on Rust after this stage.
ARG LIBCEDAR_REPO=https://github.com/lurkingryuu/libcedar.git
ARG LIBCEDAR_BRANCH=main

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
      | sh -s -- -y --default-toolchain stable --profile minimal

ENV PATH="/root/.cargo/bin:${PATH}"

RUN git clone --depth 1 --branch "${LIBCEDAR_BRANCH}" "${LIBCEDAR_REPO}" /libcedar && \
    cd /libcedar && cargo build --release

# ---- End libcedar build -----------------------------------------------------

RUN mkdir -p /tmp/boost /mysql-build

# Pre-download Boost to avoid timeout issues during cmake
RUN wget -q -O /tmp/boost.tar.bz2 https://archives.boost.io/release/1.77.0/source/boost_1_77_0.tar.bz2 && \
    tar -xjf /tmp/boost.tar.bz2 -C /tmp/boost --strip-components=1

WORKDIR /mysql-source
# Copy build config and source directories explicitly to protect the compilation cache.
# We must copy directories to their respective names to preserve the project structure.
COPY CMakeLists.txt MYSQL_VERSION *.cmake *.in *.h.cmake README INSTALL LICENSE ./
COPY sql/ sql/
COPY include/ include/
COPY storage/ storage/
COPY cmake/ cmake/
COPY libmysql/ libmysql/
COPY mysys/ mysys/
COPY extra/ extra/
COPY strings/ strings/
COPY vio/ vio/
COPY components/ components/
COPY plugin/ plugin/
COPY share/ share/
COPY libservices/ libservices/
COPY libbinlogevents/ libbinlogevents/
COPY libbinlogstandalone/ libbinlogstandalone/
COPY libchangestreams/ libchangestreams/
COPY sql-common/ sql-common/
COPY utilities/ utilities/
COPY client/ client/
COPY router/ router/
COPY packaging/ packaging/
COPY man/ man/
COPY support-files/ support-files/
COPY testclients/ testclients/
COPY scripts/ scripts/
COPY mysql-test/ mysql-test/
COPY unittest/ unittest/
COPY doxygen_resources/ doxygen_resources/

WORKDIR /mysql-build

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
    -DLIBCEDAR_DIR=/libcedar \
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
