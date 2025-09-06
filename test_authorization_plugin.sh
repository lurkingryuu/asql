#!/bin/bash

# Test script for MySQL Authorization Plugin
# This script tests the authorization plugin functionality using CLI commands
# Options:
# --simple-only: test only simple_authorization plugin
# --external-only: test only external_authorization plugin
# --mysql-socket: specify MySQL socket path
# --mysql-port: specify MySQL port (if using TCP)
# --mysql-user: MySQL user to connect as
# --mysql-password: MySQL password
# --cleanup-only: cleanup test data and exit
# --verbose: enable verbose output
# --debug: enable debug output

set -e  # Exit immediately if a command fails

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
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

print_debug() {
    if [ "$DEBUG" = true ]; then
        echo -e "${PURPLE}[DEBUG]${NC} $1"
    fi
}

print_test() {
    echo -e "${CYAN}[TEST]${NC} $1"
}

print_result() {
    if [ "$1" = "PASS" ]; then
        echo -e "${GREEN}[PASS]${NC} $2"
    elif [ "$1" = "FAIL" ]; then
        echo -e "${RED}[FAIL]${NC} $2"
    elif [ "$1" = "SKIP" ]; then
        echo -e "${YELLOW}[SKIP]${NC} $2"
    fi
}

# Function to execute MySQL command as root/admin user
mysql_exec() {
    local query="$1"
    local expect_error="${2:-false}"
    local mysql_cmd

    if [ "$DOCKER" = true ]; then
        mysql_cmd="docker compose exec mysql mysql"
    else
        mysql_cmd="mysql"
    fi

    print_debug "Executing as admin: $query"

    if [ "$expect_error" = true ]; then
        # Expect error - redirect stderr to stdout and check exit code
        local output
        output=$( $mysql_cmd $MYSQL_CONN_ARGS -e "$query" 2>&1) || return 0
        echo "$output"
        return 0
    else
        # Normal execution
        $mysql_cmd $MYSQL_CONN_ARGS -e "$query"
    fi
}

# Function to execute MySQL command as specific user
mysql_exec_as_user() {
    local username="$1"
    local password="$2"
    local query="$3"
    local expect_error="${4:-false}"
    local mysql_cmd

    if [ "$DOCKER" = true ]; then
        mysql_cmd="docker compose exec mysql mysql"
    else
        mysql_cmd="mysql"
    fi

    # Build connection arguments for specific user
    local -a user_conn_args=( -u "$username" -p"$password" )
    
    # Add transport arguments (socket/port/host)
    if [ -n "$MYSQL_TRANSPORT_ARGS" ]; then
        # shellcheck disable=SC2206  # intentional word splitting
        user_conn_args+=( $MYSQL_TRANSPORT_ARGS )
    fi

    print_debug "Executing as user $username: $query"

    if [ "$expect_error" = true ]; then
        # Expect error - redirect stderr to stdout and check exit code
        local output
        local exit_code
        output=$( $mysql_cmd "${user_conn_args[@]}" -e "$query" 2>&1 || true)
        exit_code=$?
        echo "$output"
        return 0
    else
        # Normal execution
        $mysql_cmd "${user_conn_args[@]}" -e "$query"
    fi
}

# Function to test if query succeeds (no error)
test_query_success() {
    local username="$1"
    local password="$2"
    local description="$3"
    local query="$4"

    print_test "$description"
    if mysql_exec_as_user "$username" "$password" "$query" 2>/dev/null; then
        print_result "PASS" "$description"
        return 0
    else
        print_result "FAIL" "$description"
        return 1
    fi
}

# Function to test if query fails (error expected) - executed as admin
test_query_failure() {
    local username="$1"
    local password="$2"
    local description="$3"
    local query="$4"

    print_test "$description"
    local error_output
    error_output=$(mysql_exec_as_user "$username" "$password" "$query" true 2>&1)
    if echo "$error_output" | grep -q "ERROR\|Access denied"; then
        print_result "PASS" "$description"
        return 0
    else
        print_result "FAIL" "$description"
        print_debug "No error found in output: $error_output"
        return 1
    fi
}

# Function to create test user and test access
test_user_access() {
    local username="$1"
    local password="$2"
    local description="$3"
    local expected_result="$4"  # "success" or "failure"
    local db_grant="$5"

    local host="localhost"
    if [ "$DOCKER" = true ]; then
        host="%"
    fi

    print_test "$description"

    # Create temporary user (as admin)
    mysql_exec "DROP USER IF EXISTS '$username'@'$host';"
    mysql_exec "CREATE USER '$username'@'$host' IDENTIFIED BY '$password';"

    if [ "$db_grant" = true ]; then
        mysql_exec "GRANT SELECT ON testdb.* TO '$username'@'$host';"
    fi

    # Test access as the specific user
    local test_query="USE testdb; SELECT COUNT(*) FROM test_table;"

    if [ "$expected_result" = "success" ]; then
        if mysql_exec_as_user "$username" "$password" "$test_query" 2>/dev/null; then
            print_result "PASS" "$description"
            return 0
        else
            print_result "FAIL" "$description"
            return 1
        fi
    else
        print_debug "Expected error: $test_query"
        local error_output
        error_output=$(mysql_exec_as_user "$username" "$password" "$test_query" true 2>&1)
        if echo "$error_output" | grep -q "ERROR\|Access denied"; then
            print_result "PASS" "$description"
            return 0
        else
            print_result "FAIL" "$description"
            print_debug "No error found in output: $error_output"
            return 1
        fi
    fi
}

# Function to check plugin status
check_plugin_status() {
    local plugin_name="$1"

    print_debug "Checking status of plugin: $plugin_name"

    local status=$(mysql_exec "SELECT PLUGIN_STATUS FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = '$plugin_name';" | tail -n1)

    if [ "$status" = "ACTIVE" ]; then
        print_success "Plugin $plugin_name is ACTIVE"
        return 0
    else
        print_error "Plugin $plugin_name is not active (status: $status)"
        return 1
    fi
}

# Function to install plugin
install_plugin() {
    local plugin_name="$1"
    local plugin_file="$2"

    # Get the plugin directory from MySQL
    local plugin_dir=$(mysql_exec "SHOW VARIABLES LIKE 'plugin_dir';" | tail -n1 | awk '{print $2}')

    # Remove trailing slash if present
    plugin_dir="${plugin_dir%/}"

    # Construct full plugin file path
    local full_plugin_path="$plugin_dir/$plugin_file"

    print_info "Installing plugin: $plugin_name ($full_plugin_path)"

    # Check if plugin file exists at the full path
    if [ "$DOCKER" = false ]; then
        if [ ! -f "$full_plugin_path" ]; then
            print_warning "Plugin file $full_plugin_path not found, skipping $plugin_name tests"
            print_info "Plugin directory: $plugin_dir"
            return 1
        fi
    fi

    # Try to install plugin (MySQL looks in plugin_dir)
    if mysql_exec "INSTALL PLUGIN $plugin_name SONAME '$plugin_file';"; then
        print_success "Plugin $plugin_name installed successfully"
        return 0
    else
        # If plugin already exists, treat as success
        if mysql_exec "SELECT 1 FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = '$plugin_name' AND PLUGIN_STATUS = 'ACTIVE';" >/dev/null 2>&1; then
            print_warning "Plugin $plugin_name already installed and ACTIVE; continuing"
            return 0
        fi
        # Otherwise, attempt to uninstall and reinstall once
        print_warning "Plugin $plugin_name installation failed; attempting reinstall"
        mysql_exec "UNINSTALL PLUGIN $plugin_name;" || true
        if mysql_exec "INSTALL PLUGIN $plugin_name SONAME '$plugin_file';"; then
            print_success "Plugin $plugin_name reinstalled successfully"
            return 0
        else
            print_error "Failed to install plugin $plugin_name"
            return 1
        fi
    fi
}

# Function to test simple authorization plugin
test_simple_authorization_plugin() {
    print_info "=== TESTING SIMPLE AUTHORIZATION PLUGIN ==="

    local plugin_file="simple_authorization.so"
    local plugin_name="simple_authorization"

    # Install plugin
    if ! install_plugin "$plugin_name" "$plugin_file"; then
        return 1
    fi

    # Check plugin status
    if ! check_plugin_status "$plugin_name"; then
        return 1
    fi

    local test_count=0
    local pass_count=0

    local host="localhost"
    if [ "$DOCKER" = true ]; then
        host="%"
    fi

    # Test 1: Plugin in IGNORE mode (should use built-in authorization)
    print_info "Test 1: Plugin in IGNORE mode"
    mysql_exec "SET GLOBAL simple_authorization_mode = 'ignore';"
    mysql_exec "SET GLOBAL simple_authorization_allow_user = DEFAULT;"
    mysql_exec "SET GLOBAL simple_authorization_allow_db = DEFAULT;"

    ((test_count++))
    if test_user_access "testuser_ignore" "password" "User should be denied access (no built-in privileges)" "failure"; then
        ((pass_count++))
    fi

    # Test 2: Plugin in GRANT mode with user allowlist
    print_info "Test 2: Plugin in GRANT mode with user allowlist"
    mysql_exec "SET GLOBAL simple_authorization_mode = 'grant';"
    mysql_exec "SET GLOBAL simple_authorization_allow_user = 'testuser_grant';"
    mysql_exec "SET GLOBAL simple_authorization_allow_db = DEFAULT;"

    ((test_count++))
    if test_user_access "testuser_grant" "password" "User should have access via plugin" "success" true; then
        ((pass_count++))
    fi

    # Test 3: Plugin in GRANT mode with user and database allowlist
    print_info "Test 3: Plugin in GRANT mode with user and database allowlist"
    mysql_exec "SET GLOBAL simple_authorization_mode = 'grant';"
    mysql_exec "SET GLOBAL simple_authorization_allow_user = 'testuser_db';"
    mysql_exec "SET GLOBAL simple_authorization_allow_db = 'testdb';"

    # Create user and test access to allowed database
    mysql_exec "DROP USER IF EXISTS 'testuser_db'@'$host';"
    mysql_exec "CREATE USER 'testuser_db'@'$host' IDENTIFIED BY 'password';"
    mysql_exec "GRANT SELECT ON testdb.* TO 'testuser_db'@'$host';"

    ((test_count++))
    if test_query_success "testuser_db" "password" "testuser_db should access testdb" "USE testdb; SELECT COUNT(*) FROM test_table;"; then
        ((pass_count++))
    fi

    # Test 4: Plugin in DENY mode
    print_info "Test 4: Plugin in DENY mode"
    mysql_exec "SET GLOBAL simple_authorization_mode = 'deny';"
    mysql_exec "SET GLOBAL simple_authorization_allow_user = 'testuser_deny';"
    mysql_exec "SET GLOBAL simple_authorization_allow_db = DEFAULT;"

    ((test_count++))
    if test_user_access "testuser_deny" "password" "User should be denied access (plugin deny mode)" "failure"; then
        ((pass_count++))
    fi

    # Test 5: Different SQL operations
    print_info "Test 5: Different SQL operations with plugin"
    mysql_exec "SET GLOBAL simple_authorization_mode = 'grant';"
    mysql_exec "SET GLOBAL simple_authorization_allow_user = 'testuser_ops';"
    mysql_exec "SET GLOBAL simple_authorization_allow_db = 'testdb';"

    mysql_exec "DROP USER IF EXISTS 'testuser_ops'@'$host';"
    mysql_exec "CREATE USER 'testuser_ops'@'$host' IDENTIFIED BY 'password';"

    # Test SELECT
    ((test_count++))
    if mysql_exec_as_user "testuser_ops" "password" "USE testdb; SELECT * FROM test_table;" 2>/dev/null; then
        print_result "PASS" "SELECT operation allowed"
        ((pass_count++))
    else
        print_result "FAIL" "SELECT operation denied"
    fi

    # Test INSERT (should fail - no INSERT privilege)
    ((test_count++))
    local insert_output
    insert_output=$(mysql_exec_as_user "testuser_ops" "password" "USE testdb; INSERT INTO test_table VALUES (3, 'test', 'data');" true 2>&1)
    if echo "$insert_output" | grep -q "ERROR\|Access denied"; then
        print_result "PASS" "INSERT operation correctly denied"
        ((pass_count++))
    else
        print_result "FAIL" "INSERT operation incorrectly allowed"
        print_debug "Unexpected INSERT output: $insert_output"
    fi

    # Clean up
    mysql_exec "DROP USER IF EXISTS 'testuser_ignore'@'localhost';"
    mysql_exec "DROP USER IF EXISTS 'testuser_grant'@'localhost';"
    mysql_exec "DROP USER IF EXISTS 'testuser_db'@'localhost';"
    mysql_exec "DROP USER IF EXISTS 'testuser_deny'@'localhost';"
    mysql_exec "DROP USER IF EXISTS 'testuser_ops'@'localhost';"

    print_info "Simple Authorization Plugin Test Results: $pass_count/$test_count tests passed"
    return $((test_count - pass_count))
}

# Function to test external authorization plugin
test_external_authorization_plugin() {
    print_info "=== TESTING EXTERNAL AUTHORIZATION PLUGIN ==="

    local plugin_file="external_authorization.so"
    local plugin_name="external_authorization"

    # Install plugin
    if ! install_plugin "$plugin_name" "$plugin_file"; then
        print_warning "External authorization plugin not available, skipping tests"
        return 0
    fi

    # Check plugin status
    if ! check_plugin_status "$plugin_name"; then
        return 1
    fi

    local test_count=0
    local pass_count=0

    # Configure external authorization (without actual external service)
    mysql_exec "SET GLOBAL external_authorization_url = '';"  # Empty URL should make plugin ignore
    mysql_exec "SET GLOBAL external_authorization_timeout = 1000;"

    # Test 1: Plugin with no external URL (should ignore)
    print_info "Test 1: External plugin with no URL (should ignore)"
    ((test_count++))
    if test_user_access "extuser_ignore" "password" "User should be denied (no external service configured)" "failure"; then
        ((pass_count++))
    fi

    # Clean up
    mysql_exec "DROP USER IF EXISTS 'extuser_ignore'@'localhost';"

    print_info "External Authorization Plugin Test Results: $pass_count/$test_count tests passed"
    return $((test_count - pass_count))
}

# Function to cleanup test environment
cleanup_test_environment() {
    print_info "=== CLEANING UP TEST ENVIRONMENT ==="

    # Uninstall plugins
    mysql_exec "UNINSTALL PLUGIN simple_authorization;" 2>/dev/null || true
    mysql_exec "UNINSTALL PLUGIN external_authorization;" 2>/dev/null || true

    # Drop test users
    mysql_exec "DROP USER IF EXISTS 'testuser_ignore'@'localhost';" 2>/dev/null || true
    mysql_exec "DROP USER IF EXISTS 'testuser_grant'@'localhost';" 2>/dev/null || true
    mysql_exec "DROP USER IF EXISTS 'testuser_db'@'localhost';" 2>/dev/null || true
    mysql_exec "DROP USER IF EXISTS 'testuser_deny'@'localhost';" 2>/dev/null || true
    mysql_exec "DROP USER IF EXISTS 'testuser_ops'@'localhost';" 2>/dev/null || true
    mysql_exec "DROP USER IF EXISTS 'extuser_ignore'@'localhost';" 2>/dev/null || true

    # Drop test databases
    mysql_exec "DROP DATABASE IF EXISTS testdb;" 2>/dev/null || true
    mysql_exec "DROP DATABASE IF EXISTS otherdb;" 2>/dev/null || true

    # Reset global variables
    mysql_exec "SET GLOBAL general_log = OFF;" 2>/dev/null || true

    print_success "Test environment cleaned up"
}

# Function to setup test environment
setup_test_environment() {
    print_info "=== SETTING UP TEST ENVIRONMENT ==="

    # Enable general log for debugging
    mysql_exec "SET GLOBAL general_log = ON;"
    mysql_exec "SET GLOBAL general_log_file = '/tmp/mysql-auth-plugin-test.log';"

    # Create test databases
    mysql_exec "DROP DATABASE IF EXISTS testdb;"
    mysql_exec "CREATE DATABASE testdb;"

    mysql_exec "DROP DATABASE IF EXISTS otherdb;"
    mysql_exec "CREATE DATABASE otherdb;"

    # Create test tables
    mysql_exec "USE testdb; CREATE TABLE test_table (id INT PRIMARY KEY, name VARCHAR(50), secret VARCHAR(100));"
    mysql_exec "USE testdb; INSERT INTO test_table VALUES (1, 'public_data', 'not_so_secret'), (2, 'more_data', 'also_secret');"

    mysql_exec "USE otherdb; CREATE TABLE other_table (id INT PRIMARY KEY, data VARCHAR(100));"
    mysql_exec "USE otherdb; INSERT INTO other_table VALUES (1, 'other_data');"

    print_success "Test environment setup complete"
}

# Function to check MySQL connection
check_mysql_connection() {
    print_info "Checking MySQL connection..."

    if ! mysql_exec "SELECT VERSION();" >/dev/null 2>&1; then
        print_error "Cannot connect to MySQL server"
        print_info "Please ensure MySQL server is running and connection parameters are correct"
        print_info "Current connection args: $MYSQL_CONN_ARGS"
        return 1
    fi

    local version=$(mysql_exec "SELECT VERSION();" | tail -n1)
    print_success "Connected to MySQL server: $version"
    return 0
}

# Function to check plugin directory and files
check_plugin_files() {
    print_info "Checking plugin files..."

    local plugin_dir=$(mysql_exec "SHOW VARIABLES LIKE 'plugin_dir';" | tail -n1 | awk '{print $2}')

    if [ -z "$plugin_dir" ]; then
        print_error "Cannot determine plugin directory"
        return 1
    fi

    print_info "Plugin directory: $plugin_dir"

    local simple_plugin="$plugin_dir/simple_authorization.so"
    local external_plugin="$plugin_dir/external_authorization.so"

    if [ -f "$simple_plugin" ]; then
        print_success "Simple authorization plugin found: $simple_plugin"
        SIMPLE_PLUGIN_AVAILABLE=true
    else
        print_warning "Simple authorization plugin not found: $simple_plugin"
        SIMPLE_PLUGIN_AVAILABLE=false
    fi

    if [ -f "$external_plugin" ]; then
        print_success "External authorization plugin found: $external_plugin"
        EXTERNAL_PLUGIN_AVAILABLE=true
    else
        print_warning "External authorization plugin not found: $external_plugin"
        EXTERNAL_PLUGIN_AVAILABLE=false
    fi

    if [ "$SIMPLE_PLUGIN_AVAILABLE" = false ] && [ "$EXTERNAL_PLUGIN_AVAILABLE" = false ]; then
        print_error "No authorization plugins found in plugin directory"
        print_info "Please build MySQL with authorization plugin support and ensure plugin files are in the correct location"
        return 1
    fi

    return 0
}

# Parse command line arguments
SIMPLE_ONLY=false
EXTERNAL_ONLY=false
CLEANUP_ONLY=false
VERBOSE=false
DEBUG=false
DOCKER=false


MYSQL_SOCKET=""
MYSQL_PORT=""
MYSQL_USER="root"
MYSQL_PASSWORD=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --simple-only)
            SIMPLE_ONLY=true
            shift
            ;;
        --external-only)
            EXTERNAL_ONLY=true
            shift
            ;;
        --mysql-socket)
            MYSQL_SOCKET="$2"
            shift 2
            ;;
        --mysql-port)
            MYSQL_PORT="$2"
            shift 2
            ;;
        --mysql-user)
            MYSQL_USER="$2"
            shift 2
            ;;
        --mysql-password)
            MYSQL_PASSWORD="$2"
            shift 2
            ;;
        --docker)
            DOCKER=true
            shift
            ;;
        --cleanup-only)
            CLEANUP_ONLY=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --debug)
            DEBUG=true
            shift
            ;;
        -h|--help)
            echo "MySQL Authorization Plugin Test Script"
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --simple-only          Test only simple_authorization plugin"
            echo "  --external-only        Test only external_authorization plugin"
            echo "  --mysql-socket PATH    MySQL socket path"
            echo "  --mysql-port PORT      MySQL port (for TCP connections)"
            echo "  --mysql-user USER      MySQL user to connect as"
            echo "  --mysql-password PASS  MySQL password"
            echo "  --docker               Use MySQL from Docker container"
            echo "  --cleanup-only         Cleanup test data and exit"
            echo "  --verbose              Enable verbose output"
            echo "  --debug                Enable debug output"
            echo "  -h, --help             Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0 --mysql-socket /tmp/mysql.sock"
            echo "  $0 --mysql-port 3306 --mysql-user root --mysql-password mypass"
            echo "  $0 --simple-only --verbose"
            echo "  $0 --docker --external-only"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Build MySQL connection arguments for admin/root user
MYSQL_CONN_ARGS="-u $MYSQL_USER"
if [ -n "$MYSQL_PASSWORD" ]; then
    MYSQL_CONN_ARGS="$MYSQL_CONN_ARGS -p$MYSQL_PASSWORD"
fi

# Transport-only args (for secondary connections as different users)
MYSQL_TRANSPORT_ARGS=""
if [ -n "$MYSQL_SOCKET" ]; then
    MYSQL_CONN_ARGS="$MYSQL_CONN_ARGS --socket=$MYSQL_SOCKET"
    MYSQL_TRANSPORT_ARGS="--socket=$MYSQL_SOCKET"
elif [ "$DOCKER" = true ]; then
    MYSQL_CONN_ARGS="$MYSQL_CONN_ARGS --socket=/var/run/mysqld/mysqld.sock"
    MYSQL_TRANSPORT_ARGS="--socket=/var/run/mysqld/mysqld.sock"
elif [ -n "$MYSQL_PORT" ]; then
    MYSQL_CONN_ARGS="$MYSQL_CONN_ARGS --port=$MYSQL_PORT --host=127.0.0.1"
    MYSQL_TRANSPORT_ARGS="--port=$MYSQL_PORT --host=127.0.0.1"
fi

# Enable verbose/debug output
if [ "$VERBOSE" = true ] || [ "$DEBUG" = true ]; then
    set -x
fi

print_info "=== MYSQL AUTHORIZATION PLUGIN TEST SCRIPT ==="
print_info "Connection args: $MYSQL_CONN_ARGS"

# Handle cleanup-only mode
if [ "$CLEANUP_ONLY" = true ]; then
    if check_mysql_connection; then
        cleanup_test_environment
    else
        print_error "Cannot connect to MySQL, cannot cleanup"
        exit 1
    fi
    exit 0
fi

# Main test execution
main() {
    local total_failures=0

    # Check MySQL connection
    if ! check_mysql_connection; then
        exit 1
    fi

    if [ "$DOCKER" = false ]; then
        # Check plugin files
        if ! check_plugin_files; then
            exit 1
        fi
    fi

    # Setup test environment
    setup_test_environment

    # Test plugins
    if [ "$EXTERNAL_ONLY" = false ]; then
        if [ "$SIMPLE_PLUGIN_AVAILABLE" = true ] || [ "$DOCKER" = true ]; then
            if ! test_simple_authorization_plugin; then
                ((total_failures++))
            fi
        else
            print_warning "Simple authorization plugin not available, skipping tests"
        fi
    fi

    if [ "$SIMPLE_ONLY" = false ]; then
        if [ "$EXTERNAL_PLUGIN_AVAILABLE" = true ] || [ "$DOCKER" = true ]; then
            if ! test_external_authorization_plugin; then
                ((total_failures++))
            fi
        else
            print_warning "External authorization plugin not available, skipping tests"
        fi
    fi

    # Cleanup
    cleanup_test_environment

    # Summary
    if [ $total_failures -eq 0 ]; then
        print_success "All authorization plugin tests passed!"
        exit 0
    else
        print_error "$total_failures plugin test(s) failed"
        exit 1
    fi
}

# Run main function
main
