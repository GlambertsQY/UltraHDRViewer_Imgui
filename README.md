# Ultra HDR Viewer

An Ultra HDR image viewer built with [Dear ImGui](https://github.com/ocornut/imgui) and [libultrahdr](https://github.com/google/libultrahdr).

## Features

- Decode and display Ultra HDR images (.jpg/.uhdr)
- HDR and SDR image support
- Three tone mapping algorithms: Reinhard / ACES Filmic / Uncharted 2
- Exposure and gamma adjustment
- Image zoom, pan, fit-to-window
- Drag-and-drop file opening (desktop)
- OpenGL and Vulkan graphics backends (desktop)
- Native Win32 file dialog (desktop)
- EXIF metadata display (camera info, exposure, GPS coordinates)
- Android support via SDL2 + OpenGL ES

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| GLFW | 3.4 | Window/input management (desktop) |
| SDL2 | 2.30.8 | Window/input management (Android) |
| Dear ImGui | v1.91.8 | GUI framework |
| libultrahdr | v1.4.0 | Ultra HDR decoding |
| Vulkan SDK | 1.0+ | Vulkan backend (optional, desktop) |

## Building

### 1. Clone dependencies

```bat
clone_deps.bat
```
```bash
# Linux/macOS
./clone_deps.sh
```

This downloads GLFW/SDL2, Dear ImGui, and libultrahdr source to `deps/`, and automatically applies build patches.

### 2. Configure & build

#### Desktop (Windows)

**Recommended: Ninja (requires Ninja + Clang or MSVC)**

```bat
:: Debug
cmake -B build\debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUHDR_VIEWER_VULKAN=OFF
cmake --build build\debug

:: Release
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUHDR_VIEWER_VULKAN=OFF
cmake --build build
```

**Or Visual Studio (cmake >= 3.21 for VS 2022)**

```bat
:: OpenGL backend
cmake --preset vs2022-opengl
cmake --build build --config Release

:: Vulkan backend
cmake --preset vs2022-vulkan
cmake --build build --config Release
```

**Run desktop build:**
```bat
:: Default OpenGL backend
build\bin\UltraHDRViewer.exe

:: Vulkan backend
build\bin\UltraHDRViewer.exe --vulkan
```

#### Android

Requirements: Android SDK, Android NDK 23.1+, Java 11.

```bash
cd android
./gradlew assembleDebug
```

The APK will be at `android/app/build/outputs/apk/debug/app-debug.apk`.

Install via ADB:
```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Open images from the file manager or share them to the app.

## Command Line Options (Desktop)

| Option | Description |
|---|---|
| `--opengl`, `-g` | Use OpenGL backend (default) |
| `--vulkan`, `-v` | Use Vulkan backend |
| `--width N` | Window width |
| `--height N` | Window height |
| `--help`, `-h` | Show help |

## Keyboard Shortcuts (Desktop)

| Shortcut | Action |
|---|---|
| `Ctrl+O` | Open file |
| `Ctrl+S` | Save as SDR image |
| `F` | Fit to window |
| `R` | Reset view |
| `I` | Toggle info panel |
| `Esc` | Exit |

## Project Structure

```
UltraHDRViewer_Imgui/
├── CMakeLists.txt          # CMake build configuration (desktop)
├── CMakePresets.json        # VS 2022 build presets
├── clone_deps.bat           # Dependency cloner (Windows)
├── clone_deps.sh            # Dependency cloner (Linux/macOS)
├── deps/                    # Dependency sources (after clone)
│   ├── glfw/                # GLFW 3.4 (desktop)
│   ├── sdl2/                # SDL2 2.30.x (Android)
│   ├── imgui/               # Dear ImGui
│   ├── libultrahdr/         # libultrahdr + third_party/turbojpeg
│   └── stb_image.h          # stb_image
├── src/                     # Desktop app source
│   ├── main.cpp             # Entry point
│   ├── app.h / app.cpp      # Application logic + UI
│   ├── renderer.h           # Renderer interface
│   ├── renderer_gl.h/cpp    # OpenGL backend
│   ├── renderer_vk.h/cpp    # Vulkan backend
│   ├── image_loader.h/cpp   # Ultra HDR image loading
│   ├── exif_parser.h/cpp    # EXIF metadata parser
│   ├── file_dialog.h/cpp    # Native file dialog
│   └── stb_image_impl.cpp   # stb_image implementation
└── android/                 # Android project
    ├── app/
    │   ├── build.gradle      # App module config (AGP 7.4, NDK 23.1)
    │   └── src/main/
    │       ├── AndroidManifest.xml
    │       ├── java/com/uhdrviewer/MainActivity.java
    │       └── cpp/
    │           ├── CMakeLists.txt   # SDL2 + turbojpeg + libultrahdr + ImGui
    │           ├── native.cpp        # SDL2 + ImGui app entry point
    │           ├── hdr_types.h       # Shared data structures
    │           ├── image_loader_android.cpp  # Image loading
    │           └── exif_parser_android.cpp   # EXIF parsing
    └── gradle/              # Gradle 7.6 + wrapper
```

## Architecture

```
┌──────────────────────────────────────┐
│              Application             │
│  (UI logic, input handling, layout)  │
├──────────┬───────────┬───────────────┤
│ ImageLoader │ FileDialog │   Renderer   │
│ (libultrahdr│(Win32 API) │  (interface) │
│  decode)    │            │              │
├─────────────┴────────────┼──────────────┤
│                          │              │
│                     OpenGL│  Vulkan      │
│                     Backend│  Backend    │
└──────────────────────────┴──────────────┘
```

## License

- Dear ImGui: MIT License
- libultrahdr: Apache 2.0 License
- GLFW: zlib/libpng License
