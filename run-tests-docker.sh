#!/bin/bash
# Helper script to build and run unittests in Docker

set -euo pipefail

# Navigate to the asql directory if not already there
CDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$CDIR"

BUILDER_NAME="${BUILDER_NAME:-asql-unittest-builder}"
BUILDER_NETWORK_MODE="${BUILDER_NETWORK_MODE:-host}"
BUILD_NETWORK_MODE="${BUILD_NETWORK_MODE:-host}"
IMAGE_NAME="${IMAGE_NAME:-asql-unittest}"
COMPOSE_IMAGE_NAME="${COMPOSE_IMAGE_NAME:-asql-unittest-dev}"
COMPOSE_FILE="${COMPOSE_FILE:-docker-compose.unittest.yml}"
DEFAULT_LIBCEDAR_VERSION="${DEFAULT_LIBCEDAR_VERSION:-v0.1.0}"
SKIP_BUILD="${SKIP_BUILD:-0}"
REFRESH_LIBCEDAR="${REFRESH_LIBCEDAR:-0}"
USE_DOCKER_COMPOSE="${USE_DOCKER_COMPOSE:-0}"

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
    LIBCEDAR_VERSION="${DEFAULT_LIBCEDAR_VERSION}"
elif [ "${LIBCEDAR_VERSION}" = "latest" ]; then
    LIBCEDAR_VERSION="$(resolve_latest_libcedar_version)"
fi

build_args=(
    --build-arg "LIBCEDAR_VERSION=${LIBCEDAR_VERSION}"
)

if [ -n "${PARALLEL_JOBS:-}" ]; then
    build_args+=(--build-arg "PARALLEL_JOBS=${PARALLEL_JOBS}")
fi

if [ "${REFRESH_LIBCEDAR}" = "1" ] && [ -z "${LIBCEDAR_CACHE_BUST:-}" ]; then
    LIBCEDAR_CACHE_BUST="$(date +%s)"
fi

if [ -n "${LIBCEDAR_CACHE_BUST:-}" ]; then
    build_args+=(--build-arg "LIBCEDAR_CACHE_BUST=${LIBCEDAR_CACHE_BUST}")
    echo "Forcing libcedar refresh for this build"
fi

export LIBCEDAR_VERSION
export COMPOSE_IMAGE_NAME
if [ -n "${PARALLEL_JOBS:-}" ]; then
    export PARALLEL_JOBS
fi
if [ -n "${LIBCEDAR_CACHE_BUST:-}" ]; then
    export LIBCEDAR_CACHE_BUST
fi

if [ "${USE_DOCKER_COMPOSE}" = "1" ]; then
    compose_cmd=(docker compose -f "${COMPOSE_FILE}")

    if [ "${SKIP_BUILD}" != "1" ]; then
        echo "Building ASQL unittest dev image via Docker Compose..."
        echo "Using libcedar release: ${LIBCEDAR_VERSION}"
        "${compose_cmd[@]}" build unittest
    else
        echo "Skipping compose image build and reusing the cached unittest dev image"
    fi

    echo "Running ASQL containerized authorization suite via Docker Compose..."
    if [ $# -gt 0 ]; then
        if [ "${SKIP_BUILD}" = "1" ]; then
            "${compose_cmd[@]}" run --no-build --rm unittest "$@"
        else
            "${compose_cmd[@]}" run --rm unittest "$@"
        fi
    else
        echo "Running full embedded Cedar coverage..."
        if [ "${SKIP_BUILD}" = "1" ]; then
            "${compose_cmd[@]}" run --no-build --rm unittest full
        else
            "${compose_cmd[@]}" run --rm unittest full
        fi
    fi
    exit 0
fi

if [ "${SKIP_BUILD}" != "1" ]; then
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
        "${build_args[@]}" \
        -t "${IMAGE_NAME}" \
        -f Dockerfile.unittest \
        .
else
    echo "Skipping image build and reusing ${IMAGE_NAME}"
fi

echo "Running ASQL containerized authorization suite..."
if [ $# -gt 0 ]; then
    docker run --rm "${IMAGE_NAME}" "$@"
else
    echo "Running full embedded Cedar coverage..."
    docker run --rm "${IMAGE_NAME}" full
fi
