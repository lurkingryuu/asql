#!/bin/bash

# Build and Test Script for DDL Audit Plugin
# This script builds the plugin and provides instructions for testing

set -e

echo "=== DDL Audit Plugin Build and Test Script ==="
echo

# Check if we're in the right directory
if [ ! -f "ddl_audit.cc" ]; then
    echo "Error: Please run this script from the plugin/ddl_audit directory"
    exit 1
fi

# Get the MySQL source root directory
MYSQL_ROOT=$(pwd)
while [ ! -f "$MYSQL_ROOT/CMakeLists.txt" ] && [ "$MYSQL_ROOT" != "/" ]; do
    MYSQL_ROOT=$(dirname "$MYSQL_ROOT")
done

if [ ! -f "$MYSQL_ROOT/CMakeLists.txt" ]; then
    echo "Error: Could not find MySQL source root directory"
    exit 1
fi

echo "MySQL source root: $MYSQL_ROOT"
echo

# Check if build directory exists
if [ ! -d "$MYSQL_ROOT/build" ]; then
    echo "Error: Build directory not found. Please build MySQL first."
    echo "Run: cd $MYSQL_ROOT && mkdir build && cd build && cmake .. && make"
    exit 1
fi

echo "=== Building DDL Audit Plugin ==="
cd "$MYSQL_ROOT/build"

# Build the plugin
echo "Building plugin..."
make ddl_audit

if [ $? -eq 0 ]; then
    echo "✓ Plugin built successfully!"
else
    echo "✗ Plugin build failed!"
    exit 1
fi

# Find the plugin file
PLUGIN_FILE=$(find . -name "ddl_audit.so" -type f | head -1)
if [ -z "$PLUGIN_FILE" ]; then
    echo "Error: Plugin file not found after build"
    exit 1
fi

echo "Plugin file: $PLUGIN_FILE"
echo

# Check dependencies
echo "=== Checking Dependencies ==="
echo "Checking for required libraries..."

# Check for libcurl
if ldconfig -p | grep -q libcurl; then
    echo "✓ libcurl found"
else
    echo "⚠ libcurl not found in system library path"
    echo "  You may need to install libcurl development packages"
fi

# Check for jsoncpp
if ldconfig -p | grep -q libjsoncpp; then
    echo "✓ libjsoncpp found"
else
    echo "⚠ libjsoncpp not found in system library path"
    echo "  You may need to install jsoncpp development packages"
fi

echo

# Setup instructions
echo "=== Setup Instructions ==="
echo "1. Start your MySQL server"
echo "2. Install the plugin:"
echo "   INSTALL PLUGIN ddl_audit SONAME 'ddl_audit.so';"
echo
echo "3. Configure the plugin:"
echo "   SET GLOBAL ddl_audit_cedar_url = 'http://localhost:8180';"
echo "   SET GLOBAL ddl_audit_cedar_timeout = 5000;"
echo "   SET GLOBAL ddl_audit_enabled = ON;"
echo
echo "4. Start the Cedar server example:"
echo "   cd $(dirname "$0")"
echo "   npm install"
echo "   node cedar_server_example.js"
echo
echo "5. Run the test script:"
echo "   mysql -u root -p < test_ddl_audit.sql"
echo

# Test the Cedar server example
echo "=== Testing Cedar Server Example ==="
cd "$(dirname "$0")"

if [ -f "package.json" ]; then
    echo "Installing Node.js dependencies..."
    if command -v npm >/dev/null 2>&1; then
        npm install
        echo "✓ Dependencies installed"
    else
        echo "⚠ npm not found. Please install Node.js and npm to test the Cedar server"
    fi
else
    echo "⚠ package.json not found"
fi

echo
echo "=== Plugin Information ==="
echo "Plugin Name: ddl_audit"
echo "Plugin Type: Audit Plugin"
echo "Supported Events: MYSQL_AUDIT_QUERY_STATUS_END (post-execution)"
echo "DDL Commands Captured: CREATE, ALTER, DROP operations on tables, databases, users, etc."
echo "Cedar Integration: HTTP POST to configurable URL"
echo

echo "=== Next Steps ==="
echo "1. Start the Cedar server: node cedar_server_example.js"
echo "2. Install and configure the plugin in MySQL"
echo "3. Run DDL statements to test the plugin"
echo "4. Check the Cedar server logs for received DDL data"
echo "5. Monitor MySQL error log for plugin activity"
echo

echo "=== Troubleshooting ==="
echo "If the plugin fails to load:"
echo "- Check MySQL error log for detailed error messages"
echo "- Ensure all dependencies are installed"
echo "- Verify the plugin file exists and is readable"
echo
echo "If DDL events are not captured:"
echo "- Verify ddl_audit_enabled is ON"
echo "- Check that ddl_audit_cedar_url is configured"
echo "- Ensure the Cedar server is running and accessible"
echo

echo "Build and setup complete! 🎉"
