FROM ubuntu:20.04

# Install the required packages
ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Kolkata
RUN apt-get update
RUN apt-get install build-essential software-properties-common cmake \
libboost-all-dev libssl-dev libncurses5-dev libudev-dev libtirpc-dev bison flex pkg-config wget git -y

# Set the working directory
WORKDIR /app

# Copy files to the working directory
COPY . .

# Download the boost library
RUN wget https://archives.boost.io/release/1.73.0/source/boost_1_73_0.tar.gz
RUN tar -xvzf boost_1_73_0.tar.gz

# Build the mysql-server
RUN mkdir build

WORKDIR /app/build
RUN cmake .. -DWITH_BOOST=/app/boost_1_73_0 -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl/
RUN make -j4
RUN make install

# Remove the build directory
WORKDIR /app
RUN rm -rf build

# Post installation steps
WORKDIR /usr/local/mysql
RUN mkdir mysql-files
RUN groupadd mysql
RUN useradd -r -g mysql -s /bin/false mysql
RUN chown -R mysql:mysql mysql-files
RUN bin/mysqld --initialize-insecure --user=mysql

# Start the mysql server
# CMD ["/usr/local/mysql/bin/mysql", "-u root", "--skip-password"]
ENTRYPOINT ["/usr/local/mysql/bin/mysqld_safe", "--user=mysql"]

