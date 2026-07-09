#!/bin/bash
##############################################################
# install-gamelift-sdk.sh
# Downloads and builds the AWS GameLift Server SDK 5.x
# for use with the Unreal Engine 5 dedicated server build.
#
# Usage:
#   cd server && ./install-gamelift-sdk.sh
#
# Output:
#   Plugins/GameLiftServerSDK/include/
#   Plugins/GameLiftServerSDK/lib/
##############################################################

set -euo pipefail

SDK_VERSION="5.1.2"
SDK_URL="https://gamelift-server-sdk-release.s3.amazonaws.com/cpp/GameLift-Cpp-ServerSDK-${SDK_VERSION}.zip"
INSTALL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../Plugins/GameLiftServerSDK"
TMP_DIR="/tmp/gamelift-sdk-build"

echo "[GameLift SDK] Installing version ${SDK_VERSION}..."
echo "[GameLift SDK] Target: ${INSTALL_DIR}"

# ─── Prerequisites ─────────────────────────────────────────────
command -v cmake  &>/dev/null || { echo "[ERROR] cmake not found. Install cmake >= 3.25"; exit 1; }
command -v make   &>/dev/null || { echo "[ERROR] make not found"; exit 1; }
command -v unzip  &>/dev/null || { echo "[ERROR] unzip not found"; exit 1; }

# ─── Download ──────────────────────────────────────────────────
mkdir -p "${TMP_DIR}"
cd "${TMP_DIR}"

if [ ! -f "sdk.zip" ]; then
    echo "[GameLift SDK] Downloading..."
    curl -fsSL -o sdk.zip "${SDK_URL}"
fi

unzip -q -o sdk.zip -d sdk_source
cd sdk_source

# ─── Build ─────────────────────────────────────────────────────
mkdir -p build && cd build

echo "[GameLift SDK] Configuring with CMake..."
cmake .. \
    -DBUILD_FOR_UNREAL=1 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF

echo "[GameLift SDK] Building (this takes 3-5 minutes)..."
make -j"$(nproc)" 2>&1 | tail -20

# ─── Install ───────────────────────────────────────────────────
mkdir -p "${INSTALL_DIR}/include" "${INSTALL_DIR}/lib"

# Copy headers
cp -r ../include/* "${INSTALL_DIR}/include/"

# Copy libraries
find . -name "*.a" -o -name "*.lib" | xargs -I{} cp {} "${INSTALL_DIR}/lib/"

# ─── Verify ────────────────────────────────────────────────────
HEADER="${INSTALL_DIR}/include/aws/gamelift/server/GameLiftServerAPI.h"
if [ -f "${HEADER}" ]; then
    echo "[GameLift SDK] ✅ Install complete!"
    echo "[GameLift SDK] Headers: ${INSTALL_DIR}/include"
    echo "[GameLift SDK] Libs:    ${INSTALL_DIR}/lib"
    ls "${INSTALL_DIR}/lib/"
else
    echo "[GameLift SDK] ❌ Install failed — header not found: ${HEADER}"
    exit 1
fi

# ─── Cleanup ───────────────────────────────────────────────────
rm -rf "${TMP_DIR}"
echo "[GameLift SDK] Done. Rebuild your UE5 project."
