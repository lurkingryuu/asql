# Running MySQL Docker Image

This guide explains how to run the MySQL Docker image (`lurkingryuu/mysql:latest`) directly using `docker run` and connect to it.

## Quick Start

### 1. Pull the Image

```bash
docker pull lurkingryuu/mysql:latest
```

Or pull a specific version:
```bash
docker pull lurkingryuu/mysql:v8.0.43.cedar
```

### 2. Run MySQL Container

**Basic run (with root password):**
```bash
docker run -d \
  --name mysql-server \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  -p 3306:3306 \
  lurkingryuu/mysql:latest
```

**With persistent data storage:**
```bash
docker run -d \
  --name mysql-server \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  -e MYSQL_DATABASE=mydb \
  -e MYSQL_USER=myuser \
  -e MYSQL_PASSWORD=mypassword \
  -p 3306:3306 \
  -v mysql_data:/var/lib/mysql \
  lurkingryuu/mysql:latest
```

**With host directory for data:**
```bash
mkdir -p ./mysql-data
chmod 750 ./mysql-data

docker run -d \
  --name mysql-server \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  -p 3306:3306 \
  -v $(pwd)/mysql-data:/var/lib/mysql \
  lurkingryuu/mysql:latest
```

### 3. Wait for MySQL to Start

MySQL takes 30-60 seconds to initialize on first run. Check logs:

```bash
docker logs -f mysql-server
```

Wait until you see: `MySQL init process done. Ready for start up.` or `ready for connections`.

### 4. Connect to MySQL

**From host machine:**
```bash
mysql -h 127.0.0.1 -P 3306 -u root -p
# Enter password when prompted
```

**From inside the container:**
```bash
docker exec -it mysql-server mysql -u root -p
```

**Using connection string:**
```
mysql://root:yourpassword@localhost:3306/
```

## Environment Variables

| Variable | Description | Required | Default |
|----------|-------------|----------|---------|
| `MYSQL_ROOT_PASSWORD` | Root user password | Recommended | - |
| `MYSQL_DATABASE` | Initial database to create | No | - |
| `MYSQL_USER` | Initial user to create | No | - |
| `MYSQL_PASSWORD` | Password for initial user | No | - |
| `MYSQL_DATADIR` | MySQL data directory | No | `/var/lib/mysql` |
| `MYSQL_INNODB_BUFFER_POOL_SIZE` | InnoDB buffer pool size | No | `1G` |
| `MYSQL_MAX_CONNECTIONS` | Maximum connections | No | `200` |

### Examples

**Create database and user on startup:**
```bash
docker run -d \
  --name mysql-server \
  -e MYSQL_ROOT_PASSWORD=rootpass \
  -e MYSQL_DATABASE=appdb \
  -e MYSQL_USER=appuser \
  -e MYSQL_PASSWORD=userpass \
  -p 3306:3306 \
  lurkingryuu/mysql:latest
```

**Without root password (insecure, dev only):**
```bash
docker run -d \
  --name mysql-server \
  -p 3306:3306 \
  lurkingryuu/mysql:latest
```

Then connect without password:
```bash
mysql -h 127.0.0.1 -u root
```

## Data Persistence

### Using Docker Volumes (Recommended)

```bash
# Create a named volume
docker volume create mysql_data

# Run with volume
docker run -d \
  --name mysql-server \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  -p 3306:3306 \
  -v mysql_data:/var/lib/mysql \
  lurkingryuu/mysql:latest

# Volume persists even after container removal
docker rm mysql-server
docker run -d \
  --name mysql-server-new \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  -p 3306:3306 \
  -v mysql_data:/var/lib/mysql \
  lurkingryuu/mysql:latest
```

### Using Host Directories

```bash
# Create directory with proper permissions
mkdir -p ./mysql-data
chmod 750 ./mysql-data

# Run with bind mount
docker run -d \
  --name mysql-server \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  -p 3306:3306 \
  -v $(pwd)/mysql-data:/var/lib/mysql \
  lurkingryuu/mysql:latest
```

**Note:** On first run, MySQL will initialize the data directory. Subsequent runs will use existing data.

## Common Operations

### Check Container Status

```bash
# Check if container is running
docker ps | grep mysql-server

# Check container logs
docker logs mysql-server

# Follow logs in real-time
docker logs -f mysql-server
```

### Connect to MySQL

**Interactive MySQL client:**
```bash
docker exec -it mysql-server mysql -u root -p
```

**Execute SQL command:**
```bash
docker exec mysql-server mysql -u root -p -e "SHOW DATABASES;"
```

**Run SQL file:**
```bash
docker exec -i mysql-server mysql -u root -p < script.sql
```

### Backup and Restore

**Create backup:**
```bash
# Backup all databases
docker exec mysql-server mysqldump -u root -p --all-databases > backup.sql

# Backup specific database
docker exec mysql-server mysqldump -u root -p mydb > mydb_backup.sql
```

**Restore from backup:**
```bash
# Restore all databases
docker exec -i mysql-server mysql -u root -p < backup.sql

# Restore specific database
docker exec -i mysql-server mysql -u root -p mydb < mydb_backup.sql
```

### Stop and Start

```bash
# Stop container
docker stop mysql-server

# Start container
docker start mysql-server

# Restart container
docker restart mysql-server
```

### Remove Container

```bash
# Stop and remove container (data in volume persists)
docker stop mysql-server
docker rm mysql-server

# Remove container and volume (WARNING: deletes all data!)
docker stop mysql-server
docker rm mysql-server
docker volume rm mysql_data
```

## Network Configuration

### Expose on Different Port

```bash
docker run -d \
  --name mysql-server \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  -p 3307:3306 \
  lurkingryuu/mysql:latest
```

Then connect using:
```bash
mysql -h 127.0.0.1 -P 3307 -u root -p
```

### Use Custom Docker Network

```bash
# Create network
docker network create mysql_network

# Run container on network
docker run -d \
  --name mysql-server \
  --network mysql_network \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  lurkingryuu/mysql:latest

# Connect from another container on same network
docker run -it --rm \
  --network mysql_network \
  mysql:8.0 \
  mysql -h mysql-server -u root -p
```

### Don't Expose Port Publicly

```bash
# Run without -p flag (only accessible from Docker network)
docker run -d \
  --name mysql-server \
  --network mysql_network \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  lurkingryuu/mysql:latest
```

## Resource Limits

### Set Memory and CPU Limits

```bash
docker run -d \
  --name mysql-server \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  -p 3306:3306 \
  --memory="4g" \
  --cpus="2.0" \
  lurkingryuu/mysql:latest
```

### Set Memory Reservation

```bash
docker run -d \
  --name mysql-server \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  -p 3306:3306 \
  --memory="4g" \
  --memory-reservation="2g" \
  lurkingryuu/mysql:latest
```

## Troubleshooting

### Container Won't Start

1. **Check logs:**
   ```bash
   docker logs mysql-server
   ```

2. **Check if port is already in use:**
   ```bash
   lsof -i :3306
   # or
   netstat -an | grep 3306
   ```

3. **Check data directory permissions:**
   ```bash
   ls -la ./mysql-data
   # Should be owned by UID 999 (mysql user in container)
   ```

### Can't Connect to MySQL

1. **Verify container is running:**
   ```bash
   docker ps | grep mysql-server
   ```

2. **Check if MySQL is ready:**
   ```bash
   docker exec mysql-server mysqladmin ping -h localhost
   ```

3. **Check port mapping:**
   ```bash
   docker port mysql-server
   ```

4. **Test connection from inside container:**
   ```bash
   docker exec -it mysql-server mysql -u root -p
   ```

### Reset Root Password

If you forgot the root password:

```bash
# Stop container
docker stop mysql-server

# Start with skip-grant-tables (temporary)
docker run -it --rm \
  -v mysql_data:/var/lib/mysql \
  lurkingryuu/mysql:latest \
  mysqld_safe --skip-grant-tables &

# Connect and reset password
docker exec -it <container-id> mysql -u root
mysql> ALTER USER 'root'@'localhost' IDENTIFIED BY 'newpassword';
mysql> FLUSH PRIVILEGES;
mysql> exit

# Stop temporary container and restart normally
docker stop <container-id>
docker start mysql-server
```

### Reset Database (Delete All Data)

**WARNING: This deletes all data!**

```bash
# Stop and remove container
docker stop mysql-server
docker rm mysql-server

# Remove volume
docker volume rm mysql_data

# Start fresh
docker run -d \
  --name mysql-server \
  -e MYSQL_ROOT_PASSWORD=yourpassword \
  -p 3306:3306 \
  -v mysql_data:/var/lib/mysql \
  lurkingryuu/mysql:latest
```

## Complete Example

Here's a complete example for a production-like setup:

```bash
# 1. Create data directory
mkdir -p ./mysql-data
chmod 750 ./mysql-data

# 2. Pull image
docker pull lurkingryuu/mysql:latest

# 3. Run container
docker run -d \
  --name mysql-server \
  --restart unless-stopped \
  -e MYSQL_ROOT_PASSWORD=SecureRootPassword123! \
  -e MYSQL_DATABASE=myapp \
  -e MYSQL_USER=myapp_user \
  -e MYSQL_PASSWORD=SecureUserPassword123! \
  -e MYSQL_INNODB_BUFFER_POOL_SIZE=2G \
  -e MYSQL_MAX_CONNECTIONS=200 \
  -p 3306:3306 \
  -v $(pwd)/mysql-data:/var/lib/mysql \
  --memory="4g" \
  --cpus="2.0" \
  lurkingryuu/mysql:latest

# 4. Wait for initialization (check logs)
docker logs -f mysql-server

# 5. Connect and verify
docker exec -it mysql-server mysql -u root -p -e "SHOW DATABASES;"
docker exec -it mysql-server mysql -u myapp_user -p -e "USE myapp; SHOW TABLES;"
```

## Connection Examples

### Using MySQL Client

```bash
# From host
mysql -h 127.0.0.1 -P 3306 -u root -p

# From container
docker exec -it mysql-server mysql -u root -p
```

### Using Connection Strings

**Standard MySQL:**
```
mysql://root:password@localhost:3306/database
```

**JDBC (Java):**
```
jdbc:mysql://localhost:3306/database?user=root&password=password
```

**Python (mysql-connector-python):**
```python
import mysql.connector

conn = mysql.connector.connect(
    host='localhost',
    port=3306,
    user='root',
    password='password',
    database='database'
)
```

**Node.js (mysql2):**
```javascript
const mysql = require('mysql2');

const connection = mysql.createConnection({
  host: 'localhost',
  port: 3306,
  user: 'root',
  password: 'password',
  database: 'database'
});
```

## Next Steps

- See [PRODUCTION.md](PRODUCTION.md) for production deployment with Docker Compose
- See [DOCKER_BUILD.md](DOCKER_BUILD.md) for building custom images
- See [DOCKER-QUICKSTART.md](DOCKER-QUICKSTART.md) for quick start with docker-compose

