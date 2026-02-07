# Multi-Architecture Docker Build Guide

This guide explains how to build and push Docker images for multiple architectures (e.g., `linux/amd64`, `linux/arm64`).

## Quick Start

### Local Build and Push

1. **Login to Docker Hub** (if not already logged in):
   ```bash
   docker login
   ```

2. **Run the build script**:
   ```bash
   ./build-and-push.sh
   ```

   This will:
   - Read the MySQL version from `MYSQL_VERSION` file
   - Build for `linux/amd64` and `linux/arm64` by default
   - Tag as `v8.0.43.cedar` (or your version) and `latest`
   - Push to `anonymous-user/mysql` on Docker Hub

### Custom Configuration

You can customize the build using environment variables:

```bash
# Change Docker Hub username
DOCKER_USERNAME=yourusername ./build-and-push.sh

# Change image name
IMAGE_NAME=mysql-custom ./build-and-push.sh

# Change tag suffix
TAG_SUFFIX=.custom ./build-and-push.sh

# Build for different platforms
PLATFORMS=linux/amd64,linux/arm64,linux/arm/v7 ./build-and-push.sh

# Use a different Dockerfile
DOCKERFILE=Dockerfile.custom ./build-and-push.sh
```

### Manual Build (Advanced)

If you prefer to build manually:

```bash
# Create a buildx builder (one-time setup)
docker buildx create --name multiarch-builder --driver docker-container --use
docker buildx inspect --bootstrap

# Build and push
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --file Dockerfile \
  --tag anonymous-user/mysql:v8.0.43.cedar \
  --tag anonymous-user/mysql:latest \
  --push \
  .
```

## Automated Builds with GitHub Actions

A GitHub Actions workflow (`.github/workflows/docker-build-push.yml`) is included for automated builds.

### Setup

1. **Add Docker Hub secrets to GitHub**:
   - Go to your repository → Settings → Secrets and variables → Actions
   - Add secrets:
     - `DOCKER_USERNAME`: Your Docker Hub username (e.g., `anonymous-user`)
     - `DOCKER_PASSWORD`: Your Docker Hub access token or password

2. **Workflow triggers**:
   - **Push to main/master**: Builds and pushes `latest` tag
   - **Push tags (v*)**: Builds and pushes with the tag name
   - **Manual trigger**: Use "Run workflow" button with custom options

### Manual Workflow Run

1. Go to Actions tab in GitHub
2. Select "Build and Push Multi-Arch Docker Image"
3. Click "Run workflow"
4. Optionally customize:
   - Tag suffix (default: `.cedar`)
   - Platforms (default: `linux/amd64,linux/arm64`)

## Supported Platforms

Common platforms you can build for:
- `linux/amd64` - Intel/AMD 64-bit
- `linux/arm64` - ARM 64-bit (Apple Silicon, AWS Graviton, etc.)
- `linux/arm/v7` - ARM 32-bit v7
- `linux/ppc64le` - PowerPC 64-bit little-endian
- `linux/s390x` - IBM Z

Example for multiple platforms:
```bash
PLATFORMS=linux/amd64,linux/arm64,linux/arm/v7 ./build-and-push.sh
```

## Troubleshooting

### Buildx Builder Issues

If you encounter issues with the buildx builder:

```bash
# List existing builders
docker buildx ls

# Remove and recreate the builder
docker buildx rm multiarch-builder
docker buildx create --name multiarch-builder --driver docker-container --use
docker buildx inspect --bootstrap
```

### Build Failures

- **Out of memory**: Multi-arch builds require significant resources. Consider building on a machine with at least 8GB RAM.
- **Slow builds**: The first build will be slower as it needs to build for each platform. Subsequent builds benefit from Docker layer caching.
- **Platform-specific errors**: Some dependencies may not be available for all platforms. Check the build logs for specific errors.

### Verification

After pushing, verify the multi-arch manifest:

```bash
# Inspect the manifest
docker buildx imagetools inspect anonymous-user/mysql:v8.0.43.cedar

# Test pulling for a specific platform
docker pull --platform linux/arm64 anonymous-user/mysql:v8.0.43.cedar
```

## Repetitive Builds

### Option 1: Script (Recommended for Local)

Simply run the script whenever you need to rebuild:
```bash
./build-and-push.sh
```

### Option 2: GitHub Actions (Recommended for CI/CD)

The workflow automatically builds on:
- Every push to main/master
- Every tag push
- Manual trigger

### Option 3: Cron Job (Local)

Add to your crontab for scheduled builds:
```bash
# Build every day at 2 AM
0 2 * * * cd /path/to/asql && ./build-and-push.sh
```

### Option 4: Git Hooks

Add a post-commit hook to build on every commit:
```bash
#!/bin/bash
# .git/hooks/post-commit
cd "$(git rev-parse --show-toplevel)"
./build-and-push.sh
```

## Image Tags

The script automatically generates tags based on:
- **Version tag**: `v{MYSQL_VERSION_MAJOR}.{MYSQL_VERSION_MINOR}.{MYSQL_VERSION_PATCH}{TAG_SUFFIX}`
  - Example: `v8.0.43.cedar`
- **Latest tag**: `latest` (always points to the most recent build)

You can override the tag suffix with the `TAG_SUFFIX` environment variable.

## Next Steps

After pulling or building the image, see [RUNNING_MYSQL.md](RUNNING_MYSQL.md) for instructions on how to run the MySQL container and connect to it.

