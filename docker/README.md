# Docker Setup for MySQL with Authorization Plugin

This directory contains Docker configuration files for building and running MySQL with the custom authorization plugin support.

## Quick Start

### 1. Prerequisites

- Docker Engine 20.10+
- Docker Compose 2.0+
- At least 4GB RAM available
- At least 10GB free disk space

### 2. Clone and Setup

```bash
# Clone the repository
git clone <repository-url>
cd mysql-authorization-plugin

# Copy environment configuration
cp .env.example .env
```

### 3. Configure Authorization

Edit `.env` file to configure the authorization plugin:

```bash
# For Simple Authorization (grants access to specific user/database)
SIMPLE_AUTH_MODE=grant
SIMPLE_AUTH_ALLOW_USER=testuser
SIMPLE_AUTH_ALLOW_DB=testdb

# For External Authorization (delegates to external service)
EXTERNAL_AUTH_URL=http://auth-service:8080/auth
EXTERNAL_AUTH_TIMEOUT=5000
```

### 4. Build and Start Services

```bash
# Build and start all services
docker-compose up -d

# Wait for services to be healthy
docker-compose ps

# Initialize MySQL database (first time only)
docker-compose exec mysql docker-entrypoint.sh init
```

### 5. Test Authorization

```bash
# Connect as root user
docker-compose exec mysql mysql -u root -p

# Test plugin functionality
docker-compose exec mysql mysql -u testuser -ppassword testdb

# Check plugin status
SHOW PLUGINS LIKE '%authorization%';

# Monitor authorization decisions
tail -f /var/log/mysql/general.log
```

## Configuration Options

### Environment Variables

#### MySQL Configuration
- `MYSQL_ROOT_PASSWORD`: Root user password (default: root)
- `MYSQL_DATABASE`: Default database to create (default: testdb)
- `MYSQL_USER`: Additional user to create (optional)
- `MYSQL_PASSWORD`: Password for additional user (optional)

#### Simple Authorization Plugin
- `SIMPLE_AUTH_MODE`: Plugin mode (`grant`|`deny`|`ignore`)
- `SIMPLE_AUTH_ALLOW_USER`: User to grant access to
- `SIMPLE_AUTH_ALLOW_DB`: Database to grant access to

#### External Authorization Plugin
- `EXTERNAL_AUTH_URL`: URL of external authorization service
- `EXTERNAL_AUTH_TIMEOUT`: Request timeout in milliseconds (default: 5000)

### Plugin Modes

#### 1. Simple Authorization Mode

```bash
SIMPLE_AUTH_MODE=grant
SIMPLE_AUTH_ALLOW_USER=testuser
SIMPLE_AUTH_ALLOW_DB=testdb
```

This configuration allows `testuser` to access `testdb` even without built-in MySQL privileges.

#### 2. External Authorization Mode

```bash
EXTERNAL_AUTH_URL=http://auth-service:8080/auth
EXTERNAL_AUTH_TIMEOUT=3000
```

This delegates authorization decisions to an external HTTP service.

#### 3. Combined Mode

```bash
SIMPLE_AUTH_MODE=grant
SIMPLE_AUTH_ALLOW_USER=admin
EXTERNAL_AUTH_URL=http://auth-service:8080/auth
```

Uses both plugins, with external authorization taking precedence.

## Services

### MySQL Service (`mysql`)

- **Port**: 3306
- **Data Volume**: `mysql_data` (/var/lib/mysql-data)
- **Log Volume**: `mysql_logs` (/var/log/mysql)
- **Health Check**: MySQL ping every 30 seconds

### Authorization Service (`auth-service`)

- **Port**: 8080
- **Health Check**: HTTP health endpoint
- **API**: RESTful authorization endpoint

### MySQL Client (`mysql-client`)

- **Profile**: `client` (starts only when requested)
- **Usage**: Interactive MySQL client for testing

## Usage Examples

### Example 1: Basic Authorization Testing

```bash
# Start services with simple authorization
export SIMPLE_AUTH_MODE=grant
export SIMPLE_AUTH_ALLOW_USER=testuser
export SIMPLE_AUTH_ALLOW_DB=testdb

docker-compose up -d mysql

# Wait for MySQL to be ready
docker-compose exec mysql docker-entrypoint.sh init

# Test access (should succeed due to plugin)
docker-compose exec mysql mysql -u testuser -ppassword -e "USE testdb; SELECT * FROM test_table;"
```

### Example 2: External Authorization Testing

```bash
# Start all services
docker-compose up -d

# Test external authorization
docker-compose exec mysql mysql -u testuser -ppassword -e "USE testdb; SELECT * FROM test_table;"

# Check authorization service logs
docker-compose logs auth-service
```

### Example 3: Development and Debugging

```bash
# Start services with external authorization
export EXTERNAL_AUTH_URL=http://auth-service:8080/auth
docker-compose up -d

# Monitor MySQL logs for authorization decisions
docker-compose exec mysql tail -f /var/log/mysql/general.log

# Monitor authorization service requests
docker-compose logs -f auth-service

# Start interactive MySQL client
docker-compose --profile client run --rm mysql-client
```

## External Authorization Service API

### Authorization Endpoint

**URL**: `POST /auth`

**Request Body**:
```json
{
  "user": "username",
  "host": "hostname",
  "database": "dbname",
  "table": "tablename",
  "privileges": 123,
  "event_type": "db_access|table_access|column_access|routine_access",
  "sql_command": "SELECT",
  "query": "SELECT * FROM table",
  "is_procedure": false
}
```

**Response**:
```json
{
  "result": "grant|deny|ignore",
  "reason": "explanation",
  "timestamp": "2025-01-01T00:00:00.000Z"
}
```

### Health Check Endpoint

**URL**: `GET /health`

Returns service health status.

### Policies Endpoint

**URL**: `GET /policies`

Returns current authorization policies.

## Troubleshooting

### MySQL Won't Start

```bash
# Check MySQL logs
docker-compose logs mysql

# Check MySQL data directory permissions
docker-compose exec mysql ls -la /var/lib/mysql-data

# Reinitialize MySQL data
docker-compose exec mysql rm -rf /var/lib/mysql-data/*
docker-compose exec mysql docker-entrypoint.sh init
```

### Plugin Not Loading

```bash
# Check plugin directory
docker-compose exec mysql ls -la /usr/local/mysql/lib/plugin/

# Check MySQL error log
docker-compose exec mysql tail -f /var/log/mysql/error.log

# Verify plugin installation
docker-compose exec mysql mysql -u root -p -e "SHOW PLUGINS LIKE '%authorization%';"
```

### Authorization Not Working

```bash
# Enable general logging
docker-compose exec mysql mysql -u root -p -e "SET GLOBAL general_log = ON;"

# Check authorization decisions
docker-compose exec mysql tail -f /var/log/mysql/general.log

# Test with different users
docker-compose exec mysql mysql -u testuser -ppassword testdb -e "SELECT * FROM test_table;"

# Check authorization service logs
docker-compose logs auth-service
```

### External Service Not Reachable

```bash
# Check service health
docker-compose ps

# Test service connectivity
docker-compose exec mysql curl http://auth-service:8080/health

# Check network connectivity
docker-compose exec mysql ping auth-service
```

## Development

### Building Custom MySQL Image

```bash
# Build MySQL with authorization plugin
docker build -t mysql-custom-auth .

# Run custom image
docker run -d --name mysql-custom \
  -e MYSQL_ROOT_PASSWORD=root \
  -e SIMPLE_AUTH_MODE=grant \
  -e SIMPLE_AUTH_ALLOW_USER=testuser \
  -p 3306:3306 \
  mysql-custom-auth
```

### Modifying Authorization Service

```bash
# Edit authorization service
vim docker/auth-service/server.js

# Rebuild authorization service
docker-compose build auth-service

# Restart services
docker-compose up -d
```

### Adding New Plugins

1. Add plugin source to `plugin/authorization/`
2. Update `CMakeLists.txt` to include new plugin
3. Update `Dockerfile` to copy and build new plugin
4. Add configuration environment variables
5. Update documentation

## Performance Considerations

### MySQL Performance
- Authorization plugins add minimal overhead
- External service calls may impact query latency
- Cache authorization decisions when possible
- Monitor performance with `SHOW PROCESSLIST`

### External Service Performance
- Implement request caching
- Use connection pooling
- Set appropriate timeouts
- Monitor service response times

### Resource Usage
- MySQL requires ~2GB RAM minimum
- External service requires ~100MB RAM
- Build process requires ~8GB disk space
- Consider resource limits in production

## Security Considerations

### MySQL Security
- Change default root password
- Use strong passwords for all users
- Enable SSL/TLS connections
- Restrict network access with firewalls

### Plugin Security
- Validate all input parameters
- Implement proper error handling
- Log security events
- Use HTTPS for external services

### Docker Security
- Run containers as non-root users
- Use Docker secrets for passwords
- Regularly update base images
- Scan images for vulnerabilities

## Production Deployment

### Environment Setup
```bash
# Production environment file
cp .env.example .env.production
# Edit with production values

# Deploy to production
docker-compose --env-file .env.production up -d
```

### Monitoring
```bash
# Monitor MySQL performance
docker-compose exec mysql mysql -u root -p -e "SHOW PROCESSLIST;"

# Monitor authorization service
docker-compose logs -f auth-service

# Check container resource usage
docker stats
```

### Backup and Recovery
```bash
# Backup MySQL data
docker-compose exec mysql mysqldump -u root -p --all-databases > backup.sql

# Backup volumes
docker run --rm -v mysql_auth_plugin_data:/data -v $(pwd):/backup alpine tar czf /backup/mysql-data.tar.gz -C /data .

# Restore from backup
docker-compose exec mysql mysql -u root -p < backup.sql
```

## Support

For issues and questions:
1. Check the troubleshooting section above
2. Review MySQL and Docker logs
3. Test with simple configurations first
4. Check authorization service API documentation
5. Review plugin source code and documentation

## License

This Docker setup is part of the MySQL Authorization Plugin project and is licensed under GPL-2.0.
