#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS_DIR="${SCRIPT_DIR}/deps"

echo "========================================"
echo " UltraHDR Viewer - Dependency Cloner"
echo "========================================"
echo

mkdir -p "$DEPS_DIR"

# --- GLFW 3.4 ---
echo "[1/4] Cloning GLFW 3.4..."
if [ -f "${DEPS_DIR}/glfw/CMakeLists.txt" ]; then
    echo "  Already exists, skipping."
else
    git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git "${DEPS_DIR}/glfw"
    echo "  Done."
fi
echo

# --- Dear ImGui v1.91.8 ---
echo "[2/4] Cloning Dear ImGui v1.91.8..."
if [ -f "${DEPS_DIR}/imgui/imgui.h" ]; then
    echo "  Already exists, skipping."
else
    git clone --depth 1 --branch v1.91.8 https://github.com/ocornut/imgui.git "${DEPS_DIR}/imgui"
    echo "  Done."
fi
echo

# --- libultrahdr v1.4.0 (full clone) ---
echo "[3/4] Cloning libultrahdr v1.4.0..."
if [ -f "${DEPS_DIR}/libultrahdr/CMakeLists.txt" ]; then
    echo "  Already exists, skipping."
else
    git clone --branch v1.4.0 https://github.com/google/libultrahdr.git "${DEPS_DIR}/libultrahdr"
    echo "  Done."
fi
echo

# --- Patch turbojpeg ---
echo "[4/4] Patching turbojpeg CMakeLists.txt..."
TURBOJPEG_CMAKE="${DEPS_DIR}/libultrahdr/third_party/turbojpeg/CMakeLists.txt"
if [ -f "$TURBOJPEG_CMAKE" ]; then
    sed -i 's|include(cmakescripts/BuildPackages.cmake)|# Patched: skip BuildPackages\n# include(cmakescripts/BuildPackages.cmake)|' "$TURBOJPEG_CMAKE"
    echo "  Done."
else
    echo "  turbojpeg not found, skipping patch."
fi
echo

echo "========================================"
echo " All dependencies ready!"
echo "========================================"
echo
echo "To build:"
echo "  cmake -B out/build/ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DUHDR_VIEWER_VULKAN=OFF"
echo "  cmake --build out/build/ninja-release"
