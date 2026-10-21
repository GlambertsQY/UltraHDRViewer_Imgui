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

:: --- Patch 2: deps/turbojpeg (for Android add_subdirectory builds) ---
set TURBOJPEG_DEPS=%DEPS_DIR%\turbojpeg

if not exist "%TURBOJPEG_DEPS%\CMakeLists.txt" goto :patch_turbojpeg_done
:: Allow add_subdirectory() integration (upstream guards against it)
powershell -NoProfile -Command "$f='%TURBOJPEG_DEPS%\CMakeLists.txt'; $c=[IO.File]::ReadAllText($f); if($c -notmatch 'TURBOJPEG_IS_SUBPROJECT'){ $c=$c.Replace('if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)', 'if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT TURBOJPEG_IS_SUBPROJECT)'); [IO.File]::WriteAllText($f,$c); Write-Host '  deps/turbojpeg patched to allow add_subdirectory().' }"
echo   deps/turbojpeg patched.
:patch_turbojpeg_done
echo.

:: --- Patch 3: libultrahdr (allow prebuilt JPEG for Android add_subdirectory builds) ---
set UHDR_CMAKE=%DEPS_DIR%\libultrahdr\CMakeLists.txt
if not exist "%UHDR_CMAKE%" goto :patch_uhdr_done
powershell -NoProfile -Command "$f='%UHDR_CMAKE%'; $c=[IO.File]::ReadAllText($f); if($c -match '\r\n'){ $nl=[char]13+[char]10 } else { $nl=[char]10 }; if($c -notmatch 'JPEG_PREBUILT_LIB AND JPEG_PREBUILT_INCLUDE'){ $q=[char]34; $ins='# libjpeg-turbo'+$nl+'if(JPEG_PREBUILT_LIB AND JPEG_PREBUILT_INCLUDE)'+$nl+'  set(JPEG_FOUND TRUE)'+$nl+'  set(JPEG_LIBRARIES ${JPEG_PREBUILT_LIB})'+$nl+'  set(JPEG_INCLUDE_DIRS ${JPEG_PREBUILT_INCLUDE})'+$nl+'  message(STATUS '+$q+'Using prebuilt JPEG: ${JPEG_PREBUILT_LIB}'+$q+')'+$nl+'endif()'+$nl+$nl+'if(NOT UHDR_BUILD_DEPS AND NOT JPEG_PREBUILT_LIB)'; $c=$c.Replace('# libjpeg-turbo'+$nl+'if(NOT UHDR_BUILD_DEPS)',$ins); [IO.File]::WriteAllText($f,$c); Write-Host '  libultrahdr patched for prebuilt JPEG.' }"
echo   libultrahdr patched for prebuilt JPEG.
:patch_uhdr_done
echo.

:: --- Patch 4: libultrahdr (copy libjpeg-turbo's missing installer template after ExternalProject clone) ---
if not exist "%UHDR_CMAKE%" goto :patch_uhdr_turbojpeg_done
powershell -NoProfile -Command "$f='%UHDR_CMAKE%'; $c=[IO.File]::ReadAllText($f); if($c -match '\r\n'){ $nl=[char]13+[char]10 } else { $nl=[char]10 }; if($c -notmatch 'copy_if_different'){ $old='        GIT_TAG 3.0.1'; $new='        GIT_TAG 3.0.1'+$nl+'        PATCH_COMMAND ${CMAKE_COMMAND} -E copy_if_different'+$nl+'                      ${JPEGTURBO_SOURCE_DIR}/win/vc/projectTargets-release.cmake.in'+$nl+'                      ${JPEGTURBO_SOURCE_DIR}/win/projectTargets-release.cmake.in'; $c=$c.Replace($old,$new); [IO.File]::WriteAllText($f,$c); Write-Host '  libultrahdr turbojpeg patch step added.' }"
echo   libultrahdr turbojpeg patch step added.
:patch_uhdr_turbojpeg_done
echo.

echo ========================================
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
if not defined CI pause
