#!/bin/bash

# MySQL Authorization Plugin Docker Run Script
# ============================================

set -e

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

# Function to check if Docker is running
check_docker() {
    if ! docker info >/dev/null 2>&1; then
        print_error "Docker is not running or not accessible"
        exit 1
    fi
}

# Function to check if services are built
check_services() {
    if ! docker images | grep -q mysql-authorization-plugin; then
        print_error "MySQL authorization plugin image not found. Run './build.sh mysql' first"
        exit 1
    fi

    if ! docker images | grep -q mysql-auth-service; then
        print_warning "Authorization service image not found. Building..."
        ./build.sh auth
    fi
}

# Function to start services
start_services() {
    print_status "Starting MySQL Authorization Plugin services..."

    # Create .env file if it doesn't exist
    if [ ! -f .env ]; then
        print_warning ".env file not found, creating from template..."
        cp .env.example .env 2>/dev/null || print_warning "No .env.example found, using defaults"
    fi

    # Start services
    if docker-compose up -d; then
        print_success "Services started successfully"
        show_status
    else
        print_error "Failed to start services"
        exit 1
    fi
}

# Function to stop services
stop_services() {
    print_status "Stopping MySQL Authorization Plugin services..."

    if docker-compose down; then
        print_success "Services stopped successfully"
    else
        print_error "Failed to stop services"
        exit 1
    fi
}

# Function to restart services
restart_services() {
    print_status "Restarting MySQL Authorization Plugin services..."
    stop_services
    sleep 2
    start_services
}

# Function to show service status
show_status() {
    echo ""
    print_status "Service Status:"
    docker-compose ps

    echo ""
    print_status "Service Health:"
    echo "MySQL: $(docker-compose exec -T mysql mysqladmin ping 2>/dev/null && echo "Healthy" || echo "Unhealthy")"
    echo "Auth Service: $(docker-compose exec -T auth-service curl -f http://localhost:8080/health >/dev/null 2>&1 && echo "Healthy" || echo "Unhealthy")"
}

# Function to initialize MySQL
init_mysql() {
    print_status "Initializing MySQL database..."

    if docker-compose exec mysql docker-entrypoint.sh init; then
        print_success "MySQL initialized successfully"
        show_test_info
    else
        print_error "Failed to initialize MySQL"
        exit 1
    fi
}

# Function to show test information
show_test_info() {
    echo ""
    print_status "Test Information:"
    echo "=========================================="
    echo "MySQL Root Password: ${MYSQL_ROOT_PASSWORD:-root}"
    echo ""
    echo "Test Users:"
    echo "  testuser/password     - No built-in privileges (plugin testing)"
    echo "  normaluser/password   - Has SELECT on testdb (comparison)"
    echo ""
    echo "Test Databases:"
    echo "  testdb   - Test database with sample data"
    echo "  otherdb  - Additional test database"
    echo ""
    echo "Connect to MySQL:"
    echo "  docker-compose exec mysql mysql -u root -p"
    echo ""
    echo "Test plugin authorization:"
    echo "  docker-compose exec mysql mysql -u testuser -ppassword testdb"
    echo ""
    echo "Check plugin status:"
    echo "  docker-compose exec mysql mysql -u root -p -e \"SHOW PLUGINS LIKE '%authorization%';\""
}

# Function to run tests
run_tests() {
    print_status "Running authorization plugin tests..."

    if docker-compose exec mysql sh -c "mysql -u root -p${MYSQL_ROOT_PASSWORD:-root} < /plugin/authorization/test_authorization_plugin.sql"; then
        print_success "Tests completed successfully"
        print_status "Check MySQL logs for detailed results:"
        echo "  docker-compose exec mysql tail -f /var/log/mysql/general.log"
    else
        print_error "Tests failed"
        exit 1
    fi
}

# Function to show logs
show_logs() {
    local service="${1:-mysql}"

    case "$service" in
        mysql)
            docker-compose logs -f mysql
            ;;
        auth)
            docker-compose logs -f auth-service
            ;;
        all)
            docker-compose logs -f
            ;;
        *)
            print_error "Unknown service: $service. Use: mysql, auth, or all"
            exit 1
            ;;
    esac
}

# Function to connect to MySQL client
connect_mysql() {
    print_status "Starting MySQL client..."
    docker-compose --profile client run --rm mysql-client
}

# Function to cleanup
cleanup() {
    print_status "Cleaning up Docker resources..."

    # Stop services
    docker-compose down

    # Remove volumes
    docker volume rm mysql_auth_plugin_data mysql_auth_plugin_logs 2>/dev/null || true

    # Remove networks
    docker network rm mysql_auth_network 2>/dev/null || true

    print_success "Cleanup completed"
}

# Function to show usage
usage() {
    cat << EOF
MySQL Authorization Plugin Docker Run Script

USAGE:
    $0 [COMMAND] [OPTIONS]

COMMANDS:
    start       Start all services
    stop        Stop all services
    restart     Restart all services
    status      Show service status
    init        Initialize MySQL database
    test        Run authorization plugin tests
    logs        Show service logs (mysql|auth|all)
    client      Start interactive MySQL client
    cleanup     Clean up Docker resources
    help        Show this help message

OPTIONS:
    -s, --service SERVICE    Service for logs command (mysql/auth/all)

EXAMPLES:
    $0 start         # Start services
    $0 init          # Initialize MySQL
    $0 test          # Run tests
    $0 logs mysql    # Show MySQL logs
    $0 client        # Start MySQL client
    $0 cleanup       # Clean up everything

ENVIRONMENT:
    Configure services using .env file:
    - MYSQL_ROOT_PASSWORD    Root password
    - SIMPLE_AUTH_MODE       Plugin mode (grant/deny/ignore)
    - EXTERNAL_AUTH_URL      External auth service URL

QUICK START:
    1. $0 start
    2. $0 init
    3. $0 client
    4. Test: mysql -u testuser -ppassword testdb
EOF
}

# Main script logic
main() {
    local command="$1"
    local option="$2"

    # Check prerequisites
    check_docker

    case "$command" in
        start)
            check_services
            start_services
            ;;
        stop)
            stop_services
            ;;
        restart)
            restart_services
            ;;
        status)
            show_status
            ;;
        init)
            init_mysql
            ;;
        test)
            run_tests
            ;;
        logs)
            show_logs "${option:-mysql}"
            ;;
        client)
            connect_mysql
            ;;
        cleanup)
            cleanup
            ;;
        help|--help|-h|"")
            usage
            ;;
        *)
            print_error "Unknown command: $command"
            echo ""
            usage
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"
