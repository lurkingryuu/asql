# MySQL Build and Test Script

This enhanced script automatically builds and tests MySQL with comprehensive dependency management and cross-platform support.

## Features

- **Automatic Dependency Detection & Installation**: Detects missing dependencies and installs them automatically
- **Cross-Platform Support**: Works on Ubuntu/Debian, RHEL/CentOS, Fedora, Arch Linux, and macOS
- **Smart OpenSSL Configuration**: Automatically detects and configures OpenSSL paths for different systems
- **Boost Auto-Installation**: Downloads and builds Boost 1.73.0 if not present
- **Colored Output**: Clear visual feedback with colored status messages
- **Debug Tools**: Built-in OpenSSL debugging for troubleshooting

## Usage

```bash
# Basic usage - installs dependencies and builds MySQL
./build_and_test_mysql

# Skip dependency installation
./build_and_test_mysql --skip-deps

# Only build, skip testing
./build_and_test_mysql --no-test

# Skip cleanup and initialization
./build_and_test_mysql --no-clean

# Debug OpenSSL configuration issues
./build_and_test_mysql --debug-openssl

# Show help
./build_and_test_mysql --help
```

## Dependencies Automatically Installed

### Build Tools
- `cmake` - Build configuration
- `make` - Build system  
- `gcc/clang` - C/C++ compilers
- `lld` - LLVM linker (optional)

### Development Libraries
- OpenSSL development libraries
- ncurses development libraries
- libtirpc development libraries
- bison parser generator

### Additional Tools
- MySQL client
- wget/curl for downloads
- pkg-config for library detection
- patchelf utility for binary patching

### Boost Library
- Automatically downloads and builds Boost 1.73.0 if not found

## Platform-Specific Notes

### Ubuntu/Debian
```bash
# Dependencies installed via apt-get
sudo apt-get install build-essential cmake pkg-config libssl-dev libncurses5-dev libtirpc-dev bison mysql-client wget curl patchelf
```

### RHEL/CentOS
```bash
# Dependencies installed via yum
sudo yum groupinstall "Development Tools"
sudo yum install cmake3 openssl-devel ncurses-devel libtirpc-devel bison mysql wget curl patchelf
```

### Fedora
```bash
# Dependencies installed via dnf
sudo dnf groupinstall "Development Tools" "Development Libraries"
sudo dnf install cmake openssl-devel ncurses-devel libtirpc-devel bison mysql wget curl patchelf
```

### macOS
```bash
# Dependencies installed via Homebrew
brew install cmake openssl mysql-client wget curl llvm patchelf
```

## Troubleshooting

### OpenSSL Issues
If you encounter OpenSSL-related cmake errors:

1. Run the debug command:
   ```bash
   ./build_and_test_mysql --debug-openssl
   ```

2. Install OpenSSL development packages manually:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install libssl-dev libcrypto++-dev
   
   # RHEL/CentOS
   sudo yum install openssl-devel openssl11-devel
   
   # Fedora
   sudo dnf install openssl-devel
   
   # macOS
   brew install openssl
   ```

### Boost Issues
If Boost download fails, manually download and extract:
```bash
cd $HOME
wget https://sourceforge.net/projects/boost/files/boost/1.73.0/boost_1_73_0.tar.gz
tar -xzf boost_1_73_0.tar.gz
```

### Build Issues
- Ensure you have sufficient disk space (>10GB recommended)
- The script uses (nproc-6) cores for building - adjust if needed
- Check that you have sudo privileges for installation steps

## Script Options

| Option | Description |
|--------|-------------|
| `--no-build` | Skip building MySQL |
| `--no-test` | Skip testing MySQL |
| `--no-clean` | Skip cleaning up MySQL installation |
| `--skip-deps` | Skip dependency installation check |
| `--debug-openssl` | Show OpenSSL debug information and exit |
| `--profile NUM` | Set profile number for output |
| `-h, --help` | Show help message |

## Output Files

- **Build Directory**: `./build/` (relative to script location)
- **Profile Output**: `./profile_out/profile_N.tsv`
- **Debug Log**: `/tmp/mysql_debug_trace.log`
- **MySQL Installation**: `/usr/local/mysql`

The script now uses paths relative to the script location rather than hardcoded home directory paths.