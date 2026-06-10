# UltraHDR Viewer - Skill 文件更新

## TL;DR

> **目标**: 将过时的 `.opencode/skills/ultrahdr-viewer.md` 更新为反映当前代码库重构后状态的准确版本
>
> **产出物**:
> - 1 个更新后的 Markdown 文件 (`.opencode/skills/ultrahdr-viewer.md`)
>
> **工作量**: Quick（单文件编辑，纯文档）
> **并行执行**: NO（单任务顺序）
> **关键路径**: Task 1 → F1-F2 验证 → 完成

---

## Context

### 原始需求
用户要求"为该项目生成一个skill"。发现已有 skill 文件但过时（未反映重构后的代码结构）。用户选择：
- **更新现有 skill**（非重建）
- **中英混合**语言
- **增量更新**风格
- **移除已修复**的 GLFW hints bug 警告

### 代码库变化（skill 需反映）
| 旧状态 (skill 描述) | 新状态 (当前代码) |
|---|---|
| `app.cpp` 包含全部 UI 渲染 | 拆分为 `app.cpp`（协调器）+ `ui_panels.cpp`（面板） |
| `image_loader.cpp` 含色调映射 | 提取到独立 `tonemap.cpp` |
| `fprintf(stderr, ...)` 散落各处 | 统一为 `log.h` 的 `LOG_ERROR`/`LOG_INFO` 宏 |
| 硬编码魔法数字 | 提取到 `config.h` 的 `namespace Config` 常量 |
| `ImageData::pixels` 为 raw `new[]` | 桌面端已改为 `std::vector<uint8_t>` |
| GLFW hints 在窗口创建**之后**设置（Bug） | **已修复** — hints 现在在 `glfwCreateWindow` 之前 |
| 零测试 | Google Test + 3 个测试文件 + baseline 数据 |

### Metis 审查 - 关键发现
- **P0**: Android 端 `ImageData` 结构仍用 raw pointer — 平台已分歧，skill 需记录
- **P0**: README.md 同样过时 — 需确认是否纳入范围
- **P1**: 色调映射算法在 `tonemap.cpp`（桌面）和 `image_loader_android.cpp`（Android）中重复
- **P1**: `renderImagePanel()` 仍在 `app.cpp` 中未提取 — 不应过度描述"纯协调器"模式
- **P1**: 像素比较章节引用文件名错误（`ref.raw` → 应为 `tests/baseline/ref_pixels.raw`）
- **P2**: `config.h` 常量 `PANEL_IMAGE_MARGIN` 依赖 `PANEL_CONTROLS_WIDTH`
- **P2**: 测试素材 `MVIMG_20251123_200209.jpg` 的位置未记录

---

## Work Objectives

### 核心目标
将 `.opencode/skills/ultrahdr-viewer.md`（168 行）更新为准确描述当前 21 个源文件的代码库结构。

### 具体产出物
- 1 个更新后的 skill 文件：`.opencode/skills/ultrahdr-viewer.md`

### Definition of Done
- [ ] 项目结构树包含全部 21 个 `src/` 文件
- [ ] 所有过时描述已更正（image_loader, app, 错误处理, 内存管理）
- [ ] 新增文件章节（config.h, log.h, tonemap, ui_panels）
- [ ] GLFW hints bug 警告已移除，初始化顺序保留为最佳实践
- [ ] 测试章节包含实际可执行命令
- [ ] Common Pitfalls 更新平台分歧记录
- [ ] 像素比较章节文件名已修正
- [ ] 文件保持在 200-300 行

### Must Have
- [ ] 项目结构树更新（+4 个文件：config.h, log.h, tonemap.cpp/h, ui_panels.cpp/h）
- [ ] `image_loader` 描述：移除"tone mapping"→ 标注"image decoding + backward-compat includes tonemap.h"
- [ ] `app` 描述：明确标注 `renderImagePanel` 保留在 app.cpp，面板委托给 ui_panels
- [ ] 错误处理章节：`fprintf` → `LOG_ERROR/LOG_INFO` 宏（log.h）
- [ ] 内存管理章节：raw pointer → `std::vector<uint8_t>`（桌面），标注 Android 仍用 raw pointer
- [ ] GLFW hints bug：移除警告 → 初始化顺序保留为"最佳实践"
- [ ] 测试章节：添加 `ctest` 命令、列出 3 个测试文件、标注 baseline
- [ ] Common Pitfalls #3：更新为平台分歧说明
- [ ] 像素比较文件名：修正为 `tests/baseline/ref_pixels.raw`
- [ ] 色调映射算法重复警告（桌面 + Android 独立副本）

### Must NOT Have
- [ ] 不修改任何 `.cpp`/`.h` 源码文件
- [ ] 不修改 Android 代码
- [ ] 不重写或重新组织 skill 文件结构（增量更新）
- [ ] 不翻译代码块、命令或 API 名称（仅翻译标题和描述）
- [ ] 不修改测试文件或添加新测试

---

## Verification Strategy

### 验证方式
- **自动测试**: N/A（纯文档产物）
- **Agent QA**: 手动验证（人工审查）
- **框架**: N/A

### QA 策略
- 最终审查：读取生成的 skill 文件，逐项对照 Must Have 清单验证
- README.md 对比：检查二者是否一致（若不一致，记录为已知问题）

---

## Execution Strategy

### 单文件顺序执行

```
Task 1（唯一实现任务）:
  读取现有 skill 文件 → 应用全部更新 → 写入 .opencode/skills/ultrahdr-viewer.md

Final Verification:
  F1: Must Have 清单对照验证
  F2: README.md 一致性检查
```

---

## TODOs

- [x] 1. 更新 `.opencode/skills/ultrahdr-viewer.md` 文件

  **What to do**:
  1. 读取现有 `.opencode/skills/ultrahdr-viewer.md`（168 行）
  2. 按以下顺序应用增量修改：

  **A. 项目结构树（~第 27-48 行）**:
  - 在 `src/` 树中添加：`config.h`, `log.h`, `tonemap.h/.cpp`, `ui_panels.h/.cpp`
  - 更新 `app.h/.cpp` 描述：从 "UI rendering" → "App lifecycle + image panel + UI orchestration"
  - 更新 `image_loader.h/.cpp` 描述：从 "tone mapping, file dialog" → "Ultra HDR decoding, backward-compat includes tonemap.h"

  **B. 构造顺序（~第 50-69 行）**:
  - 重命名标题："Construction Order / 初始化顺序 (Best Practice)"（移除 "CRITICAL" 标签）
  - 将 bug 语言替换为："Bug 已在重构中修复 — hints 现在在 `app.cpp` 中于 `glfwCreateWindow` 之前设置。该顺序仍然是重要的最佳实践。"
  - 注释中删除 "this is a bug" → 改为 "now fixed — hints are set in app.cpp before glfwCreateWindow"

  **C. 错误处理（~第 106-108 行）**:
  - 将整个小节从 "当前/目标" 改为 "已实现"
  - 文本：`LOG_ERROR(fmt, ...)` 和 `LOG_INFO(fmt, ...)` 宏定义于 `src/log.h`，使用 `fprintf`，无外部依赖。
  - 添加对 `log.h` 的交叉引用

  **D. 内存管理（~第 110-113 行）**:
  - 更新：`ImageData::pixels` 桌面端现在使用 `std::vector<uint8_t>`
  - 添加警告：Android 副本（`android/.../hdr_types.h`）仍使用 `uint8_t* pixels` — 两个平台独立维护
  - Vulkan/GLFWwindow 行保持不变

  **E. 测试（~第 115-140 行）**:
  - 更新 Framework 子节：添加实际构建/运行命令
    ```bat
    cmake --preset vs2022-opengl
    cmake --build out/build/vs2022-opengl --config Debug
    ctest --test-dir out/build/vs2022-opengl --output-on-failure
    ```
  - 添加已有测试文件列表：
    - `tests/tonemap_test.cpp` — 色调映射算法正确性（范围、恒等性、模式差异、gamma）
    - `tests/exif_parser_test.cpp` — EXIF 解析（相机信息、GPS、方向、尺寸）
    - `tests/image_load_test.cpp` — 图片加载（ImageData + HDRData 输出、baseline 保存）
  - 更新基线部分：`ref.raw` → `tests/baseline/ref_pixels.raw`
  - 更新像素比较命令中的文件名

  **F. Common Pitfalls（~第 142-150 行）**:
  - 删除 Pitfall #1（GLFW hints bug — 已修复）
  - 更新 Pitfall #3：从 "Don't change ImageData struct layout" → 
    "ImageData struct layout: 桌面（`src/renderer.h`）使用 `std::vector<uint8_t>`；Android（`android/.../hdr_types.h`）使用 `uint8_t* pixels`。两个平台独立维护 — 修改一个不会影响另一个。"
  - 添加新 Pitfall：色调映射算法在 `src/tonemap.cpp`（桌面）和 `android/.../image_loader_android.cpp`（Android）中重复。算法修改必须同时应用于两个文件。

  **G. 手动验证清单（~第 152-167 行）**:
  - 添加：`[ ] 测试：ctest --test-dir out/build/vs2022-opengl --output-on-failure（全部通过）`

  **H. 新增章节 — 文件说明**:
  在项目结构树之后添加简洁的新增模块说明：

  ```
  ### 新增模块（重构提取）

  | 文件 | 职责 | 提取自 |
  |------|------|--------|
  | `config.h` | `namespace Config` — 窗口/布局/滑块范围常量 | 提取自 app.cpp 的魔法数字 |
  | `log.h` | `LOG_ERROR`/`LOG_INFO` 宏 — 最小化日志，无外部依赖 | 替换散落的 `fprintf` |
  | `tonemap.cpp/h` | `toneMapImage()` / `applyToneMap()` — 3 种色调映射模式 | 提取自 image_loader.cpp |
  | `ui_panels.cpp/h` | `renderMenuBar/ControlPanel/InfoPanel/AboutDialog` | 提取自 app.cpp |

  **注意**: `renderImagePanel()`（图像视口 + 缩放/平移/棋盘格）保留在 `app.cpp` 中 —
  因为它与 app 状态（`m_zoom`, `m_offsetX`, `m_texture`, `m_isDragging`）紧密耦合。
  在未理解这些依赖关系的情况下不要提取它。

  **注意**: `config.h` 中 `PANEL_IMAGE_MARGIN = 290.0f` 依赖于 `PANEL_CONTROLS_WIDTH = 280.0f`（= 280 + 10 间距）。
  修改 `PANEL_CONTROLS_WIDTH` 时也应调整 margin。

  **注意**: `image_loader.h` 包含 `#include "tonemap.h"` 以保持向后兼容。
  ```

  **I. 标题翻译**（中英混合）:
  - 所有章节标题：英文原文 + 中文翻译（如 `## Testing / 测试`）
  - 关键术语：首次出现时双语标注（如 "tone mapping / 色调映射"）
  - 代码块、命令、API 名称：仅英文
  - 文件描述：中英混合（如 `config.h — 窗口/布局常量`）

  **J. 头部元信息更新**:
  - 在文件头部添加数据来源标注：
    ```
    > C++17 Ultra HDR image viewer: Dear ImGui + GLFW + libultrahdr
    > Target: Windows Desktop, VS2022 + CMake
    > Skill generated from codebase analysis — reflects post-refactor state
    ```

  **Must NOT do**:
  - 不修改 `deps/` 结构
  - 不修改 Build Matrix 表格
  - 不重写 Texture Handles / Platform Guards / GLFW Lifecycle 章节（未变化）
  - 不翻译代码块或命令
  - 不触碰任何 `.cpp`/`.h` 文件
  - 不同时更新 README.md（除非用户明确要求）

  **Recommended Agent Profile**:
  - **Category**: `writing`
    - Reason: 纯文档更新——需要精确的 markdown 编辑和双语写作
  - **Skills**: [`customize-opencode`]
    - `customize-opencode`: 在 `.opencode/config/` 下创建/编辑 skill 文件

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: 单任务
  - **Blocks**: F1, F2（验证任务）
  - **Blocked By**: None（可立即开始）

  **References**:
  - 现有 skill: `.opencode/skills/ultrahdr-viewer.md`（完整文件）— 修改的基础
  - 项目结构: `src/config.h`, `src/log.h`, `src/tonemap.h`, `src/tonemap.cpp`, `src/ui_panels.h`, `src/ui_panels.cpp` — 新增模块参考
  - 渲染器接口: `src/renderer.h:10-20` — ImageData struct 定义（确认 std::vector）
  - 应用协调器: `src/app.cpp:29-43` — GLFW hints 现在的位置（确认 bug 已修复）
  - 测试基础设施: `tests/CMakeLists.txt`（完整）— 测试目标名和链接
  - 测试文件: `tests/tonemap_test.cpp`, `tests/exif_parser_test.cpp`, `tests/image_load_test.cpp` — 内容摘要
  - 日志宏: `src/log.h:5-6` — LOG_ERROR/LOG_INFO 定义
  - 常量: `src/config.h:3-16` — 所有 Config 常量
  - 图像加载器: `src/image_loader.h` — 检查向后兼容 include
  - Android 副本: `android/app/src/main/cpp/hdr_types.h` — 确认原始指针持续性
  - 项目 README: `README.md` — 检查一致性

  **Acceptance Criteria**:
  - [ ] 项目结构树包含全部 21 个 `src/` 文件（新增 config.h, log.h, tonemap.cpp/h, ui_panels.cpp/h）
  - [ ] `image_loader` 描述不再提及 "tone mapping"
  - [ ] `app` 描述提及对 ui_panels 的委托
  - [ ] 错误处理章节引用 `log.h` 宏，不引用 `fprintf`
  - [ ] 内存管理章节说明 `std::vector<uint8_t>`（桌面）+ Android 分歧
  - [ ] GLFW hints bug 警告已移除
  - [ ] 测试章节包含 `ctest` 命令
  - [ ] Common Pitfall #3 更新为平台分歧说明
  - [ ] 像素比较文件名已修正
  - [ ] 色调映射重复已记录
  - [ ] 章节标题有中文翻译
  - [ ] 文件总行数在 200-300 之间

  **QA Scenarios**:

  ```
  Scenario: Happy path — 所有 Must Have 条目均存在且正确
    Tool: Bash (read the file)
    Preconditions: skill 文件已写入 .opencode/skills/ultrahdr-viewer.md
    Steps:
      1. Read .opencode/skills/ultrahdr-viewer.md
      2. 验证项目结构树包含 config.h, log.h, tonemap.cpp, tonemap.h, ui_panels.cpp, ui_panels.h
      3. 验证 "tone mapping" 不在 image_loader 描述中
      4. 验证 LOG_ERROR 在错误处理章节中被引用
      5. 验证 "std::vector" 出现在内存管理章节中
      6. 验证 ctest 命令出现在测试章节中
      7. 验证 "ref_pixels.raw" 出现在像素比较章节中
      8. 验证 "this is a bug" 已被替换
      9. 验证 "Android" 和 "uint8_t*" 出现在 Common Pitfalls 中
      10. 验证中文翻译文本存在于章节标题中
    Expected Result: 所有 10 项检查通过
    Failure Indicators: 任何 Must Have 条目缺失或不正确
    Evidence: .sisyphus/evidence/task-1-verification.txt

  Scenario: Failure — 不修改任何代码文件
    Tool: Bash (git diff)
    Preconditions: skill 更新完成
    Steps:
      1. git diff --name-only
      2. 验证仅更改了 .sisyphus/plans/skill-ultrahdr-update.md, .opencode/skills/ultrahdr-viewer.md, .sisyphus/drafts/skill-update-ultrahdr-viewer.md
      3. 验证无 .cpp/.h 文件被修改
    Expected Result: 仅 markdown 文件被修改
    Failure Indicators: git diff 显示任何代码文件被修改
    Evidence: .sisyphus/evidence/task-1-no-code-changes.txt
  ```

  **Commit**: YES
  - Message: `docs(skill): update ultrahdr-viewer skill to reflect refactored codebase`
  - Files: `.opencode/skills/ultrahdr-viewer.md`

---

## Final Verification Wave

- [ ] F1. **Must Have 对照验证** — `quick`
  读取 `.opencode/skills/ultrahdr-viewer.md`，逐项对照 Must Have 清单。确认所有 10 个 Must Have 条目均已满足。报告通过/未通过。
  Output: `Must Have [N/10] | 缺失项: [...] | VERDICT: APPROVE/REJECT`

- [ ] F2. **README.md 一致性检查** — `quick`
  读取 `README.md` 并比较与 skill 文件的项目结构。记录差异（在 README 而非 skill 中）。如果 README.md 不在范围内，记录为"已知不一致"。
  Output: `Consistency [MATCH/N issues] | Differences: [...] | VERDICT: APPROVE/WARN`

---

## Commit Strategy

- **Task 1**: `docs(skill): update ultrahdr-viewer skill to reflect refactored codebase`
  - 文件: `.opencode/skills/ultrahdr-viewer.md`

---

## Success Criteria

### Verification Commands
```bash
# 验证 skill 文件存在且有效
Test-Path -LiteralPath ".opencode/skills/ultrahdr-viewer.md"
# Expected: True

# 验证没有代码文件被意外修改
git diff --name-only -- '*.cpp' '*.h'
# Expected: (no output)
```

### Final Checklist
- [ ] 全部 10 个 Must Have 条目已满足
- [ ] 无代码文件（.cpp/.h）被修改
- [ ] Skill 文件在 200-300 行之间
- [ ] README.md 差异已审计（或在范围外）
