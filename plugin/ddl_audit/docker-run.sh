#!/bin/bash

# Docker run script for Cedar DDL Audit Server
# This script provides easy commands to build, run, and manage the container

set -e

CONTAINER_NAME="cedar-ddl-audit-server"
IMAGE_NAME="cedar-ddl-audit"
PORT="8180"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if Docker is running
check_docker() {
    if ! docker info > /dev/null 2>&1; then
        log_error "Docker is not running. Please start Docker and try again."
        exit 1
    fi
}

# Build the Docker image
build_image() {
    log_info "Building Docker image: $IMAGE_NAME"
    docker build -t $IMAGE_NAME .
    log_success "Image built successfully"
}

# Run the container
run_container() {
    log_info "Starting container: $CONTAINER_NAME"
    
    # Check if container already exists
    if docker ps -a --format "table {{.Names}}" | grep -q "^$CONTAINER_NAME$"; then
        log_warning "Container $CONTAINER_NAME already exists"
        read -p "Do you want to remove it and create a new one? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            docker rm -f $CONTAINER_NAME
        else
            log_info "Starting existing container..."
            docker start $CONTAINER_NAME
            return
        fi
    fi
    
    # Run the container
    docker run -d \
        --name $CONTAINER_NAME \
        -p $PORT:8180 \
        -e NODE_ENV=production \
        --restart unless-stopped \
        $IMAGE_NAME
    
    log_success "Container started successfully"
    log_info "Server is available at: http://localhost:$PORT"
}

# Stop the container
stop_container() {
    log_info "Stopping container: $CONTAINER_NAME"
    docker stop $CONTAINER_NAME
    log_success "Container stopped"
}

# Remove the container
remove_container() {
    log_info "Removing container: $CONTAINER_NAME"
    docker rm -f $CONTAINER_NAME
    log_success "Container removed"
}

# Show container logs
show_logs() {
    log_info "Showing logs for container: $CONTAINER_NAME"
    docker logs -f $CONTAINER_NAME
}

# Show container status
show_status() {
    log_info "Container status:"
    docker ps -a --filter name=$CONTAINER_NAME --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
}

# Test the server
test_server() {
    log_info "Testing server endpoints..."
    
    # Test health endpoint
    log_info "Testing health endpoint..."
    if curl -s http://localhost:$PORT/health > /dev/null; then
        log_success "Health endpoint is working"
    else
        log_error "Health endpoint is not responding"
        return 1
    fi
    
    # Test status endpoint
    log_info "Testing status endpoint..."
    if curl -s http://localhost:$PORT/v1/status > /dev/null; then
        log_success "Status endpoint is working"
    else
        log_error "Status endpoint is not responding"
        return 1
    fi
    
    # Test DDL audit endpoint
    log_info "Testing DDL audit endpoint..."
    response=$(curl -s -X POST http://localhost:$PORT/v1/ddl_audit \
        -H "Content-Type: application/json" \
        -d '{
            "ddl_type": "CREATE_TABLE",
            "sql_command_id": 1,
            "query": "CREATE TABLE test (id INT)",
            "database": "test_db",
            "table": "test",
            "user": "root",
            "host": "localhost",
            "timestamp": "2025-01-01T12:00:00Z",
            "context": {
                "ip_address": "127.0.0.1",
                "connection_id": 123
            }
        }')
    
    if echo "$response" | grep -q "success"; then
        log_success "DDL audit endpoint is working"
    else
        log_error "DDL audit endpoint test failed"
        return 1
    fi
    
    log_success "All tests passed!"
}

# Clean up everything
cleanup() {
    log_info "Cleaning up..."
    docker rm -f $CONTAINER_NAME 2>/dev/null || true
    docker rmi $IMAGE_NAME 2>/dev/null || true
    log_success "Cleanup completed"
}

# Show help
show_help() {
    echo "Cedar DDL Audit Server - Docker Management Script"
    echo
    echo "Usage: $0 [COMMAND]"
    echo
    echo "Commands:"
    echo "  build     Build the Docker image"
    echo "  run       Build and run the container"
    echo "  start     Start the existing container"
    echo "  stop      Stop the container"
    echo "  restart   Restart the container"
    echo "  remove    Remove the container"
    echo "  logs      Show container logs"
    echo "  status    Show container status"
    echo "  test      Test the server endpoints"
    echo "  cleanup   Remove container and image"
    echo "  help      Show this help message"
    echo
    echo "Examples:"
    echo "  $0 run          # Build and run the container"
    echo "  $0 logs         # View container logs"
    echo "  $0 test         # Test the server"
    echo "  $0 cleanup      # Remove everything"
}

# Main script logic
main() {
    check_docker
    
    case "${1:-help}" in
        build)
            build_image
            ;;
        run)
            build_image
            run_container
            ;;
        start)
            docker start $CONTAINER_NAME
            log_success "Container started"
            ;;
        stop)
            stop_container
            ;;
        restart)
            stop_container
            run_container
            ;;
        remove)
            remove_container
            ;;
        logs)
            show_logs
            ;;
        status)
            show_status
            ;;
        test)
            test_server
            ;;
        cleanup)
            cleanup
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            log_error "Unknown command: $1"
            show_help
            exit 1
            ;;
    esac
}

# Run the main function
main "$@"
