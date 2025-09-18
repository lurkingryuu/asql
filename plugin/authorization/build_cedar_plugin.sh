#!/bin/bash

# Build and Install Script for Cedar Authorization Plugin
# This script helps build and install the Cedar authorization plugin for MySQL

set -e  # Exit on any error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
PLUGIN_NAME="cedar_authorization"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
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

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to check dependencies
check_dependencies() {
    print_status "Checking dependencies..."
    
    local missing_deps=0
    
    if ! command_exists cmake; then
        print_error "CMake is required but not installed"
        missing_deps=$((missing_deps + 1))
    fi
    
    if ! command_exists make; then
        print_error "Make is required but not installed"
        missing_deps=$((missing_deps + 1))
    fi
    
    if ! command_exists g++; then
        print_error "G++ compiler is required but not installed"
        missing_deps=$((missing_deps + 1))
    fi
    
    # Check for curl headers
    if ! pkg-config --exists libcurl; then
        print_warning "libcurl development headers not found"
        print_warning "You may need to install libcurl-dev or ensure it's available"
    fi

    # Check for jsoncpp headers
    if ! pkg-config --exists jsoncpp; then
        print_warning "jsoncpp development headers not found"
        print_warning "You may need to install libjsoncpp-dev or ensure it's available"
    fi
    
    if [ $missing_deps -gt 0 ]; then
        print_error "Missing $missing_deps required dependencies"
        return 1
    fi
    
    print_success "All required dependencies found"
    return 0
}

# Function to install system dependencies (Ubuntu/Debian)
install_dependencies_ubuntu() {
    print_status "Installing dependencies for Ubuntu/Debian..."

    sudo apt-get update
    sudo apt-get install -y \
        cmake \
        make \
        g++ \
        libssl-dev \
        pkg-config \
        libcurl4-openssl-dev \
        libjsoncpp-dev

    print_success "Dependencies installed"
}

# Function to configure build
configure_build() {
    print_status "Configuring build..."
    
    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Configure with CMake
    cmake -DWITH_AUTHORIZATION_PLUGINS=ON \
          -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          "$PROJECT_ROOT"
    
    print_success "Build configured"
}

# Function to build plugin
build_plugin() {
    print_status "Building $PLUGIN_NAME plugin..."
    
    cd "$BUILD_DIR"
    
    # Build the specific plugin
    make $PLUGIN_NAME -j$(nproc)
    
    print_success "Plugin built successfully"
}

# Function to find MySQL plugin directory
find_mysql_plugin_dir() {
    local plugin_dir=""
    
    # Try to get plugin directory from MySQL
    if command_exists mysql; then
        plugin_dir=$(mysql -u root -e "SHOW VARIABLES LIKE 'plugin_dir';" --silent --skip-column-names 2>/dev/null | cut -f2)
    fi
    
    # Fallback locations
    if [ -z "$plugin_dir" ] || [ ! -d "$plugin_dir" ]; then
        for dir in "/usr/lib/mysql/plugin" "/usr/local/mysql/lib/plugin" "/opt/mysql/lib/plugin"; do
            if [ -d "$dir" ]; then
                plugin_dir="$dir"
                break
            fi
        done
    fi
    
    echo "$plugin_dir"
}

# Function to install plugin
install_plugin() {
    print_status "Installing $PLUGIN_NAME plugin..."
    
    local plugin_file="$BUILD_DIR/plugin/authorization/${PLUGIN_NAME}.so"
    
    # Check if plugin file exists
    if [ ! -f "$plugin_file" ]; then
        print_error "Plugin file not found: $plugin_file"
        return 1
    fi
    
    # Find MySQL plugin directory
    local mysql_plugin_dir=$(find_mysql_plugin_dir)
    
    if [ -z "$mysql_plugin_dir" ] || [ ! -d "$mysql_plugin_dir" ]; then
        print_error "MySQL plugin directory not found"
        print_error "Please specify the plugin directory manually:"
        print_error "sudo cp $plugin_file /path/to/mysql/plugin/directory/"
        return 1
    fi
    
    print_status "Installing to: $mysql_plugin_dir"
    
    # Copy plugin file
    sudo cp "$plugin_file" "$mysql_plugin_dir/"
    sudo chown mysql:mysql "$mysql_plugin_dir/${PLUGIN_NAME}.so" 2>/dev/null || true
    sudo chmod 755 "$mysql_plugin_dir/${PLUGIN_NAME}.so"
    
    print_success "Plugin installed to $mysql_plugin_dir"
}

# Function to test plugin installation
test_plugin() {
    print_status "Testing plugin installation..."
    
    # Create test SQL
    local test_sql="
        INSTALL PLUGIN $PLUGIN_NAME SONAME '${PLUGIN_NAME}.so';
        SHOW PLUGINS;
        SELECT PLUGIN_NAME, PLUGIN_STATUS FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = '$PLUGIN_NAME';
        UNINSTALL PLUGIN $PLUGIN_NAME;
    "
    
    echo "$test_sql" > /tmp/test_${PLUGIN_NAME}.sql
    
    print_status "Test SQL created at: /tmp/test_${PLUGIN_NAME}.sql"
    print_status "Run the following to test the plugin:"
    echo "mysql -u root -p < /tmp/test_${PLUGIN_NAME}.sql"
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help              Show this help message"
    echo "  -d, --deps              Install system dependencies (Ubuntu/Debian only)"
    echo "  -c, --configure         Configure build only"
    echo "  -b, --build             Build plugin only"
    echo "  -i, --install           Install plugin only"
    echo "  -t, --test              Create test SQL only"
    echo "  -a, --all               Do everything (configure, build, install)"
    echo "  --clean                 Clean build directory"
    echo ""
    echo "Examples:"
    echo "  $0 --all               # Complete build and install"
    echo "  $0 --deps --all        # Install dependencies and build"
    echo "  $0 --build --install   # Build and install only"
}

# Function to clean build directory
clean_build() {
    print_status "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    print_success "Build directory cleaned"
}

# Main function
main() {
    local do_deps=false
    local do_configure=false
    local do_build=false
    local do_install=false
    local do_test=false
    local do_clean=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_usage
                exit 0
                ;;
            -d|--deps)
                do_deps=true
                shift
                ;;
            -c|--configure)
                do_configure=true
                shift
                ;;
            -b|--build)
                do_build=true
                shift
                ;;
            -i|--install)
                do_install=true
                shift
                ;;
            -t|--test)
                do_test=true
                shift
                ;;
            -a|--all)
                do_configure=true
                do_build=true
                do_install=true
                do_test=true
                shift
                ;;
            --clean)
                do_clean=true
                shift
                ;;
            *)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
    
    # Show banner
    echo "========================================"
    echo "Cedar Authorization Plugin Build Script"
    echo "========================================"
    echo ""
    
    # Clean if requested
    if [ "$do_clean" = true ]; then
        clean_build
        exit 0
    fi
    
    # Install dependencies if requested
    if [ "$do_deps" = true ]; then
        if [ -f /etc/debian_version ]; then
            install_dependencies_ubuntu
        else
            print_warning "Dependency installation only supported on Ubuntu/Debian"
            print_warning "Please install dependencies manually:"
            print_warning "- cmake, make, g++"
            print_warning "- libcurl4-openssl-dev"
            print_warning "- libjsoncpp-dev"
        fi
    fi
    
    # Check dependencies
    if ! check_dependencies; then
        print_error "Dependency check failed"
        if [ -f /etc/debian_version ]; then
            print_status "Try running: $0 --deps"
            print_status "Or manually install: sudo apt-get install libcurl4-openssl-dev libjsoncpp-dev"
        fi
        exit 1
    fi
    
    # Configure build
    if [ "$do_configure" = true ]; then
        configure_build
    fi
    
    # Build plugin
    if [ "$do_build" = true ]; then
        if [ ! -f "$BUILD_DIR/Makefile" ]; then
            print_warning "Build not configured, configuring now..."
            configure_build
        fi
        build_plugin
    fi
    
    # Install plugin
    if [ "$do_install" = true ]; then
        install_plugin
    fi
    
    # Create test SQL
    if [ "$do_test" = true ]; then
        test_plugin
    fi
    
    # Show completion message
    if [ "$do_configure" = true ] || [ "$do_build" = true ] || [ "$do_install" = true ]; then
        echo ""
        print_success "Cedar authorization plugin setup complete!"
        echo ""
        print_status "Next steps:"
        echo "1. Start the Cedar service (or use mock_cedar_service.py)"
        echo "2. Load the plugin in MySQL:"
        echo "   INSTALL PLUGIN $PLUGIN_NAME SONAME '${PLUGIN_NAME}.so';"
        echo "3. Configure the service URL:"
        echo "   SET GLOBAL cedar_authorization_url = 'http://localhost:8180/v1/is_authorized';"
        echo "4. Test with the provided test_cedar_plugin.sql"
        echo ""
    fi
}

# Run main function with all arguments
main "$@"
