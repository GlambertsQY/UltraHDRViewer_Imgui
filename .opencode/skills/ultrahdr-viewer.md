# UltraHDR Viewer - Project Skill

> C++17 Ultra HDR image viewer: Dear ImGui + GLFW + libultrahdr
> Target: Windows Desktop, VS2022 + CMake
> Skill generated from codebase analysis - reflects post-refactor state / 基于代码分析生成 - 反映重构后状态

---

## TL;DR Build / 快速构建

```bat
:: 1. Clone deps (one-time)
clone_deps.bat

:: 2. Configure + Build (VS2022 OpenGL, Debug)
cmake --preset vs2022-opengl
cmake --build out/build/vs2022-opengl --config Debug

:: 3. Run
out\build\vs2022-opengl\bin\Debug\UltraHDRViewer.exe

:: Alternative: VS2022 Vulkan
cmake --preset vs2022-vulkan
cmake --build out/build/vs2022-vulkan --config Release
out\build\vs2022-vulkan\bin\Release\UltraHDRViewer.exe --vulkan
```

## Project Structure / 项目结构

```
src/
├── main.cpp              # Entry point, CLI args, DPI awareness
├── app.h / app.cpp       # App lifecycle, image panel, UI orchestration
├── config.h              # Window/layout/slider constants (namespace Config)
├── log.h                 # LOG_ERROR/LOG_INFO macros (no external deps)
├── renderer.h            # Renderer interface + ImageData struct
├── renderer_gl.h / .cpp  # OpenGL backend (#ifdef UHDR_VIEWER_OPENGL_ENABLED)
├── renderer_vk.h / .cpp  # Vulkan backend (#ifdef UHDR_VIEWER_VULKAN_ENABLED)
├── image_loader.h / .cpp # Ultra HDR decoding, backward-compat includes tonemap.h
├── tonemap.h / .cpp      # Tone mapping: toneMapImage()/applyToneMap() (3 modes)
├── ui_panels.h / .cpp    # UI panels: MenuBar/ControlPanel/InfoPanel/AboutDialog
├── exif_parser.h / .cpp  # EXIF metadata parser (JPEG APP1 segments)
├── file_dialog.h / .cpp  # Win32 native file open/save dialogs
└── stb_image_impl.cpp    # stb_image implementation unit

deps/                     # After clone_deps.bat
├── glfw/                 # GLFW 3.4
├── imgui/                # Dear ImGui v1.91.8
├── libultrahdr/          # libultrahdr v1.4.0 + turbojpeg
├── sdl2/                 # SDL2 2.30.8 (Android only)
├── turbojpeg/            # turbojpeg 3.0.1 (Android only)
└── stb_image.h           # stb_image single-header
```

## New Modules / 新增模块（重构提取）

| File | Responsibility / 职责 | Extracted From / 提取自 |
|------|------------------------|--------------------------|
| `config.h` | `namespace Config` - window/layout/slider constants | Magic numbers in app.cpp |
| `log.h` | `LOG_ERROR`/`LOG_INFO` macros - minimal, no deps | Scattered `fprintf` |
| `tonemap.cpp/h` | `toneMapImage()` / `applyToneMap()` - 3 tone mapping modes | image_loader.cpp |
| `ui_panels.cpp/h` | `renderMenuBar/ControlPanel/InfoPanel/AboutDialog` | app.cpp |

**Note / 注意**: `renderImagePanel()` (image viewport + zoom/pan/checkerboard) remains in `app.cpp`
because it is tightly coupled to app state (`m_zoom`, `m_offsetX`, `m_texture`, `m_isDragging`).
Do NOT extract it without understanding these dependencies.

**Note / 注意**: In `config.h`, `PANEL_IMAGE_MARGIN = 290.0f` depends on `PANEL_CONTROLS_WIDTH = 280.0f`
(= 280 + 10 gap). Adjust margin when changing panel width.

**Note / 注意**: `image_loader.h` includes `#include "tonemap.h"` for backward compatibility.

## Construction Order / 初始化顺序 (Best Practice)

When modifying `Application::init`, follow this tested sequence:

```
1. glfwInit()
2. glfwWindowHint(GLFW_CLIENT_API, ...)    ← Hints BEFORE window creation
3. glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3)  ← Must be here, not in renderer
4. glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0)  ← Must be here, not in renderer
5. glfwCreateWindow(...)
6. glfwMakeContextCurrent(window)          ← OpenGL only
7. Renderer::init(window)                  ← ImGui context, backend init
8. glfwGetFramebufferSize(...)
```

**Note**: A previous bug placed GLFW hints inside the renderer after `glfwCreateWindow`
> This caused the window to use driver-default GL version instead of 3.0.
> **This is now fixed** - hints are set in `app.cpp` before `glfwCreateWindow`.
The order remains important as a best practice.

## Build Matrix / 构建矩阵

| Preset | Backend | Generator | Configs |
|--------|---------|-----------|---------|
| `vs2022-opengl` | OpenGL | VS 17 2022 | Debug, Release |
| `vs2022-vulkan` | Vulkan | VS 17 2022 | Debug, Release |
| `vs2022-both` | Both | VS 17 2022 | Debug, Release |
| `ninja-debug` | OpenGL | Ninja | Debug |
| `ninja-release` | OpenGL | Ninja | Release |

**After every code change wave, verify at minimum:**
```

cmake --preset vs2022-opengl && cmake --build out/build/vs2022-opengl --config Debug
```
Must compile with **0 errors, 0 new warnings** vs baseline.

## Code Conventions / 代码规范

### Platform Guards / 平台守卫
```cpp
#ifdef UHDR_VIEWER_OPENGL_ENABLED   // OpenGL-specific code
#ifdef UHDR_VIEWER_VULKAN_ENABLED   // Vulkan-specific code
#ifdef _WIN32                        // Win32 API (file dialogs, DPI)
```

### GLFW Lifecycle / GLFW 生命周期
- `glfwInit()` and `glfwTerminate()` can each be called only **once per process**
- Test harnesses MUST NOT call them in individual test cases
- Use a global test fixture or mock GLFW entirely

### Texture Handles / 纹理句柄
- OpenGL: `(void*)(intptr_t)glTextureID` - cast via intptr_t, NOT direct
- Vulkan: pointer to heap-allocated `VulkanTexture` struct
- **MUST NOT** change `void* m_texture` to a typed pointer without tagged union

### Error Reporting / 错误报告
- `LOG_ERROR(fmt, ...)` / `LOG_INFO(fmt, ...)` macros defined in `src/log.h`
- Uses `fprintf` internally - no external dependencies (no spdlog/fmt)
- All error and info output goes through these macros

### Memory Management / 内存管理
- `ImageData::pixels` (desktop, `src/renderer.h`): `std::vector<uint8_t>` - no manual allocation
- **Platform divergence**: Android (`android/.../hdr_types.h`) still uses `uint8_t* pixels` with `new[]`/`delete[]`
  - the two platforms have independent copies; changing one does NOT affect the other
- Vulkan handles (`VkDevice`, `VkInstance`, etc.) stay raw - they have their own lifecycle
- GLFWwindow* stays raw - GLFW owns it

## Testing / 测试

### Framework: Google Test (v1.14.0)
- Acquired via CMake `FetchContent` (`tests/CMakeLists.txt`)
- Test target: `UltraHDRViewerTests`

### Build & Run Tests
```bat
cmake --preset vs2022-opengl
cmake --build out/build/vs2022-opengl --config Debug
ctest --test-dir out/build/vs2022-opengl --output-on-failure
```

### Test Files
| File | Coverage |
|------|----------|
| `tests/tonemap_test.cpp` | Tone mapping: output bounds, Reinhard identity, exposure zero, mode differences, gamma |
| `tests/exif_parser_test.cpp` | EXIF parsing: camera info, orientation range, image dimensions, GPS validation |
| `tests/image_load_test.cpp` | Image loading: ImageData + HDRData output, baseline save to `tests/baseline/ref_pixels.raw` |

### Baseline / Pixel Diff Verification
Baseline file: `tests/baseline/ref_pixels.raw` (generated by `image_load_test` from `MVIMG_20251123_200209.jpg`)

```powershell
Compare-Object (Get-Content tests/baseline/ref_pixels.raw -Encoding Byte) (Get-Content test.raw -Encoding Byte)
```

## Common Pitfalls / 常见陷阱

1. **Don't call glfwTerminate in tests** - once per process only; use global test fixture or mock
2. **ImageData struct layout diverged by platform** - Desktop (`src/renderer.h`) uses `std::vector<uint8_t>`; Android (`android/.../hdr_types.h`) uses `uint8_t* pixels`. Changes on one platform DO NOT propagate to the other.
3. **New .cpp files must be added to APP_SOURCES in CMakeLists.txt**
4. **Vulkan backend uses `#ifdef UHDR_VIEWER_VULKAN_ENABLED`** - preserve these guards
5. **`vkDeviceWaitIdle()` in updateTexture** - Vulkan destroys/recreates GPU resources; OpenGL uses `glTexSubImage2D`
6. **DPI**: `SetProcessDPIAware()` is called, NOT per-monitor V2 - blurry on mixed-DPI multi-monitor
7. **Tone mapping algorithms are duplicated** - `src/tonemap.cpp` (desktop) and `android/.../image_loader_android.cpp` (Android) contain identical Reinhard/ACES/Uncharted2 implementations. Any algorithm change MUST be applied to both files.

## Manual Verification Checklist / 手动验证清单 (per wave)

After each wave, run:
```





[ ] Build: cmake --preset vs2022-opengl && cmake --build ... --config Debug (0 errors)
[ ] Launch: out\build\vs2022-opengl\bin\Debug\UltraHDRViewer.exe
[ ] Open: File > Open > MVIMG_20251123_200209.jpg
[ ] Display: image visible, not black, not corrupted
[ ] Zoom: mouse wheel zooms in/out centered on cursor
[ ] Pan: left-click drag moves the image
[ ] Info: press I → EXIF data shows camera model, exposure
[ ] Tone: mode dropdown → image changes appearance
[ ] Exposure: slider → brightness changes
[ ] Gamma: slider → contrast changes
[ ] Reset: press R → view resets to fit
[ ] Exit: Esc → clean exit (no crash)
[ ] Tests: ctest --test-dir out/build/vs2022-opengl --output-on-failure (all pass)
```
