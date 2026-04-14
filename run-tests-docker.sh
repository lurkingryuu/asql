#!/bin/bash
# Helper script to build and run unittests in Docker

# Exit on error
set -e

# Navigate to the asql directory if not already there
CDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$CDIR"

BUILDER_NAME="${BUILDER_NAME:-asql-unittest-builder}"
BUILDER_NETWORK_MODE="${BUILDER_NETWORK_MODE:-host}"
BUILD_NETWORK_MODE="${BUILD_NETWORK_MODE:-host}"

resolve_latest_libcedar_version() {
    local latest_release_url resolved_url release_tag
    latest_release_url="https://github.com/lurkingryuu/libcedar/releases/latest"
    resolved_url="$(curl -fsSLI -o /dev/null -w '%{url_effective}' "${latest_release_url}")"
    release_tag="${resolved_url##*/}"

    if [ -z "${release_tag}" ] || [ "${release_tag}" = "latest" ]; then
        echo "Failed to resolve the latest libcedar release from GitHub" >&2
        return 1
    fi

    printf '%s\n' "${release_tag}"
}

if [ -z "${LIBCEDAR_VERSION:-}" ]; then
    LIBCEDAR_VERSION="$(resolve_latest_libcedar_version)"
fi

if ! docker buildx inspect "${BUILDER_NAME}" >/dev/null 2>&1; then
    echo "Creating buildx builder: ${BUILDER_NAME}"
    docker buildx create \
        --name "${BUILDER_NAME}" \
        --driver docker-container \
        --driver-opt "network=${BUILDER_NETWORK_MODE}" \
        --use
    docker buildx inspect --bootstrap >/dev/null
else
    docker buildx use "${BUILDER_NAME}"
fi

echo "Building ASQL Unittest Docker image..."
echo "Using libcedar release: ${LIBCEDAR_VERSION}"
docker buildx build \
    --network "${BUILD_NETWORK_MODE}" \
    --load \
    --build-arg "LIBCEDAR_VERSION=${LIBCEDAR_VERSION}" \
    --build-arg "LIBCEDAR_CACHE_BUST=$(date +%s)" \
    -t asql-unittest \
    -f Dockerfile.unittest \
    .

echo "Running ASQL containerized authorization suite..."
if [ $# -gt 0 ]; then
    docker run --rm asql-unittest "$@"
else
    echo "Running full embedded Cedar coverage..."
    docker run --rm asql-unittest full
fi
