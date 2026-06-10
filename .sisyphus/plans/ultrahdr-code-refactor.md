# UltraHDR Viewer - 代码重构与优化

## TL;DR

> **Quick Summary**: 对现有 UltraHDR 图片浏览器进行全面重构：Bug修复 → 架构拆分 → 代码质量 → 性能优化，TDD 驱动，Google Test 保障行为不变。
> 
> **Deliverables**:
> - Google Test 测试基础设施 + 像素对比基线
> - 修复5个已知 Bug（GLFW hints、死代码、saveImageFile 等）
> - 架构拆分 app.cpp (456行 → 4文件) + image_loader 职责分离
> - 代码质量提升（vector 替代 raw pointer、LOG_ERROR 宏、常量提取）
> - 性能优化（色调映射分支外提、像素拷贝合并、纹理复用）
> - Skill 文件部署到 .opencode/skills/ultrahdr-viewer.md
> 
> **Estimated Effort**: Large (5 waves, ~30 tasks)
> **Parallel Execution**: YES - 4 waves + final review
> **Critical Path**: Wave 0 → Wave A → Wave B → Wave C → Wave D → Wave F

---

## Context

### Original Request
为 UltraHDRViewer_Imgui 项目生成 OpenCode skill + 代码重构工作计划，Windows + VS2022。

### Interview Summary
**Key Discussions**:
- **优先级**: A(Bug修复) → B(架构拆分) → C(代码质量) → D(性能优化)，全部做
- **测试策略**: TDD，引入 Google Test，重构前先建像素对比基线
- **Vulkan**: 保留并同步优化，不删除
- **平台**: 仅 Windows Desktop，Android 仅保证头文件兼容

**Research Findings**:
- app.cpp 456行，UI渲染+业务逻辑全耦合
- renderer_gl.cpp GLFW hints bug 比表面严重：hints 在窗口创建后设置→静默无效
- saveImageFile 写 raw RGBA 字节，无实际保存功能
- exif_parser 有 42 行未调用的死代码(parseGPSStrings)
- file_dialog.cpp 与 image_loader.cpp 文件对话框代码重复
- ImageData 使用原始 new[]/delete[]，无 RAII
- 色调映射 switch(mode) 在逐像素循环内部

### Metis Review
**Identified Gaps** (addressed):
- 缺少 Wave 0（角色化测试基线）→ 已新增
- GLFW hints bug 应在 app.cpp 修复而非 renderer_gl.cpp → 已确认
- saveImageFile 功能残缺 → 新增为 Wave A 任务
- Vulkan updateTexture 性能问题 → 加入 Wave D
- 构造顺序不变式需在 skill 中重点标注 → 已加入
- 共享头文件兼容性约束 → 明确 Android 独立头文件无需担心

---

## Work Objectives

### Core Objective
在保证行为完全不变的前提下，按 A→B→C→D 顺序重构 UltraHDR Viewer 代码库，提升可维护性和性能。

### Concrete Deliverables
- `tests/` 目录 + Google Test CMake 集成 + 像素基线
- 修复的 `src/app.cpp`（GLFW hints 前置）
- 清理的 `src/exif_parser.cpp`（移除死代码）
- 拆分后的 `src/ui_panels.cpp`、`src/image_controller.cpp`
- 重构的 `src/image_loader.cpp`（职责分离）
- 优化的 `src/image_loader.cpp`（色调映射性能）
- `.opencode/skills/ultrahdr-viewer.md`（从 draft 部署）

### Definition of Done
- [ ] 所有3个 VS2022 preset 编译通过 (0 errors, 0 new warnings)
- [ ] 像素对比：重构前后加载同一图片输出完全一致
- [ ] 所有 Google Test 用例通过
- [ ] 手动验证清单 12 项全部通过
- [ ] Skill 文件已部署到 .opencode/skills/

### Must Have
- 行为不变：重构前后 pixel-identical 输出
- 所有 CMake preset 编译通过
- TDD：每个重构任务前先写测试
- GLFW 构造顺序正确
- LOG_ERROR 宏替换所有 fprintf(stderr)

### Must NOT Have (Guardrails)
- 不改变 Vulkan 句柄类型 (VkDevice/VkInstance 等)
- 不引入 spdlog/fmt/Boost 等外部依赖（除 Google Test）
- 不修改 Android 端代码或破坏其编译
- 不新增功能（异步加载、HDR10 显示等 → out of scope）
- 不改变 ImageData/ExifData/HDRData 公开接口布局
- 不超过 4 个新文件拆分 app.cpp

---

## Verification Strategy (MANDATORY)

> **ZERO HUMAN INTERVENTION** - ALL verification is agent-executed. No exceptions.

### Test Decision
- **Infrastructure exists**: NO (需从零搭建)
- **Automated tests**: TDD (先写测试→再重构)
- **Framework**: Google Test (via CMake FetchContent)
- **TDD Flow**: RED (failing baseline test) → GREEN (minimal impl) → REFACTOR

### QA Policy
Every task MUST include agent-executed QA scenarios.
Evidence saved to `.sisyphus/evidence/task-{N}-{scenario-slug}.{ext}`.

- **Build verification**: Use Bash (cmake --build) — check exit code, scan for warnings
- **Pixel diff**: Use Bash (Compare-Object) — byte-level comparison
- **Manual GUI checklist**: Use specified checklist — each checkpoint has exact steps
- **Unit tests**: Use Bash (ctest / gtest binary) — check pass/fail counts

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 0 (Start Immediately - test infrastructure, MAX PARALLEL):
├── Task 0.1: Google Test CMake integration [quick]
├── Task 0.2: EXIF parser characterization test [quick]
├── Task 0.3: Tone mapping characterization test [quick]
├── Task 0.4: Image loading characterization test [quick]
├── Task 0.5: Pixel baseline capture [quick]
└── Task 0.6: Deploy skill file to .opencode/skills/ [quick]

Wave A (After Wave 0 - bug fixes, MAX PARALLEL):
├── Task A.1: Fix GLFW context hints order [quick]
├── Task A.2: Remove exif_parser dead code (parseGPSStrings) [quick]
├── Task A.3: Fix saveImageFile to use file dialog + valid format [quick]
├── Task A.4: Deduplicate file dialog implementations [quick]
└── Task A.5: Fix loadImage same-file early return [quick]

Wave B (After Wave A - architecture refactoring, MAX PARALLEL):
├── Task B.1: Extract UI panels from app.cpp → ui_panels.cpp [deep]
├── Task B.2: Extract image controller from app.cpp → image_controller.cpp [deep]
├── Task B.3: Separate file dialog from image_loader.cpp → file_dialog.cpp [quick]
├── Task B.4: Extract tone mapping to tonemap.cpp [quick]
└── Task B.5: Update CMakeLists.txt for new files [quick]

Wave C (After Wave B - code quality, MAX PARALLEL):
├── Task C.1: Replace ImageData::pixels raw ptr with std::vector<uint8_t> [deep]
├── Task C.2: Introduce LOG_ERROR macro, replace fprintf(stderr) [quick]
├── Task C.3: Extract hardcoded constants to config.h [quick]
├── Task C.4: Add texture size validation (GL_MAX_TEXTURE_SIZE check) [quick]
└── Task C.5: Fix zero-size window viewport guard in OpenGL renderer [quick]

Wave D (After Wave C - performance, MAX PARALLEL):
├── Task D.1: Lift tone mapping switch(mode) out of pixel loop [quick]
├── Task D.2: Merge double pixel-copy loop in loadImageFile [quick]
├── Task D.3: Add same-file reload early exit (texture reuse) [quick]
├── Task D.4: Optimize Vulkan updateTexture (buffer staging) [deep]
└── Task D.5: Update CMakeLists.txt for any new files [quick]

Wave FINAL (After ALL tasks — 4 parallel reviews, then user okay):
├── Task F1: Plan Compliance Audit (oracle)
├── Task F2: Code Quality Review (unspecified-high)
├── Task F3: Real Manual QA (unspecified-high + playwright)
└── Task F4: Scope Fidelity Check (deep)
-> Present results -> Get explicit user okay

Critical Path: 0.1 → 0.2/0.3/0.4 → 0.5 → A.1 → A.2-A.5 → B.1/B.2 → C.1 → D.1/D.2 → F1-F4 → user okay
Parallel Speedup: ~75% faster than sequential
Max Concurrent: 6 (Waves 0, B, C, D)
```

### Dependency Matrix

- **0.1-0.5**: - - A.1-A.5, B.1-B.5, 0
- **0.6**: - - (none), 0
- **A.1**: 0.5 - B.1, C.4, 1
- **A.2**: 0.5 - B.4, C.2, 1
- **A.3**: 0.5 - B.3, 1
- **A.4**: 0.5 - B.3, 1
- **A.5**: 0.5 - D.3, 1
- **B.1**: A.1 - C.1, C.3, 2
- **B.2**: A.1 - C.1, C.3, 2
- **B.3**: A.3, A.4 - C.2, 2
- **B.4**: A.2 - D.1, 2
- **B.5**: B.1-B.4 - C.1-C.5, D.1-D.5, 2
- **C.1**: B.1, B.2 - D.1, D.2, 3
- **C.2**: B.3, A.2 - D.5, 3
- **C.3**: B.1, B.2 - D.5, 3
- **C.4**: A.1 - D.5, 3
- **C.5**: A.1 - D.5, 3
- **D.1**: C.1, B.4 - F1-F4, 4
- **D.2**: C.1 - F1-F4, 4
- **D.3**: A.5 - F1-F4, 4
- **D.4**: C.1 - F1-F4, 4
- **D.5**: C.2, C.3, C.4, C.5 - F1-F4, 4

---

## TODOs

### Wave 0: Characterization + Test Infrastructure

- [x] 0.1 Google Test CMake Integration

  **What to do**:
  - Add Google Test via CMake `FetchContent` in CMakeLists.txt
  - Create `tests/` directory with `CMakeLists.txt`
  - Create `tests/main.cpp` with gtest main
  - Add `UltraHDRViewerTests` executable target
  - Link against `uhdr` library for image loading tests
  - Verify: `cmake --build ... && ctest` runs 0 tests (infrastructure only)

  **Must NOT do**:
  - Do NOT use `clone_deps.bat` for gtest (use FetchContent only)
  - Do NOT link tests against GLFW/imgui (unit tests don't need GUI)

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 0 (with 0.2, 0.3, 0.4, 0.5, 0.6)
  - **Blocks**: 0.2, 0.3, 0.4
  - **Blocked By**: None

  **References**:
  - `CMakeLists.txt:1-6` - Existing cmake_minimum_required + project setup
  - `CMakeLists.txt:101-131` - Existing APP_SOURCES + target_link_libraries pattern
  - Official: https://google.github.io/googletest/quickstart-cmake.html - FetchContent usage

  **Acceptance Criteria**:
  - [ ] `tests/CMakeLists.txt` exists with gtest target
  - [ ] `cmake --preset vs2022-opengl` configures without errors
  - [ ] `cmake --build ... --target UltraHDRViewerTests` builds successfully
  - [ ] `ctest --test-dir out/build/vs2022-opengl` runs 0 tests (pass)

  **QA Scenarios**:
  ```
  Scenario: CMake configure + build with gtest
    Tool: Bash (cmake)
    Preconditions: deps/ populated (clone_deps.bat already run)
    Steps:
      1. cmake --preset vs2022-opengl
      2. cmake --build out/build/vs2022-opengl --config Debug --target UltraHDRViewerTests
      3. out\build\vs2022-opengl\bin\Debug\UltraHDRViewerTests.exe --gtest_list_tests
    Expected Result: Build succeeds with 0 errors. --gtest_list_tests runs without crash.
    Failure Indicators: cmake error, link error, executable not found
    Evidence: .sisyphus/evidence/task-0.1-build.txt

  Scenario: Verify no GLFW dependency in tests
    Tool: grep
    Preconditions: tests/CMakeLists.txt created
    Steps:
      1. grep -r "glfw" tests/CMakeLists.txt
    Expected Result: No glfw references in test CMakeLists.txt
    Evidence: .sisyphus/evidence/task-0.1-no-glfw.txt
  ```

  **Commit**: YES
  - Message: `test: add Google Test infrastructure via FetchContent`
  - Files: `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/main.cpp`

- [x] 0.2 EXIF Parser Characterization Test

  **What to do**:
  - Write `tests/exif_parser_test.cpp`
  - Load `MVIMG_20251123_200209.jpg` from repo root as binary
  - Call `parseExif()` on raw bytes
  - Assert: `valid == true`, `make` non-empty, `model` non-empty
  - Assert: `orientation` in range [1,8]
  - Assert: GPS values reasonable (lat/lon near expected range)
  - Serialize key fields to JSON for later comparison

  **Must NOT do**:
  - Do NOT modify exif_parser.cpp (this is baseline characterization)

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 0.3, 0.4)
  - **Parallel Group**: Wave 0
  - **Blocks**: A.2
  - **Blocked By**: 0.1

  **References**:
  - `src/exif_parser.h:6-55` - ExifData struct definition
  - `src/exif_parser.cpp:244-282` - parseExif() implementation
  - `MVIMG_20251123_200209.jpg` - Test image in repo root

  **Acceptance Criteria**:
  - [ ] `tests/exif_parser_test.cpp` created with 5+ assertions
  - [ ] `bun test` or `UltraHDRViewerTests --gtest_filter="*Exif*"` → ALL PASS
  - [ ] Test reads MVIMG_20251123_200209.jpg from correct relative path

  **QA Scenarios**:
  ```
  Scenario: Parse EXIF from known UltraHDR image
    Tool: Bash (gtest)
    Preconditions: 0.1 complete, test image exists
    Steps:
      1. cmake --build out/build/vs2022-opengl --config Debug --target UltraHDRViewerTests
      2. out\build\vs2022-opengl\bin\Debug\UltraHDRViewerTests.exe --gtest_filter="*Exif*"
    Expected Result: All EXIF tests pass (PASSED, 0 FAILED)
    Failure Indicators: Test crashes, assertion failure, file not found
    Evidence: .sisyphus/evidence/task-0.2-exif-test.txt
  ```

  **Commit**: YES
  - Message: `test: add EXIF parser characterization tests`
  - Files: `tests/exif_parser_test.cpp`

- [x] 0.3 Tone Mapping Characterization Test

  **What to do**:
  - Write `tests/tonemap_test.cpp`
  - Create synthetic HDR test data: 4x4 float RGBA image with known values
  - Call `toneMapImage()` with all 3 modes (Reinhard, ACES, Uncharted 2)
  - Assert: output bounds [0,255] for all pixels
  - Assert: exposure=1.0, gamma=1.0 gives identity-like output
  - Assert: exposure=0.0 → all pixels 0
  - Assert: different modes produce different outputs
  - Save reference output to `ref_tonemapped.raw` for later pixel-diff

  **Must NOT do**:
  - Do NOT modify tone mapping code (baseline only)

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 0.2, 0.4)
  - **Parallel Group**: Wave 0
  - **Blocks**: D.1
  - **Blocked By**: 0.1

  **References**:
  - `src/image_loader.h:35-40` - toneMapImage signature
  - `src/image_loader.cpp:54-93` - toneMapImage implementation + helper functions

  **Acceptance Criteria**:
  - [ ] `tests/tonemap_test.cpp` created with 6+ assertions
  - [ ] `UltraHDRViewerTests --gtest_filter="*ToneMap*"` → ALL PASS
  - [ ] `ref_tonemapped.raw` saved for future comparison

  **QA Scenarios**:
  ```
  Scenario: All tone mapping modes produce valid output
    Tool: Bash (gtest)
    Preconditions: 0.1 complete
    Steps:
      1. cmake --build out/build/vs2022-opengl --config Debug --target UltraHDRViewerTests
      2. out\build\vs2022-opengl\bin\Debug\UltraHDRViewerTests.exe --gtest_filter="*ToneMap*"
    Expected Result: All tone map tests pass
    Evidence: .sisyphus/evidence/task-0.3-tonemap-test.txt
  ```

  **Commit**: YES
  - Message: `test: add tone mapping characterization tests`
  - Files: `tests/tonemap_test.cpp`

- [x] 0.4 Image Loading Characterization Test

  **What to do**:
  - Write `tests/image_load_test.cpp`
  - Call `loadImageFile()` on `MVIMG_20251123_200209.jpg`
  - Assert: returns true, ImageData dimensions > 0, pixels not null
  - Assert: HDRData populated (if Ultra HDR detected)
  - Save raw RGBA pixels to `ref_pixels.raw` for byte-level comparison
  - Call `loadRegularImage()` if image is also valid SDR
  - Save to `ref_regular.raw`

  **Must NOT do**:
  - Do NOT modify image_loader.cpp

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 0.2, 0.3)
  - **Parallel Group**: Wave 0
  - **Blocks**: A.1, A.2
  - **Blocked By**: 0.1

  **References**:
  - `src/image_loader.h:23-29` - loadImageFile + loadRegularImage signatures
  - `src/image_loader.cpp:105-193` - loadImageFile implementation
  - `MVIMG_20251123_200209.jpg` - Test image

  **Acceptance Criteria**:
  - [ ] `tests/image_load_test.cpp` created
  - [ ] `UltraHDRViewerTests --gtest_filter="*ImageLoad*"` → ALL PASS
  - [ ] `ref_pixels.raw` saved with known size (width*height*4 bytes)

  **QA Scenarios**:
  ```
  Scenario: Load UltraHDR image and save baseline
    Tool: Bash (gtest)
    Preconditions: 0.1 complete, test image exists
    Steps:
      1. cmake --build out/build/vs2022-opengl --config Debug --target UltraHDRViewerTests
      2. out\build\vs2022-opengl\bin\Debug\UltraHDRViewerTests.exe --gtest_filter="*ImageLoad*"
    Expected Result: All image load tests pass, ref_pixels.raw created
    Evidence: .sisyphus/evidence/task-0.4-imageload-test.txt
  ```

  **Commit**: YES
  - Message: `test: add image loading characterization tests`
  - Files: `tests/image_load_test.cpp`

- [x] 0.5 Pixel Baseline Capture Script

  **What to do**:
  - Create `tests/capture_baseline.cpp` standalone tool
  - Load `MVIMG_20251123_200209.jpg`, save pixel output to `tests/baseline/ref_pixels.raw`
  - Apply tone map (Reinhard, exposure=1.0, gamma=2.2), save to `tests/baseline/ref_tonemapped.raw`
  - Apply tone map (ACES, exposure=1.0, gamma=2.2), save to `tests/baseline/ref_tonemapped_aces.raw`
  - Run all 0.2-0.4 characterization tests → confirm all pass
  - Document: "These baseline files represent the CORRECT pre-refactor behavior"

  **Must NOT do**:
  - Do NOT change any source code

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 0.6)
  - **Parallel Group**: Wave 0
  - **Blocks**: A.1, A.2, A.3, A.4, A.5 (all Wave A)
  - **Blocked By**: 0.2, 0.3, 0.4

  **References**:
  - `src/image_loader.h:35-43` - applyToneMap + saveImageToFile signatures
  - `tests/` - Tests from 0.2, 0.3, 0.4

  **Acceptance Criteria**:
  - [ ] `tests/baseline/ref_pixels.raw` exists
  - [ ] `tests/baseline/ref_tonemapped.raw` exists
  - [ ] `tests/baseline/ref_tonemapped_aces.raw` exists
  - [ ] All Wave 0 tests pass: `UltraHDRViewerTests.exe` → ALL PASS

  **QA Scenarios**:
  ```
  Scenario: Capture and verify pixel baseline
    Tool: Bash
    Preconditions: 0.1-0.4 complete
    Steps:
      1. out\build\vs2022-opengl\bin\Debug\UltraHDRViewerTests.exe
      2. Test-Path tests/baseline/ref_pixels.raw
      3. $size = (Get-Item tests/baseline/ref_pixels.raw).Length; Write-Host "Baseline size: $size bytes"
    Expected Result: All tests pass. Baseline files exist with non-zero size.
    Failure Indicators: Missing baseline files, test failure, zero-byte files
    Evidence: .sisyphus/evidence/task-0.5-baseline.txt
  ```

  **Commit**: YES
  - Message: `test: capture pixel baseline for regression detection`
  - Files: `tests/baseline/*.raw`, `tests/capture_baseline.cpp`

- [x] 0.6 Deploy Skill File

  **What to do**:
  - Copy `.sisyphus/drafts/skill-ultrahdr-viewer.md` → `.opencode/skills/ultrahdr-viewer.md`
  - Verify skill file has all required sections (build, structure, invariants, pitfalls, checklist)
  - Verify: `.opencode/skills/ultrahdr-viewer.md` exists and is non-empty

  **Must NOT do**:
  - Do NOT modify the skill content (copy verbatim from draft)

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 0.5)
  - **Parallel Group**: Wave 0
  - **Blocks**: None (standalone)
  - **Blocked By**: None

  **References**:
  - `.sisyphus/drafts/skill-ultrahdr-viewer.md` - Source skill content

  **Acceptance Criteria**:
  - [ ] `.opencode/skills/ultrahdr-viewer.md` exists
  - [ ] File size > 2KB (not empty/template)
  - [ ] Contains "Construction Order Invariant" section

  **QA Scenarios**:
  ```
  Scenario: Skill file deployed correctly
    Tool: Bash
    Preconditions: Draft exists
    Steps:
      1. Test-Path .opencode/skills/ultrahdr-viewer.md
      2. $content = Get-Content .opencode/skills/ultrahdr-viewer.md -Raw
      3. if ($content -match "Construction Order") { Write-Host "PASS: invariant section found" }
    Expected Result: File exists, contains construction order invariant
    Evidence: .sisyphus/evidence/task-0.6-skill-deploy.txt
  ```

  **Commit**: YES
  - Message: `docs: deploy project skill file`
  - Files: `.opencode/skills/ultrahdr-viewer.md`

---

### Wave A: Bug Fixes

- [x] A.1 Fix GLFW Context Hints Order

  **What to do**:
  - Move GLFW context version hints from `renderer_gl.cpp:49-58` to `app.cpp` BEFORE `glfwCreateWindow`
  - In `app.cpp`, before `glfwCreateWindow`:
    - If OpenGL backend: set `GLFW_CONTEXT_VERSION_MAJOR=3`, `GLFW_CONTEXT_VERSION_MINOR=0`
    - Set `GLFW_OPENGL_PROFILE` hint for macOS
  - Remove the hint-setting code from `OpenGLRenderer::init`
  - Keep `glfwMakeContextCurrent` and ImGui init in renderer
  - Verify: app runs with correct GL context version

  **Must NOT do**:
  - Do NOT change the construction order sequence
  - Do NOT modify Vulkan init path

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with A.2, A.3, A.4, A.5)
  - **Parallel Group**: Wave A
  - **Blocks**: B.1, B.2, C.4, C.5
  - **Blocked By**: 0.5

  **References**:
  - `src/app.cpp:20-46` - Application::init with backend selection and window creation
  - `src/renderer_gl.cpp:44-70` - OpenGLRenderer::init (hints to be removed from here)
  - `src/renderer_gl.h:20-23` - m_glslVersion member

  **Acceptance Criteria**:
  - [ ] GLFW hints appear BEFORE `glfwCreateWindow` in app.cpp
  - [ ] `renderer_gl.cpp` no longer sets GLFW window hints
  - [ ] `cmake --preset vs2022-opengl && cmake --build ...` → 0 errors
  - [ ] `cmake --preset vs2022-vulkan && cmake --build ...` → 0 errors (Vulkan path unchanged)

  **QA Scenarios**:
  ```
  Scenario: Build succeeds with hints moved
    Tool: Bash (cmake)
    Preconditions: 0.5 complete
    Steps:
      1. cmake --preset vs2022-opengl
      2. cmake --build out/build/vs2022-opengl --config Debug 2>&1 | Select-String "error"
    Expected Result: Build succeeds, 0 errors output
    Failure Indicators: compile error, link error
    Evidence: .sisyphus/evidence/task-A.1-build.txt

  Scenario: Vulkan build still works (untouched path)
    Tool: Bash (cmake)
    Steps:
      1. cmake --preset vs2022-vulkan
      2. cmake --build out/build/vs2022-vulkan --config Debug 2>&1 | Select-String "error"
    Expected Result: Vulkan build succeeds, 0 errors
    Evidence: .sisyphus/evidence/task-A.1-vulkan-build.txt
  ```

  **Commit**: YES
  - Message: `fix(renderer): move GLFW context hints before window creation`
  - Files: `src/app.cpp`, `src/renderer_gl.cpp`

- [x] A.2 Remove exif_parser Dead Code

  **What to do**:
  - Delete `parseGPSStrings()` function (lines 216-242 in exif_parser.cpp)
  - Verify: grep for `parseGPSStrings` returns 0 results in entire codebase
  - Run EXIF characterization tests → must still pass
  - Run pixel baseline comparison → must be identical

  **Must NOT do**:
  - Do NOT modify GPS parsing logic inside `parseIFD` (the working code)
  - Do NOT change ExifData struct

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with A.1, A.3, A.4, A.5)
  - **Parallel Group**: Wave A
  - **Blocks**: B.4
  - **Blocked By**: 0.5

  **References**:
  - `src/exif_parser.cpp:216-242` - parseGPSStrings (to delete)
  - `src/exif_parser.cpp:109-213` - parseIFD (keep, contains actual GPS parsing)
  - `tests/exif_parser_test.cpp` - EXIF tests from 0.2

  **Acceptance Criteria**:
  - [ ] `grep -r "parseGPSStrings" src/` → 0 results
  - [ ] EXIF characterization tests pass (0.2)
  - [ ] `cmake --preset vs2022-opengl && cmake --build ...` → 0 errors
  - [ ] Pixel output identical to baseline

  **QA Scenarios**:
  ```
  Scenario: Dead code removed, EXIF still works
    Tool: Bash (grep + gtest)
    Preconditions: 0.1-0.5 complete
    Steps:
      1. grep -r "parseGPSStrings" src/
      2. out\build\vs2022-opengl\bin\Debug\UltraHDRViewerTests.exe --gtest_filter="*Exif*"
    Expected Result: grep returns no matches. EXIF tests all pass.
    Evidence: .sisyphus/evidence/task-A.2-deadcode.txt
  ```

  **Commit**: YES
  - Message: `fix(exif): remove unused parseGPSStrings dead code`
  - Files: `src/exif_parser.cpp`

- [x] A.3 Fix saveImageFile

  **What to do**:
  - Modify `saveImageFile` in `app.cpp:372-376` to:
    1. Call `openSaveDialog()` from `file_dialog.h` to get output path
    2. If user cancels → return early (no operation)
    3. Write a valid BMP file header + pixel data (simplest lossless format)
  - BMP format: 54-byte header + BGRA pixel data
  - Or alternatively: use stb_image_write.h for PNG output (add to deps/)

  **Must NOT do**:
  - Do NOT add heavy dependencies
  - Do NOT change the save function signature

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with A.1, A.2, A.4, A.5)
  - **Parallel Group**: Wave A
  - **Blocks**: B.3
  - **Blocked By**: 0.5

  **References**:
  - `src/app.cpp:372-376` - Current saveImageFile (hardcoded path)
  - `src/file_dialog.h` - openSaveDialog declaration
  - `src/file_dialog.cpp` - Save dialog implementation

  **Acceptance Criteria**:
  - [ ] saveImageFile shows file dialog before saving
  - [ ] Output file is a valid viewable format (BMP or PNG)
  - [ ] `cmake --preset vs2022-opengl && cmake --build ...` → 0 errors

  **QA Scenarios**:
  ```
  Scenario: Save dialog opens, output is valid BMP
    Tool: Bash (cmake + hex check)
    Preconditions: 0.5 complete, app builds
    Steps:
      1. cmake --build out/build/vs2022-opengl --config Debug
      2. Launch app, load image, press Ctrl+S
      3. Verify: save dialog appears (manual via checklist)
      4. Check output file starts with "BM" (BMP magic)
    Expected Result: Output file is valid BMP
    Evidence: .sisyphus/evidence/task-A.3-save.txt
  ```

  **Commit**: YES
  - Message: `fix(save): use file dialog and write valid BMP output`
  - Files: `src/app.cpp`, `src/file_dialog.cpp`

- [x] A.4 Deduplicate File Dialog Implementations

  **What to do**:
  - Remove `openFileDialog()` from `image_loader.cpp:29-49`
  - Keep `openImageDialog()` in `file_dialog.cpp` as the single implementation
  - Update `app.cpp:362-369` to call `openImageDialog()` from `file_dialog.h`
  - Verify: File > Open still works correctly

  **Must NOT do**:
  - Do NOT remove `openFileDialog` declaration from `image_loader.h` without updating all callers

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with A.1, A.2, A.3, A.5)
  - **Parallel Group**: Wave A
  - **Blocks**: B.3
  - **Blocked By**: 0.5

  **References**:
  - `src/image_loader.cpp:29-49` - openFileDialog (to remove)
  - `src/file_dialog.cpp` - openImageDialog (keep)
  - `src/app.cpp:362-369` - openImageFile (caller to update)

  **Acceptance Criteria**:
  - [ ] `grep -r "openFileDialog" src/` → only in file_dialog.cpp + header
  - [ ] `cmake --preset vs2022-opengl && cmake --build ...` → 0 errors
  - [ ] File > Open menu item still works

  **QA Scenarios**:
  ```
  Scenario: File dialog dedup, Open still works
    Tool: Bash (grep + cmake)
    Steps:
      1. grep -r "openFileDialog" src/image_loader.cpp
      2. cmake --preset vs2022-opengl
      3. cmake --build out/build/vs2022-opengl --config Debug
    Expected Result: grep returns 0 results in image_loader.cpp. Build succeeds.
    Evidence: .sisyphus/evidence/task-A.4-dedup.txt
  ```

  **Commit**: YES
  - Message: `fix(dialog): deduplicate file dialog implementations`
  - Files: `src/image_loader.cpp`, `src/image_loader.h`, `src/app.cpp`

- [x] A.5 Fix loadImage Same-File Early Return

  **What to do**:
  - In `app.cpp:378-423` (loadImage), add early return check:
    - If `!path.empty() && m_currentImage && path == m_currentImage->filePath` → log "same file, skipping" and return
  - This prevents unnecessary texture destroy/recreate

  **Must NOT do**:
  - Do NOT skip the actual load (only skip duplicate loads)

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with A.1-A.4)
  - **Parallel Group**: Wave A
  - **Blocks**: D.3
  - **Blocked By**: 0.5

  **References**:
  - `src/app.cpp:378-423` - loadImage implementation

  **Acceptance Criteria**:
  - [ ] Loading same file path twice → second load is skipped
  - [ ] Loading different file path → loads normally
  - [ ] `cmake --preset vs2022-opengl && cmake --build ...` → 0 errors

  **QA Scenarios**:
  ```
  Scenario: Same file reload is skipped
    Tool: Bash (cmake + manual)
    Steps:
      1. cmake --build out/build/vs2022-opengl --config Debug
      2. Launch app, Open MVIMG_20251123_200209.jpg twice
      3. Second open should log "same file, skipping"
    Expected Result: No texture destroy/recreate on duplicate load
    Evidence: .sisyphus/evidence/task-A.5-samefile.txt
  ```

  **Commit**: YES
  - Message: `fix(load): skip duplicate load of same file path`
  - Files: `src/app.cpp`

---

### Wave B: Architecture Refactoring

- [ ] B.1 Extract UI Panels from app.cpp

  **What to do**:
  - Create `src/ui_panels.h` and `src/ui_panels.cpp`
  - Move these methods from `Application` class to free functions in namespace:
    - `renderMenuBar()` → forward declarations stay, implementation moves
    - `renderControlPanel()` → parameters passed explicitly
    - `renderInfoPanel()` → parameters for exif/image data
    - `renderAboutDialog()` → parameter for show flag
    - `renderImagePanel()` → keep in app.cpp (too coupled with input)
  - Update `Application::renderUI()` to call extracted functions
  - Verify: characterization tests pass, pixel output identical

  **Must NOT do**:
  - Do NOT change pixel rendering logic
  - Do NOT exceed 4 new files across all Wave B

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with B.2)
  - **Blocks**: C.1, C.3
  - **Blocked By**: A.1

  **References**:
  - `src/app.cpp:90-96` - renderUI dispatcher
  - `src/app.cpp:98-134` - renderMenuBar
  - `src/app.cpp:136-178` - renderControlPanel
  - `src/app.cpp:278-342` - renderInfoPanel
  - `src/app.cpp:344-355` - renderAboutDialog

  **Acceptance Criteria**:
  - [ ] `src/ui_panels.h` and `src/ui_panels.cpp` exist
  - [ ] `cmake --preset vs2022-opengl && cmake --build ...` → 0 errors
  - [ ] UI looks identical (manual checklist)
  - [ ] Pixel output identical to baseline

  **QA Scenarios**:
  ```
  Scenario: Extracted UI panels render identically
    Tool: Bash (cmake + gtest)
    Steps:
      1. cmake --build out/build/vs2022-opengl --config Debug
      2. UltraHDRViewerTests.exe
    Expected Result: All tests pass, build succeeds
    Evidence: .sisyphus/evidence/task-B.1-uipanels.txt
  ```

  **Commit**: YES
  - Message: `refactor(ui): extract UI panel rendering to ui_panels.cpp`
  - Files: `src/ui_panels.h`, `src/ui_panels.cpp`, `src/app.cpp`, `src/app.h`

- [ ] B.2 Extract Image Controller from app.cpp

  **What to do**:
  - Create `src/image_controller.h` and `src/image_controller.cpp`
  - Move image state management: m_currentImage, m_hdrData, m_exifData, m_texture
  - Move view state: m_offsetX/Y, m_zoom, m_fitToWindow, m_isDragging
  - Move tone mapping state: m_exposure, m_gamma, m_toneMappingMode, m_toneMapDirty
  - Move methods: loadImage, reapplyToneMapping, resetView, fitToWindow
  - Application holds ImageController by unique_ptr

  **Must NOT do**:
  - Do NOT change ImageData/HDRData ownership model
  - Do NOT change rendering code

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with B.1)
  - **Blocks**: C.1, C.3
  - **Blocked By**: A.1

  **References**:
  - `src/app.h:36-51` - Image state members
  - `src/app.h:63-68` - Image operation methods
  - `src/app.cpp:76-86` - reapplyToneMapping
  - `src/app.cpp:378-426` - loadImage/resetView/fitToWindow

  **Acceptance Criteria**:
  - [ ] `src/image_controller.h` and `src/image_controller.cpp` exist
  - [ ] ImageController owns all image state
  - [ ] All characterization tests pass
  - [ ] Pixel output identical

  **QA Scenarios**:
  ```
  Scenario: Extracted controller preserves state
    Tool: Bash (gtest)
    Steps:
      1. cmake --build out/build/vs2022-opengl --config Debug
      2. UltraHDRViewerTests.exe
    Expected Result: All tests pass
    Evidence: .sisyphus/evidence/task-B.2-controller.txt
  ```

  **Commit**: YES
  - Message: `refactor(core): extract ImageController from Application`
  - Files: `src/image_controller.h`, `src/image_controller.cpp`, `src/app.cpp`, `src/app.h`

- [ ] B.3 Separate File Dialog from image_loader.cpp

  **What to do**:
  - Remove Win32 headers (#include <windows.h>, <commdlg.h>, glfw3native.h) from image_loader.cpp
  - Remove openFileDialog declaration from image_loader.h
  - Consolidate all file dialog code into file_dialog.cpp

  **Must NOT do**:
  - Do NOT remove platform guards from file_dialog.cpp
  - Do NOT break non-Windows builds

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with B.1, B.2, B.4)
  - **Blocks**: C.2
  - **Blocked By**: A.3, A.4

  **References**:
  - `src/image_loader.cpp:13-20` - Win32 headers to remove
  - `src/image_loader.h:20` - openFileDialog declaration to remove

  **Acceptance Criteria**:
  - [ ] grep "commdlg" src/image_loader.cpp → 0
  - [ ] grep "glfw3native" src/image_loader.cpp → 0
  - [ ] Build succeeds

  **QA Scenarios**:
  ```
  Scenario: Dialog code fully in file_dialog.cpp
    Tool: Bash (grep + cmake)
    Steps:
      1. grep "commdlg" src/image_loader.cpp
      2. cmake --build out/build/vs2022-opengl --config Debug
    Expected Result: grep returns 0. Build succeeds.
    Evidence: .sisyphus/evidence/task-B.3-separate.txt
  ```

  **Commit**: YES
  - Message: `refactor(dialog): consolidate file dialog code`
  - Files: `src/image_loader.cpp`, `src/image_loader.h`

- [ ] B.4 Extract Tone Mapping to Separate File

  **What to do**:
  - Create `src/tonemap.h` and `src/tonemap.cpp`
  - Move: reinhard(), acesFilmic(), uncharted2Curve(), toneMapImage(), applyToneMap()
  - Update image_loader.h to remove declarations
  - Add #include "tonemap.h" where needed

  **Must NOT do**:
  - Do NOT change algorithm behavior

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with B.1-B.3)
  - **Blocks**: D.1
  - **Blocked By**: A.2

  **References**:
  - `src/image_loader.cpp:54-93` - All tone mapping code

  **Acceptance Criteria**:
  - [ ] `src/tonemap.h` and `src/tonemap.cpp` exist
  - [ ] Tone map tests (0.3) pass
  - [ ] Pixel output identical

  **Commit**: YES
  - Message: `refactor(tonemap): extract tone mapping to separate module`
  - Files: `src/tonemap.h`, `src/tonemap.cpp`, `src/image_loader.cpp`, `src/image_loader.h`

- [ ] B.5 Update CMakeLists.txt for All New Files

  **What to do**:
  - Add all new .cpp files to APP_SOURCES
  - Verify all 5 presets compile

  **Must NOT do**:
  - Do NOT add unnecessary libraries

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `[]`

  **Parallelization**:
  - **Can Run In Parallel**: NO (runs after B.1-B.4)
  - **Blocks**: C.1-C.5, D.1-D.5
  - **Blocked By**: B.1, B.2, B.3, B.4

  **Acceptance Criteria**:
  - [ ] All 5 CMake presets build with 0 errors
  - [ ] All characterization tests pass

  **Commit**: YES
  - Message: `build: update CMakeLists.txt for new Wave B source files`
  - Files: `CMakeLists.txt`

---

### Wave C: Code Quality

- [ ] C.1 Replace ImageData::pixels with std::vector<uint8_t>

  **What to do**:
  - Change `uint8_t* pixels = nullptr;` → `std::vector<uint8_t> pixels;` in ImageData
  - Update dataSize() to return pixels.size()
  - Remove manual destructor (vector manages memory)
  - Replace all `new uint8_t[...]` with `pixels.resize(...)` and `.data()`
  - Replace `delete[] pixels` — handled by vector destructor
  - Update OpenGL/Vulkan texture upload to use `.data()`

  **Must NOT do**:
  - Do NOT change ImageData struct layout compatibility
  - Do NOT change Vulkan handle types

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: `[]`

  **Parallelization**:
  - Parallel: YES (with C.2-C.5)
  - Blocks: D.1, D.2
  - Blocked By: B.1, B.2

  **References**:
  - `src/renderer.h:10` - ImageData::pixels declaration
  - `src/image_loader.cpp:22-24` - Destructor with delete[]
  - `src/image_loader.cpp:92,169,229` - new uint8_t[] allocations
  - `src/renderer_gl.cpp:35` - glTexImage2D with image.pixels

  **Acceptance Criteria**:
  - [ ] `grep "new uint8_t" src/` → 0 in production code
  - [ ] `grep "delete\[\]" src/` → 0 in production code
  - [ ] Build succeeds, pixel-diff identical
  - [ ] Vulkan backend builds

  **Commit**: YES — `refactor(memory): replace raw pointer with std::vector in ImageData`

- [ ] C.2 Introduce LOG_ERROR Macro

  **What to do**:
  - Create `src/log.h` with LOG_ERROR/LOG_INFO macros wrapping fprintf
  - Replace all `fprintf(stderr, ...)` → `LOG_ERROR(...)`
  - Replace debug `fprintf(stdout, ...)` → `LOG_INFO(...)`
  - Keep `printf()` in main.cpp (user-facing CLI output)
  - No external dependencies

  **Recommended Agent Profile**: `quick`
  **Parallelization**: YES (with C.1, C.3-C.5), Blocks: D.5, Blocked By: B.3, A.2

  **Acceptance Criteria**:
  - [ ] `grep -rn "fprintf(stderr" src/ | grep -v "log.h"` → 0
  - [ ] Build succeeds, error messages still visible

  **Commit**: YES — `refactor(log): introduce LOG_ERROR/LOG_INFO macros`

- [ ] C.3 Extract Hardcoded Constants to config.h

  **What to do**:
  - Create `src/config.h` with namespace Config containing named constexpr values
  - Extract: window size, panel widths, zoom/ exposure/gamma slider ranges
  - Replace magic numbers in app.cpp

  **Must NOT do**:
  - Do NOT extract tone mapping algorithm coefficients (those ARE the algorithm)

  **Recommended Agent Profile**: `quick`
  **Parallelization**: YES (with C.1-C.2, C.4-C.5), Blocks: D.5, Blocked By: B.1, B.2

  **Commit**: YES — `refactor(config): extract hardcoded constants to config.h`

- [ ] C.4 Add Texture Size Validation

  **What to do**:
  - Check GL_MAX_TEXTURE_SIZE in OpenGL createTexture
  - Check maxImageDimension2D in Vulkan createTexture
  - Return nullptr + log error for oversized images

  **Recommended Agent Profile**: `quick`
  **Parallelization**: YES (with C.1-C.3, C.5), Blocked By: A.1

  **Commit**: YES — `fix(renderer): add GPU texture size validation`

- [ ] C.5 Fix Zero-Size Window Viewport Guard

  **What to do**:
  - Add `if (displayW <= 0 || displayH <= 0) return;` in OpenGLRenderer::render
  - Vulkan already has this guard

  **Recommended Agent Profile**: `quick`
  **Parallelization**: YES (with C.1-C.4), Blocked By: A.1

  **Commit**: YES — `fix(renderer): guard against zero-size viewport in OpenGL`

---

### Wave D: Performance Optimization

- [ ] D.1 Lift Tone Mapping switch(mode) Out of Pixel Loop

  **What to do**:
  - Move switch(mode) outside the for-loop in toneMapImage
  - Use if/else branches with separate inner loops per mode
  - Must produce pixel-identical output

  **Recommended Agent Profile**: `quick`
  **Parallelization**: YES (with D.2-D.4), Blocks: F1-F4, Blocked By: C.1, B.4

  **References**: `src/tonemap.cpp` (created in B.4), `tests/tonemap_test.cpp`

  **Commit**: YES — `perf(tonemap): lift mode switch out of per-pixel loop`

- [ ] D.2 Merge Double Pixel-Copy Loop in loadImageFile

  **What to do**:
  - Fuse two row-copy loops (SDR + HDR) into one fused loop
  - Must produce pixel-identical output

  **Recommended Agent Profile**: `quick`
  **Parallelization**: YES (with D.1, D.3-D.4), Blocked By: C.1

  **References**: `src/image_loader.cpp:172-186`

  **Commit**: YES — `perf(loader): fuse duplicate pixel-copy loops`

- [ ] D.3 Verify Same-File Reload Early Exit

  **What to do**:
  - Verify A.5 fix survived architecture refactoring
  - Same file path → skip reload

  **Recommended Agent Profile**: `quick`
  **Parallelization**: YES (with D.1-D.2, D.4), Blocked By: A.5

  **Commit**: YES — `perf(load): verify same-file reload early exit after refactoring`

- [ ] D.4 Optimize Vulkan updateTexture

  **What to do**:
  - Replace destroy+recreate with buffer staging + vkCmdCopyBufferToImage
  - Eliminate vkDeviceWaitIdle() from updateTexture path
  - Smooth tone mapping slider adjustment

  **Must NOT do**: Do NOT break OpenGL path

  **Recommended Agent Profile**: `deep`
  **Parallelization**: YES (with D.1-D.3), Blocked By: C.1

  **References**: `src/renderer_vk.cpp:663-672`

  **Commit**: YES — `perf(vulkan): optimize updateTexture with buffer staging`

- [ ] D.5 Final CMakeLists.txt Polish

  **What to do**: Add any missing files, verify all 5 presets build, run all tests

  **Recommended Agent Profile**: `quick`
  **Parallelization**: NO (after D.1-D.4), Blocks: F1-F4, Blocked By: C.2-C.5

  **Commit**: YES — `build: final CMakeLists.txt update`

---

## Final Verification Wave

- [ ] F1 Plan Compliance Audit

  **What to do**: Review all MUST/MUST NOT rules, verify scope boundaries
  **Agent**: `oracle`

- [ ] F2 Code Quality Review

  **What to do**: Review new/modified code for quality, duplication, coverage
  **Category**: `unspecified-high`, **Skills**: `["review-work"]`

- [ ] F3 Real Manual QA

  **What to do**: Execute 12-item checklist from skill file on final build
  **Category**: `unspecified-high`, **Skills**: `["playwright"]`

- [ ] F4 Scope Fidelity Check

  **What to do**: Verify no features added/removed, guardrails respected, Android cmake configures
  **Category**: `deep`

---

## Summary

| Wave | Tasks | Parallel | Files |
|------|-------|----------|-------|
| Wave 0: Characterization | 6 | ALL 6 | ~8 new |
| Wave A: Bug Fixes | 5 | ALL 5 | ~6 modified |
| Wave B: Architecture | 5 | 4P + 1S | ~10 modified |
| Wave C: Code Quality | 5 | ALL 5 | ~8 modified |
| Wave D: Performance | 5 | 4P + 1S | ~5 modified |
| Wave F: Review | 4 | ALL 4 | 0 (read-only) |
| **Total** | **30** | - | **~35** |

### Execution Order
```
0.1-0.6 (parallel, 6 agents)
  → A.1-A.5 (parallel, 5 agents)
  → B.1-B.4 (parallel) → B.5 (serial)
  → C.1-C.5 (parallel, 5 agents)
  → D.1-D.4 (parallel) → D.5 (serial)
  → F1-F4 (parallel, 4 agents)
  → User Approval
```
