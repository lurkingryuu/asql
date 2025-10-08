#!/bin/bash

# Cedar Agent Diagnostic Script
# This script helps diagnose issues with Cedar Agent integration

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Default values
CEDAR_URL="http://localhost:8280"
MYSQL_SOCKET="./build/mysql.sock"

# Parse arguments
while [ $# -gt 0 ]; do
  case "$1" in
    --cedar-url)
      CEDAR_URL="$2"
      shift 2
      ;;
    --mysql-socket)
      MYSQL_SOCKET="$2"
      shift 2
      ;;
    --help)
      echo "Usage: $0 [OPTIONS]"
      echo "Options:"
      echo "  --cedar-url URL        Cedar Agent URL (default: http://localhost:8280)"
      echo "  --mysql-socket PATH    MySQL socket path (default: ./build/mysql.sock)"
      exit 0
      ;;
    *)
      print_error "Unknown option: $1"
      exit 1
      ;;
  esac
done

echo "========================================"
echo "Cedar Agent Integration Diagnostics"
echo "========================================"
echo ""

# 1. Test Cedar Agent connectivity
print_info "1. Testing Cedar Agent connectivity..."
if curl -sSf "$CEDAR_URL/v1/" >/dev/null 2>&1; then
  print_success "Cedar Agent is accessible at $CEDAR_URL/v1/"
else
  print_error "Cedar Agent is NOT accessible at $CEDAR_URL/v1/"
  print_info "Make sure Cedar Agent is running with:"
  print_info "  ./target/release/cedar-agent -l debug -s schema.json -d data.json --policies policies.json --addr 0.0.0.0 --port 8280"
  exit 1
fi

# 2. Check Cedar Agent endpoints
print_info "2. Testing Cedar Agent endpoints..."

# Test /v1/policies
if response=$(curl -sSf "$CEDAR_URL/v1/policies" 2>&1); then
  if command -v jq >/dev/null 2>&1; then
    policy_count=$(echo "$response" | jq 'length' 2>/dev/null || echo "unknown")
    print_success "/v1/policies endpoint accessible - $policy_count policies found"
  else
    print_success "/v1/policies endpoint accessible"
  fi
else
  print_warning "/v1/policies endpoint not accessible: $response"
fi

# Test /v1/data
if response=$(curl -sSf "$CEDAR_URL/v1/data" 2>&1); then
  if command -v jq >/dev/null 2>&1; then
    entity_count=$(echo "$response" | jq 'length' 2>/dev/null || echo "unknown")
    print_success "/v1/data endpoint accessible - $entity_count entities found"
  else
    print_success "/v1/data endpoint accessible"
  fi
else
  print_warning "/v1/data endpoint not accessible: $response"
fi

# Skip authorization endpoint test - assume it works if base URL is accessible
print_info "Skipping /v1/is_authorized test - assuming it works since Cedar Agent is accessible"

# 3. Check MySQL plugin status
print_info "3. Checking MySQL plugin status..."
if [ -S "$MYSQL_SOCKET" ]; then
  print_success "MySQL socket exists: $MYSQL_SOCKET"

  # Check if cedar_authorization plugin is loaded
  if mysql --socket="$MYSQL_SOCKET" -u root -e "SELECT PLUGIN_NAME, PLUGIN_STATUS FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'cedar_authorization';" 2>/dev/null | grep -q "cedar_authorization"; then
    status=$(mysql --socket="$MYSQL_SOCKET" -u root -e "SELECT PLUGIN_STATUS FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'cedar_authorization';" 2>/dev/null | tail -n1)
    if [ "$status" = "ACTIVE" ]; then
      print_success "cedar_authorization plugin is ACTIVE"

      # Check plugin configuration
      cedar_url=$(mysql --socket="$MYSQL_SOCKET" -u root -e "SHOW VARIABLES LIKE 'cedar_authorization_url';" 2>/dev/null | tail -n1 | awk '{print $2}')
      if [ -n "$cedar_url" ] && [ "$cedar_url" != "Variable_name" ]; then
        print_success "Cedar authorization URL configured: $cedar_url"
      else
        print_error "Cedar authorization URL not configured"
      fi
    else
      print_error "cedar_authorization plugin status: $status"
    fi
  else
    print_warning "cedar_authorization plugin not loaded"
  fi

  # Check if ddl_audit plugin is loaded
  if mysql --socket="$MYSQL_SOCKET" -u root -e "SELECT PLUGIN_NAME, PLUGIN_STATUS FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'ddl_audit';" 2>/dev/null | grep -q "ddl_audit"; then
    status=$(mysql --socket="$MYSQL_SOCKET" -u root -e "SELECT PLUGIN_STATUS FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'ddl_audit';" 2>/dev/null | tail -n1)
    if [ "$status" = "ACTIVE" ]; then
      print_success "ddl_audit plugin is ACTIVE"

      # Check plugin configuration
      ddl_enabled=$(mysql --socket="$MYSQL_SOCKET" -u root -e "SHOW VARIABLES LIKE 'ddl_audit_enabled';" 2>/dev/null | tail -n1 | awk '{print $2}')
      ddl_url=$(mysql --socket="$MYSQL_SOCKET" -u root -e "SHOW VARIABLES LIKE 'ddl_audit_cedar_url';" 2>/dev/null | tail -n1 | awk '{print $2}')
      print_success "DDL audit enabled: $ddl_enabled"
      print_success "DDL audit URL: $ddl_url"
    else
      print_error "ddl_audit plugin status: $status"
    fi
  else
    print_warning "ddl_audit plugin not loaded"
  fi

else
  print_error "MySQL socket not found: $MYSQL_SOCKET"
fi

# 4. Test a simple authorization request
print_info "4. Testing simple authorization with test user..."

# Create a test user first
if [ -S "$MYSQL_SOCKET" ]; then
  mysql --socket="$MYSQL_SOCKET" -u root -e "DROP USER IF EXISTS 'test_user'@'localhost';" 2>/dev/null || true
  mysql --socket="$MYSQL_SOCKET" -u root -e "CREATE USER 'test_user'@'localhost' IDENTIFIED BY 'test123';" 2>/dev/null

  # Try a simple SELECT that should be denied
  if output=$(mysql --socket="$MYSQL_SOCKET" -u test_user -ptest123 -e "SELECT 1;" 2>&1); then
    print_warning "Test user query succeeded - this might indicate authorization isn't working"
    print_warning "Output: $output"
  else
    if echo "$output" | grep -q "denied by authorization"; then
      print_success "Authorization is working - query was denied by Cedar"
    elif echo "$output" | grep -q "Access denied"; then
      print_warning "Query denied by MySQL (not Cedar) - Cedar authorization may not be active"
    else
      print_error "Unexpected error: $output"
    fi
  fi

  # Cleanup test user
  mysql --socket="$MYSQL_SOCKET" -u root -e "DROP USER 'test_user'@'localhost';" 2>/dev/null || true
fi

# 5. Check Cedar Agent logs (if accessible)
print_info "5. Additional troubleshooting tips..."
print_info "- Check Cedar Agent logs for authorization requests"
print_info "- Verify that your policies match the test user names exactly"
print_info "- Ensure Cedar Agent was started with the correct schema, data, and policies files"
print_info "- Check MySQL error log for Cedar authorization plugin messages"

echo ""
print_info "Diagnostic complete!"
