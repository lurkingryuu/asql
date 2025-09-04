#!/bin/bash

# MySQL Authorization Plugin Docker Build Script
# ===============================================

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

# Function to check if Docker Compose is available
check_docker_compose() {
    if ! docker-compose version >/dev/null 2>&1; then
        if ! docker compose version >/dev/null 2>&1; then
            print_error "Docker Compose is not available"
            exit 1
        fi
    fi
}

# Function to build MySQL with authorization plugin
build_mysql() {
    print_status "Building MySQL with Authorization Plugin support..."
    print_warning "This may take 30-60 minutes depending on your system"

    if docker build -t mysql-authorization-plugin .; then
        print_success "MySQL with authorization plugin built successfully"
    else
        print_error "Failed to build MySQL"
        exit 1
    fi
}

# Function to build authorization service
build_auth_service() {
    print_status "Building External Authorization Service..."

    if docker build -t mysql-auth-service ./docker/auth-service/; then
        print_success "Authorization service built successfully"
    else
        print_error "Failed to build authorization service"
        exit 1
    fi
}

# Function to build all services
build_all() {
    print_status "Building all services..."

    # Build authorization service first (faster)
    build_auth_service

    # Build MySQL (slower)
    build_mysql

    print_success "All services built successfully"
}

# Function to clean up build artifacts
clean() {
    print_status "Cleaning up build artifacts..."

    # Remove built images
    docker rmi mysql-authorization-plugin 2>/dev/null || true
    docker rmi mysql-auth-service 2>/dev/null || true

    # Remove dangling images
    docker image prune -f

    # Remove build cache
    docker builder prune -f

    print_success "Cleanup completed"
}

# Function to show usage
usage() {
    cat << EOF
MySQL Authorization Plugin Docker Build Script

USAGE:
    $0 [COMMAND]

COMMANDS:
    mysql       Build MySQL with authorization plugin support
    auth        Build external authorization service
    all         Build all services (default)
    clean       Clean up build artifacts
    help        Show this help message

EXAMPLES:
    $0 all          # Build everything
    $0 mysql        # Build only MySQL
    $0 clean        # Clean up

ENVIRONMENT:
    The build process uses the following from the main project:
    - MySQL source code with authorization plugin patches
    - Plugin source files in plugin/authorization/
    - Build configuration in docker/mysql/

NOTES:
    - First build may take 30-60 minutes
    - Requires at least 4GB RAM and 10GB disk space
    - Subsequent builds will be faster due to Docker layer caching
EOF
}

# Main script logic
main() {
    local command="${1:-all}"

    # Check prerequisites
    check_docker
    check_docker_compose

    case "$command" in
        mysql)
            build_mysql
            ;;
        auth)
            build_auth_service
            ;;
        all)
            build_all
            ;;
        clean)
            clean
            ;;
        help|--help|-h)
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
