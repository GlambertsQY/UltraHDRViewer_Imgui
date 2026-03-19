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
echo "[1/6] Cloning GLFW 3.4..."
if [ -f "${DEPS_DIR}/glfw/CMakeLists.txt" ]; then
    echo "  Already exists, skipping."
else
    git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git "${DEPS_DIR}/glfw"
    echo "  Done."
fi
echo

# --- Dear ImGui v1.91.8 ---
echo "[2/6] Cloning Dear ImGui v1.91.8..."
if [ -f "${DEPS_DIR}/imgui/imgui.h" ]; then
    echo "  Already exists, skipping."
else
    git clone --depth 1 --branch v1.91.8 https://github.com/ocornut/imgui.git "${DEPS_DIR}/imgui"
    echo "  Done."
fi
echo

# --- libultrahdr v1.4.0 (full clone) ---
echo "[3/6] Cloning libultrahdr v1.4.0..."
if [ -f "${DEPS_DIR}/libultrahdr/CMakeLists.txt" ]; then
    echo "  Already exists, skipping."
else
    git clone --branch v1.4.0 https://github.com/google/libultrahdr.git "${DEPS_DIR}/libultrahdr"
    echo "  Done."
fi
echo

# --- stb_image.h (single-file image library) ---
echo "[4/6] Downloading stb_image.h..."
if [ -f "${DEPS_DIR}/stb_image.h" ]; then
    echo "  Already exists, skipping."
else
    curl -sL https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -o "${DEPS_DIR}/stb_image.h"
    echo "  Done."
fi
echo

# --- SDL2 2.30.x (for Android support) ---
echo "[5/6] Cloning SDL2 2.30.8..."
if [ -f "${DEPS_DIR}/sdl2/include/SDL.h" ]; then
    echo "  Already exists, skipping."
else
    git clone --depth 1 --branch release-2.30.8 https://github.com/libsdl-org/SDL.git "${DEPS_DIR}/sdl2"
    echo "  Done."
fi
echo

# --- Patch turbojpeg CMakeLists.txt (libultrahdr third_party + Android deps) ---
echo "[6/6] Patching turbojpeg CMakeLists.txt..."

# Patch 1: libultrahdr/third_party/turbojpeg (for ExternalProject and Windows builds)
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
    echo "  libultrahdr/third_party/turbojpeg patched."
else
    echo "  libultrahdr/third_party/turbojpeg not found, skipping."
fi

# Patch 2: deps/turbojpeg (for Android add_subdirectory builds)
TURBOJPEG_DEPS="${DEPS_DIR}/turbojpeg"
if [ -f "${TURBOJPEG_DEPS}/CMakeLists.txt" ]; then
    # Add subproject guard after cmake_minimum_required
    if ! grep -q "TURBOJPEG_IS_SUBPROJECT" "${TURBOJPEG_DEPS}/CMakeLists.txt"; then
        python3 -c "
import sys
with open('${TURBOJPEG_DEPS}/CMakeLists.txt', 'r') as f:
    content = f.read()

# Add subproject guard after cmake_minimum_required
guard = '''if(TURBOJPEG_IS_SUBPROJECT)
    set(CMAKE_INSTALL_DOCDIR \"doc\" CACHE PATH \"\")
    set(CMAKE_INSTALL_INCLUDEDIR \"include\" CACHE PATH \"\")
    set(_TJ_SKIP_INSTALL ON)
endif()
'''
idx = content.find('if(POLICY CMP0065)')
if idx != -1:
    content = content[:idx] + guard + content[idx:]
with open('${TURBOJPEG_DEPS}/CMakeLists.txt', 'w') as f:
    f.write(content)
"
        echo "  Added TURBOJPEG_IS_SUBPROJECT guard."
    fi

    # Wrap install blocks with _TJ_SKIP_INSTALL
    python3 -c "
import re
with open('${TURBOJPEG_DEPS}/CMakeLists.txt', 'r') as f:
    content = f.read()

# Wrap the main install section
old = '''install(TARGETS rdjpgcom wrjpgcom RUNTIME DESTINATION \${CMAKE_INSTALL_BINDIR})

install(FILES \${CMAKE_CURRENT_SOURCE_DIR}/README.ijg'''
new = '''if(NOT _TJ_SKIP_INSTALL)
install(TARGETS rdjpgcom wrjpgcom RUNTIME DESTINATION \${CMAKE_INSTALL_BINDIR})

install(FILES \${CMAKE_CURRENT_SOURCE_DIR}/README.ijg'''
content = content.replace(old, new)

old2 = '''install(FILES \${CMAKE_CURRENT_BINARY_DIR}/jconfig.h
  \${CMAKE_CURRENT_SOURCE_DIR}/jerror.h \${CMAKE_CURRENT_SOURCE_DIR}/jmorecfg.h
  \${CMAKE_CURRENT_SOURCE_DIR}/jpeglib.h
  DESTINATION \${CMAKE_INSTALL_INCLUDEDIR})

include(cmakescripts/BuildPackages.cmake)

configure_file'''
new2 = '''install(FILES \${CMAKE_CURRENT_BINARY_DIR}/jconfig.h
  \${CMAKE_CURRENT_SOURCE_DIR}/jerror.h \${CMAKE_CURRENT_SOURCE_DIR}/jmorecfg.h
  \${CMAKE_CURRENT_SOURCE_DIR}/jpeglib.h
  DESTINATION \${CMAKE_INSTALL_INCLUDEDIR})
endif() # _TJ_SKIP_INSTALL

include(cmakescripts/BuildPackages.cmake)

if(NOT _TJ_SKIP_INSTALL)
configure_file'''
content = content.replace(old2, new2)

old3 = '''add_custom_target(uninstall COMMAND \${CMAKE_COMMAND} -P cmake_uninstall.cmake)'''
new3 = '''add_custom_target(uninstall COMMAND \${CMAKE_COMMAND} -P cmake_uninstall.cmake)
endif()'''
content = content.replace(old3, new3)

with open('${TURBOJPEG_DEPS}/CMakeLists.txt', 'w') as f:
    f.write(content)
" 2>/dev/null || echo "  (Python patch skipped)"
    echo "  deps/turbojpeg patched for Android."
else
    echo "  deps/turbojpeg not found, skipping."
fi

# Patch 3: Also clone deps/turbojpeg if it doesn't exist (needed for Android)
if [ ! -d "${TURBOJPEG_DEPS}" ]; then
    git clone --depth 1 --branch 3.0.1 https://github.com/libjpeg-turbo/libjpeg-turbo.git "${TURBOJPEG_DEPS}"
    echo "  Cloned deps/turbojpeg for Android."
fi
echo

echo "========================================"
echo " All dependencies ready!"
echo "========================================"
echo
echo "To build for Windows with Ninja:"
echo "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUHDR_VIEWER_VULKAN=OFF"
echo "  cmake --build build"
echo
echo "To build for Android:"
echo "  cd android"
echo "  gradlew assembleDebug"
echo
