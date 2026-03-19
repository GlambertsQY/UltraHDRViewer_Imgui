@echo off
setlocal enabledelayedexpansion

set DEPS_DIR=%~dp0deps
set SCRIPT_DIR=%~dp0

echo ========================================
echo  UltraHDR Viewer - Dependency Cloner
echo ========================================
echo.

if not exist "%DEPS_DIR%" mkdir "%DEPS_DIR%"

:: --- GLFW 3.4 ---
echo [1/5] Cloning GLFW 3.4...
if exist "%DEPS_DIR%\glfw\CMakeLists.txt" (
    echo   Already exists, skipping.
) else (
    git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git "%DEPS_DIR%\glfw"
    if errorlevel 1 (
        echo   ERROR: Failed to clone GLFW
        exit /b 1
    )
    echo   Done.
)
echo.

:: --- Dear ImGui v1.91.8 ---
echo [2/5] Cloning Dear ImGui v1.91.8...
if exist "%DEPS_DIR%\imgui\imgui.h" (
    echo   Already exists, skipping.
) else (
    git clone --depth 1 --branch v1.91.8 https://github.com/ocornut/imgui.git "%DEPS_DIR%\imgui"
    if errorlevel 1 (
        echo   ERROR: Failed to clone Dear ImGui
        exit /b 1
    )
    echo   Done.
)
echo.

:: --- libultrahdr v1.4.0 (full clone - needs third_party/turbojpeg) ---
echo [3/5] Cloning libultrahdr v1.4.0...
if exist "%DEPS_DIR%\libultrahdr\CMakeLists.txt" (
    echo   Already exists, skipping.
) else (
    git clone --branch v1.4.0 https://github.com/google/libultrahdr.git "%DEPS_DIR%\libultrahdr"
    if errorlevel 1 (
        echo   ERROR: Failed to clone libultrahdr
        exit /b 1
    )
    echo   Done.
)
echo.

:: --- stb_image.h (single-file image library) ---
echo [4/5] Downloading stb_image.h...
if exist "%DEPS_DIR%\stb_image.h" (
    echo   Already exists, skipping.
) else (
    powershell -Command "Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/nothings/stb/master/stb_image.h' -OutFile '%DEPS_DIR%\stb_image.h'"
    echo   Done.
)
echo.

:: --- Patch turbojpeg CMakeLists.txt (skip BuildPackages.cmake and its dependencies) ---
echo [5/5] Patching turbojpeg CMakeLists.txt...
set TURBOJPEG=%DEPS_DIR%\libultrahdr\third_party\turbojpeg

if exist "%TURBOJPEG%\CMakeLists.txt" (
    :: Comment out BuildPackages.cmake include
    powershell -Command "(Get-Content '%TURBOJPEG%\CMakeLists.txt') -replace '^(include\(cmakescripts/BuildPackages.cmake\))$', '# Patched by clone_deps.bat: skip BuildPackages (needs missing NSIS installer templates)' | Set-Content '%TURBOJPEG%\CMakeLists.txt'"

    :: Comment out install() commands that depend on BuildPackages.cmake output
    powershell -Command "$c = Get-Content '%TURBOJPEG%\CMakeLists.txt' -Raw; $c = $c -replace '(?m)^(install\(FILES \$\{CMAKE_CURRENT_BINARY_DIR\}/pkgscripts/libjpeg\.pc$)', '# Patched: $1'; $c = $c -replace '(?s)(install\(FILES.*?\$\{CMAKE_CURRENT_BINARY_DIR\}/pkgscripts/libturbojpeg\.pc.*?\))', '# Patched: $1'; $c = $c -replace '(?s)(install\(FILES\s*\$\{CMAKE_CURRENT_BINARY_DIR\}/pkgscripts/\$\{CMAKE_PROJECT_NAME\}Config\.cmake.*?\))', '# Patched: $1'; $c = $c -replace '(?m)^(install\(EXPORT \$\{CMAKE_PROJECT_NAME\}Targets.*?\))', '# Patched: $1'; Set-Content '%TURBOJPEG%\CMakeLists.txt' -Value $c"

    :: Create missing template file for BuildPackages.cmake (win/projectTargets-release.cmake.in)
    :: ExternalProject clones this source, so the file must exist to avoid configure errors
    if not exist "%TURBOJPEG%\win\projectTargets-release.cmake.in" (
        copy "%TURBOJPEG%\win\vc\projectTargets-release.cmake.in" "%TURBOJPEG%\win\projectTargets-release.cmake.in" >nul
        echo   Created missing win/projectTargets-release.cmake.in
    )
    echo   Patched.
) else (
    echo   turbojpeg not found, skipping.
)
echo.

echo ========================================
echo  All dependencies ready!
echo ========================================
echo.
echo To build with Ninja (recommended):
echo   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUHDR_VIEWER_VULKAN=OFF
echo   cmake --build build
echo.
echo Or with VS 2019/2022 (if cmake ^>= 3.21):
echo   cmake --preset vs2022-opengl
echo   cmake --build build --config Release
echo.
pause
