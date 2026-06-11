# Android 构建修复与重构

## TL;DR

> **目标**: 修复 Android 构建问题 — NDK 版本、死代码清理、加载/色调映射去重、ImageData 迁移 std::vector、添加 GTest 单元测试
>
> **产出物**:
> - 修改：`android/` 下 ~7 个文件（build.gradle, CMakeLists.txt, native.cpp, hdr_types.h, image_loader_android.cpp）
> - 删除：`image_data.h`, `exif_data.h`（死文件）
> - 新增：`tonemap_android_test.cpp`, `exif_android_test.cpp`（Android GTest）
>
> **工作量**: Medium（多文件重构，需要逐像素验证）
> **并行执行**: YES（Wave 1: 6 个独立任务，Wave 2: 4 个依赖任务）
> **关键路径**: Task 1.1 → Task 2.1 → Task 2.2 → Task 2.4 → F1-F4

---

## Context

### 分析发现（Oracle + Metis）
Android 代码有一组与桌面平行的独立实现，但积累了严重技术债务：

| # | 严重度 | 问题 |
|---|---|---|
| 1 | MAJOR | `native.cpp` 自带 loadImage() + tone curves + applyToneMap() 与 `image_loader_android.cpp` 完全重复 |
| 2 | MAJOR | NDK 版本不匹配：build.gradle 写 21.4，README 要求 23.1+ |
| 3 | MINOR | `image_data.h` / `exif_data.h` 与 `hdr_types.h` 重复定义（死文件） |
| 4 | MINOR | `native.cpp` 中的魔法数字（0.01f, 10.0f, 4.0f, 20.0f 等） |
| — | NEW | Android ImageData 迁移到 std::vector（对齐桌面） |
| — | NEW | 添加 GTest 单元测试（`-fno-exceptions` 冲突需解决） |

### 架构决策
- **GTest 方案**: 创建独立测试目标（不带 `-fno-exceptions`），不影响主二进制
- **色调映射**: `native.cpp` 委托给 `image_loader_android.cpp` 的 `toneMapImage()`/`applyToneMap()`，不共享 `src/tonemap.cpp`
- **Android 独立**: 保持 Android 代码自主，不跨包含桌面头文件

---

## Work Objectives

### 核心目标
消除 Android 端 ~200 行重复代码，统一数据结构，添加单元测试，使 Android 构建与 README 一致。

### 具体产出物
- 修复 `android/app/build.gradle`（NDK 23.1+）
- 删除 `image_data.h`, `exif_data.h`
- 重构 `native.cpp`：去除内联 loader + tone curves + applyToneMap
- 迁移 `hdr_types.h` ImageData 到 `std::vector<uint8_t>`
- 更新 `image_loader_android.cpp` 适配 ImageData 变更
- 更新 `CMakeLists.txt`（GTest 目标 + STB_IMAGE_IMPL 提取）
- 新增 `tonemap_android_test.cpp`, `exif_android_test.cpp`

### Must Have
- [ ] NDK 版本统一为 23.1+（build.gradle + README 一致）
- [ ] 删除死文件 image_data.h, exif_data.h（无残留 #include）
- [ ] native.cpp 不再包含内联 tone curves（ringbuf/acesFilm/unch2）
- [ ] native.cpp 委托图像加载给 image_loader_android.cpp 的公共 API
- [ ] ImageData 迁移到 std::vector<uint8_t>（零 raw new[]/delete[]）
- [ ] 色调映射像素级输出前后一致（逐字节验证）
- [ ] GTest 测试编译通过（独立目标，无 -fno-exceptions）
- [ ] 所有 4 个 ABI 编译通过

### Must NOT Have
- [ ] 不修改 JNI 接口签名或 AndroidApp 结构布局
- [ ] 不修改 exif_parser_android.cpp（保持独立）
- [ ] 不共享桌面 src/tonemap.cpp（保持 Android 独立）
- [ ] 不引入新的外部依赖（除 GTest 外）
- [ ] 不修改 UI 逻辑或 Java/Kotlin 代码
- [ ] 不添加需要真机或模拟器的测试

---

## Verification Strategy

### 构建验证
```bash
cd android && ./gradlew assembleDebug
# Expected: BUILD SUCCESSFUL
```

### 像素一致性验证
```
# 重构前：用 native.cpp 内联 applyToneMap 生成 baseline
# 重构后：用 image_loader_android.cpp toneMapImage 生成输出
# Compare-Object 逐字节对比 → 零差异
```

### GTest 验证
```bash
# 独立测试目标，在构建后运行：
ctest --test-dir out/build/android-test --output-on-failure
```

---

## Execution Strategy

```
Wave 1（零风险 — 可并行，无需编译验证）:
├── Task 1.1: NDK 版本修复 + README 更新
├── Task 1.2: 删除死文件 image_data.h / exif_data.h
├── Task 1.3: 从 image_loader_android.cpp 提取 STB_IMAGE_IMPLEMENTATION
├── Task 1.4: 生成色调映射 baseline（重构前像素快照）
├── Task 1.5: 添加 GTest 测试目标到 CMakeLists.txt
└── Task 1.6: 创建 tonemap_android_test.cpp（调用 image_loader_android.cpp）

Wave 2（依赖 Wave 1 — 需要编译验证）:
├── Task 2.1: ImageData 迁移到 std::vector（hdr_types.h + image_loader_android.cpp）
├── Task 2.2: 色调映射去重（native.cpp → 委托 image_loader_android.cpp）
├── Task 2.3: 加载管线去重（native.cpp → 委托 loadImageFile/loadRegularImage）
└── Task 2.4: 魔法数字替换 + native.cpp 清理（移除 stb 直接引用）

Wave FINAL（4 并行验证）:
├── Task F1: 构建验证（4 ABI + GTest）
├── Task F2: 像素一致性验证
├── Task F3: 死代码扫描
└── Task F4: 计划合规审计

Critical Path: Task 1.4 → Task 2.1 → Task 2.2 → Task 2.4 → F1-F4
```

---

## TODOs

### Wave 1 — 零风险准备（可并行）

- [x] 1.1 NDK 版本修复 + README 更新

  **What to do**:
  - `android/app/build.gradle`: 将 `ndk { version "21.4.7075529" }` 替换为 `ndkVersion "23.1.7779620"`（正确 DSL 位置在 `android` 块下）
  - `README.md`: 确认 "Android NDK 23.1+" 与 build.gradle 一致

  **Must NOT do**: 修改 AGP 版本或 compileSdk

  **Recommended Agent Profile**: `quick` — 单行版本号修改
  **Parallelization**: Wave 1（与 1.2-1.6 并行）| **Blocks**: 无 | **Blocked By**: 无

  **Acceptance Criteria**:
  - [ ] `grep "21.4" android/` 返回零结果
  - [ ] build.gradle 中的 NDK 使用 `android.ndkVersion` DSL（非 defaultConfig.ndk.version）

  **QA Scenarios**:
  ```
  Scenario: NDK version consistent across all files
    Tool: Bash (grep)
    Steps:
      1. grep -rn "21.4" android/app/build.gradle → 零匹配
      2. grep -rn "ndkVersion" android/app/build.gradle → 输出 "23.1.7779620"
      3. grep "NDK" README.md → 包含 "23.1"
    Expected Result: 所有引用一致
    Evidence: .sisyphus/evidence/task-1.1-ndk.txt
  ```

  **Commit**: YES | Message: `fix(android): update NDK version to 23.1.7779620`

- [x] 1.2 删除死文件 image_data.h / exif_data.h

  **What to do**:
  - 删除 `android/app/src/main/cpp/image_data.h`
  - 删除 `android/app/src/main/cpp/exif_data.h`
  - 确认无文件引用这两个头文件（当前零引用）

  **Must NOT do**: 修改 hdr_types.h 或任何其他头文件

  **Recommended Agent Profile**: `quick` — 纯删除操作
  **Parallelization**: Wave 1（与 1.1, 1.3-1.6 并行）| **Blocks**: 无 | **Blocked By**: 无

  **Acceptance Criteria**:
  - [ ] `image_data.h` 和 `exif_data.h` 不存在
  - [ ] `grep -r "image_data.h\|exif_data.h" android/` 返回零结果

  **QA Scenarios**:
  ```
  Scenario: Dead files removed, no residual references
    Tool: Bash
    Steps:
      1. Test-Path "android/app/src/main/cpp/image_data.h" → False
      2. Test-Path "android/app/src/main/cpp/exif_data.h" → False
      3. grep -rn "image_data.h\|exif_data.h" android/ → 零输出
    Expected Result: 文件已删除，代码库清洁
    Evidence: .sisyphus/evidence/task-1.2-dead-files.txt
  ```

  **Commit**: YES | Message: `chore(android): remove dead duplicate header files`

- [x] 1.3 提取 STB_IMAGE_IMPLEMENTATION 到独立翻译单元

  **What to do**:
  - 从 `image_loader_android.cpp` 中移除 L10-11（`#define STB_IMAGE_IMPLEMENTATION` + `#include "stb_image.h"`）
  - 创建 `android/app/src/main/cpp/stb_image_impl.cpp`（单文件，内容同被移除的两行）
  - 更新 `CMakeLists.txt` 的 APP_SOURCES 添加 `stb_image_impl.cpp`
  - `image_loader_android.cpp` 改为普通 `#include "stb_image.h"`（无 IMPLEMENTATION 宏）

  **Must NOT do**: 修改 native.cpp 的 stb_image.h include（Wave 2 Task 2.3 会处理）

  **Recommended Agent Profile**: `quick` — 简单文件拆分
  **Parallelization**: Wave 1（与 1.1-1.6 并行）| **Blocks**: 无 | **Blocked By**: 无

  **Acceptance Criteria**:
  - [ ] `stb_image_impl.cpp` 存在且内容为 `#define STB_IMAGE_IMPLEMENTATION` + `#include "stb_image.h"`
  - [ ] `image_loader_android.cpp` 不再定义 STB_IMAGE_IMPLEMENTATION
  - [ ] `CMakeLists.txt` APP_SOURCES 包含 stb_image_impl.cpp

  **QA Scenarios**:
  ```
  Scenario: STB_IMAGE_IMPLEMENTATION extracted, image_loader_android.cpp clean
    Tool: Bash (grep)
    Steps:
      1. Select-String "STB_IMAGE_IMPLEMENTATION" android/app/src/main/cpp/stb_image_impl.cpp → 1 match
      2. Select-String "STB_IMAGE_IMPLEMENTATION" android/app/src/main/cpp/image_loader_android.cpp → 0 matches
    Expected Result: IMPL 宏仅在 stb_image_impl.cpp
    Evidence: .sisyphus/evidence/task-1.3-stb.txt
  ```

  **Commit**: YES | Message: `refactor(android): extract STB_IMAGE_IMPLEMENTATION to separate TU`

- [x] 1.4 生成色调映射 baseline（重构前像素快照）

  **What to do**:
  - 编写独立测试程序调用 `image_loader_android.cpp` 的 `toneMapImage()`
  - 使用固定 HDR 输入（4×4: R=0.25, G=0.50, B=0.75, A=1.0）
  - 分别用 mode=0（Reinhard）、mode=1（ACES）、mode=2（Uncharted2）生成输出
  - 保存为 `tests/baseline/android_reinhard.bin`, `android_aces.bin`, `android_uncharted2.bin`
  - 同样参数调用 `native.cpp` 内联版本（ringbuf/acesFilm/unch2），验证输出逐字节一致

  **Must NOT do**: 修改任何源代码（纯数据采集）

  **Recommended Agent Profile**: `unspecified-high` — 需要编写数据采集程序
  **Parallelization**: 顺序（Wave 1 最后）| **Blocks**: Task 2.2（提供验证 baseline）| **Blocked By**: 无

  **Acceptance Criteria**:
  - [ ] 3 个 baseline 文件存在且非空
  - [ ] native.cpp 内联版与 image_loader_android.cpp 版输出逐字节一致（零差异）

  **QA Scenarios**:
  ```
  Scenario: Baseline files match between native.cpp and image_loader_android.cpp
    Tool: Bash (Compare-Object)
    Steps:
      1. 用 native.cpp 内联 applyToneMap 生成 native_reinhard.bin
      2. 用 image_loader_android.cpp toneMapImage 生成 loader_reinhard.bin  
      3. Compare-Object (Get-Content native_reinhard.bin -Encoding Byte) (Get-Content loader_reinhard.bin -Encoding Byte)
    Expected Result: 零输出（零差异）
    Evidence: .sisyphus/evidence/task-1.4-baseline.txt
  ```

  **Commit**: YES | Message: `test(android): add tone mapping baseline data`

- [x] 1.5 添加 GTest 测试目标到 CMakeLists.txt

  **What to do**:
  - 在 `android/app/src/main/cpp/CMakeLists.txt` 末尾添加独立测试目标
  - 测试目标不包含 `-fno-exceptions` 标志
  - 链接 GTest（CMake FetchContent 或使用已有 googletest）
  - 测试源文件列表独立于 APP_SOURCES
  - 复用已有的 libultrahdr、turbojpeg 链接

  **Must NOT do**: 修改主目标的编译选项或 APP_SOURCES

  **Recommended Agent Profile**: `unspecified-high` — CMake 配置，需处理交叉编译
  **Parallelization**: Wave 1（与 1.1-1.3, 1.6 并行）| **Blocks**: Task 1.6（测试文件需要目标存在）| **Blocked By**: 无

  **Acceptance Criteria**:
  - [ ] CMakeLists.txt 包含独立测试目标（如 `uhdrviewer_test`）
  - [ ] 测试目标不设置 `-fno-exceptions`
  - [ ] 测试目标链接 GTest + uhdr + turbojpeg
  - [ ] 测试目标 include 路径覆盖 hdr_types.h 和 libultrahdr

  **QA Scenarios**:
  ```
  Scenario: CMakeLists.txt contains separate test target without -fno-exceptions
    Tool: Bash (grep)
    Steps:
      1. Select-String "uhdrviewer_test|UltraHDRViewerTest" android/app/src/main/cpp/CMakeLists.txt
      2. Verify test target does NOT have -fno-exceptions
    Expected Result: 测试目标存在且配置正确
    Evidence: .sisyphus/evidence/task-1.5-cmake.txt
  ```

  **Commit**: YES | Message: `build(android): add GTest test target to CMake`

- [x] 1.6 创建 Android 色调映射单元测试

  **What to do**:
  - 创建 `android/app/src/main/cpp/tonemap_android_test.cpp`
  - 测试 `image_loader_android.cpp` 的 `toneMapImage()` 函数
  - 测试用例：
    1. OutputInRange：所有 3 个模式输出在 [0, 255]
    2. ReinhardExpectedValues：R=51, G=85, B=109, A=255
    3. ExposureZeroAllBlack：exposure=0 时全部 R=G=B=0
    4. DifferentModesDiffer：3 模式输出互异
    5. GammaChangesOutput：gamma=1.0 vs 2.2 输出不同
    6. RegressionMatchBaseline：输出与 Task 1.4 baseline 逐字节一致

  **Must NOT do**: 测试 native.cpp 或 UI 逻辑（纯单元测试）

  **Recommended Agent Profile**: `unspecified-high` — GTest 测试编写 + 像素比较
  **Parallelization**: Wave 1（与 1.1-1.5 并行）| **Blocks**: 无 | **Blocked By**: Task 1.5（测试目标需要存在）

  **Acceptance Criteria**:
  - [ ] 6 个测试用例全部通过
  - [ ] 测试调用 image_loader_android.cpp 的 toneMapImage（非 native.cpp 内联版）
  - [ ] RegressionMatchBaseline 逐字节匹配 baseline

  **QA Scenarios**:
  ```
  Scenario: All 6 tone map tests pass, regression matches baseline
    Tool: Bash (ctest)
    Steps:
      1. cmake --build ... && ctest --test-dir ... -R ToneMap_Android
    Expected Result: 6/6 tests passed
    Evidence: .sisyphus/evidence/task-1.6-tonemap-test.txt
  ```

  **Commit**: YES | Message: `test(android): add GTest tone mapping unit tests`

- [x] 1.7 创建 Android EXIF 解析单元测试

  **What to do**:
  - 创建 `android/app/src/main/cpp/exif_android_test.cpp`
  - 测试 `exif_parser_android.cpp` 的 `parseExif()` 函数
  - 测试用例：
    1. ParseUltraHDRImage：加载 MVIMG_20251123_200209.jpg，验证 valid=true, make/model 非空, orientation∈[0,8], pixelX>0, pixelY>0
    2. NullInputReturnsInvalid：parseExif(nullptr, 0) → valid=false
    3. SmallInputReturnsInvalid：parseExif(data, 3) → valid=false

  **Must NOT do**: 修改 exif_parser_android.cpp 源码

  **Recommended Agent Profile**: `unspecified-high` — GTest 测试编写
  **Parallelization**: Wave 1（与 1.1-1.6 并行）| **Blocks**: 无 | **Blocked By**: Task 1.5（测试目标需要存在）

  **Acceptance Criteria**:
  - [ ] 3 个测试用例全部通过
  - [ ] ParseUltraHDRImage 验证至少 5 个字段非默认值

  **QA Scenarios**:
  ```
  Scenario: All EXIF tests pass
    Tool: Bash (ctest)
    Steps:
      1. ctest --test-dir ... -R Exif_Android
    Expected Result: 3/3 tests passed
    Evidence: .sisyphus/evidence/task-1.7-exif-test.txt
  ```

  **Commit**: YES | Message: `test(android): add GTest EXIF parsing unit tests`

---

### Wave 2 — 重构实现（依赖 Wave 1，顺序执行）

- [x] 2.1 ImageData 迁移到 `std::vector<uint8_t>`

  **What to do**:
  - `hdr_types.h`: 将 `uint8_t* pixels = nullptr` 改为 `std::vector<uint8_t> pixels`；移除 `~ImageData()` 声明
  - `hdr_types.h`: 更新 `dataSize()` 返回 `pixels.size()`
  - `image_loader_android.cpp`: 移除 `ImageData::~ImageData()` 实现
  - `image_loader_android.cpp`: 替换所有 `delete[] sdr.pixels` → `sdr.pixels.clear()`；替换 `sdr.pixels = new uint8_t[...]` → `sdr.pixels.resize(...)`；替换 `outSDR.pixels = new uint8_t[...]` → `outSDR.pixels.resize(...)`
  - `image_loader_android.cpp`: `applyToneMap()` 中 `delete[] sdr.pixels` → 移除 + `sdr.pixels.resize(...)`
  - `native.cpp`: 替换所有 `delete[] app->currentImage->pixels` → `app->currentImage->pixels.clear()` / 移除
  - `native.cpp`: 替换所有 `image->pixels = new uint8_t[...]` → `image->pixels.resize(...)`
  - `native.cpp`: 替换 `app->currentImage->pixels` 直接指针访问 → `.data()`（glTexImage2D, glTexSubImage2D, memcpy 调用）
  - `native.cpp` L73: 移除 `if (app->currentImage->pixels) delete[] app->currentImage->pixels;`（vector 自管理）

  **Must NOT do**: 修改 JNI 接口、AndroidApp 结构、Java/Kotlin 代码

  **Recommended Agent Profile**: `deep` — 多文件、需要逐行替换，确保无遗漏
  **Parallelization**: 顺序 | **Blocks**: Task 2.2, 2.3 | **Blocked By**: Task 1.4（baseline）

  **References**:
  - `src/renderer.h:10-20` — 桌面 ImageData 结构（目标参考）
  - Android 原始代码: `hdr_types.h:7-15`, `image_loader_android.cpp:13-15,51-57,66-142`, `native.cpp:69-98,100-215,343-351`
  - oldString/newString 映射表见下方

  **Acceptance Criteria**:
  - [ ] `grep -rn "delete\[\].*pixels" android/app/src/main/cpp/` 返回零结果
  - [ ] `grep -rn "new uint8_t\[" android/app/src/main/cpp/` 返回零结果
  - [ ] `grep -rn "uint8_t\* pixels" android/app/src/main/cpp/` 返回零结果
  - [ ] `glTexImage2D` 和 `glTexSubImage2D` 调用使用 `.data()`

  **QA Scenarios**:
  ```
  Scenario: Zero raw pointer allocation remaining in Android code
    Tool: Bash (grep)
    Steps:
      1. grep -rn "delete\[\].*pixels\|new uint8_t\[" android/app/src/main/cpp/ → 零匹配
      2. grep -rn "uint8_t\* pixels" android/app/src/main/cpp/hdr_types.h → 零匹配
      3. grep "std::vector<uint8_t> pixels" android/app/src/main/cpp/hdr_types.h → 1 match
    Expected Result: 所有 pixels 使用 std::vector
    Evidence: .sisyphus/evidence/task-2.1-vector.txt
  ```

  **Commit**: YES | Message: `refactor(android): migrate ImageData to std::vector<uint8_t>`

- [x] 2.2 色调映射去重 — native.cpp 委托给 image_loader_android.cpp

  **What to do**:
  - `native.cpp`: 删除 L56-67 的 3 个内联 tone curves（ringbuf, acesFilm, unch2）
  - `native.cpp`: 删除 L69-98 的 `applyToneMap(AndroidApp*)` 函数
  - `native.cpp`: 在 L208（`applyToneMap(app)` 调用点）替换为调用 `image_loader_android.cpp` 的 `applyToneMap(HDRData&, ImageData&, ...)`
  - 调用后设置 `app->textureDirty = true`（原函数内部操作）
  - 验证色调映射输出与 Task 1.4 baseline 逐字节一致

  **Must NOT do**: 修改 image_loader_android.cpp 的 tone map 函数签名

  **Recommended Agent Profile**: `unspecified-high` — 删除 + 委托重构，需要像素验证
  **Parallelization**: 顺序 | **Blocks**: Task 2.4 | **Blocked By**: Task 2.1, 1.4

  **Acceptance Criteria**:
  - [ ] `grep -c "ringbuf\|acesFilm\|unch2" native.cpp` 返回 0
  - [ ] `grep "static float ringbuf\|static float acesFilm\|static float unch2" native.cpp` 返回零匹配
  - [ ] `applyToneMap(AndroidApp*)` 函数签名不存在于 native.cpp
  - [ ] 色调映射输出与 baseline 逐字节一致

  **QA Scenarios**:
  ```
  Scenario: native.cpp delegates tone mapping to image_loader_android.cpp
    Tool: Bash (grep)
    Steps:
      1. grep -c "ringbuf\|acesFilm\|unch2" native.cpp → 0
      2. grep "static float ringbuf\|static float acesFilm\|static float unch2" native.cpp → 零匹配
      3. Compare-Object baseline vs new output → 零差异
    Expected Result: native.cpp 无内联 tone curves，输出不变
    Evidence: .sisyphus/evidence/task-2.2-tonemap-dedup.txt
  ```

  **Commit**: YES | Message: `refactor(android): delegate tone mapping to image_loader_android.cpp`

- [x] 2.3 加载管线去重 — native.cpp 委托给 loadImageFile/loadRegularImage

  **What to do**:
  - `native.cpp`: 重构 `loadImage(AndroidApp*, const std::string&)` — 移除 L115-188 的内联 Ultra HDR 解码逻辑
  - 新逻辑：
    1. 清理旧 texture + 重置状态（保留）
    2. 调用 `image_loader_android.cpp` 的 `loadImageFile()` 尝试 Ultra HDR 解码
    3. 失败则调用 `loadRegularImage()` 作为 fallback
    4. 提取返回的 ImageData/ExifData 到 app 状态
    5. 创建 OpenGL 纹理
    6. 如有 HDR 数据则调用 tone map
  - 移除 `native.cpp` 的 `#include "stb_image.h"`（不再直接调用 stbi_load）
  - 移除 `native.cpp` 对 `ultrahdr_api.h` 的 UHDR API 直接调用（uhdr_create_decoder 等）

  **Must NOT do**: 修改 image_loader_android.cpp 的函数签名或行为

  **Recommended Agent Profile**: `deep` — 管线重构，需要理解现有逻辑并精确保留行为
  **Parallelization**: 顺序 | **Blocks**: Task 2.4 | **Blocked By**: Task 2.1

  **Acceptance Criteria**:
  - [ ] `grep "uhdr_create_decoder\|uhdr_decode\|uhdr_dec_set" native.cpp` 返回零匹配
  - [ ] `grep "stbi_load" native.cpp` 返回零匹配
  - [ ] `loadImage()` 调用 `loadImageFile()` 或 `loadRegularImage()`

  **QA Scenarios**:
  ```
  Scenario: No inline Ultra HDR decode or stb_load in native.cpp
    Tool: Bash (grep)
    Steps:
      1. grep "uhdr_create_decoder\|uhdr_decode" native.cpp → 零匹配
      2. grep "stbi_load\|stbi_image_free" native.cpp → 零匹配
    Expected Result: native.cpp 干净委托
    Evidence: .sisyphus/evidence/task-2.3-loader-dedup.txt
  ```

  **Commit**: YES | Message: `refactor(android): delegate image loading to image_loader_android.cpp`

- [x] 2.4 魔法数字替换 + final cleanup

  **What to do**:
  - `native.cpp` 中硬编码常量替换：
    - `0.01f` → 引用 `android_constants.h`（新建）或直接内联 `constexpr`（曝光最小值）
    - `10.0f` → 曝光最大值
    - `1.0f` → gamma 最小值
    - `4.0f` → gamma 最大值
    - `0.05f` → 缩放最小值
    - `20.0f` → 缩放最大值
  - 创建 `android/app/src/main/cpp/android_config.h`（Android 专用常量，与桌面 `config.h` 平行）
  - 移除 `native.cpp` 中不再需要的 include（stb_image.h, 如 ultrahdr_api.h 无其他引用）
  - 最终检查：`native.cpp` 无残留死代码

  **Must NOT do**: 在 native.cpp 中创建新的内联函数或逻辑

  **Recommended Agent Profile**: `quick` — 机械替换 + 配置文件新建
  **Parallelization**: 顺序 | **Blocks**: 无 | **Blocked By**: Task 2.2, 2.3

  **Acceptance Criteria**:
  - [ ] `android_config.h` 存在且包含所有 UI 范围常量
  - [ ] `native.cpp` 无硬编码 UI 范围值（0.01f, 10.0f, 4.0f, 20.0f 等）
  - [ ] `native.cpp` 中 dead includes 已移除

  **QA Scenarios**:
  ```
  Scenario: Magic numbers replaced, no dead includes
    Tool: Bash (grep)
    Steps:
      1. grep "0\.01f\|10\.0f\|4\.0f\|20\.0f\|0\.05f" native.cpp → 仅出现在 android_config.h include 相关
      2. grep "#include \"stb_image.h\"" native.cpp → 零匹配
    Expected Result: 清洁代码
    Evidence: .sisyphus/evidence/task-2.4-cleanup.txt
  ```

  **Commit**: YES | Message: `refactor(android): extract constants to android_config.h`

---

## Final Verification Wave

- [ ] F1. **构建验证** — `quick`
  运行 `./gradlew assembleDebug`，确认 4 ABI 全部编译通过，GTest 目标链接成功。
  Output: `Build [PASS/FAIL] | GTests [PASS/N fail] | VERDICT`

- [ ] F2. **像素一致性验证** — `quick`
  对比重构前后色调映射输出，逐字节验证无差异。
  Output: `PixelDiff [MATCH/MISMATCH] | VERDICT`

- [ ] F3. **死代码扫描** — `quick`
  Grep 验证：零 `delete[] pixels`、零 `new uint8_t[`、零内联 tone curves、零 uhdr_create_decoder 在 native.cpp。
  Output: `DeadCode [N violations] | VERDICT`

- [ ] F4. **计划合规审计** — `oracle`
  逐项对照 Must Have / Must NOT Have 清单验证。
  Output: `MustHave [N/8] | MustNotHave [N/6] | VERDICT`

---

## Commit Strategy

- **Wave 1**: 按任务独立提交（每个 ~1-3 个文件）
- **Wave 2**: 按任务独立提交（每个 ~1-4 个文件），确保每次提交可编译
- **Wave FINAL**: 单一提交记录验证结果

---

## Success Criteria

### Verification Commands
```bash
cd android && ./gradlew assembleDebug 2>&1 | Select-String "BUILD"
# Expected: BUILD SUCCESSFUL

Select-String -Path "android/app/src/main/cpp/native.cpp" -Pattern "ringbuf|acesFilm|unch2|uhdr_create_decoder|delete\[\].*pixels|new uint8_t\["
# Expected: (no output)
```

### Final Checklist
- [ ] NDK 版本 23.1+ 在 build.gradle 和 README.md 中一致
- [ ] image_data.h, exif_data.h 已删除且无残留引用
- [ ] ImageData 使用 std::vector，zero raw pointer allocation
- [ ] 色调映射输出逐字节与 baseline 一致
- [ ] 4 ABI 编译通过
- [ ] GTest 测试可编译可运行
