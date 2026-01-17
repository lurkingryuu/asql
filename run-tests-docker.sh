#!/bin/bash
# Helper script to build and run unittests in Docker

# Exit on error
set -e

# Navigate to the asql directory if not already there
CDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$CDIR"

echo "Building ASQL Unittest Docker image..."
docker build -t asql-unittest -f Dockerfile.unittest .

echo "Running ASQL Unittests..."
# If arguments are provided, pass them to ctest (e.g., -R authorization-t)
if [ $# -gt 0 ]; then
    docker run --rm asql-unittest "$@"
else
    # Default: run the authorization-t test as it's the primary focus
    echo "Running authorization tests (authorization-t)..."
    docker run --rm asql-unittest -R authorization-t --output-on-failure
fi
