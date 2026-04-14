#!/bin/bash

set -e  # Exit immediately if a command fails

# MySQL Build and Test Script for ABAC Plugin
# Options:
# --no-build: skip building MySQL
# --no-clean: skip cleaning up MySQL installation
# --skip-deps: skip dependency installation check
# --debug-openssl: show OpenSSL debug information and exit
# --force-openssl-1-1: force installation and use of OpenSSL 1.1.1

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to detect the operating system
detect_os() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        if command -v apt-get >/dev/null 2>&1; then
            echo "ubuntu"
        elif command -v yum >/dev/null 2>&1; then
            echo "rhel"
        elif command -v dnf >/dev/null 2>&1; then
            echo "fedora"
        elif command -v pacman >/dev/null 2>&1; then
            echo "arch"
        else
            echo "linux"
        fi
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    else
        echo "unknown"
    fi
}

# Function to install dependencies
install_dependencies() {
    local os_type=$(detect_os)
    print_info "Detected OS: $os_type"
    print_info "Checking and installing dependencies..."

    case $os_type in
        "ubuntu")
            print_info "Installing dependencies for Ubuntu/Debian..."
            sudo apt-get update
            # Install core dependencies first
            sudo apt-get install -y \
                build-essential \
                cmake \
                pkg-config \
                libssl-dev \
                libcrypto++-dev \
                libncurses5-dev \
                libtirpc-dev \
                libcurl4-openssl-dev \
                bison \
                mysql-client \
                wget \
                curl \
                patchelf
            
            # Try to install optional packages
            sudo apt-get install -y lld clang || {
                print_warning "lld/clang not available, will use default linker and compiler"
            }
            
            # Verify OpenSSL installation and try to install OpenSSL 1.1 for MySQL compatibility
            if ! pkg-config --exists openssl; then
                print_warning "OpenSSL pkg-config not found, trying alternative installation"
                sudo apt-get install -y openssl libssl-dev libssl3-dev || true
            fi
            
            # Try to install OpenSSL 1.1 for MySQL 8.0.27 compatibility
            print_info "Installing OpenSSL 1.1 compatibility libraries for MySQL..."
            sudo apt-get install -y libssl1.1-dev libssl1.1 || {
                print_warning "OpenSSL 1.1 not available, will use OpenSSL 3.x"
            }
            
            # Install optional dependencies for additional MySQL features
            print_info "Installing optional dependencies for MySQL features..."
            sudo apt-get install -y libsasl2-dev libldap2-dev || {
                print_warning "Some optional dependencies not available (SASL/LDAP support may be limited)"
            }
            ;;
        "rhel")
            print_info "Installing dependencies for RHEL/CentOS..."
            sudo yum install -y epel-release || true
            sudo yum groupinstall -y "Development Tools"
            # Install core dependencies
            sudo yum install -y \
                cmake3 \
                openssl-devel \
                openssl11-devel \
                ncurses-devel \
                libtirpc-devel \
                libcurl-devel \
                bison \
                mysql \
                wget \
                curl \
                patchelf
            
            # Try to install optional packages
            sudo yum install -y lld clang || {
                print_warning "lld/clang not available, will use default linker and compiler"
            }
            
            # Install optional dependencies for additional MySQL features
            sudo yum install -y cyrus-sasl-devel openldap-devel || {
                print_warning "Some optional dependencies not available (SASL/LDAP support may be limited)"
            }
            
            # Create cmake symlink if cmake3 is installed
            if command -v cmake3 >/dev/null 2>&1 && ! command -v cmake >/dev/null 2>&1; then
                sudo ln -sf /usr/bin/cmake3 /usr/bin/cmake
            fi
            ;;
        "fedora")
            print_info "Installing dependencies for Fedora..."
            sudo dnf groupinstall -y "Development Tools" "Development Libraries"
            # Install core dependencies
            sudo dnf install -y \
                cmake \
                openssl-devel \
                ncurses-devel \
                libtirpc-devel \
                libcurl-devel \
                bison \
                mysql \
                wget \
                curl \
                patchelf
            
            # Try to install optional packages
            sudo dnf install -y lld clang || {
                print_warning "lld/clang not available, will use default linker and compiler"
            }
            
            # Install optional dependencies for additional MySQL features
            sudo dnf install -y cyrus-sasl-devel openldap-devel || {
                print_warning "Some optional dependencies not available (SASL/LDAP support may be limited)"
            }
            ;;
        "arch")
            print_info "Installing dependencies for Arch Linux..."
            sudo pacman -Sy --noconfirm \
                base-devel \
                cmake \
                openssl \
                ncurses \
                libtirpc \
                curl \
                bison \
                mysql \
                wget \
                patchelf \
                lld \
                clang || {
                print_warning "Some packages not available, installing core dependencies"
                sudo pacman -Sy --noconfirm \
                    base-devel \
                    cmake \
                    openssl \
                    ncurses \
                    libtirpc \
                    curl \
                    bison \
                    mysql \
                    wget \
                    patchelf
            }
            ;;
        "macos")
            print_info "Installing dependencies for macOS..."
            if ! command -v brew >/dev/null 2>&1; then
                print_error "Homebrew not found. Please install Homebrew first:"
                print_info "Run: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
                exit 1
            fi
            brew install cmake openssl mysql-client wget curl llvm patchelf
            ;;
        *)
            print_warning "Unknown OS. Please install the following dependencies manually:"
            print_info "- build-essential/development tools"
            print_info "- cmake"
            print_info "- openssl development libraries"
            print_info "- libcurl development libraries"
            print_info "- ncurses development libraries"
            print_info "- bison"
            print_info "- mysql client"
            print_info "- wget and curl"
            print_info "- patchelf utility"
            print_info "- lld linker (optional)"
            ;;
    esac
}

# Function to install Boost if not present
install_boost() {
    local boost_dir="$HOME/boost_1_77_0"

    if [ -d "$boost_dir" ]; then
        print_success "Boost 1.77.0 already installed at $boost_dir"
        return 0
    fi

    print_info "Installing Boost 1.77.0..."
    cd "$HOME"

    if [ ! -f "boost_1_77_0.tar.gz" ]; then
        print_info "Downloading Boost 1.77.0..."
        # Try multiple sources
        wget https://archives.boost.io/release/1.77.0/source/boost_1_77_0.tar.gz || \
        wget https://sourceforge.net/projects/boost/files/boost/1.77.0/boost_1_77_0.tar.gz/download -O boost_1_77_0.tar.gz || \
        curl -L https://github.com/boostorg/boost/releases/download/boost-1.77.0/boost_1_77_0.tar.gz -o boost_1_77_0.tar.gz || {
            print_error "Failed to download Boost 1.77.0 from all available sources"
            print_info "Please download boost_1_77_0.tar.gz manually and place it in $HOME/"
            exit 1
        }
    fi

    print_info "Extracting Boost..."
    tar -xzf boost_1_77_0.tar.gz

    cd boost_1_77_0
    print_info "Building Boost (this may take a while)..."
    ./bootstrap.sh --prefix="$boost_dir"
    ./b2 -j$(nproc) --prefix="$boost_dir" install

    cd "$HOME"
    rm -f boost_1_77_0.tar.gz

    print_success "Boost 1.77.0 installed successfully"
}

# Function to install OpenSSL 1.1.1 for MySQL compatibility
install_openssl_1_1() {
    local openssl_dir="$HOME/openssl-1.1.1w"
    local openssl_install_dir="$HOME/openssl-1.1.1w-install"
    
    if [ -d "$openssl_install_dir" ]; then
        print_success "OpenSSL 1.1.1w already installed at $openssl_install_dir"
        return 0
    fi
    
    print_info "Installing OpenSSL 1.1.1w for MySQL compatibility..."
    cd "$HOME"
    
    if [ ! -f "openssl-1.1.1w.tar.gz" ]; then
        print_info "Downloading OpenSSL 1.1.1w..."
        wget https://www.openssl.org/source/openssl-1.1.1w.tar.gz || \
        curl -L https://www.openssl.org/source/openssl-1.1.1w.tar.gz -o openssl-1.1.1w.tar.gz || {
            print_error "Failed to download OpenSSL 1.1.1w"
            return 1
        }
    fi
    
    print_info "Extracting OpenSSL..."
    tar -xzf openssl-1.1.1w.tar.gz
    
    cd openssl-1.1.1w
    print_info "Configuring OpenSSL..."
    ./config --prefix="$openssl_install_dir" --openssldir="$openssl_install_dir" shared
    
    print_info "Building OpenSSL (this may take a while)..."
    make -j$(nproc)
    make install
    
    cd "$HOME"
    rm -f openssl-1.1.1w.tar.gz
    rm -rf openssl-1.1.1w
    
    print_success "OpenSSL 1.1.1w installed successfully at $openssl_install_dir"
}

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to debug OpenSSL installation
debug_openssl() {
    print_info "=== OPENSSL DEBUG INFORMATION ==="
    
    print_info "OpenSSL command version:"
    if command_exists openssl; then
        openssl version
    else
        print_warning "OpenSSL command not found"
    fi
    
    print_info "pkg-config OpenSSL check:"
    if pkg-config --exists openssl; then
        print_success "pkg-config found OpenSSL: $(pkg-config --modversion openssl)"
        print_info "OpenSSL CFLAGS: $(pkg-config --cflags openssl)"
        print_info "OpenSSL LIBS: $(pkg-config --libs openssl)"
    else
        print_warning "pkg-config cannot find OpenSSL"
    fi
    
    print_info "Checking common OpenSSL paths:"
    for path in "/usr/include/openssl" "/usr/local/include/openssl" "/opt/homebrew/include/openssl" "/usr/local/opt/openssl/include/openssl"; do
        if [ -d "$path" ]; then
            print_success "Found headers: $path"
        fi
    done
    
    print_info "Checking OpenSSL libraries (including version-specific):"
    for path in "/usr/lib/x86_64-linux-gnu/libssl.so" "/usr/lib/x86_64-linux-gnu/libssl.so.3" "/usr/lib/x86_64-linux-gnu/libssl.so.1.1" "/usr/lib64/libssl.so" "/usr/lib64/libssl.so.3" "/usr/lib64/libssl.so.1.1" "/usr/lib/libssl.so" "/usr/local/lib/libssl.so" "/opt/homebrew/lib/libssl.dylib" "/usr/local/opt/openssl/lib/libssl.dylib"; do
        if [ -f "$path" ]; then
            print_success "Found library: $path"
            if command_exists readelf && [[ "$path" == *.so* ]]; then
                soname=$(readelf -d "$path" 2>/dev/null | grep SONAME || true)
                if [ -n "$soname" ]; then
                    print_info "  SONAME: $soname"
                fi
            fi
        fi
    done
    
    print_info "OpenSSL version compatibility check:"
    if command_exists openssl; then
        openssl_version=$(openssl version | cut -d' ' -f2)
        if [[ "$openssl_version" =~ ^3\. ]]; then
            print_warning "OpenSSL 3.x detected - MySQL 8.0.27 may have compatibility issues"
            print_info "Consider using MySQL 8.0.30+ or install OpenSSL 1.1.x"
        elif [[ "$openssl_version" =~ ^1\.1\. ]]; then
            print_success "OpenSSL 1.1.x detected - should be compatible with MySQL 8.0.27"
        else
            print_warning "OpenSSL version $openssl_version - compatibility unknown"
        fi
    fi
    
    print_info "====================================="
}

# Function to check all dependencies
check_dependencies() {
    print_info "Checking system dependencies..."
    
    local missing_deps=()
    local os_type=$(detect_os)
    
    # Check essential build tools
    if ! command_exists cmake; then
        missing_deps+=("cmake")
    fi
    
    if ! command_exists make; then
        missing_deps+=("make")
    fi
    
    if ! command_exists gcc && ! command_exists clang; then
        missing_deps+=("compiler (gcc or clang)")
    fi
    
    # Check MySQL client
    if ! command_exists mysql; then
        missing_deps+=("mysql-client")
    fi
    
    # Check for pkg-config
    if ! command_exists pkg-config; then
        missing_deps+=("pkg-config")
    fi
    
    # Check for patchelf
    if ! command_exists patchelf; then
        missing_deps+=("patchelf")
    fi
    
    # Check OpenSSL development libraries
    if [[ "$os_type" == "macos" ]]; then
        if [ ! -d "/usr/local/opt/openssl" ] && [ ! -d "/opt/homebrew/opt/openssl" ]; then
            missing_deps+=("openssl")
        fi
    else
        # Check multiple ways OpenSSL could be installed on Linux
        openssl_found=false
        
        if pkg-config --exists openssl; then
            openssl_found=true
        elif [ -d "/usr/include/openssl" ] && ([ -f "/usr/lib/x86_64-linux-gnu/libssl.so" ] || [ -f "/usr/lib64/libssl.so" ] || [ -f "/usr/lib/libssl.so" ]); then
            openssl_found=true
        elif command_exists openssl && [ -f "/usr/include/openssl/ssl.h" ]; then
            openssl_found=true
        fi
        
        if [ "$openssl_found" = false ]; then
            missing_deps+=("openssl-dev")
        fi
    fi
    
    # Check Boost
    if [ ! -d "$HOME/boost_1_77_0" ]; then
        print_warning "Boost 1.77.0 not found at $HOME/boost_1_77_0"
        missing_deps+=("boost")
    fi
    
    if [ ${#missing_deps[@]} -eq 0 ]; then
        print_success "All dependencies are satisfied"
        return 0
    else
        print_warning "Missing dependencies: ${missing_deps[*]}"
        return 1
    fi
}

# Parse command line arguments
# Variables
NO_BUILD=0
NO_TEST=0
NO_CLEAN=0
CLEANUP_FEATURES=0
SKIP_DEPS=0
FORCE_OPENSSL_1_1=0

while [[ $# -gt 0 ]]; do
    key="$1"
    case $key in
        --no-build)
            NO_BUILD=1
            shift
            ;;
        --no-test)
            NO_TEST=1
            shift
            ;;
        --no-clean)
            NO_CLEAN=1
            shift
            ;;
        --cleanup-features)
            CLEANUP_FEATURES=1
            shift
            ;;
        --skip-deps)
            SKIP_DEPS=1
            shift
            ;;
        --debug-openssl)
            debug_openssl
            exit 0
            ;;
        --force-openssl-1-1)
            FORCE_OPENSSL_1_1=1
            shift
            ;;
        -h|--help)
            echo "MySQL Build and Test Script for ABAC Plugin"
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --no-build           Skip building MySQL"
            echo "  --no-test            Skip testing MySQL"
            echo "  --no-clean           Skip cleaning up MySQL installation"
            echo "  --cleanup-features   Cleanup all ABAC test features and exit"
            echo "  --skip-deps          Skip dependency installation check"
            echo "  --debug-openssl      Show OpenSSL debug information and exit"
            echo "  --force-openssl-1-1  Force installation and use of OpenSSL 1.1.1"
            echo "  -h, --help           Show this help message"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            print_info "Use --help for usage information"
            exit 1
            ;;
    esac
done

CURRENT_DIR=$(pwd)

# Variables
MYSQL_INSTALL_DIR="/usr/local/mysql"
MYSQL_BUILD_DIR="$CURRENT_DIR/build"
MYSQL_DATA_DIR="$MYSQL_BUILD_DIR/data"
MYSQL_SOCKET="$MYSQL_BUILD_DIR/mysql.sock"
DEBUG_LOG="/tmp/mysql_debug_trace.log"

# Handle cleanup features option
if [ "$CLEANUP_FEATURES" -eq 1 ]; then
    cleanup_all_features
    exit 0
fi

# Check and install dependencies
if [ "$SKIP_DEPS" -eq 0 ]; then
    print_info "=== DEPENDENCY CHECK ==="
    
    if ! check_dependencies; then
        print_info "Installing missing dependencies..."
        install_dependencies
        
        # Install Boost if missing
        if [ ! -d "$HOME/boost_1_77_0" ]; then
            install_boost
        fi
        
        # Check if we need to install custom OpenSSL for MySQL compatibility
        if [ "$FORCE_OPENSSL_1_1" -eq 1 ] || (pkg-config --exists openssl && [[ "$(pkg-config --modversion openssl)" =~ ^3\. ]]); then
            if [ ! -d "$HOME/openssl-1.1.1w-install" ]; then
                if [ "$FORCE_OPENSSL_1_1" -eq 1 ]; then
                    print_info "Force OpenSSL 1.1.1 requested. Installing..."
                else
                    print_info "OpenSSL 3.x detected. Pre-installing OpenSSL 1.1.1 for MySQL compatibility..."
                fi
                install_openssl_1_1
            fi
        fi
        
        print_info "Re-checking dependencies after installation..."
        if ! check_dependencies; then
            print_error "Some dependencies are still missing. Please install them manually."
            exit 1
        fi
    fi
    
    print_success "All dependencies satisfied!"
    print_info "========================"
else
    print_warning "Skipping dependency check (--skip-deps specified)"
fi

# Clean up MySQL installation
if [ "$NO_CLEAN" -eq 0 ]; then
    print_info "=== STOPPING AND REMOVING ANY PREVIOUS MYSQL INSTALLATION ==="
    if pgrep mysqld >/dev/null 2>&1; then
        print_info "Stopping MySQL service..."
        pkill -9 mysqld
    fi

    # Remove previous data directory (if exists)
    if [ -d "$MYSQL_DATA_DIR" ]; then
        print_info "Removing existing MySQL data directory: $MYSQL_DATA_DIR"
        rm -rf "$MYSQL_DATA_DIR"
    fi

    # Remove previous debug log (if exists)
    if [ -f "$DEBUG_LOG" ]; then
        print_info "Removing existing debug log: $DEBUG_LOG"
        rm -f "$DEBUG_LOG"
    fi
fi


# Build MySQL
if [ "$NO_BUILD" -eq 0 ]; then

    # Remove previous MySQL installation (if exists)
    if [ -d "$MYSQL_INSTALL_DIR" ]; then
        print_info "Removing existing MySQL installation: $MYSQL_INSTALL_DIR"
        sudo rm -rf "$MYSQL_INSTALL_DIR"
    fi

    print_info "=== CONFIGURING AND BUILDING MYSQL ==="
    mkdir -p "$MYSQL_BUILD_DIR"
    cd "$MYSQL_BUILD_DIR"

    # Basic CMake configuration for MySQL with ABAC plugin
    os_type=$(detect_os)
    cmake_args="-DWITH_BOOST=$HOME/boost_1_77_0 -DWITH_DEBUG=0 -DMYSQL_UNIX_ADDR=$MYSQL_SOCKET -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DMYSQL_DYNAMIC_PLUGIN=true"
    
    # Configure SSL/OpenSSL
    openssl_version=""
    if command_exists openssl; then
        openssl_version=$(openssl version | cut -d' ' -f2)
    fi
    
    # Check if we should use custom OpenSSL 1.1.1
    CUSTOM_OPENSSL_DIR="$HOME/openssl-1.1.1w-install"
    if [ "$FORCE_OPENSSL_1_1" -eq 1 ] || ([ -d "$CUSTOM_OPENSSL_DIR" ] && [[ "$openssl_version" =~ ^3\. ]]); then
        print_info "Using custom OpenSSL 1.1.1 installation"
        cmake_args="$cmake_args -DWITH_SSL=$CUSTOM_OPENSSL_DIR"
    elif [[ "$os_type" == "macos" ]]; then
        # macOS with Homebrew
        if [ -d "/opt/homebrew/opt/openssl" ]; then
            # Apple Silicon Mac
            cmake_args="$cmake_args -DWITH_SSL=/opt/homebrew/opt/openssl -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl"
        elif [ -d "/usr/local/opt/openssl" ]; then
            # Intel Mac
            cmake_args="$cmake_args -DWITH_SSL=/usr/local/opt/openssl -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl"
        else
            cmake_args="$cmake_args -DWITH_SSL=system"
        fi
    else
        # Linux - Handle OpenSSL version compatibility
        if pkg-config --exists openssl; then
            openssl_pkg_version=$(pkg-config --modversion openssl)
            print_info "Using system OpenSSL: $openssl_pkg_version"
            
            # Check if OpenSSL 3.x - MySQL 8.0.27 may have issues with OpenSSL 3.x
            if [[ "$openssl_pkg_version" =~ ^3\. ]]; then
                print_warning "OpenSSL 3.x detected. MySQL 8.0.27 may have compatibility issues."
                print_info "Trying explicit OpenSSL configuration..."
                
                # Try to find OpenSSL 1.1 libraries first
                if [ -f "/usr/lib/x86_64-linux-gnu/libssl.so.1.1" ]; then
                    print_info "Found OpenSSL 1.1 libraries, using them"
                    cmake_args="$cmake_args -DWITH_SSL=system -DOPENSSL_ROOT_DIR=/usr -DOPENSSL_INCLUDE_DIR=/usr/include -DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so.1.1 -DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so.1.1"
                elif [ -f "/usr/lib64/libssl.so.1.1" ]; then
                    print_info "Found OpenSSL 1.1 libraries (RHEL style), using them"
                    cmake_args="$cmake_args -DWITH_SSL=system -DOPENSSL_ROOT_DIR=/usr -DOPENSSL_INCLUDE_DIR=/usr/include -DOPENSSL_SSL_LIBRARY=/usr/lib64/libssl.so.1.1 -DOPENSSL_CRYPTO_LIBRARY=/usr/lib64/libcrypto.so.1.1"
                else
                    # Fallback: try to use OpenSSL 3.x with explicit paths
                    print_info "Using OpenSSL 3.x with explicit library paths"
                    if [ -f "/usr/lib/x86_64-linux-gnu/libssl.so" ]; then
                        cmake_args="$cmake_args -DWITH_SSL=system -DOPENSSL_ROOT_DIR=/usr -DOPENSSL_INCLUDE_DIR=/usr/include -DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so -DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so"
                    elif [ -f "/usr/lib64/libssl.so" ]; then
                        cmake_args="$cmake_args -DWITH_SSL=system -DOPENSSL_ROOT_DIR=/usr -DOPENSSL_INCLUDE_DIR=/usr/include -DOPENSSL_SSL_LIBRARY=/usr/lib64/libssl.so -DOPENSSL_CRYPTO_LIBRARY=/usr/lib64/libcrypto.so"
                    else
                        cmake_args="$cmake_args -DWITH_SSL=system"
                    fi
                fi
            else
                # OpenSSL 1.1.x should work fine
                cmake_args="$cmake_args -DWITH_SSL=system"
            fi
        elif [ -d "/usr/include/openssl" ] && [ -f "/usr/lib/x86_64-linux-gnu/libssl.so" ]; then
            # Ubuntu/Debian style paths
            cmake_args="$cmake_args -DWITH_SSL=system -DOPENSSL_ROOT_DIR=/usr -DOPENSSL_INCLUDE_DIR=/usr/include -DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so -DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so"
        elif [ -d "/usr/include/openssl" ] && [ -f "/usr/lib64/libssl.so" ]; then
            # RHEL/CentOS style paths
            cmake_args="$cmake_args -DWITH_SSL=system -DOPENSSL_ROOT_DIR=/usr -DOPENSSL_INCLUDE_DIR=/usr/include -DOPENSSL_SSL_LIBRARY=/usr/lib64/libssl.so -DOPENSSL_CRYPTO_LIBRARY=/usr/lib64/libcrypto.so"
        else
            print_error "OpenSSL development libraries not found!"
            debug_openssl
            print_info "Please install OpenSSL development packages:"
            case $os_type in
                "ubuntu")
                    print_info "  sudo apt-get install libssl-dev libcrypto++-dev"
                    print_info "  # For MySQL 8.0.27, you may also need OpenSSL 1.1:"
                    print_info "  sudo apt-get install libssl1.1-dev"
                    ;;
                "rhel")
                    print_info "  sudo yum install openssl-devel openssl11-devel"
                    ;;
                "fedora")
                    print_info "  sudo dnf install openssl-devel"
                    ;;
                "arch")
                    print_info "  sudo pacman -S openssl"
                    ;;
            esac
            print_info "After installation, run the script again."
            exit 1
        fi
    fi
    
    # Try to use lld if available
    if command_exists lld || command_exists ld.lld; then
        cmake_args="$cmake_args -DCMAKE_LINKER=lld"
    fi
    
    print_info "Running cmake with: $cmake_args"
    
    # Try cmake with the configured SSL settings
    if ! cmake .. $cmake_args; then
        print_warning "CMAKE failed with system OpenSSL. Trying with custom OpenSSL 1.1.1..."
        
        # Install OpenSSL 1.1.1 if not present
        if ! install_openssl_1_1; then
            print_error "Failed to install OpenSSL 1.1.1"
            exit 1
        fi
        
        # Use custom OpenSSL 1.1.1 installation
        CUSTOM_OPENSSL_DIR="$HOME/openssl-1.1.1w-install"
        cmake_args_fallback="-DWITH_BOOST=$HOME/boost_1_77_0 -DWITH_DEBUG=0 -DWITH_SSL=$CUSTOM_OPENSSL_DIR -DMYSQL_UNIX_ADDR=$MYSQL_SOCKET -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DMYSQL_DYNAMIC_PLUGIN=true"
        
        # Add linker if available
        if command_exists lld || command_exists ld.lld; then
            cmake_args_fallback="$cmake_args_fallback -DCMAKE_LINKER=lld"
        fi
        
        print_info "Running cmake with custom OpenSSL 1.1.1: $cmake_args_fallback"
        
        # Clean cmake cache first
        rm -f CMakeCache.txt
        rm -rf CMakeFiles
        
        if ! cmake .. $cmake_args_fallback; then
            print_error "CMAKE failed with custom OpenSSL 1.1.1!"
            print_info "This might be a MySQL version compatibility issue."
            print_info "Let's try one more approach with explicit library paths..."
            
            # Try with explicit library paths
            cmake_args_explicit="-DWITH_BOOST=$HOME/boost_1_77_0 -DWITH_DEBUG=0 -DWITH_SSL=$CUSTOM_OPENSSL_DIR -DOPENSSL_ROOT_DIR=$CUSTOM_OPENSSL_DIR -DOPENSSL_INCLUDE_DIR=$CUSTOM_OPENSSL_DIR/include -DOPENSSL_SSL_LIBRARY=$CUSTOM_OPENSSL_DIR/lib/libssl.so -DOPENSSL_CRYPTO_LIBRARY=$CUSTOM_OPENSSL_DIR/lib/libcrypto.so -DMYSQL_UNIX_ADDR=$MYSQL_SOCKET -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DMYSQL_DYNAMIC_PLUGIN=true"
            
            if command_exists lld || command_exists ld.lld; then
                cmake_args_explicit="$cmake_args_explicit -DCMAKE_LINKER=lld"
            fi
            
            print_info "Trying with explicit paths: $cmake_args_explicit"
            rm -f CMakeCache.txt
            rm -rf CMakeFiles
            
            cmake .. $cmake_args_explicit || {
                print_error "CMAKE failed with all OpenSSL approaches!"
                print_info "This MySQL version may be incompatible with available OpenSSL versions."
                print_info "Consider:"
                print_info "1. Using MySQL 8.0.30+ (better OpenSSL 3.x support)"
                print_info "2. Using MySQL 5.7.x (better OpenSSL 1.1.x support)"
                print_info "3. Manually patching MySQL 8.0.27 for OpenSSL 3.x compatibility"
                exit 1
            }
        fi
        
        print_success "CMAKE succeeded with custom OpenSSL 1.1.1"
    else
        print_success "CMAKE succeeded with system OpenSSL"
    fi

    num_cores=$(nproc)
    num_cores=$((num_cores - 6))
    if [ "$num_cores" -lt 1 ]; then
        num_cores=1
    fi
    print_info "Building MySQL with $num_cores cores..."
    make -j"$num_cores"

    print_info "Installing MySQL..."
    sudo make install

    print_success "MySQL Build Successful!"

    print_info "Setting up MySQL User and Permissions..."
    if ! id "mysql" &>/dev/null; then
        sudo groupadd -f mysql
        sudo useradd -r -g mysql -s /bin/false mysql
    fi

    sudo mkdir -p "$MYSQL_INSTALL_DIR/mysql-files"
    sudo chown -R mysql:mysql "$MYSQL_INSTALL_DIR/mysql-files"
fi

if [ "$NO_CLEAN" -eq 0 ]; then
    cd "$MYSQL_BUILD_DIR"
    
    # Re-check and remove any leftover MySQL data
    if [ -d "$MYSQL_DATA_DIR" ]; then
        print_info "Cleaning up old MySQL data directory..."
        rm -rf "$MYSQL_DATA_DIR"
    fi

    mkdir -p "$MYSQL_DATA_DIR"

    # Initialize MySQL
    print_info "=== INITIALIZING MYSQL SERVER ==="
    bin/mysqld --initialize-insecure --user="$(whoami)" --datadir="$MYSQL_DATA_DIR"

    # Start MySQL Server with Debugging Enabled
    print_info "=== STARTING MYSQL SERVER ==="
    # bin/mysqld --debug=d:t:i:o,$DEBUG_LOG --user="$(whoami)" --datadir="$MYSQL_DATA_DIR" &
    bin/mysqld --user="$(whoami)" --datadir="$MYSQL_DATA_DIR" --socket="$MYSQL_SOCKET" --bind-address=127.0.0.1 &

    # Instructions to View Debug Logs
    print_info "To View Debug Logs, Run: less $DEBUG_LOG"
    print_info "To Filter Specific Debug Messages, Use: grep '<YourKeyword>' $DEBUG_LOG"

    sleep 5  # Allow MySQL to start
fi

# Test MySQL connection
print_info "=== RUNNING MYSQL TEST ==="
mysql -u root --skip-password --socket="$MYSQL_SOCKET" -e "SELECT VERSION();" || { 
    print_error "MySQL test failed"; 
    exit 1; 
}

print_success "MySQL Build and Test Successful!"
print_success "MySQL Server is Running!"

if [ "$NO_TEST" -eq 0 ]; then
    print_info "=== RUNNING UNIT TESTS (CTest) ==="
    if ! ctest --test-dir "$MYSQL_BUILD_DIR" --output-on-failure; then
        print_error "CTest failed"
        exit 1
    fi
    print_success "CTest completed successfully!"
else
    print_warning "Skipping CTest (--no-test specified)"
fi


