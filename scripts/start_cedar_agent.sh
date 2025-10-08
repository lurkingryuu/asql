#!/bin/bash

# Cedar Agent Startup Script for MySQL Testing
# This script starts the Cedar Agent with the correct configuration for MySQL plugin testing

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Default configuration
CEDAR_AGENT_BIN="./target/release/cedar-agent"
SCHEMA_FILE="scripts/cedar_agent_schema.json"
DATA_FILE="scripts/cedar_agent_data.json"
POLICIES_FILE="scripts/cedar_agent_policies.json"
PORT=8280
LOG_LEVEL="debug"
ADDR="0.0.0.0"

# Parse arguments
while [ $# -gt 0 ]; do
  case "$1" in
    --cedar-agent)
      CEDAR_AGENT_BIN="$2"
      shift 2
      ;;
    --schema)
      SCHEMA_FILE="$2"
      shift 2
      ;;
    --data)
      DATA_FILE="$2"
      shift 2
      ;;
    --policies)
      POLICIES_FILE="$2"
      shift 2
      ;;
    --port)
      PORT="$2"
      shift 2
      ;;
    --log-level)
      LOG_LEVEL="$2"
      shift 2
      ;;
    --help)
      echo "Usage: $0 [OPTIONS]"
      echo ""
      echo "Start Cedar Agent with MySQL testing configuration"
      echo ""
      echo "Options:"
      echo "  --cedar-agent PATH    Path to cedar-agent binary (default: ./target/release/cedar-agent)"
      echo "  --schema FILE         Schema JSON file (default: scripts/cedar_agent_schema.json)"
      echo "  --data FILE           Data JSON file (default: scripts/cedar_agent_data.json)"
      echo "  --policies FILE       Policies JSON file (default: scripts/cedar_agent_policies.json)"
      echo "  --port PORT           Port to listen on (default: 8280)"
      echo "  --log-level LEVEL     Log level: error|warn|info|debug|trace (default: debug)"
      echo "  --help                Show this help"
      echo ""
      echo "Example:"
      echo "  $0"
      echo "  $0 --port 8280 --log-level info"
      exit 0
      ;;
    *)
      print_error "Unknown option: $1"
      exit 1
      ;;
  esac
done

print_info "========================================="
print_info "Cedar Agent Startup for MySQL Testing"
print_info "========================================="

# Check if Cedar Agent binary exists
if [ ! -f "$CEDAR_AGENT_BIN" ]; then
  print_error "Cedar Agent binary not found: $CEDAR_AGENT_BIN"
  print_info "Please build Cedar Agent first or specify the correct path with --cedar-agent"
  exit 1
fi

# Check if configuration files exist
if [ ! -f "$SCHEMA_FILE" ]; then
  print_error "Schema file not found: $SCHEMA_FILE"
  exit 1
fi

if [ ! -f "$DATA_FILE" ]; then
  print_error "Data file not found: $DATA_FILE"
  exit 1
fi

if [ ! -f "$POLICIES_FILE" ]; then
  print_error "Policies file not found: $POLICIES_FILE"
  exit 1
fi

print_success "Found Cedar Agent: $CEDAR_AGENT_BIN"
print_success "Found schema: $SCHEMA_FILE"
print_success "Found data: $DATA_FILE"
print_success "Found policies: $POLICIES_FILE"

# Check if port is already in use
if lsof -Pi :$PORT -sTCP:LISTEN >/dev/null 2>&1; then
  print_error "Port $PORT is already in use"
  print_info "Please stop the existing service or use a different port"
  exit 1
fi

print_info ""
print_info "Starting Cedar Agent with configuration:"
print_info "  - Port: $PORT"
print_info "  - Log Level: $LOG_LEVEL"
print_info "  - Address: $ADDR"
print_info "  - Schema: $SCHEMA_FILE"
print_info "  - Data: $DATA_FILE"
print_info "  - Policies: $POLICIES_FILE"
print_info ""

# Start Cedar Agent
print_info "Executing:"
echo "  $CEDAR_AGENT_BIN \\"
echo "    -l $LOG_LEVEL \\"
echo "    -s $SCHEMA_FILE \\"
echo "    -d $DATA_FILE \\"
echo "    --policies $POLICIES_FILE \\"
echo "    --addr $ADDR \\"
echo "    --port $PORT"
echo ""

exec "$CEDAR_AGENT_BIN" \
  -l "$LOG_LEVEL" \
  -s "$SCHEMA_FILE" \
  -d "$DATA_FILE" \
  --policies "$POLICIES_FILE" \
  --addr "$ADDR" \
  --port "$PORT"

