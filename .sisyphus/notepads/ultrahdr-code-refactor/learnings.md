# UltraHDR Code Refactor - Learnings

## 2026-06-09 Session Start
- Project: UltraHDRViewer_Imgui (ImGui + GLFW + libultrahdr)
- All deps cloned (glfw, imgui, libultrahdr, stb_image)

## Task B.4: Extract tone mapping (2026-06-09)
- Created src/tonemap.h + src/tonemap.cpp, moved 5 functions verbatim
- Forward declarations for HDRData/ImageData avoid circular includes
- #include "tonemap.h" in image_loader.h for backward compat
- Added tonemap.cpp to both CMakeLists (main + tests)
- Build: 0 errors, ToneMapTest: 7/7 passed
- User confirmed: refactor A→B→C→D, TDD with Google Test, keep Vulkan
- VS2022 primary build tool on Windows

## 2026-06-09: Task 0.1 - Google Test Infrastructure
- Added Google Test v1.14.0 via CMake FetchContent (NOT clone_deps.bat)
- Created tests/ directory with tests/CMakeLists.txt and tests/main.cpp
- Test executable: UltraHDRViewerTests (links against uhdr + GTest::gtest_main, NOT glfw/imgui)
- Build cmd: cmake --build out/build/vs2022-opengl --config Debug --target UltraHDRViewerTests
- Output: out/build/vs2022-opengl/bin/Debug/UltraHDRViewerTests.exe
- Note: Running on CMake 4.3 required fixing cmake_minimum_required compat in libultrahdr's turbojpeg ExternalProject. Added CMAKE_POLICY_VERSION_MINIMUM=3.5 to UHDR_CMAKE_ARGS in deps/libultrahdr/CMakeLists.txt.

## Task 0.3 - Tone Map Characterization Tests
- Created tests/tonemap_test.cpp with 7 tests (well over 6 assertions)
- toneMapImage always sets alpha=255 regardless of exposure; ExposureZeroAllBlack test must verify RGB=0, alpha=255
- CMakeLists.txt already had file(GLOB TEST_SOURCES *.cpp) from task 0.2
- Replicated toneMapImage + helpers inline (image_loader.cpp has GLFW/ultrahdr deps that would break test linkage)
- Reinhard expected values: 0.25→51, 0.5→85, 0.75→109, alpha→255 (with gamma=1.0, exposure=1.0)
- All 7 tests pass: OutputInRange (×3 modes), ReinhardExpectedValues, ExposureZeroAllBlack, DifferentModesDiffer, GammaChangesOutput


## Task 0.4 - Image Load Characterization Test
- Created tests/image_load_test.cpp with LoadUltraHDR test
- loadImageFile returns true for MVIMG_20251123_200209.jpg; ImageData: 4096x3072, HDRData populated with matching dimensions
- Baseline raw pixels saved to tests/baseline/ref_pixels.raw (50,331,648 bytes = 4096*3072*4)
- Replaced tonemap_test.cpp duplicated toneMapImage code: now uses image_loader.cpp version (no more duplicate symbol)
- Fixed LNK2005 (multiply defined toneMapImage): removed inline replication from tonemap_test.cpp
- Fixed LNK2019 (glfwGetWin32Window): added linker stub in tests/main.cpp instead of linking GLFW
- Updated tests/CMakeLists.txt: added ../src/image_loader.cpp, ../src/stb_image_impl.cpp, and GLFW include dir
- Note: CMake Cache required deletion after CMakeLists.txt changes to regenerate vcxproj correctly
- All 9 tests pass: ExifParserTest (1), ImageLoadTest (1), ToneMapTest (7)
- Fixed ToneMapTest.ExposureZeroAllBlack (was failing with duplicated toneMapImage; now passes with image_loader.cpp version)

## Task A.2 - Remove parseGPSStrings
- Removed ~26-line dead parseGPSStrings() function from src/exif_parser.cpp
- Confirmed 0 references via grep before/after
- All 9 tests PASS (including ExifParserTest.ParseUltraHDRImage)
- Build: 0 errors (pre-existing warnings only - C4244 in app.cpp, C4477 in image_loader.cpp, LNK4098)

## Task A.1 - Fix GLFW context version hints order
- **Root cause**: glfwWindowHint() for GL context version was called in OpenGLRenderer::init() (renderer_gl.cpp:49-59) AFTER glfwCreateWindow() in app.cpp:33. GLFW hints are consumed at window creation time and are silently ignored afterward.
- **Fix**: Moved hints to pp.cpp lines 34-41, BEFORE glfwCreateWindow() on line 43.
- **renderer_gl.cpp changes**: Removed the GLFW hint block (was lines 49-59). Kept only m_glslVersion = "#version 150" for macOS (line 49-51) since GLSL version is consumed by ImGui_ImplOpenGL3_Init(), not by GLFW window creation.
- **m_glslVersion**: Default is "#version 130" (header line 22) for GL 3.0 on Windows; overridden to "#version 150" on macOS for GL 3.2 Core.
- **Construction order invariant preserved**: glfwInit() �� hints �� glfwCreateWindow() �� makeContext �� renderer.init
- **Verification**: OpenGL build 0 errors, Vulkan build (pre-existing errors in renderer_vk.cpp unrelated), all 9 tests pass.

## Task B.5 - Add Wave B sources to APP_SOURCES (2026-06-10)
- Root CMakeLists.txt already listed src/tonemap.cpp in APP_SOURCES; only src/ui_panels.cpp needed to be added.
- ui_panels.cpp also needs #include "ultrahdr_api.h" so UHDR_LIB_VERSION_STR is visible.
- Debug build completed successfully after the CMake/source update.
- Test binary UltraHDRViewerTests.exe reports 9/9 passed.

## Task C.1 - Replace uint8_t* pixels with std::vector<uint8_t> (2026-06-10)
- **Files changed**: renderer.h, image_loader.cpp, tonemap.cpp, renderer_gl.cpp, renderer_vk.cpp, app.cpp, tests/image_load_test.cpp
- **Pattern**: `uint8_t* pixels = nullptr` → `std::vector<uint8_t> pixels`, `~ImageData()` removed, `dataSize()` now returns `pixels.size()`
- **Allocations**: `new uint8_t[...]` → `pixels.resize(...)`, `delete[]` removed (3 sites: loadImageFile, loadRegularImage, applyToneMap)
- **Access**: `image.pixels` (as pointer) → `image.pixels.data()` for APIs expecting `const void*` (glTexImage2D, memcpy, f.write, toneMapImage)
- **Null checks**: `!image.pixels` / `img.pixels != nullptr` → `image.pixels.empty()` / `EXPECT_FALSE(img.pixels.empty())`
- **Android**: NOT touched — Android has independent ImageData in android/app/src/main/cpp/ (hdr_types.h, image_data.h, image_loader_android.cpp)
- **Verification**: `grep "new uint8_t\[" src/` → 0 matches, `grep "delete\[\]" src/` → 0 matches, build 0 errors, 9/9 tests pass

## Task D.2 - Fuse Ultra HDR pixel copy loop (2026-06-10)
- Merged the SDR and HDR row-copy passes in loadImageFile into one nested loop.
- Kept RGBA order and stride handling unchanged; HDR alpha remains 1.0f.
- Build and tests passed: cmake --preset vs2022-opengl; cmake --build out/build/vs2022-opengl --config Debug; UltraHDRViewerTests 9/9.

## Task C.2 + D.1 - Logging macros + tone mapping branch lift (2026-06-10)
- Added src/log.h with LOG_ERROR/LOG_INFO wrappers and replaced all fprintf(stderr/stdout) in src/ (kept printf in main.cpp help text).
- Reworked toneMapImage so mode selection happens outside the pixel loop; 9/9 tests still pass.
- Build/test verification stayed green after the refactor; output behavior remained unchanged.

2026-06-10: Added Config namespace for window/panel/slider defaults; reused it in both Application UI code and shared UI panel helpers.
2026-06-10: OpenGL texture creation should check GL_MAX_TEXTURE_SIZE before upload; Vulkan should cache physical device limits for the same validation.
2026-06-10: renderer_gl.cpp now uses log.h + LOG_ERROR for texture-limit failures; app.cpp dead panel/dialog methods were removed because UIPanels namespace owns the active UI rendering path.
