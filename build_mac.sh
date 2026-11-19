#!/bin/bash
# Builds SubGuard.app on macOS. Needs the Xcode command line tools and CMake
# (xcode-select --install ; brew install cmake).
set -euo pipefail
cd "$(dirname "$0")"

command -v cmake >/dev/null || { echo "CMake is missing: brew install cmake"; exit 1; }

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"
ctest --test-dir build --output-on-failure -C Release

# 5.1 test files (about 550 MB with the Room Sim Pro set, not kept in git)
mkdir -p test-files
"$(find build -name subguard_testfiles -type f -maxdepth 3 | head -1)" test-files

APP="$(find build -name SubGuard.app -maxdepth 4 | head -1)"
echo
echo "Built: $(pwd)/$APP"
echo "Test files: $(pwd)/test-files (see README.txt inside)"
echo "Room Sim Pro test files: $(pwd)/test-files/room-sim-pro (load default_rooms.subguardrooms, see docs/FICHIERS_TEST_ROOM_SIM_PRO.md)"
