#!/bin/bash
set -e

# Multi-Architecture Docker Build Script with Resource Limits
#
# Usage examples:
#   # Build for both amd64 and arm64 (default)
#   ./build-and-push.sh
#
#   # Build only for amd64
#   ./build-and-push.sh --amd64-only
#
#   # Use only 4 CPUs
#   MAX_CPUS=4 ./build-and-push.sh
#
#   # Use 50% of available CPUs (6 CPUs on a 12-core machine)
#   MAX_CPU_PERCENT=50 ./build-and-push.sh
#
#   # Limit memory to 8GB
#   MEMORY_LIMIT=8g ./build-and-push.sh
#
#   # Combine limits
#   MAX_CPUS=4 MEMORY_LIMIT=8g ./build-and-push.sh --amd64-only

# Configuration
DOCKER_USERNAME="${DOCKER_USERNAME:-lurkingryuu}"
IMAGE_NAME="${IMAGE_NAME:-mysql}"
DOCKERFILE="${DOCKERFILE:-Dockerfile}"

# CPU/Resource limits
# Set MAX_CPUS to limit parallel build jobs (default: use all available CPUs)
# Example: MAX_CPUS=4 to use only 4 CPUs
MAX_CPUS="${MAX_CPUS:-}"
# Set MAX_CPU_PERCENT to limit CPU usage percentage (0-100, default: no limit)
# Example: MAX_CPU_PERCENT=50 to use max 50% of available CPUs
MAX_CPU_PERCENT="${MAX_CPU_PERCENT:-}"
# Set MEMORY_LIMIT to limit memory usage (e.g., "8g", "4096m")
MEMORY_LIMIT="${MEMORY_LIMIT:-}"

# Cross-platform function to get number of CPUs
get_cpu_count() {
    if command -v nproc >/dev/null 2>&1; then
        # Linux
        nproc
    elif command -v sysctl >/dev/null 2>&1; then
        # macOS/BSD
        sysctl -n hw.ncpu
    else
        # Fallback
        echo "4"
    fi
}

# Read MySQL version from MYSQL_VERSION file
if [ -f "MYSQL_VERSION" ]; then
    source MYSQL_VERSION
    VERSION="${MYSQL_VERSION_MAJOR}.${MYSQL_VERSION_MINOR}.${MYSQL_VERSION_PATCH}${MYSQL_VERSION_EXTRA}"
else
    echo "Warning: MYSQL_VERSION file not found. Using 'latest' as version."
    VERSION="latest"
fi

# Determine tag suffix (e.g., .cedar if needed)
TAG_SUFFIX="${TAG_SUFFIX:-.authorization.plugin}"
FULL_VERSION_TAG="v${VERSION}${TAG_SUFFIX}"

# Platforms to build for
# Check for --amd64-only flag
if [ "$1" = "--amd64-only" ]; then
    PLATFORMS="linux/amd64"
    shift
else
    PLATFORMS="${PLATFORMS:-linux/amd64,linux/arm64}"
fi

# Full image name
FULL_IMAGE_NAME="${DOCKER_USERNAME}/${IMAGE_NAME}"

# Calculate parallel jobs based on CPU limits
TOTAL_CPUS=$(get_cpu_count)
if [ -n "${MAX_CPUS}" ]; then
    PARALLEL_JOBS="${MAX_CPUS}"
elif [ -n "${MAX_CPU_PERCENT}" ]; then
    PARALLEL_JOBS=$((TOTAL_CPUS * MAX_CPU_PERCENT / 100))
    # Ensure at least 1 job
    [ ${PARALLEL_JOBS} -lt 1 ] && PARALLEL_JOBS=1
else
    # Use all available CPUs
    PARALLEL_JOBS="${TOTAL_CPUS}"
fi

echo "=========================================="
echo "Multi-Architecture Docker Build & Push"
echo "=========================================="
echo "Image: ${FULL_IMAGE_NAME}"
echo "Version: ${VERSION}"
echo "Tags: ${FULL_VERSION_TAG}, latest"
echo "Platforms: ${PLATFORMS}"
echo "Resource Limits:"
echo "  Parallel Jobs: ${PARALLEL_JOBS} (out of ${TOTAL_CPUS} available CPUs)"
[ -n "${MEMORY_LIMIT}" ] && echo "  Memory Limit: ${MEMORY_LIMIT}"
echo "=========================================="
echo ""

# Ensure buildx builder exists and is using the correct driver
BUILDER_NAME="multiarch-builder"
if ! docker buildx inspect ${BUILDER_NAME} &>/dev/null; then
    echo "Creating buildx builder: ${BUILDER_NAME}"
    
    # Build driver options with resource limits
    DRIVER_OPTS="--driver docker-container"
    if [ -n "${MAX_CPU_PERCENT}" ] || [ -n "${MAX_CPUS}" ]; then
        # Calculate CPU limit for the builder container
        if [ -n "${MAX_CPUS}" ]; then
            CPU_LIMIT="${MAX_CPUS}"
        else
            CPU_LIMIT="${PARALLEL_JOBS}"
        fi
        DRIVER_OPTS="${DRIVER_OPTS} --driver-opt env.BUILDKIT_STEP_LOG_MAX_SIZE=10485760"
    fi
    
    docker buildx create --name ${BUILDER_NAME} ${DRIVER_OPTS} --use
    
    # Set resource limits on the builder container if specified
    BUILDER_CONTAINER="buildx_buildkit_${BUILDER_NAME}0"
    if docker ps -a --format '{{.Names}}' | grep -q "^${BUILDER_CONTAINER}$"; then
        if [ -n "${MAX_CPUS}" ] || [ -n "${MAX_CPU_PERCENT}" ]; then
            echo "Setting CPU limit on builder container..."
            docker update --cpus="${CPU_LIMIT}" ${BUILDER_CONTAINER} 2>/dev/null || true
        fi
        if [ -n "${MEMORY_LIMIT}" ]; then
            echo "Setting memory limit on builder container..."
            docker update --memory="${MEMORY_LIMIT}" ${BUILDER_CONTAINER} 2>/dev/null || true
        fi
    fi
    
    docker buildx inspect --bootstrap
else
    echo "Using existing buildx builder: ${BUILDER_NAME}"
    docker buildx use ${BUILDER_NAME}
    
    # Update resource limits on existing builder if specified
    BUILDER_CONTAINER="buildx_buildkit_${BUILDER_NAME}0"
    if docker ps -a --format '{{.Names}}' | grep -q "^${BUILDER_CONTAINER}$"; then
        if [ -n "${MAX_CPUS}" ] || [ -n "${MAX_CPU_PERCENT}" ]; then
            CPU_LIMIT="${PARALLEL_JOBS}"
            echo "Updating CPU limit on builder container to ${CPU_LIMIT}..."
            docker update --cpus="${CPU_LIMIT}" ${BUILDER_CONTAINER} 2>/dev/null || true
        fi
        if [ -n "${MEMORY_LIMIT}" ]; then
            echo "Updating memory limit on builder container to ${MEMORY_LIMIT}..."
            docker update --memory="${MEMORY_LIMIT}" ${BUILDER_CONTAINER} 2>/dev/null || true
        fi
    fi
fi

# Login to Docker Hub if not already logged in
if ! docker info | grep -q "Username"; then
    echo "Please login to Docker Hub:"
    docker login
fi

# Build and push for multiple architectures
echo ""
echo "Building and pushing multi-architecture image..."
BUILD_ARGS=""
if [ -n "${PARALLEL_JOBS}" ]; then
    BUILD_ARGS="--build-arg PARALLEL_JOBS=${PARALLEL_JOBS}"
fi

docker buildx build \
    --platform ${PLATFORMS} \
    --file ${DOCKERFILE} \
    ${BUILD_ARGS} \
    --tag ${FULL_IMAGE_NAME}:${FULL_VERSION_TAG} \
    --tag ${FULL_IMAGE_NAME}:latest \
    --push \
    .

echo ""
echo "=========================================="
echo "Build and push completed successfully!"
echo "=========================================="
echo "Image: ${FULL_IMAGE_NAME}"
echo "Tags pushed:"
echo "  - ${FULL_IMAGE_NAME}:${FULL_VERSION_TAG}"
echo "  - ${FULL_IMAGE_NAME}:latest"
echo "Platforms: ${PLATFORMS}"
echo ""
echo "To pull the image:"
echo "  docker pull ${FULL_IMAGE_NAME}:${FULL_VERSION_TAG}"
echo "  docker pull ${FULL_IMAGE_NAME}:latest"
echo "=========================================="


