# Cedar DDL Audit Server - Docker Setup

This directory contains Docker configuration for running the Cedar DDL Audit Server as a containerized service.

## Quick Start

### Using Docker Compose (Recommended)

1. **Build and start the service:**
   ```bash
   docker-compose up --build
   ```

2. **Run in background:**
   ```bash
   docker-compose up -d --build
   ```

3. **View logs:**
   ```bash
   docker-compose logs -f cedar-ddl-audit
   ```

4. **Stop the service:**
   ```bash
   docker-compose down
   ```

### Using Docker directly

1. **Build the image:**
   ```bash
   docker build -t cedar-ddl-audit .
   ```

2. **Run the container:**
   ```bash
   docker run -d \
     --name cedar-ddl-audit-server \
     -p 8180:8180 \
     cedar-ddl-audit
   ```

3. **View logs:**
   ```bash
   docker logs -f cedar-ddl-audit-server
   ```

4. **Stop the container:**
   ```bash
   docker stop cedar-ddl-audit-server
   docker rm cedar-ddl-audit-server
   ```

## Configuration

### Environment Variables

- `NODE_ENV`: Set to `production` for optimized performance

### Ports

- **8180**: HTTP server port for DDL audit endpoint

### Health Check

The container includes a health check that verifies the service is responding:
- **Endpoint**: `GET /health`
- **Interval**: 30 seconds
- **Timeout**: 3 seconds
- **Retries**: 3

## API Endpoints

### DDL Audit Endpoint
- **URL**: `POST http://localhost:8180/v1/ddl_audit`
- **Content-Type**: `application/json`
- **Description**: Receives DDL audit data from MySQL plugin

### Status Endpoint
- **URL**: `GET http://localhost:8180/v1/status`
- **Description**: Returns current entity status

### Health Check
- **URL**: `GET http://localhost:8180/health`
- **Description**: Health check endpoint

## Testing the Container

1. **Start the container:**
   ```bash
   docker-compose up -d
   ```

2. **Test the health endpoint:**
   ```bash
   curl http://localhost:8180/health
   ```

3. **Test the DDL audit endpoint:**
   ```bash
   curl -X POST http://localhost:8180/v1/ddl_audit \
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
     }'
   ```

4. **Check status:**
   ```bash
   curl http://localhost:8180/v1/status
   ```

## Integration with MySQL

Once the container is running, configure your MySQL DDL Audit Plugin:

```sql
-- Install the plugin (if not already installed)
INSTALL PLUGIN ddl_audit SONAME 'ddl_audit.so';

-- Configure the plugin to use the Docker container
SET GLOBAL ddl_audit_cedar_url = 'http://localhost:8180';
SET GLOBAL ddl_audit_cedar_timeout = 5000;
SET GLOBAL ddl_audit_enabled = ON;

-- Test with a DDL statement
CREATE DATABASE test_audit;
USE test_audit;
CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(50));
```

## Monitoring

### View Container Logs
```bash
# Using docker-compose
docker-compose logs -f cedar-ddl-audit

# Using docker directly
docker logs -f cedar-ddl-audit-server
```

### Check Container Status
```bash
# Using docker-compose
docker-compose ps

# Using docker directly
docker ps | grep cedar-ddl-audit
```

### Monitor Resource Usage
```bash
docker stats cedar-ddl-audit-server
```

## Security Notes

- The container runs as a non-root user (`cedar`) for security
- Only port 8180 is exposed
- The application runs in production mode
- Health checks are configured for monitoring

## Troubleshooting

### Container Won't Start
1. Check if port 8180 is already in use:
   ```bash
   lsof -i :8180
   ```

2. Check container logs:
   ```bash
   docker-compose logs cedar-ddl-audit
   ```

### Health Check Failing
1. Verify the service is running:
   ```bash
   curl http://localhost:8180/health
   ```

2. Check if the container is healthy:
   ```bash
   docker inspect cedar-ddl-audit-server | grep -A 10 Health
   ```

### MySQL Plugin Can't Connect
1. Ensure the container is running and accessible
2. Check firewall settings
3. Verify the URL configuration in MySQL
4. Check MySQL error logs for connection issues
