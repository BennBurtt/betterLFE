#!/bin/bash
# Builds SubGuard.app on macOS. Needs the Xcode command line tools and CMake
# (xcode-select --install ; brew install cmake).
set -euo pipefail
cd "$(dirname "$0")"

command -v cmake >/dev/null || { echo "CMake is missing: brew install cmake"; exit 1; }

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"
ctest --test-dir build --output-on-failure -C Release

APP="$(find build -name SubGuard.app -maxdepth 4 | head -1)"
echo
echo "Built: $(pwd)/$APP"
