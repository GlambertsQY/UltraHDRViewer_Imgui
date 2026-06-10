# UltraHDR Viewer - Project Skill

> C++17 Ultra HDR image viewer: Dear ImGui + GLFW + libultrahdr
> Target: Windows Desktop, VS2022 + CMake

---

## TL;DR Build

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

## Project Structure

```
src/
├── main.cpp              # Entry point, CLI args, DPI awareness
├── app.h / app.cpp       # Application: init sequence, run loop, UI rendering
├── renderer.h            # Renderer interface + ImageData struct
├── renderer_gl.h / .cpp  # OpenGL backend (#ifdef UHDR_VIEWER_OPENGL_ENABLED)
├── renderer_vk.h / .cpp  # Vulkan backend (#ifdef UHDR_VIEWER_VULKAN_ENABLED)
├── image_loader.h / .cpp # Ultra HDR decoding, tone mapping, file dialog
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

## CRITICAL: Construction Order Invariant

When modifying `Application::init`, `Renderer::init`, **NEVER change this sequence**:

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

**Why this matters**: GLFW window hints are consumed at `glfwCreateWindow` time.
Setting them afterwards is a silent no-op. The current code sets GL version hints
inside `OpenGLRenderer::init` (at line 49-58 of renderer_gl.cpp), which is called
AFTER `glfwCreateWindow` in `app.cpp:33` — **this is a bug**. The window uses the
driver's default GL version, not the intended 3.0.

## Build Matrix

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

## Code Conventions

### Platform Guards
```cpp
#ifdef UHDR_VIEWER_OPENGL_ENABLED   // OpenGL-specific code
#ifdef UHDR_VIEWER_VULKAN_ENABLED   // Vulkan-specific code
#ifdef _WIN32                        // Win32 API (file dialogs, DPI)
```

### GLFW Lifecycle
- `glfwInit()` and `glfwTerminate()` can each be called only **once per process**
- Test harnesses MUST NOT call them in individual test cases
- Use a global test fixture or mock GLFW entirely

### Texture Handles
- OpenGL: `(void*)(intptr_t)glTextureID` — cast via intptr_t, NOT direct
- Vulkan: pointer to heap-allocated `VulkanTexture` struct
- **MUST NOT** change `void* m_texture` to a typed pointer without tagged union

### Error Reporting
- Currently: `fprintf(stderr, ...)` scattered throughout
- Target: `LOG_ERROR(fmt, ...)` macro — no external dependencies (no spdlog/fmt)

### Memory Management
- `ImageData::pixels` is raw `new[]`/`delete[]` — **target**: `std::vector<uint8_t>`
- Vulkan handles (`VkDevice`, `VkInstance`, etc.) stay raw — they have their own lifecycle
- GLFWwindow* stays raw — GLFW owns it

## Testing

### Framework: Google Test
- Acquired via CMake `FetchContent` or cloned to `deps/googletest/`
- Tests live in `tests/` directory
- Separate CMake target: `UltraHDRViewerTests`

### Test Categories
| Category | What | GLFW Needed |
|----------|------|-------------|
| Unit (logic) | EXIF parser, tone mapping math, constants | No |
| Unit (image) | Image loading/decoding, pixel correctness | No |
| Integration | Renderer texture upload, UI layout | Yes (init once) |

### Characterization Tests (Baseline)
Before any refactoring, establish:
1. Load `MVIMG_20251123_200209.jpg` → save raw pixels as `ref.raw`
2. Apply tone mapping with defaults → save as `ref_tonemapped.raw`
3. Parse EXIF → serialize known fields to JSON for comparison

### Pixel Diff Verification
```powershell
# After refactoring, verify identical output:
Compare-Object (Get-Content ref.raw -Encoding Byte) (Get-Content test.raw -Encoding Byte)
# Expected: no output (no differences)
```

## Common Pitfalls

1. **Don't set GLFW hints in renderer init** — they take effect at window creation time
2. **Don't call glfwTerminate in tests** — once per process only
3. **Don't change ImageData struct layout** — Android has independent copies that must stay compatible
4. **New .cpp files must be added to APP_SOURCES in CMakeLists.txt**
5. **Vulkan backend uses `#ifdef UHDR_VIEWER_VULKAN_ENABLED` — preserve these guards**
6. **`vkDeviceWaitIdle()` in updateTexture** — Vulkan destroys/recreates GPU resources; OpenGL uses `glTexSubImage2D`
7. **DPI**: `SetProcessDPIAware()` is called, NOT per-monitor V2 — blurry on mixed-DPI multi-monitor

## Manual Verification Checklist (per wave)

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
```
