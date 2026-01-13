#!/bin/bash
# Build script for Market Data Feed Handler

set -e

cd "$(dirname "$0")/.."
BUILD_DIR="build"
BUILD_TYPE="${1:-Release}"

echo "============================================"
echo "  Building Market Data Feed Handler"
echo "  Build Type: $BUILD_TYPE"
echo "============================================"

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# Build
cmake --build . --parallel $(sysctl -n hw.ncpu 2>/dev/null || nproc)

echo ""
echo "Build complete!"
echo "Executables:"
echo "  - build/exchange_simulator"
echo "  - build/feed_handler"
