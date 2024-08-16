#!/bin/sh
# check if the user is root
if [ $(id -u) -ne 0 ]; then
    echo "This script must be run as root"
    exit 1
fi

# Download and install the required packages
sudo apt install build-essential software-properties-common cmake \
 libboost-all-dev libssl-dev libncurses5-dev libudev-dev libtirpc-dev bison flex pkg-config wget git -y

# # Download the source code and build the project
# git clone https://github.com/lurkingryuu/asql

# Download boost
wget https://archives.boost.io/release/1.73.0/source/boost_1_73_0.tar.gz

# Extract the boost tarball
tar -xvzf boost_1_73_0.tar.gz

# Build asql
cd asql
mkdir build
cd build
cmake .. -DWITH_BOOST=$HOME/boost_1_73_0 -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl/ -DOPENSSL_CRYPTO_LIBRARY=/usr/local/opt/openssl/lib/
make -j4
sudo make install

# Add the asql binary to the PATH
echo "export PATH=$PATH:/usr/local/mysql/bin" >> ~/.bashrc

# Source the bashrc file
source ~/.bashrc

# Post installation steps
cd /usr/local/mysql/
sudo mkdir mysql-files

sudo groupadd mysql
sudo useradd -r -g mysql -s /bin/false mysql
sudo chown -R mysql:mysql mysql-files

sudo bin/mysqld --initialize-insecure '--user=mysql'
sudo bin/mysqld_safe --user=mysql &
mysql -u root --skip-password