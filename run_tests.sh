#!/bin/bash

# Convenience script to run authorization plugin tests with common configurations
# This script provides shortcuts for different test scenarios

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_SCRIPT="$SCRIPT_DIR/test_authorization_plugin.sh"

# Color codes
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_header() {
    echo -e "${BLUE}================================================${NC}"
    echo -e "${BLUE}  MySQL Authorization Plugin Test Runner${NC}"
    echo -e "${BLUE}================================================${NC}"
    echo ""
}

print_usage() {
    echo "Usage: $0 [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  basic      - Run basic tests with socket connection"
    echo "  tcp        - Run tests with TCP connection"
    echo "  simple     - Test only simple authorization plugin"
    echo "  external   - Test only external authorization plugin"
    echo "  all        - Run all available tests"
    echo "  cleanup    - Cleanup test environment only"
    echo "  help       - Show this help message"
    echo ""
    echo "Options:"
    echo "  --socket PATH    MySQL socket path (default: /tmp/mysql.sock)"
    echo "  --port PORT      MySQL port (default: 3306)"
    echo "  --user USER      MySQL user (default: root)"
    echo "  --password PASS  MySQL password (default: empty)"
    echo "  --verbose        Enable verbose output"
    echo "  --debug          Enable debug output"
    echo ""
    echo "Examples:"
    echo "  $0 basic"
    echo "  $0 tcp --port 3306 --user mysql --password secret"
    echo "  $0 simple --socket /var/lib/mysql/mysql.sock --verbose"
    echo "  $0 cleanup"
}

# Default values
MYSQL_SOCKET="/tmp/mysql.sock"
MYSQL_PORT="3306"
MYSQL_USER="root"
MYSQL_PASSWORD=""
VERBOSE=""
DEBUG=""

# Parse command line arguments
COMMAND=""
while [[ $# -gt 0 ]]; do
    case $1 in
        basic|tcp|simple|external|all|cleanup|help)
            if [ -n "$COMMAND" ]; then
                echo "Error: Multiple commands specified" >&2
                exit 1
            fi
            COMMAND="$1"
            shift
            ;;
        --socket)
            MYSQL_SOCKET="$2"
            shift 2
            ;;
        --port)
            MYSQL_PORT="$2"
            shift 2
            ;;
        --user)
            MYSQL_USER="$2"
            shift 2
            ;;
        --password)
            MYSQL_PASSWORD="$2"
            shift 2
            ;;
        --verbose)
            VERBOSE="--verbose"
            shift
            ;;
        --debug)
            DEBUG="--debug"
            shift
            ;;
        *)
            echo "Unknown option: $1" >&2
            print_usage
            exit 1
            ;;
    esac
done

# Show help if no command provided
if [ -z "$COMMAND" ] || [ "$COMMAND" = "help" ]; then
    print_header
    print_usage
    exit 0
fi

print_header

# Execute the appropriate test command
case $COMMAND in
    basic)
        echo -e "${GREEN}Running basic authorization plugin tests...${NC}"
        echo "Socket: $MYSQL_SOCKET"
        echo ""
        "$TEST_SCRIPT" --mysql-socket "$MYSQL_SOCKET" $VERBOSE $DEBUG
        ;;

    tcp)
        echo -e "${GREEN}Running authorization plugin tests via TCP...${NC}"
        echo "Host: 127.0.0.1:$MYSQL_PORT"
        echo "User: $MYSQL_USER"
        echo ""
        if [ -n "$MYSQL_PASSWORD" ]; then
            "$TEST_SCRIPT" --mysql-port "$MYSQL_PORT" --mysql-user "$MYSQL_USER" --mysql-password "$MYSQL_PASSWORD" $VERBOSE $DEBUG
        else
            "$TEST_SCRIPT" --mysql-port "$MYSQL_PORT" --mysql-user "$MYSQL_USER" $VERBOSE $DEBUG
        fi
        ;;

    simple)
        echo -e "${GREEN}Running simple authorization plugin tests...${NC}"
        echo "Socket: $MYSQL_SOCKET"
        echo ""
        "$TEST_SCRIPT" --simple-only --mysql-socket "$MYSQL_SOCKET" $VERBOSE $DEBUG
        ;;

    external)
        echo -e "${GREEN}Running external authorization plugin tests...${NC}"
        echo "Socket: $MYSQL_SOCKET"
        echo ""
        "$TEST_SCRIPT" --external-only --mysql-socket "$MYSQL_SOCKET" $VERBOSE $DEBUG
        ;;

    all)
        echo -e "${GREEN}Running all authorization plugin tests...${NC}"
        echo "Socket: $MYSQL_SOCKET"
        echo ""
        "$TEST_SCRIPT" --mysql-socket "$MYSQL_SOCKET" $VERBOSE $DEBUG
        ;;

    cleanup)
        echo -e "${YELLOW}Cleaning up authorization plugin test environment...${NC}"
        echo "Socket: $MYSQL_SOCKET"
        echo ""
        "$TEST_SCRIPT" --cleanup-only --mysql-socket "$MYSQL_SOCKET" $VERBOSE $DEBUG
        ;;
esac
