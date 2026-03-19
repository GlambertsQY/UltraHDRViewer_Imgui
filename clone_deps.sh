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
echo "[1/5] Cloning GLFW 3.4..."
if [ -f "${DEPS_DIR}/glfw/CMakeLists.txt" ]; then
    echo "  Already exists, skipping."
else
    git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git "${DEPS_DIR}/glfw"
    echo "  Done."
fi
echo

# --- Dear ImGui v1.91.8 ---
echo "[2/5] Cloning Dear ImGui v1.91.8..."
if [ -f "${DEPS_DIR}/imgui/imgui.h" ]; then
    echo "  Already exists, skipping."
else
    git clone --depth 1 --branch v1.91.8 https://github.com/ocornut/imgui.git "${DEPS_DIR}/imgui"
    echo "  Done."
fi
echo

# --- libultrahdr v1.4.0 (full clone) ---
echo "[3/5] Cloning libultrahdr v1.4.0..."
if [ -f "${DEPS_DIR}/libultrahdr/CMakeLists.txt" ]; then
    echo "  Already exists, skipping."
else
    git clone --branch v1.4.0 https://github.com/google/libultrahdr.git "${DEPS_DIR}/libultrahdr"
    echo "  Done."
fi
echo

# --- stb_image.h (single-file image library) ---
echo "[4/5] Downloading stb_image.h..."
if [ -f "${DEPS_DIR}/stb_image.h" ]; then
    echo "  Already exists, skipping."
else
    curl -sL https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -o "${DEPS_DIR}/stb_image.h"
    echo "  Done."
fi
echo

# --- Patch turbojpeg CMakeLists.txt ---
echo "[5/5] Patching turbojpeg CMakeLists.txt..."
TURBOJPEG="${DEPS_DIR}/libultrahdr/third_party/turbojpeg"

if [ -f "${TURBOJPEG}/CMakeLists.txt" ]; then
    # Comment out BuildPackages.cmake include
    sed -i 's|^(include(cmakescripts/BuildPackages.cmake))$| # Patched by clone_deps.sh: skip BuildPackages\n\1|' "${TURBOJPEG}/CMakeLists.txt" 2>/dev/null || \
    sed -i 's|include(cmakescripts/BuildPackages.cmake)|# Patched: skip BuildPackages\n#include(cmakescripts/BuildPackages.cmake)|' "${TURBOJPEG}/CMakeLists.txt"

    # Comment out install() commands that depend on BuildPackages.cmake output
    sed -i 's|^(install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pkgscripts/libjpeg.pc$)"|# Patched: \1"|' "${TURBOJPEG}/CMakeLists.txt"
    sed -i 's|^(install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pkgscripts/libturbojpeg.pc$)|# Patched: \1|' "${TURBOJPEG}/CMakeLists.txt"
    sed -i 's|^(install(FILES$|# Patched: \1|' "${TURBOJPEG}/CMakeLists.txt"
    sed -i 's|^\s*\$\{CMAKE_CURRENT_BINARY_DIR\}/pkgscripts/\$\{CMAKE_PROJECT_NAME\}Config.cmake$|#   \0|' "${TURBOJPEG}/CMakeLists.txt"
    sed -i 's|^(install(EXPORT \$\{CMAKE_PROJECT_NAME\}Targets)|# Patched: \1|' "${TURBOJPEG}/CMakeLists.txt"

    # Create missing template file for BuildPackages.cmake (win/projectTargets-release.cmake.in)
    if [ ! -f "${TURBOJPEG}/win/projectTargets-release.cmake.in" ] && [ -f "${TURBOJPEG}/win/vc/projectTargets-release.cmake.in" ]; then
        cp "${TURBOJPEG}/win/vc/projectTargets-release.cmake.in" "${TURBOJPEG}/win/projectTargets-release.cmake.in"
        echo "  Created missing win/projectTargets-release.cmake.in"
    fi
    echo "  Patched."
else
    echo "  turbojpeg not found, skipping."
fi
echo

echo "========================================"
echo " All dependencies ready!"
echo "========================================"
echo
echo "To build with Ninja:"
echo "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUHDR_VIEWER_VULKAN=OFF"
echo "  cmake --build build"
