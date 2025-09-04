# Building MySQL with Authorization Plugin Support

This guide explains how to build MySQL with the new authorization plugin functionality and compile the example plugins.

## Prerequisites

### System Requirements
- Linux, macOS, or Windows (with MinGW/MSYS2)
- GCC 5.3+ or Clang 3.4+ or MSVC 2019+
- CMake 3.5+
- Git

### Dependencies
- Boost 1.73.0+
- OpenSSL 1.1.1+
- libcurl (for external authorization plugin)
- jsoncpp (for external authorization plugin)

## Building MySQL Server with Authorization Plugin Support

### 1. Get MySQL Source Code

```bash
# Clone MySQL source code
git clone https://github.com/mysql/mysql-server.git
cd mysql-server

# Checkout latest stable version
git checkout 8.0
```

### 2. Apply Authorization Plugin Changes

Copy the authorization plugin files to the MySQL source tree:

```bash
# Copy plugin headers
cp include/mysql/plugin_authorization.h mysql-server/include/mysql/

# Copy plugin management code
cp sql/sql_authorization_plugin.h mysql-server/sql/
cp sql/sql_authorization_plugin.cc mysql-server/sql/

# Copy example plugins
mkdir -p mysql-server/plugin/authorization
cp plugin/authorization/* mysql-server/plugin/authorization/
```

### 3. Update MySQL Build Configuration

The following files need to be modified to include authorization plugin support:

**include/mysql/plugin.h:**
```diff
#define MYSQL_KEYRING_PLUGIN 10           /* The Keyring plugin type   */
#define MYSQL_CLONE_PLUGIN 11             /* The Clone plugin type   */
+#define MYSQL_AUTHORIZATION_PLUGIN 12     /* The Authorization plugin type */
-#define MYSQL_MAX_PLUGIN_TYPE_NUM 12      /* The number of plugin types   */
+#define MYSQL_MAX_PLUGIN_TYPE_NUM 13      /* The number of plugin types   */
```

**sql/sql_plugin.cc:**
```diff
const LEX_CSTRING plugin_type_names[MYSQL_MAX_PLUGIN_TYPE_NUM] = {
    // ... existing entries ...
    {STRING_WITH_LEN("KEYRING")},
    {STRING_WITH_LEN("CLONE")},
+   {STRING_WITH_LEN("AUTHORIZATION")}};
```

Update the plugin initialization arrays and include the new header files as shown in the main implementation.

### 4. Configure Build

```bash
mkdir build && cd build

# Basic configuration
cmake .. \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DWITH_BOOST=/path/to/boost \
  -DWITH_SSL=system \
  -DMYSQL_DATADIR=/usr/local/mysql/data \
  -DSYSCONFDIR=/etc \
  -DWITH_AUTHORIZATION_PLUGIN=ON

# For development with debug symbols
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWITH_BOOST=/path/to/boost \
  -DWITH_SSL=system \
  -DWITH_DEBUG=ON \
  -DWITH_AUTHORIZATION_PLUGIN=ON
```

### 5. Build MySQL

```bash
# Build with parallel jobs (adjust -j based on CPU cores)
make -j$(nproc)

# Install (optional, for system-wide installation)
sudo make install
```

### 6. Build Authorization Plugins

The authorization plugins are built as part of the main MySQL build if the authorization plugin directory is present:

```bash
# Simple authorization plugin
ls build/plugin/authorization/simple_authorization.so

# External authorization plugin (if dependencies are available)
ls build/plugin/authorization/external_authorization.so
```

## Manual Plugin Compilation

If you want to build plugins separately from the main MySQL build:

### Simple Authorization Plugin

```bash
# Set variables
MYSQL_INCLUDE="/usr/local/mysql/include"
PLUGIN_DIR="/usr/local/mysql/lib/plugin"

# Compile simple authorization plugin
gcc -shared -fPIC \
  -I${MYSQL_INCLUDE} \
  -o simple_authorization.so \
  simple_authorization.cc

# Install plugin
cp simple_authorization.so ${PLUGIN_DIR}/
```

### External Authorization Plugin

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install libcurl4-openssl-dev libjsoncpp-dev

# Compile external authorization plugin
gcc -shared -fPIC \
  -I${MYSQL_INCLUDE} \
  -lcurl -ljsoncpp \
  -o external_authorization.so \
  external_authorization.cc

# Install plugin
cp external_authorization.so ${PLUGIN_DIR}/
```

## Platform-Specific Instructions

### Linux (Ubuntu/Debian)

```bash
# Install build dependencies
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git \
  libboost-all-dev \
  libssl-dev \
  libncurses5-dev \
  libcurl4-openssl-dev \
  libjsoncpp-dev \
  pkg-config

# Build MySQL with authorization plugins
# (follow general build instructions above)
```

### Linux (CentOS/RHEL/Fedora)

```bash
# Install build dependencies
sudo yum install -y \
  gcc-c++ cmake git \
  boost-devel \
  openssl-devel \
  ncurses-devel \
  libcurl-devel \
  jsoncpp-devel \
  pkgconfig

# Or for newer versions:
sudo dnf install -y \
  gcc-c++ cmake git \
  boost-devel \
  openssl-devel \
  ncurses-devel \
  libcurl-devel \
  jsoncpp-devel \
  pkgconfig
```

### macOS

```bash
# Install dependencies via Homebrew
brew install cmake boost openssl curl jsoncpp

# Build with specific paths
cmake .. \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DWITH_BOOST=$(brew --prefix boost) \
  -DOPENSSL_ROOT_DIR=$(brew --prefix openssl) \
  -DWITH_AUTHORIZATION_PLUGIN=ON

make -j$(sysctl -n hw.ncpu)
```

### Windows (MSYS2/MinGW)

```bash
# Install MSYS2 and dependencies
pacman -S mingw-w64-x86_64-gcc \
          mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-boost \
          mingw-w64-x86_64-openssl \
          mingw-w64-x86_64-curl \
          mingw-w64-x86_64-jsoncpp

# Build (use MinGW64 shell)
cmake .. -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DWITH_AUTHORIZATION_PLUGIN=ON

mingw32-make -j4
```

## Testing the Build

### 1. Start MySQL Server

```bash
# Initialize data directory (first time only)
./bin/mysqld --initialize-insecure --user=mysql --datadir=/tmp/mysql-data

# Start server
./bin/mysqld --user=mysql --datadir=/tmp/mysql-data --socket=/tmp/mysql.sock
```

### 2. Connect and Test Plugin Loading

```bash
# Connect to server
./bin/mysql -S /tmp/mysql.sock

# Check plugin directory
SHOW VARIABLES LIKE 'plugin_dir';

# Install simple authorization plugin
INSTALL PLUGIN simple_authorization SONAME 'simple_authorization.so';

# Verify plugin installation
SHOW PLUGINS;
SELECT * FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'simple_authorization';
```

### 3. Run Test Suite

```bash
# Run authorization plugin tests
./bin/mysql -S /tmp/mysql.sock < plugin/authorization/test_authorization_plugin.sql

# Check results
tail -f /var/log/mysql/error.log | grep -i authorization
```

## Troubleshooting

### Common Build Issues

**CMake configuration errors:**
```bash
# Clear CMake cache and reconfigure
rm -rf CMakeCache.txt CMakeFiles/
cmake .. [your options]
```

**Missing dependencies:**
```bash
# Check what libraries are missing
ldd build/sql/mysqld | grep "not found"

# Install missing packages
sudo apt-get install [missing-package-dev]
```

**Plugin compilation errors:**
```bash
# Check MySQL include path
find /usr -name "plugin.h" -path "*/mysql/*" 2>/dev/null

# Check plugin directory permissions
ls -la /usr/local/mysql/lib/plugin/
```

### Runtime Issues

**Plugin won't load:**
```sql
-- Check plugin directory and permissions
SHOW VARIABLES LIKE 'plugin_dir';

-- Check error log
SHOW VARIABLES LIKE 'log_error';

-- Try loading with full path
INSTALL PLUGIN simple_authorization SONAME '/full/path/to/simple_authorization.so';
```

**Authorization not working:**
```sql
-- Enable general query log
SET GLOBAL general_log = ON;

-- Check plugin status  
SHOW PLUGINS;

-- Check plugin variables
SHOW VARIABLES LIKE 'simple_auth%';
```

### Performance Optimization

**Debug vs Release builds:**
- Debug builds include extensive logging but are slower
- Release builds optimize for performance
- RelWithDebInfo provides good balance for development

**Compiler optimizations:**
```bash
# For maximum performance
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native"
```

**Plugin optimization:**
- Minimize work in authorization callbacks
- Cache authorization decisions when possible
- Use async patterns for external service calls

## Deployment

### Production Deployment

1. **Build with optimizations:**
   ```bash
   cmake .. -DCMAKE_BUILD_TYPE=Release -DWITH_AUTHORIZATION_PLUGIN=ON
   make -j$(nproc)
   ```

2. **Install MySQL server:**
   ```bash
   sudo make install
   ```

3. **Install plugins:**
   ```bash
   sudo cp build/plugin/authorization/*.so /usr/local/mysql/lib/plugin/
   sudo chown mysql:mysql /usr/local/mysql/lib/plugin/*.so
   sudo chmod 644 /usr/local/mysql/lib/plugin/*.so
   ```

4. **Configure MySQL:**
   ```bash
   # Add to my.cnf
   [mysqld]
   plugin-load-add=simple_authorization.so
   simple_auth_mode=grant
   simple_auth_allow_user=authorized_user
   ```

### Docker Deployment

```dockerfile
FROM mysql:8.0

# Copy custom MySQL binary with authorization plugin support
COPY --from=builder /usr/local/mysql /usr/local/mysql

# Copy authorization plugins
COPY plugins/*.so /usr/local/mysql/lib/plugin/

# Custom configuration
COPY my.cnf /etc/mysql/conf.d/

# Set permissions
USER mysql
```

### Kubernetes Deployment

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: mysql-with-auth-plugin
spec:
  template:
    spec:
      containers:
      - name: mysql
        image: mysql-with-auth-plugin:latest
        env:
        - name: MYSQL_ROOT_PASSWORD
          value: rootpassword
        - name: MYSQL_PLUGIN_LOAD_ADD
          value: simple_authorization.so
        volumeMounts:
        - name: plugin-config
          mountPath: /etc/mysql/conf.d/auth-plugin.cnf
          subPath: auth-plugin.cnf
      volumes:
      - name: plugin-config
        configMap:
          name: mysql-auth-plugin-config
```
