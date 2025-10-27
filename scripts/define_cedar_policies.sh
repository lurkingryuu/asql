#!/bin/bash

# Define Cedar Policies Script
# This script loads policies to Cedar Agent when running with 0 initial policies

set -e

# Colors for output
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
POLICIES_FILE="./scripts/cedar_agent_policies.json"

# Parse arguments
while [ $# -gt 0 ]; do
  case "$1" in
    --cedar-url)
      CEDAR_URL="$2"
      shift 2
      ;;
    --policies-file)
      POLICIES_FILE="$2"
      shift 2
      ;;
    --help)
      echo "Usage: $0 [OPTIONS]"
      echo "Options:"
      echo "  --cedar-url URL           Cedar Agent URL (default: http://localhost:8280)"
      echo "  --policies-file PATH      Path to policies JSON file (default: ./scripts/cedar_agent_policies.json)"
      echo "  --help                    Show this help message"
      exit 0
      ;;
    *)
      print_error "Unknown option: $1"
      exit 1
      ;;
  esac
done

echo "========================================"
echo "Cedar Policies Loader - Step 3"
echo "========================================"
echo ""

# Step 3: Load policies to Cedar Agent
print_info "Step 3: Loading policies to Cedar Agent..."

# Check if Cedar Agent is accessible
print_info "Checking Cedar Agent connectivity at $CEDAR_URL..."
if ! curl -sSf "$CEDAR_URL/v1/" >/dev/null 2>&1; then
  print_error "Cedar Agent is not accessible at $CEDAR_URL/v1/"
  print_info "Make sure Cedar Agent is running with:"
  print_info "  ./target/release/cedar-agent -l debug -s schema.json -d data.json --addr 0.0.0.0 --port 8280"
  exit 1
fi
print_success "Cedar Agent is accessible"

# Check if policies file exists
if [ ! -f "$POLICIES_FILE" ]; then
  print_error "Policies file not found: $POLICIES_FILE"
  exit 1
fi
print_success "Policies file found: $POLICIES_FILE"

# Validate JSON format
if ! jq empty "$POLICIES_FILE" 2>/dev/null; then
  print_error "Invalid JSON format in policies file: $POLICIES_FILE"
  exit 1
fi
print_success "Policies file has valid JSON format"

# Check current policy count
print_info "Checking current policies in Cedar Agent..."
current_policies=$(curl -sSf "$CEDAR_URL/v1/policies" 2>/dev/null || echo "[]")
if command -v jq >/dev/null 2>&1; then
  current_count=$(echo "$current_policies" | jq 'length' 2>/dev/null || echo "0")
  print_info "Current policy count: $current_count"
else
  print_info "Current policies endpoint accessible (jq not available for count)"
fi

# Load each policy from the JSON file
print_info "Loading policies from $POLICIES_FILE..."
policy_count=0
failed_count=0

# Read policies from JSON file and load each one
while IFS= read -r policy; do
  policy_id=$(echo "$policy" | jq -r '.id' 2>/dev/null)
  if [ "$policy_id" != "null" ] && [ -n "$policy_id" ]; then
    print_info "Loading policy: $policy_id"

    # Attempt to load the policy via REST API
    response=$(curl -s -w "\n%{http_code}" -X POST \
      -H "Content-Type: application/json" \
      -d "$policy" \
      "$CEDAR_URL/v1/policies" 2>/dev/null)

    http_code=$(echo "$response" | tail -n1)
    response_body=$(echo "$response" | head -n -1)

    if [ "$http_code" = "200" ] || [ "$http_code" = "201" ]; then
      print_success "✓ Policy loaded: $policy_id"
      ((policy_count++))
    else
      print_error "✗ Failed to load policy: $policy_id (HTTP $http_code)"
      if [ -n "$response_body" ]; then
        print_error "  Response: $response_body"
      fi
      ((failed_count++))
    fi
  fi
done < <(jq -c '.[]' "$POLICIES_FILE" 2>/dev/null)

echo ""
print_info "Policy loading summary:"
print_success "Successfully loaded: $policy_count policies"
if [ $failed_count -gt 0 ]; then
  print_error "Failed to load: $failed_count policies"
fi

# Verify final policy count
print_info "Verifying loaded policies..."
final_policies=$(curl -sSf "$CEDAR_URL/v1/policies" 2>/dev/null || echo "[]")
if command -v jq >/dev/null 2>&1; then
  final_count=$(echo "$final_policies" | jq 'length' 2>/dev/null || echo "0")
  print_success "Final policy count in Cedar Agent: $final_count"

  # Show policy IDs for verification
  if [ "$final_count" -gt 0 ]; then
    print_info "Loaded policy IDs:"
    echo "$final_policies" | jq -r '.[].id' 2>/dev/null | while read -r pid; do
      echo "  - $pid"
    done
  fi
else
  print_success "Policies endpoint accessible (jq not available for detailed verification)"
fi

echo ""
if [ $failed_count -eq 0 ]; then
  print_success "Step 3 completed successfully! All policies loaded to Cedar Agent."
else
  print_warning "Step 3 completed with some failures. Check the errors above."
fi

echo ""
print_info "Next steps:"
print_info "1. Verify Cedar Agent is working with: curl $CEDAR_URL/v1/policies"
print_info "2. Run the integrated tests to verify authorization is working"
print_info "3. Check the cedar_diagnostic.sh script for further verification"
