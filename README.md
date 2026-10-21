# Ultra HDR Viewer

An Ultra HDR image viewer built with [Dear ImGui](https://github.com/ocornut/imgui) and [libultrahdr](https://github.com/google/libultrahdr).

## Features

- Decode and display Ultra HDR images (.jpg/.uhdr)
- HDR and SDR image support
- Three tone mapping algorithms: Reinhard / ACES Filmic / Uncharted 2
- Exposure and gamma adjustment
- HDR display output: scRGB / HDR10 (desktop Vulkan) and HDR10/PQ (Android EGL)
- Image zoom, pan, fit-to-window (mouse on desktop, touch gestures on Android)
- Drag-and-drop file opening (desktop)
- OpenGL and Vulkan graphics backends (desktop)
- Native Win32 file dialog (desktop)
- EXIF metadata display (camera info, exposure, GPS coordinates)
- Android 17 (API 37) support with true HDR decode and HDR10 direct output

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| GLFW | 3.4 | Window/input management (desktop) |
| Dear ImGui | v1.91.8 | GUI framework |
| libultrahdr | v1.4.0 | Ultra HDR decoding |
| Vulkan SDK | 1.0+ | Vulkan backend (optional, desktop) |

Android rendering uses the platform EGL + OpenGL ES 3 APIs directly (no SDL2 dependency).

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

**Or Visual Studio (cmake >= 3.21 for VS 2026)**

```bat
:: OpenGL backend
cmake --preset vs2026-opengl
cmake --build build --config Release

:: Vulkan backend
cmake --preset vs2026-vulkan
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

Requirements: JDK 17, Android SDK Platform 37 (Android 17), Build-Tools 36+, NDK 28.2.

The repository includes a setup script that installs the toolchain into `.tools/`
(project-local, no system changes):

```powershell
powershell -ExecutionPolicy Bypass -File android\setup_android_sdk.ps1
```

Then build:

```powershell
cd android
$env:JAVA_HOME="$PWD\..\.tools\jdk-17"
.\gradlew.bat assembleDebug
```

The APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`.

Install via ADB:
```powershell
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

Usage on device:
- Tap **Open Image** (or the empty image area) to pick an image through the system document picker.
- Images can also be opened with `ACTION_VIEW` / `ACTION_SEND` from a file manager or gallery.
- Single-finger drag pans, pinch zooms, double-tap resets to fit.
- **HDR Output (PQ)** is enabled automatically for Ultra HDR images on HDR10-capable displays.

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
| `H` | Toggle HDR output (HDR display + Vulkan only) |
| `Esc` | Exit |

## HDR Display Output

### Desktop (Vulkan only)

On HDR-capable displays the app can output true HDR instead of tone-mapped SDR:

- **Requirements**: Vulkan backend (`--vulkan`), an HDR-capable display with HDR enabled in Windows settings, and an image with real HDR data (Ultra HDR).
- **Toggle**: `H` key or *View → HDR 输出*. scRGB (FP16 linear) is preferred; HDR10 (PQ/BT.2020) is used as fallback. Moving the window to an SDR display falls back to SDR automatically.
- **OpenGL backend is SDR-only by design**: Windows WGL has no HDR swapchain mechanism (no way to set scRGB/HDR10 color space on the window framebuffer), so HDR output is impossible there — use `--vulkan` instead.
- **Testing on an SDR machine**: set `UHDR_MOCK_HDR_OUTPUT=scrgb` (or `hdr10` / `both`) to simulate an HDR display. The full activation path runs (pipeline, FP16 texture, shaders, style baking); the real swapchain stays SDR and values > 1.0 are simply clamped.

### Android (HDR10 / PQ)

- **Requirements**: an HDR10-capable Android display and an Ultra HDR image with real HDR data.
- **How it works**: the app creates a 10-bit EGL window surface with `EGL_GL_COLORSPACE_BT2020_PQ_EXT`, uploads the decoded image as an RGBA16F texture, and converts linear BT.709 → BT.2020 → ST2084 (PQ) in a GLES3 shader. SMPTE 2086 / CTA-861.3 metadata is published when the driver exposes the extensions.
- **Toggle**: **HDR Output (PQ)** in the controls panel. It is enabled automatically for Ultra HDR images and disabled again for SDR images.
- **Fallback**: if the device or driver lacks `EGL_EXT_gl_colorspace_bt2020_pq` or a 10-bit EGL config, the app stays on the SDR path and disables the toggle.
- ImGui renders into an SDR RGBA8 offscreen buffer; a final shader converts it (sRGB → BT.2020 → PQ at a 400-nit UI white point) and composites it over the HDR image, preserving panel colors and alpha blending.

## Project Structure

```
UltraHDRViewer_Imgui/
├── CMakeLists.txt          # CMake build configuration (desktop)
├── CMakePresets.json        # VS 2026 build presets
├── clone_deps.bat           # Dependency cloner (Windows)
├── clone_deps.sh            # Dependency cloner (Linux/macOS)
├── deps/                    # Dependency sources (after clone)
│   ├── glfw/                # GLFW 3.4 (desktop)
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
    ├── setup_android_sdk.ps1  # Installs JDK 17 + SDK 37 + NDK r28 into .tools/
    ├── app/
    │   ├── build.gradle      # App module config (AGP 9.1, NDK 28.2, targetSdk 37)
    │   └── src/main/
    │       ├── AndroidManifest.xml
    │       ├── java/com/uhdrviewer/MainActivity.java
    │       └── cpp/
    │           ├── CMakeLists.txt   # turbojpeg + libultrahdr + ImGui (GLES3)
    │           ├── native.cpp        # EGL/GLES render thread, touch input, UI
    │           ├── egl_context.cpp   # EGL HDR10/SDR surface + metadata
    │           ├── hdr_gles.cpp      # PQ shader + FP16 texture + style baking
    │           ├── hdr_types.h       # Shared data structures
    │           ├── image_loader_android.cpp  # True HDR decode
    │           └── exif_parser_android.cpp   # EXIF parsing
    └── gradle/              # Gradle 9.3.1 + wrapper
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
