# Dockerfile for MySQL External Authorization Service
FROM node:18-alpine

# Set working directory
WORKDIR /app

# Copy package files
COPY docker/auth-service/package*.json ./

# Install dependencies
RUN npm ci --only=production

# Copy application code
COPY docker/auth-service/server.js ./

# Create non-root user
RUN addgroup -g 1001 -S nodejs && \
    adduser -S authservice -u 1001

# Change ownership
RUN chown -R authservice:nodejs /app

# Switch to non-root user
USER authservice

# Expose port
EXPOSE 8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD node -e "require('http').get('http://localhost:8080/health', (res) => process.exit(res.statusCode === 200 ? 0 : 1))"

# Start the service
CMD ["npm", "start"]
