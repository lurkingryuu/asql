# Dockerfile for Cedar DDL Audit Server
FROM node:18-alpine

# Set working directory
WORKDIR /app

# Copy package files
COPY package*.json ./

# Install dependencies
RUN npm install --only=production

# Copy application files
COPY cedar_server_example.js ./

# Create non-root user for security
RUN addgroup -g 1001 -S nodejs && \
    adduser -S cedar -u 1001

# Change ownership of the app directory
RUN chown -R cedar:nodejs /app
USER cedar

# Expose port
EXPOSE 8180

# Health check
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD node -e "const http = require('http'); \
    const options = { host: 'localhost', port: 8180, path: '/health', timeout: 2000 }; \
    const req = http.request(options, (res) => { \
      process.exit(res.statusCode === 200 ? 0 : 1); \
    }); \
    req.on('error', () => process.exit(1)); \
    req.end();"

# Start the application
CMD ["node", "cedar_server_example.js"]
