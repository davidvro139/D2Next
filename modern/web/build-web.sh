#!/usr/bin/env bash
# Builds D2Next as wasm64 + WebGL 2 into out/web-dist (D2Next.html / .js / .wasm) on Linux or macOS.
# Needs the Emscripten SDK activated (emcmake on PATH), CMake 3.25 or newer and Ninja.
# On Windows use Build-Web.cmd instead: it fetches everything it needs by itself.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${D2MODERN_WEB_BUILD_DIR:-"$ROOT_DIR/out/web-build"}"
DIST_DIR="$ROOT_DIR/out/web-dist"
CONFIG="${D2MODERN_CONFIG:-Release}"

for tool in emcmake cmake ninja; do
  command -v "$tool" >/dev/null 2>&1 || { echo "$tool is missing (see modern/web/README.md)."; exit 1; }
done
cmake_version="$(cmake --version | head -1 | awk '{print $3}')"
if [[ "$(printf '%s\n3.25.0\n' "$cmake_version" | sort -V | head -1)" != "3.25.0" ]]; then
  echo "CMake $cmake_version is too old; 3.25 or newer is needed."
  exit 1
fi

# MEMORY64 must be on the compiler flags before CMake's pointer-size check.
emcmake cmake -S "$ROOT_DIR/modern" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$CONFIG" -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
  -DCMAKE_C_FLAGS="-sMEMORY64=1 -fwasm-exceptions" -DCMAKE_CXX_FLAGS="-sMEMORY64=1 -fwasm-exceptions" \
  -DSDL_CAMERA=OFF -DSDL_JACK=OFF -DSDL_PIPEWIRE=OFF -DSDL_IBUS=OFF -DSDL_HIDAPI_LIBUSB=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF
cmake --build "$BUILD_DIR"

mkdir -p "$DIST_DIR"
cp "$BUILD_DIR/D2Next.html" "$BUILD_DIR/D2Next.js" "$BUILD_DIR/D2Next.wasm" "$ROOT_DIR/modern/web/serve.ps1" "$DIST_DIR/"
echo
echo "Built: $DIST_DIR"
echo "Serve that folder over HTTP (node modern/web/serve.js, or any static web server) and open D2Next.html."
