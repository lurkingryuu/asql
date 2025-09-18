#!/bin/bash

# Comprehensive test script for the Cedar Authorization Plugin
# Modeled after: test_authorization_plugin.sh
#
# Features:
# - Installs and validates the cedar_authorization plugin
# - Optionally starts the mock Cedar service (Flask) for end-to-end tests
# - Sets cedar_authorization_url and timeout
# - Creates test databases/tables/users and runs policy-driven checks
# - Exercises success/failure cases, including time/day/IP-based policies when feasible
# - Provides cleanup facilities
#
# Options:
# --mysql-socket PATH       MySQL socket path
# --mysql-port PORT         MySQL TCP port (with host=127.0.0.1)
# --mysql-user USER         MySQL admin user (default: root)
# --mysql-password PASS     MySQL admin password
# --docker                  Execute mysql inside docker compose svc `mysql`
# --start-cedar-service     Start mock Cedar Flask service locally
# --cedar-service-port PORT Port for mock Cedar service (default: 8180)
# --cedar-url URL           Override cedar_authorization_url (default: http://localhost:8180/v1/is_authorized)
# --cleanup-only            Cleanup data and uninstall plugin; do not run tests
# --verbose                 Enable verbose output (set -x)
# --debug                   Enable debug messages

set -e

# Globals
CEDAR_SERVICE_PID=""

# Traps for cleanup
cleanup_on_exit() {
  print_debug "Cleaning up on exit..."
  stop_cedar_service
  exit 0
}
trap cleanup_on_exit SIGINT SIGTERM EXIT

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

# Printers
print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }
print_debug() { if [ "$DEBUG" = true ]; then echo -e "${PURPLE}[DEBUG]${NC} $1"; fi; }
print_test() { echo -e "${CYAN}[TEST]${NC} $1"; }
print_result() {
  case "$1" in
    PASS) echo -e "${GREEN}[PASS]${NC} $2";;
    FAIL) echo -e "${RED}[FAIL]${NC} $2";;
    SKIP) echo -e "${YELLOW}[SKIP]${NC} $2";;
  esac
}

# MySQL helpers (admin)
mysql_exec() {
  local query="$1"; local expect_error="${2:-false}"; local mysql_cmd
  if [ "$DOCKER" = true ]; then mysql_cmd="docker compose exec mysql mysql"; else mysql_cmd="mysql"; fi
  print_debug "Executing as admin: $query"
  if [ "$expect_error" = true ]; then
    local output; output=$( $mysql_cmd $MYSQL_CONN_ARGS -e "$query" 2>&1 ) || return 0; echo "$output"; return 0
  else
    $mysql_cmd $MYSQL_CONN_ARGS -e "$query"
  fi
}

# MySQL helpers (as user)
mysql_exec_as_user() {
  local username="$1"; local password="$2"; local query="$3"; local expect_error="${4:-false}"; local mysql_cmd
  if [ "$DOCKER" = true ]; then mysql_cmd="docker compose exec mysql mysql"; else mysql_cmd="mysql"; fi
  local -a user_conn_args=( -u "$username" -p"$password" );
  if [ -n "$MYSQL_TRANSPORT_ARGS" ]; then user_conn_args+=( $MYSQL_TRANSPORT_ARGS ); fi
  print_debug "Executing as user $username: $query"
  if [ "$expect_error" = true ]; then
    local output; local exit_code; output=$( $mysql_cmd "${user_conn_args[@]}" -e "$query" 2>&1 || true); exit_code=$?; echo "$output"; return 0
  else
    $mysql_cmd "${user_conn_args[@]}" -e "$query"
  fi
}

# Test helpers
test_query_success() {
  local user="$1"; local pass="$2"; local desc="$3"; local query="$4"
  print_test "$desc"
  local output
  if output=$(mysql_exec_as_user "$user" "$pass" "$query" 2>&1); then
    print_result PASS "$desc"
    return 0
  else
    print_result FAIL "$desc"
    print_error "Command output: $output"
    return 1
  fi
}

test_query_failure() {
  local user="$1"; local pass="$2"; local desc="$3"; local query="$4"
  print_test "$desc"
  local out; out=$( mysql_exec_as_user "$user" "$pass" "$query" true 2>&1 )
  if echo "$out" | grep -q "ERROR\|Access denied"; then print_result PASS "$desc"; return 0; else print_result FAIL "$desc"; print_debug "No error in: $out"; return 1; fi
}

# Plugin helpers
check_plugin_status() {
  local plugin_name="$1"
  print_debug "Checking status of plugin: $plugin_name"
  local status=$( mysql_exec "SELECT PLUGIN_STATUS FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = '$plugin_name';" | tail -n1 )
  if [ "$status" = "ACTIVE" ]; then print_success "Plugin $plugin_name is ACTIVE"; return 0; else print_error "Plugin $plugin_name not active (status: $status)"; return 1; fi
}

install_plugin() {
  local plugin_name="$1"; local plugin_file="$2"
  local plugin_dir=$( mysql_exec "SHOW VARIABLES LIKE 'plugin_dir';" | tail -n1 | awk '{print $2}' )
  plugin_dir="${plugin_dir%/}"
  local full_plugin_path="$plugin_dir/$plugin_file"
  print_info "Installing plugin: $plugin_name ($full_plugin_path)"
  if [ "$DOCKER" = false ]; then
    if [ ! -f "$full_plugin_path" ]; then
      print_warning "Plugin file not found: $full_plugin_path"
      return 1
    fi
  fi
  if mysql_exec "INSTALL PLUGIN $plugin_name SONAME '$plugin_file';"; then
    print_success "Installed $plugin_name"; return 0
  else
    if mysql_exec "SELECT 1 FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME='$plugin_name' AND PLUGIN_STATUS='ACTIVE';" >/dev/null 2>&1; then
      print_warning "$plugin_name already installed; continuing"; return 0
    fi
    print_warning "Install failed; retrying after uninstall"
    mysql_exec "UNINSTALL PLUGIN $plugin_name;" || true
    if mysql_exec "INSTALL PLUGIN $plugin_name SONAME '$plugin_file';"; then print_success "Reinstalled $plugin_name"; return 0; else print_error "Failed to install $plugin_name"; return 1; fi
  fi
}

# Cedar service helpers (mock Flask server)
start_cedar_service() {
  local script_path="plugin/authorization/mock_cedar_service.py"
  print_info "Starting mock Cedar service on port $CEDAR_SERVICE_PORT..."
  if [ ! -f "$script_path" ]; then print_error "Mock service not found: $script_path"; return 1; fi
  if ! command -v python3 >/dev/null 2>&1; then print_error "python3 not found; cannot start mock Cedar service"; return 1; fi
  # Quick check for Flask import without failing hard
  if ! python3 -c "import flask" >/dev/null 2>&1; then
    print_warning "Flask not available; mock service may fail to start"
  fi
  if lsof -Pi :$CEDAR_SERVICE_PORT -sTCP:LISTEN >/dev/null 2>&1; then
    print_warning "Port $CEDAR_SERVICE_PORT in use; assuming service is running"
    return 0
  fi
  local log_path="plugin/authorization/mock_cedar_service.log"
  nohup python3 "$script_path" --host localhost --port "$CEDAR_SERVICE_PORT" > "$log_path" 2>&1 &
  CEDAR_SERVICE_PID=$!
  local attempts=0 max_attempts=15
  while [ $attempts -lt $max_attempts ]; do
    if curl -sSf "http://localhost:$CEDAR_SERVICE_PORT/health" >/dev/null 2>&1; then
      print_success "Mock Cedar service started (PID: $CEDAR_SERVICE_PID)"
      return 0
    fi
    sleep 1; attempts=$((attempts+1)); print_debug "Waiting for Cedar service ($attempts/$max_attempts)"
  done
  print_error "Failed to start mock Cedar service"
  return 1
}

stop_cedar_service() {
  if [ -n "$CEDAR_SERVICE_PID" ]; then
    print_info "Stopping mock Cedar service (PID: $CEDAR_SERVICE_PID)"
    kill "$CEDAR_SERVICE_PID" 2>/dev/null || true
    wait "$CEDAR_SERVICE_PID" 2>/dev/null || true
    CEDAR_SERVICE_PID=""
    print_success "Mock Cedar service stopped"
  fi
}

check_cedar_service() {
  curl -sSf "http://localhost:$CEDAR_SERVICE_PORT/health" >/dev/null 2>&1
}

# Env setup/cleanup
setup_test_environment() {
  print_info "=== SETTING UP CEDAR TEST ENVIRONMENT ==="
  mysql_exec "SET GLOBAL general_log = ON;" 2>/dev/null || true
  mysql_exec "SET GLOBAL general_log_file = '/tmp/mysql-cedar-plugin-test.log';" 2>/dev/null || true

  # Create DBs/tables
  mysql_exec "DROP DATABASE IF EXISTS test_cedar_db;"
  mysql_exec "CREATE DATABASE test_cedar_db;"
  mysql_exec "USE test_cedar_db; CREATE TABLE employees (id INT PRIMARY KEY AUTO_INCREMENT, name VARCHAR(100) NOT NULL, department VARCHAR(50), salary DECIMAL(10,2), hire_date DATE);"
  mysql_exec "USE test_cedar_db; CREATE TABLE orders (order_id INT PRIMARY KEY AUTO_INCREMENT, customer_id INT, product_name VARCHAR(100), quantity INT, order_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP);"
  mysql_exec "USE test_cedar_db; CREATE TABLE payroll (employee_id INT PRIMARY KEY, base_salary DECIMAL(10,2), bonus DECIMAL(10,2), tax_deductions DECIMAL(10,2), net_pay DECIMAL(10,2));"
  mysql_exec "USE test_cedar_db; CREATE TABLE public_data (id INT PRIMARY KEY AUTO_INCREMENT, title VARCHAR(100), description TEXT, created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP);"

  # Seed data
  mysql_exec "USE test_cedar_db; INSERT INTO employees (name, department, salary, hire_date) VALUES ('John Doe','Engineering',75000.00,'2023-01-15'),('Jane Smith','Marketing',65000.00,'2023-02-20'),('Bob Johnson','Sales',55000.00,'2023-03-10'),('Alice Williams','HR',70000.00,'2023-04-05');"
  mysql_exec "USE test_cedar_db; INSERT INTO orders (customer_id, product_name, quantity) VALUES (1,'Widget A',10),(2,'Widget B',5),(3,'Widget C',8),(1,'Widget D',12);"
  mysql_exec "USE test_cedar_db; INSERT INTO payroll (employee_id, base_salary, bonus, tax_deductions, net_pay) VALUES (1,75000.00,5000.00,15000.00,65000.00),(2,65000.00,3000.00,12000.00,56000.00),(3,55000.00,2000.00,10000.00,47000.00),(4,70000.00,4000.00,14000.00,60000.00);"
  mysql_exec "USE test_cedar_db; INSERT INTO public_data (title, description) VALUES ('Public Announcement 1','This is public information available to all users.'),('Public Announcement 2','Another piece of public information.'),('Public FAQ','Frequently asked questions and answers.');"

  # Stored procedure
  mysql_exec "USE test_cedar_db; DROP PROCEDURE IF EXISTS calculate_bonus;" || true
  # Create procedure via stdin so DELIMITER works (mysql -e does not support DELIMITER)
  if [ "$DOCKER" = true ]; then
    docker compose exec -T mysql mysql $MYSQL_CONN_ARGS <<'SQL'
USE test_cedar_db;
DELIMITER //
CREATE PROCEDURE calculate_bonus(IN emp_id INT)
BEGIN
  DECLARE bonus_amount DECIMAL(10,2);
  SELECT salary * 0.1 INTO bonus_amount FROM employees WHERE id = emp_id;
  SELECT CONCAT('Bonus for employee ', emp_id, ' is: $', bonus_amount) AS result;
END //
DELIMITER ;
SQL
  else
    mysql $MYSQL_CONN_ARGS <<'SQL'
USE test_cedar_db;
DELIMITER //
CREATE PROCEDURE calculate_bonus(IN emp_id INT)
BEGIN
  DECLARE bonus_amount DECIMAL(10,2);
  SELECT salary * 0.1 INTO bonus_amount FROM employees WHERE id = emp_id;
  SELECT CONCAT('Bonus for employee ', emp_id, ' is: $', bonus_amount) AS result;
END //
DELIMITER ;
SQL
  fi

  # Users
  local host="localhost"; if [ "$DOCKER" = true ]; then host="%"; fi
  for u in alice bob charlie maintenance admin hr_user developer auditor; do
    mysql_exec "DROP USER IF EXISTS '$u'@'$host';" 2>/dev/null || true
    mysql_exec "CREATE USER '$u'@'$host' IDENTIFIED BY 'password123';"
  done

  # No explicit grants needed - Cedar plugin handles all authorization decisions
  # This allows us to test pure policy-based authorization without built-in ACL interference


  print_success "Cedar test environment ready"
}

cleanup_test_environment() {
  print_info "=== CLEANING UP CEDAR TEST ENVIRONMENT ==="
  if [ "$START_CEDAR_SERVICE" = true ]; then stop_cedar_service; fi
  mysql_exec "SET GLOBAL general_log = OFF;" 2>/dev/null || true
  mysql_exec "SET GLOBAL cedar_authorization_url = DEFAULT;" 2>/dev/null || true
  mysql_exec "SET GLOBAL cedar_authorization_timeout = DEFAULT;" 2>/dev/null || true
  mysql_exec "UNINSTALL PLUGIN cedar_authorization;" 2>/dev/null || true
  local hosts=("localhost" "%"); for h in "${hosts[@]}"; do for u in alice bob charlie maintenance admin hr_user developer auditor; do mysql_exec "DROP USER IF EXISTS '$u'@'$h';" 2>/dev/null || true; done; done
  mysql_exec "DROP DATABASE IF EXISTS test_cedar_db;" 2>/dev/null || true
  print_success "Cleanup complete"
}

# Plugin file presence check
check_cedar_plugin_file() {
  print_info "Checking cedar plugin file..."
  local dir=$( mysql_exec "SHOW VARIABLES LIKE 'plugin_dir';" | tail -n1 | awk '{print $2}' )
  if [ -z "$dir" ]; then print_error "Cannot determine plugin_dir"; return 1; fi
  local so="$dir/cedar_authorization.so"
  if [ -f "$so" ] || [ "$DOCKER" = true ]; then print_success "Found cedar plugin: $so"; return 0; else print_warning "Cedar plugin not found: $so"; return 1; fi
}

# Tests for Cedar policies
run_cedar_tests() {
  print_info "=== RUNNING CEDAR AUTHORIZATION TESTS ==="

  # Install and configure plugin
  if ! install_plugin "cedar_authorization" "cedar_authorization.so"; then
    print_error "Cedar plugin not available; aborting tests"
    return 1
  fi
  if ! check_plugin_status "cedar_authorization"; then return 1; fi

  # Configure URL/timeout
  local url_to_use="$CEDAR_URL"
  mysql_exec "SET GLOBAL cedar_authorization_url = '$url_to_use';"
  mysql_exec "SET GLOBAL cedar_authorization_timeout = ${CEDAR_TIMEOUT_MS};"

  # If requested, start mock Cedar service
  local cedar_started=false
  if [ "$START_CEDAR_SERVICE" = true ]; then
    if start_cedar_service; then cedar_started=true; else print_warning "Failed to start mock service; tests may fail"; fi
  elif check_cedar_service; then
    print_info "Mock Cedar service already running; proceeding"
  else
    print_warning "No Cedar service detected on $CEDAR_URL; tests will likely be denied"
  fi

  local test_count=0; local pass_count=0
  local host="localhost"; [ "$DOCKER" = true ] && host="%"

  # 1. Alice SELECT employees (Allow)
  ((test_count++))
  if test_query_success "alice" "password123" "Alice can SELECT employees" "USE test_cedar_db; SELECT * FROM employees LIMIT 1;"; then ((pass_count++)); fi

  # 2. Bob time-based access to orders
  ((test_count++))
  current_hms=$(date +%H%M%S); current_hour=$(date +%H)
  if [ $current_hms -ge 90000 ] && [ $current_hms -le 170000 ]; then
    if test_query_success "bob" "password123" "Bob allowed during business hours (${current_hour}h)" "USE test_cedar_db; SELECT * FROM orders LIMIT 1;"; then ((pass_count++)); fi
  else
    # Expect denial outside 9-17
    if test_query_failure "bob" "password123" "Bob denied outside business hours (${current_hour}h)" "USE test_cedar_db; SELECT * FROM orders LIMIT 1;"; then ((pass_count++)); fi
  fi

  # 3. Charlie IP-based access (likely denied on localhost)
  ((test_count++))
  if test_query_failure "charlie" "password123" "Charlie denied from non-internal IP" "USE test_cedar_db; SELECT * FROM public_data LIMIT 1;"; then ((pass_count++)); fi

  # 4. Maintenance weekend DDL (Create)
  day=$(date +%a | tr '[:upper:]' '[:lower:]')
  ((test_count++))
  if [ "$day" = "sat" ] || [ "$day" = "sun" ]; then
    if test_query_success "maintenance" "password123" "Maintenance can CREATE table on weekends" "USE test_cedar_db; CREATE TABLE IF NOT EXISTS maintenance_log (id INT, operation VARCHAR(100)); DROP TABLE maintenance_log;"; then ((pass_count++)); fi
  else
    if test_query_failure "maintenance" "password123" "Maintenance denied DDL on weekdays" "USE test_cedar_db; CREATE TABLE maintenance_log (id INT); DROP TABLE maintenance_log;"; then ((pass_count++)); fi
  fi

  # 5. Admin weekday access (any action)
  ((test_count++))
  if [ "$day" = "mon" ] || [ "$day" = "tue" ] || [ "$day" = "wed" ] || [ "$day" = "thu" ] || [ "$day" = "fri" ]; then
    if test_query_success "admin" "password123" "Admin can CREATE/DROP on weekdays" "CREATE DATABASE IF NOT EXISTS test_admin_db; DROP DATABASE IF EXISTS test_admin_db;"; then ((pass_count++)); fi
  else
    if test_query_failure "admin" "password123" "Admin denied DDL on weekends" "CREATE DATABASE IF NOT EXISTS test_admin_db; DROP DATABASE IF EXISTS test_admin_db;"; then ((pass_count++)); fi
  fi

  # 6. Payroll access restriction (deny outside 08:00-18:00)
  ((test_count++))
  if [ $current_hms -ge 80000 ] && [ $current_hms -le 180000 ]; then
    if test_query_success "alice" "password123" "Payroll allowed during business hours" "USE test_cedar_db; SELECT * FROM payroll LIMIT 1;"; then ((pass_count++)); fi
  else
    if test_query_failure "alice" "password123" "Payroll denied outside business hours" "USE test_cedar_db; SELECT * FROM payroll LIMIT 1;"; then ((pass_count++)); fi
  fi

  # 7. Delete restrictions: alice allowed, bob denied
  ((test_count++))
  if test_query_success "alice" "password123" "Alice can DELETE (restricted user)" "USE test_cedar_db; START TRANSACTION; DELETE FROM orders WHERE order_id = 1; ROLLBACK;"; then ((pass_count++)); fi
  ((test_count++))
  if test_query_failure "bob" "password123" "Bob cannot DELETE" "USE test_cedar_db; DELETE FROM orders WHERE order_id = -1;"; then ((pass_count++)); fi

  # 8. Column-level access for HR on salary (IP/time constraints in mock -> often denied on localhost)
  ((test_count++))
  if test_query_failure "hr_user" "password123" "HR salary column likely denied (env constraints)" "USE test_cedar_db; SELECT salary FROM employees LIMIT 1;"; then ((pass_count++)); fi

  # 9. Procedure execution for developer during weekday business hours
  ((test_count++))
  if [ $current_hms -ge 90000 ] && [ $current_hms -le 170000 ] && { [ "$day" = "mon" ] || [ "$day" = "tue" ] || [ "$day" = "wed" ] || [ "$day" = "thu" ] || [ "$day" = "fri" ]; }; then
    if test_query_success "developer" "password123" "Developer can CALL procedure in business hours" "USE test_cedar_db; CALL calculate_bonus(1);"; then ((pass_count++)); fi
  else
    if test_query_failure "developer" "password123" "Developer denied CALL outside policy window" "USE test_cedar_db; CALL calculate_bonus(1);"; then ((pass_count++)); fi
  fi

  # 10. Auditor read access
  ((test_count++))
  if test_query_success "auditor" "password123" "Auditor can SELECT employees" "USE test_cedar_db; SELECT * FROM employees LIMIT 1;"; then ((pass_count++)); fi

  # Error scenarios: invalid URL and service down
  ((test_count++))
  mysql_exec "SET GLOBAL cedar_authorization_url = 'http://invalid-url:9999';"
  # Use a table-select so privileges are required and plugin is consulted
  if test_query_failure "alice" "password123" "DENY when cedar URL invalid" "USE test_cedar_db; SELECT * FROM employees LIMIT 1;"; then ((pass_count++)); fi
  mysql_exec "SET GLOBAL cedar_authorization_url = '$url_to_use';"

  print_info "Cedar Plugin Test Results: $pass_count/$test_count tests passed"
  [ $test_count -eq $pass_count ]
}

# Parse arguments
DOCKER=false
VERBOSE=false
DEBUG=false
CLEANUP_ONLY=false
START_CEDAR_SERVICE=false
CEDAR_SERVICE_PORT=8180
CEDAR_TIMEOUT_MS=5000
CEDAR_URL="http://localhost:8180/v1/is_authorized"

MYSQL_SOCKET=""
MYSQL_PORT=""
MYSQL_USER="root"
MYSQL_PASSWORD=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mysql-socket) MYSQL_SOCKET="$2"; shift 2;;
    --mysql-port) MYSQL_PORT="$2"; shift 2;;
    --mysql-user) MYSQL_USER="$2"; shift 2;;
    --mysql-password) MYSQL_PASSWORD="$2"; shift 2;;
    --docker) DOCKER=true; shift;;
    --start-cedar-service) START_CEDAR_SERVICE=true; shift;;
    --cedar-service-port) CEDAR_SERVICE_PORT="$2"; shift 2;;
    --cedar-url) CEDAR_URL="$2"; shift 2;;
    --cedar-timeout-ms) CEDAR_TIMEOUT_MS="$2"; shift 2;;
    --cleanup-only) CLEANUP_ONLY=true; shift;;
    --verbose) VERBOSE=true; shift;;
    --debug) DEBUG=true; shift;;
    -h|--help)
      echo "Cedar Authorization Plugin Test Script"
      echo "Usage: $0 [OPTIONS]"
      echo ""
      echo "Options:"
      echo "  --mysql-socket PATH      MySQL socket path"
      echo "  --mysql-port PORT        MySQL TCP port (host=127.0.0.1)"
      echo "  --mysql-user USER        MySQL admin user (default: root)"
      echo "  --mysql-password PASS    MySQL admin password"
      echo "  --docker                 Use docker compose mysql"
      echo "  --start-cedar-service    Start local mock Cedar Flask service"
      echo "  --cedar-service-port N   Mock Cedar port (default: 8180)"
      echo "  --cedar-url URL          cedar_authorization_url (default: http://localhost:8180/v1/is_authorized)"
      echo "  --cedar-timeout-ms N     cedar_authorization_timeout (default: 5000)"
      echo "  --cleanup-only           Cleanup and exit"
      echo "  --verbose                Enable verbose output"
      echo "  --debug                  Enable debug messages"
      echo "  -h, --help               Show this help"
      echo ""
      echo "Examples:"
      echo "  $0 --mysql-socket /tmp/mysql.sock --start-cedar-service"
      echo "  $0 --mysql-port 3306 --mysql-user root --mysql-password secret --cedar-url http://localhost:8180/v1/is_authorized"
      exit 0
      ;;
    *) print_error "Unknown option: $1"; echo "Use --help for usage"; exit 1;;
  esac
done

# Build MySQL connection args
MYSQL_CONN_ARGS="-u $MYSQL_USER"
if [ -n "$MYSQL_PASSWORD" ]; then MYSQL_CONN_ARGS="$MYSQL_CONN_ARGS -p$MYSQL_PASSWORD"; fi
MYSQL_TRANSPORT_ARGS=""
if [ -n "$MYSQL_SOCKET" ]; then
  MYSQL_CONN_ARGS="$MYSQL_CONN_ARGS --socket=$MYSQL_SOCKET"; MYSQL_TRANSPORT_ARGS="--socket=$MYSQL_SOCKET"
elif [ "$DOCKER" = true ]; then
  MYSQL_CONN_ARGS="$MYSQL_CONN_ARGS --socket=/var/run/mysqld/mysqld.sock"; MYSQL_TRANSPORT_ARGS="--socket=/var/run/mysqld/mysqld.sock"
elif [ -n "$MYSQL_PORT" ]; then
  MYSQL_CONN_ARGS="$MYSQL_CONN_ARGS --port=$MYSQL_PORT --host=127.0.0.1"; MYSQL_TRANSPORT_ARGS="--port=$MYSQL_PORT --host=127.0.0.1"
fi

if [ "$VERBOSE" = true ] || [ "$DEBUG" = true ]; then set -x; fi

print_info "=== CEDAR AUTHORIZATION PLUGIN TEST SCRIPT ==="
print_info "Connection args: $MYSQL_CONN_ARGS"

check_mysql_connection() {
  print_info "Checking MySQL connection..."
  if ! mysql_exec "SELECT VERSION();" >/dev/null 2>&1; then
    print_error "Cannot connect to MySQL"
    print_info "Current args: $MYSQL_CONN_ARGS"
    return 1
  fi
  local version=$( mysql_exec "SELECT VERSION();" | tail -n1 )
  print_success "Connected to MySQL: $version"
  return 0
}

# Cleanup-only mode
if [ "$CLEANUP_ONLY" = true ]; then
  if check_mysql_connection; then cleanup_test_environment; else print_error "Cannot connect to MySQL; cleanup aborted"; exit 1; fi
  exit 0
fi

main() {
  local failures=0
  if ! check_mysql_connection; then exit 1; fi
  if [ "$DOCKER" = false ]; then check_cedar_plugin_file || print_warning "Proceeding without verifying .so file"; fi
  setup_test_environment
  if ! run_cedar_tests; then failures=$((failures+1)); fi
  cleanup_test_environment
  if [ $failures -eq 0 ]; then print_success "All Cedar plugin tests passed!"; exit 0; else print_error "$failures Cedar test suite(s) failed"; exit 1; fi
}

main
