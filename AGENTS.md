# AGENTS.md

Guidance for AI coding agents working in this repository. Keep changes minimal and verify with the commands below.

## Project

C++17 true-HDR image viewer built on Dear ImGui 1.91.8, GLFW and libultrahdr.

- **Windows desktop**: OpenGL 3.0 backend (SDR only) and Vulkan backend (scRGB / HDR10 output)
- **Android 17 (API 37)**: native EGL + OpenGL ES 3 backend with HDR10 (BT.2020/PQ) output
- Dependencies are **not** vendored; `deps/` is produced by the clone scripts

## Quick start

```bat
clone_deps.bat
cmake --preset vs2026-opengl
cmake --build out/build/vs2026-opengl --config Debug
out\build\vs2026-opengl\bin\Debug\UltraHDRViewer.exe
ctest --test-dir out/build/vs2026-opengl -C Debug --output-on-failure
```

```bat
:: Vulkan / HDR output
cmake --preset vs2026-vulkan
cmake --build out/build/vs2026-vulkan --config Release
out\build\vs2026-vulkan\bin\Release\UltraHDRViewer.exe --vulkan
```

Presets: `vs2026-opengl`, `vs2026-vulkan`, `vs2026-both`, `ninja-debug`, `ninja-release`.
Linux/macOS: `./clone_deps.sh` + the Ninja presets.

Android (toolchain installs into the project-local `.tools/`, no system changes):

```powershell
powershell -ExecutionPolicy Bypass -File android\setup_android_sdk.ps1
cd android
$env:JAVA_HOME = "$PWD\..\.tools\jdk-17"
.\gradlew.bat assembleDebug
```

## Layout

```
src/            Desktop sources (app, renderers, decode, tone mapping, EXIF, shaders)
android/        Gradle project + native EGL/GLES backend (cpp/native.cpp, egl_context, hdr_gles)
tests/          GoogleTest suites (desktop + Android host tests)
deps/           Cloned by clone_deps.bat/.sh (gitignored)
docs/images/    README assets
.github/workflows/ci.yml
```

## Invariants (do not break)

- **True HDR decode**: always `UHDR_IMG_FMT_64bppRGBAHalfFloat` + `UHDR_CT_LINEAR`. `UHDR_CT_SRGB` + `RGBA8888` returns only the SDR rendition and silently fakes HDR.
- **HDR data convention**: linear BT.709 float RGBA, `1.0 = 203 nits` (`ColorSpace::HDR_WHITE_NITS`).
- **OpenGL is SDR-only by design** (WGL has no HDR swapchain). HDR desktop output is Vulkan only.
- **Android HDR10**: 10-bit EGL surface with `EGL_GL_COLORSPACE_BT2020_PQ_EXT`. ImGui must render into an SDR RGBA8 FBO first; a composite shader converts sRGB → BT.2020 → PQ (~400-nit UI white). Never render ImGui directly into PQ.
- **Android ImGui backend is custom**: set `io.DisplaySize` / `io.DeltaTime` every frame; the checkerboard must stay under ImGui's 16-bit index limit (65535 vertices).
- **Tone mapping is duplicated**: `src/tonemap.cpp` and `android/app/src/main/cpp/image_loader_android.cpp`. Apply algorithm changes to both.
- **Platform data types differ**: desktop `ImageData` uses `std::vector<uint8_t>`, Android uses raw `uint8_t*` (`hdr_types.h`). They are independent copies.
- **Vulkan textures**: `createTexture` returns a `VkDescriptorSet` cast to `ImTextureID` and resolves it through an internal map. Do not return raw `VulkanTexture*`.
- **Shutdown must stay idempotent** — `~VulkanRenderer()` and `Application::shutdown()` both call `shutdown()`.
- **HDR mode needs `ImGuiWindowFlags_NoBackground`** on the image window, otherwise the opaque window covers the HDR quad.

## Conventions

- C++17, 4-space indent, `#ifdef UHDR_VIEWER_OPENGL_ENABLED` / `UHDR_VIEWER_VULKAN_ENABLED` guards.
- Logging via `LOG_ERROR` / `LOG_INFO` (`src/log.h`); no new logging dependencies.
- New `.cpp` files must be added to `APP_SOURCES` (or the relevant target) in CMake.
- Shaders: edit `src/shaders/hdr_quad.vert/.frag`, recompile with `glslc` to the committed `*_spv.h` headers; never hand-edit SPIR-V headers.
- Keep dependency patches in `clone_deps.*` **idempotent** — they run on every CI build and on re-runs locally.
- Do not add SDL2 back: Android uses raw EGL/GLES now.

## Tests

- GoogleTest v1.14 via `FetchContent`; targets `UltraHDRViewerTests` (desktop) and `AndroidLoaderTests` (compiles the Android decode/EXIF/tone-map units on the host).
- `MVIMG_20251123_200209.jpg` is private and absent from the repo: the tests that need it `GTEST_SKIP`, so a clean clone reports 29 tests with 3 skipped.
- `HdrRoundTripTest` encodes `synthetic_uhdr_test.jpg` on the fly; run it once if you need the sample:
  `out\build\vs2026-opengl\bin\Debug\UltraHDRViewerTests.exe --gtest_filter=HdrRoundTripTest.*`

## CI

`.github/workflows/ci.yml` runs on push to `master` and on pull requests:

- **windows**: `windows-2025`, caches `deps`, installs Vulkan SDK via choco (cached), Ninja Release build, `ctest`
- **android**: preinstalled SDK/JDK 17, installs NDK `28.2.13676358` + CMake `3.22.1`, `assembleDebug`, verifies the APK exists

Runner facts worth remembering:

- No Visual Studio 2026 on the runner — CI uses Ninja, not the VS presets.
- Runner CMake is 4.x, so the turbojpeg sub-build needs `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (forwarded through `UHDR_CMAKE_ARGS`).
- libultrahdr clones `third_party/turbojpeg` at **build time** via `ExternalProject`, which re-clones and discards pre-build edits. Patch it with a `PATCH_COMMAND`, not by editing `third_party/` beforehand.
- In batch/PowerShell patches, `.NET Regex.Replace` does **not** interpret `\n` in the replacement string — use `[char]10` / `[char]13`. A literal `\n` once collapsed an entire CMakeLists into a comment and silently removed the `uhdr` target.
- `clone_deps.bat` skips `pause` when `CI` is defined; keep it that way.

## Release

Tags do not trigger CI. Cut releases manually:

```powershell
# bump CMakeLists project VERSION and android/app/build.gradle versionName
git tag -a vX.Y.Z -m "UltraHDR Viewer vX.Y.Z"
git push origin vX.Y.Z
gh release create vX.Y.Z --title "UltraHDR Viewer vX.Y.Z" --generate-notes `
  "<path>\UltraHDRViewer-vX.Y.Z-win64.exe" `
  "<path>\UltraHDRViewer-vX.Y.Z-android-debug.apk"
```

The APK currently attached is **debug-signed**; a store-ready build needs a keystore and `assembleRelease`.

## Do not commit

`deps/`, `out/`, `.tools/`, `captures/`, `imgui.ini`, `*.jpg` (except `docs/images/`), build logs. Large binaries (`*.exe`, `*.apk`, `*.zip`) are tracked with Git LFS.
