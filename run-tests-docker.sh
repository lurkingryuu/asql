#!/bin/bash
# Helper script to build and run unittests in Docker

# Exit on error
set -e

# Navigate to the asql directory if not already there
CDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$CDIR"

echo "Building ASQL Unittest Docker image..."
docker build --network=host -t asql-unittest -f Dockerfile.unittest .

echo "Running ASQL containerized authorization suite..."
if [ $# -gt 0 ]; then
    docker run --rm asql-unittest "$@"
else
    echo "Running full embedded Cedar coverage..."
    docker run --rm asql-unittest full
fi
