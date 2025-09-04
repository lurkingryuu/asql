# MySQL Authorization Plugin - Docker Quick Start Guide

## 🚀 Quick Start

### Prerequisites
- Docker Engine 20.10+
- Docker Compose 2.0+
- 4GB RAM minimum
- 10GB free disk space

### 1. Environment Setup

```bash
# Navigate to project directory
cd /path/to/mysql-authorization-plugin

# Create environment configuration
cat > .env << EOF
# MySQL Configuration
MYSQL_ROOT_PASSWORD=root
MYSQL_DATABASE=testdb

# Authorization Plugin Configuration
SIMPLE_AUTH_MODE=grant
SIMPLE_AUTH_ALLOW_USER=testuser
SIMPLE_AUTH_ALLOW_DB=testdb
EOF
```

### 2. Build Services

```bash
# Build MySQL with authorization plugin (takes 30-60 minutes first time)
docker-compose build mysql

# Build authorization service
docker-compose build auth-service
```

### 3. Start Services

```bash
# Start all services
docker-compose up -d

# Wait for services to be healthy
docker-compose ps
```

### 4. Initialize MySQL

```bash
# Initialize database and create test users (first time only)
docker-compose exec mysql docker-entrypoint.sh init
```

### 5. Test Authorization

```bash
# Connect as root to verify setup
docker-compose exec mysql mysql -u root -p -e "SHOW PLUGINS LIKE '%authorization%';"

# Test plugin authorization (should succeed due to plugin)
docker-compose exec mysql mysql -u testuser -ppassword -e "USE testdb; SELECT * FROM test_table;"

# Compare with normal user (has built-in privileges)
docker-compose exec mysql mysql -u normaluser -ppassword -e "USE testdb; SELECT * FROM test_table;"
```

## 📋 Configuration Options

### Simple Authorization Mode
```bash
# Allow specific user on specific database
SIMPLE_AUTH_MODE=grant
SIMPLE_AUTH_ALLOW_USER=testuser
SIMPLE_AUTH_ALLOW_DB=testdb
```

### External Authorization Mode
```bash
# Delegate to external service
SIMPLE_AUTH_MODE=ignore
EXTERNAL_AUTH_URL=http://auth-service:8080/auth
```

### Combined Mode
```bash
# Use both plugins
SIMPLE_AUTH_MODE=grant
SIMPLE_AUTH_ALLOW_USER=admin
EXTERNAL_AUTH_URL=http://auth-service:8080/auth
```

## 🧪 Testing Scenarios

### Scenario 1: Plugin Grants Access
```bash
# Configure plugin to grant access
echo "SIMPLE_AUTH_MODE=grant" >> .env
echo "SIMPLE_AUTH_ALLOW_USER=testuser" >> .env

# Restart services
docker-compose restart

# Test: User without MySQL privileges can access via plugin
docker-compose exec mysql mysql -u testuser -ppassword testdb
```

### Scenario 2: Plugin Denies Access
```bash
# Configure plugin to deny access
echo "SIMPLE_AUTH_MODE=deny" >> .env

# Restart services
docker-compose restart

# Test: All users denied access despite MySQL privileges
docker-compose exec mysql mysql -u normaluser -ppassword testdb  # Should fail
```

### Scenario 3: External Authorization
```bash
# Enable external authorization
echo "EXTERNAL_AUTH_URL=http://auth-service:8080/auth" >> .env

# Restart services
docker-compose restart

# Test: Authorization delegated to external service
docker-compose exec mysql mysql -u testuser -ppassword testdb

# Check authorization service logs
docker-compose logs auth-service
```

## 🔧 Troubleshooting

### Services Won't Start
```bash
# Check service logs
docker-compose logs mysql
docker-compose logs auth-service

# Check service health
docker-compose ps
```

### Plugin Not Working
```bash
# Check plugin installation
docker-compose exec mysql mysql -u root -p -e "SHOW PLUGINS LIKE '%authorization%';"

# Enable MySQL general logging
docker-compose exec mysql mysql -u root -p -e "SET GLOBAL general_log = ON;"

# Check authorization decisions
docker-compose exec mysql tail -f /var/log/mysql/general.log
```

### Permission Issues
```bash
# Check MySQL data directory permissions
docker-compose exec mysql ls -la /var/lib/mysql-data

# Reinitialize MySQL if needed
docker-compose exec mysql rm -rf /var/lib/mysql-data/*
docker-compose restart mysql
docker-compose exec mysql docker-entrypoint.sh init
```

## 📊 Monitoring

### MySQL Logs
```bash
# Follow MySQL logs
docker-compose logs -f mysql

# Check authorization decisions
docker-compose exec mysql tail -f /var/log/mysql/general.log
```

### Authorization Service Logs
```bash
# Follow authorization service logs
docker-compose logs -f auth-service

# Check service health
curl http://localhost:8080/health
```

### Container Resources
```bash
# Monitor resource usage
docker stats

# Check container health
docker-compose ps
```

## 🧹 Cleanup

```bash
# Stop services
docker-compose down

# Remove volumes (WARNING: This deletes data!)
docker volume rm mysql_auth_plugin_data mysql_auth_plugin_logs

# Remove networks
docker network rm mysql_auth_network

# Remove images (optional)
docker rmi mysql-authorization-plugin mysql-auth-service
```

## 📚 Advanced Usage

### Custom MySQL Configuration
```bash
# Mount custom configuration
echo "
[mysqld]
innodb_buffer_pool_size = 512M
max_connections = 200
" > docker/mysql/custom.cnf

# Update docker-compose.yml to mount the file
# Add to mysql service volumes:
# - ./docker/mysql/custom.cnf:/etc/mysql/conf.d/custom.cnf:ro
```

### External Authorization Service Customization
```bash
# Modify authorization policies
vim docker/auth-service/server.js

# Rebuild service
docker-compose build auth-service
docker-compose up -d auth-service
```

### Production Deployment
```bash
# Use production environment
cp .env .env.production
# Edit .env.production with production values

# Deploy
docker-compose --env-file .env.production up -d

# Enable external authorization
echo "EXTERNAL_AUTH_URL=https://auth.company.com/auth" >> .env.production
```

## 🆘 Help and Support

### Common Issues

1. **Build fails with out of memory**
   - Increase Docker memory limit to 8GB+
   - Close other applications

2. **MySQL initialization fails**
   - Check available disk space
   - Verify volume permissions

3. **Plugin doesn't load**
   - Verify plugin files exist in container
   - Check MySQL error log for plugin errors

4. **External service unreachable**
   - Check service health: `curl http://localhost:8080/health`
   - Verify network connectivity between containers

### Getting Help

1. Check logs: `docker-compose logs`
2. Test basic connectivity: `docker-compose exec mysql mysqladmin ping`
3. Verify plugin status: `SHOW PLUGINS LIKE '%authorization%';`
4. Check authorization service: `curl http://localhost:8080/policies`

## 🎯 Next Steps

1. **Explore the plugin code** in `plugin/authorization/`
2. **Customize authorization policies** in `docker/auth-service/server.js`
3. **Add new plugins** following the existing pattern
4. **Test with your application** using the authorization hooks
5. **Deploy to production** with proper security and monitoring

---

**Happy authorizing! 🔐**
