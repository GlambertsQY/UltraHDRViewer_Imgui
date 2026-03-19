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
echo [1/7] Cloning GLFW 3.4...
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
echo [2/7] Cloning Dear ImGui v1.91.8...
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
echo [3/7] Cloning libultrahdr v1.4.0...
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
echo [4/7] Downloading stb_image.h...
if exist "%DEPS_DIR%\stb_image.h" (
    echo   Already exists, skipping.
) else (
    powershell -Command "Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/nothings/stb/master/stb_image.h' -OutFile '%DEPS_DIR%\stb_image.h'"
    echo   Done.
)
echo.

:: --- SDL2 2.30.x (for Android support) ---
echo [5/7] Cloning SDL2 2.30.8...
if exist "%DEPS_DIR%\sdl2\include\SDL.h" (
    echo   Already exists, skipping.
) else (
    git clone --depth 1 --branch release-2.30.8 https://github.com/libsdl-org/SDL.git "%DEPS_DIR%\sdl2"
    if errorlevel 1 (
        echo   ERROR: Failed to clone SDL2
        exit /b 1
    )
    echo   Done.
)
echo.

:: --- turbojpeg 3.0.1 (for Android add_subdirectory builds) ---
echo [6/7] Cloning turbojpeg 3.0.1...
if exist "%DEPS_DIR%\turbojpeg\CMakeLists.txt" (
    echo   Already exists, skipping.
) else (
    git clone --depth 1 --branch 3.0.1 https://github.com/libjpeg-turbo/libjpeg-turbo.git "%DEPS_DIR%\turbojpeg"
    if errorlevel 1 (
        echo   ERROR: Failed to clone turbojpeg
        exit /b 1
    )
    echo   Done.
)
echo.

:: --- Patch turbojpeg CMakeLists.txt files ---
echo [7/7] Patching turbojpeg CMakeLists.txt...

:: --- Patch 1: libultrahdr/third_party/turbojpeg (for ExternalProject and Windows builds) ---
set TURBOJPEG=%DEPS_DIR%\libultrahdr\third_party\turbojpeg

if exist "%TURBOJPEG%\CMakeLists.txt" (
    :: Comment out BuildPackages.cmake include
    powershell -Command "(Get-Content '%TURBOJPEG%\CMakeLists.txt') -replace '^(include\(cmakescripts/BuildPackages.cmake\))$', '# Patched by clone_deps.bat: skip BuildPackages (needs missing NSIS installer templates)' | Set-Content '%TURBOJPEG%\CMakeLists.txt'"

    :: Comment out install() commands that depend on BuildPackages.cmake output
    powershell -Command "$c = Get-Content '%TURBOJPEG%\CMakeLists.txt' -Raw; $c = $c -replace '(?m)^(install\(FILES \$\{CMAKE_CURRENT_BINARY_DIR\}/pkgscripts/libjpeg\.pc$)', '# Patched: $1'; $c = $c -replace '(?s)(install\(FILES.*?\$\{CMAKE_CURRENT_BINARY_DIR\}/pkgscripts/libturbojpeg\.pc.*?\))', '# Patched: $1'; $c = $c -replace '(?s)(install\(FILES\s*\$\{CMAKE_CURRENT_BINARY_DIR\}/pkgscripts/\$\{CMAKE_PROJECT_NAME\}Config\.cmake.*?\))', '# Patched: $1'; $c = $c -replace '(?m)^(install\(EXPORT \$\{CMAKE_PROJECT_NAME\}Targets.*?\))', '# Patched: $1'; Set-Content '%TURBOJPEG%\CMakeLists.txt' -Value $c"

    :: Create missing template file for BuildPackages.cmake (win/projectTargets-release.cmake.in)
    if not exist "%TURBOJPEG%\win\projectTargets-release.cmake.in" (
        copy "%TURBOJPEG%\win\vc\projectTargets-release.cmake.in" "%TURBOJPEG%\win\projectTargets-release.cmake.in" >nul
        echo   Created missing win/projectTargets-release.cmake.in
    )
    echo   libultrahdr/third_party/turbojpeg patched.
) else (
    echo   libultrahdr/third_party/turbojpeg not found, skipping.
)

:: --- Patch 2: deps/turbojpeg (for Android add_subdirectory builds) ---
set TURBOJPEG_DEPS=%DEPS_DIR%\turbojpeg

if exist "%TURBOJPEG_DEPS%\CMakeLists.txt" (
    :: Add subproject guard after cmake_minimum_required
    powershell -Command ^
        "$c = Get-Content '%TURBOJPEG_DEPS%\CMakeLists.txt' -Raw; " ^
        "$guard = \"`nif(TURBOJPEG_IS_SUBPROJECT)`n" ^
            "    set(CMAKE_INSTALL_DOCDIR `\"doc`\" CACHE PATH `\"`\")`n" ^
            "    set(CMAKE_INSTALL_INCLUDEDIR `\"include`\" CACHE PATH `\"`\")`n" ^
            "    set(_TJ_SKIP_INSTALL ON)`n" ^
            "endif()`n`n\"; " ^
        "$idx = $c.IndexOf('if(POLICY CMP0065)'); " ^
        "if ($idx -gt 0 -and $c.IndexOf('TURBOJPEG_IS_SUBPROJECT') -lt 0) { " ^
        "    $c = $c.Substring(0, $idx) + $guard + $c.Substring($idx); " ^
        "    Set-Content -Path '%TURBOJPEG_DEPS%\CMakeLists.txt' -Value $c; " ^
        "    Write-Host '  Added TURBOJPEG_IS_SUBPROJECT guard.' " ^
        "}"

    :: Wrap install blocks with _TJ_SKIP_INSTALL
    powershell -Command ^
        "$c = Get-Content '%TURBOJPEG_DEPS%\CMakeLists.txt' -Raw; " ^
        "$c = $c -replace '(?m)^(install\(TARGETS rdjpgcom wrjpgcom RUNTIME DESTINATION \$\{CMAKE_INSTALL_BINDIR\}\))', \"`n`$1\"; " ^
        "$c = $c -replace '^(install\(FILES \$\{CMAKE_CURRENT_SOURCE_DIR\}/README\.ijg)', \"if(NOT _TJ_SKIP_INSTALL)`n`$1\"; " ^
        "$c = $c -replace '(?m)^(install\(FILES \$\{CMAKE_CURRENT_BINARY_DIR\}/jconfig\.h)', \"endif() # _TJ_SKIP_INSTALL`n`n`$1\"; " _
        "-Replace '(?m)^(include\(cmakescripts/BuildPackages.cmake\))', \"`n`$1`n`nif(NOT _TJ_SKIP_INSTALL)\"; " _
        "-Replace '(?m)^(add_custom_target\(uninstall)', \"endif() # _TJ_SKIP_INSTALL`n`n`$1\"; " _
        "-Replace '^(configure_file\(\"\${CMAKE_CURRENT_SOURCE_DIR}/cmakescripts/cmake_uninstall)', \"`n`$1\"; " _
        "Set-Content -Path '%TURBOJPEG_DEPS%\CMakeLists.txt' -Value $c; " _
        "Write-Host '  deps/turbojpeg patched for Android.'"

    echo   deps/turbojpeg patched.
) else (
    echo   deps/turbojpeg not found, skipping.
)
echo.

echo ========================================
echo  All dependencies ready!
echo ========================================
echo.
echo To build for Windows with Ninja (recommended):
echo   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUHDR_VIEWER_VULKAN=OFF
echo   cmake --build build
echo.
echo To build for Android:
echo   cd android
echo   gradlew assembleDebug
echo.
pause
