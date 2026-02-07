# Production Deployment Guide

This guide explains how to deploy MySQL in production using Docker Compose.

## Prerequisites

- Docker Engine 20.10+
- Docker Compose 2.0+
- At least 4GB RAM available
- Persistent storage for MySQL data

## Quick Start

1. **Copy the environment file:**
   ```bash
   cp .env.prod.example .env.prod
   ```

2. **Edit `.env.prod` with your production values:**
   ```bash
   # IMPORTANT: Set strong passwords!
   MYSQL_ROOT_PASSWORD=your_strong_root_password_here
   MYSQL_USER=your_app_user
   MYSQL_PASSWORD=your_strong_user_password_here
   MYSQL_DATABASE=your_database_name
   ```

3. **Create data directories:**
   ```bash
   mkdir -p data/mysql data/logs
   chmod 750 data/mysql data/logs
   ```

4. **Start the service:**
   ```bash
   docker compose -f docker-compose.prod.yml --env-file .env.prod up -d
   ```

5. **Check status:**
   ```bash
   docker compose -f docker-compose.prod.yml ps
   docker compose -f docker-compose.prod.yml logs -f mysql
   ```

## Configuration

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `MYSQL_IMAGE` | Docker image to use | `anonymous-user/mysql:latest` |
| `MYSQL_PORT` | Port to expose MySQL | `3306` |
| `MYSQL_ROOT_PASSWORD` | Root password (required in production) | - |
| `MYSQL_DATABASE` | Initial database to create | - |
| `MYSQL_USER` | Initial user to create | - |
| `MYSQL_PASSWORD` | Password for initial user | - |
| `MYSQL_CPU_LIMIT` | CPU limit | `4.0` |
| `MYSQL_MEMORY_LIMIT` | Memory limit | `4G` |
| `MYSQL_CPU_RESERVATION` | CPU reservation | `2.0` |
| `MYSQL_MEMORY_RESERVATION` | Memory reservation | `2G` |
| `MYSQL_INNODB_BUFFER_POOL_SIZE` | InnoDB buffer pool size | `1G` |
| `MYSQL_MAX_CONNECTIONS` | Maximum connections | `200` |
| `MYSQL_DATA_PATH` | Path for MySQL data | `./data/mysql` |
| `MYSQL_LOGS_PATH` | Path for MySQL logs | `./data/logs` |

### Resource Limits

Adjust resource limits based on your server capacity:

```bash
# In .env.prod
MYSQL_CPU_LIMIT=8.0
MYSQL_MEMORY_LIMIT=8G
MYSQL_CPU_RESERVATION=4.0
MYSQL_MEMORY_RESERVATION=4G
```

### Volume Paths

For production, use absolute paths:

```bash
# In .env.prod
MYSQL_DATA_PATH=/var/lib/mysql-prod
MYSQL_LOGS_PATH=/var/log/mysql-prod
```

## Security Best Practices

1. **Use Strong Passwords:**
   - Generate strong passwords using a password manager
   - Never commit passwords to version control
   - Use Docker secrets in production (see below)

2. **Network Security:**
   - Don't expose MySQL port publicly unless necessary
   - Use Docker networks to isolate services
   - Consider using a reverse proxy with SSL/TLS

3. **File Permissions:**
   ```bash
   chmod 750 data/mysql data/logs
   chown -R 999:999 data/mysql data/logs  # mysql user UID/GID
   ```

4. **Use Docker Secrets (Recommended):**
   ```yaml
   # In docker-compose.prod.yml
   secrets:
     mysql_root_password:
       external: true
   services:
     mysql:
       secrets:
         - mysql_root_password
       environment:
         - MYSQL_ROOT_PASSWORD_FILE=/run/secrets/mysql_root_password
   ```

## Connecting to MySQL

### From Host Machine

```bash
mysql -h 127.0.0.1 -P 3306 -u root -p
```

### From Another Container

```bash
mysql -h mysql-server-prod -u root -p
```

### Connection String

```
mysql://root:password@localhost:3306/database_name
```

> **Note:** For detailed instructions on running the MySQL image directly with `docker run` (without Docker Compose), see [RUNNING_MYSQL.md](RUNNING_MYSQL.md).

## Monitoring

### Health Check

The container includes a health check that runs every 30 seconds:

```bash
docker compose -f docker-compose.prod.yml ps
```

### Logs

View logs:
```bash
# Follow logs
docker compose -f docker-compose.prod.yml logs -f mysql

# Last 100 lines
docker compose -f docker-compose.prod.yml logs --tail=100 mysql
```

### Metrics

Connect to MySQL and check status:
```bash
mysql -h 127.0.0.1 -u root -p -e "SHOW STATUS;"
mysql -h 127.0.0.1 -u root -p -e "SHOW PROCESSLIST;"
```

## Backup and Restore

### Backup

```bash
# Create backup
docker compose -f docker-compose.prod.yml exec mysql mysqldump -u root -p --all-databases > backup.sql

# Backup specific database
docker compose -f docker-compose.prod.yml exec mysql mysqldump -u root -p database_name > backup.sql
```

### Restore

```bash
# Restore from backup
docker compose -f docker-compose.prod.yml exec -T mysql mysql -u root -p < backup.sql

# Restore specific database
docker compose -f docker-compose.prod.yml exec -T mysql mysql -u root -p database_name < backup.sql
```

## Maintenance

### Stop Service

```bash
docker compose -f docker-compose.prod.yml stop
```

### Start Service

```bash
docker compose -f docker-compose.prod.yml start
```

### Restart Service

```bash
docker compose -f docker-compose.prod.yml restart mysql
```

### Update Image

```bash
# Pull latest image
docker pull anonymous-user/mysql:latest

# Recreate container
docker compose -f docker-compose.prod.yml up -d --force-recreate mysql
```

## Troubleshooting

### Container Won't Start

1. Check logs:
   ```bash
   docker compose -f docker-compose.prod.yml logs mysql
   ```

2. Check data directory permissions:
   ```bash
   ls -la data/mysql
   ```

3. Verify environment variables:
   ```bash
   docker compose -f docker-compose.prod.yml config
   ```

### Connection Refused

1. Check if container is running:
   ```bash
   docker compose -f docker-compose.prod.yml ps
   ```

2. Check port binding:
   ```bash
   docker compose -f docker-compose.prod.yml port mysql 3306
   ```

3. Check firewall rules

### Performance Issues

1. Adjust resource limits in `.env.prod`
2. Tune MySQL configuration (InnoDB buffer pool, connections, etc.)
3. Monitor resource usage:
   ```bash
   docker stats mysql-server-prod
   ```

## Production Checklist

- [ ] Set strong root password
- [ ] Set strong user passwords
- [ ] Configure resource limits appropriately
- [ ] Set up regular backups
- [ ] Configure log rotation
- [ ] Set up monitoring and alerts
- [ ] Review security settings
- [ ] Test backup and restore procedures
- [ ] Document connection strings and credentials securely
- [ ] Set up SSL/TLS if exposing publicly
- [ ] Configure firewall rules
- [ ] Review and adjust MySQL configuration for your workload


