# Draft: UltraHDR Viewer - Skill + Refactor Plan

## Requirements (confirmed)
- [项目已有完整实现]: 基于 Dear ImGui + GLFW + libultrahdr 的 Ultra HDR 图片浏览器
- [两者都需要]: 创建 OpenCode skill 文件 + 工作重构计划
- [代码重构与优化]: 主要目标是改进现有代码结构、性能优化、跨平台兼容性
- [Windows + VS2022]: 目标平台

## Technical Decisions
- [已确认]: 重构范围 = A(Bug修复)→B(架构拆分)→C(代码质量)→D(性能优化)，全部做
- [已确认]: TDD + Google Test，先搭建 Wave 0 测试基础设施
- [已确认]: 按 A→B→C→D 顺序，先保证正确性再优化性能

## Research Findings (代码审查发现)

### 架构问题
1. **app.cpp 过大 (456行)**: UI渲染逻辑(`renderUI/renderMenuBar/renderControlPanel/renderImagePanel/renderInfoPanel/renderAboutDialog`) 与业务逻辑耦合
2. **image_loader.cpp 职责混乱**: 文件对话框 + 图片加载 + 色调映射 混在一个文件
3. **exif_parser.cpp 重复逻辑**: GPS解析在 `parseIFD` 内嵌了完整逻辑，又有独立的 `parseGPSStrings` 函数(未被调用)
4. **缺少抽象层**: 没有清晰的 Model-View 分离，Application 类承担了所有职责

### 代码质量问题
5. **原始指针内存管理**: `ImageData::pixels` 使用 `new[]/delete[]`，应改用 `std::vector<uint8_t>` 或 `std::unique_ptr<uint8_t[]>`
6. **无统一错误处理**: 到处散落 `fprintf(stderr, ...)`，无日志框架
7. **硬编码魔法数字**: 窗口默认尺寸(1440x900)、面板宽度(290/280/320)、色调映射常量
8. **renderer_gl.cpp Bug**: OpenGL context hints 在窗口创建**之后**才设置（应该在glfwCreateWindow之前）

### 性能相关
9. **色调映射逐像素分支**: `toneMapImage` 内 switch(mode) 在循环内，应提到循环外
10. **重复的像素拷贝**: loadImageFile 中同时写入 HDR 和 SDR 缓冲区，做了两次逐像素循环
11. **无纹理缓存**: 每次重新加载都销毁再创建纹理，updateTexture 已存在但未充分利用

### 平台问题
12. **renderer_vk.cpp**: 未审查，但编译配置支持 Vulkan
13. **file_dialog.cpp**: 独立文件存在但 image_loader.cpp 中有重复的 openFileDialog 实现

## Confirmed Decisions (from user)
- [优先级]: A(Bug修复) → B(架构拆分) → C(代码质量) → D(性能优化)，全部做
- [测试策略]: TDD，引入 Google Test
- [Vulkan]: 保留并优化
- [平台]: 仅 Windows Desktop (VS2022)，Android 不做重构但头文件不能破坏编译

## Metis Review - Critical Findings

### 需要新增 Wave 0（Characterization + Test Infrastructure）
- 当前零测试，需要在重构前建立像素对比基线
- 用 MVIMG_20251123_200209.jpg 作为测试图片
- 需要先搭建 Google Test 框架

### 关键 Bug 补充
- **renderer_gl.cpp 的 GLFW hints bug 比我预想的严重**: hints 在 glfwCreateWindow 之后设置是**静默无效**的，窗口使用驱动默认的 GL 版本而非代码意图的 3.0
- **saveImageFile 功能残缺**: 既不用 file_dialog 也不保存可视格式，直接写 raw RGBA 字节
- **Vulkan updateTexture 性能问题**: 销毁+重建整个 GPU 资源，比 OpenGL 的 glTexSubImage2D 慢 ~100x

### Guardrails
1. **构造顺序不可变**: glfwInit → hints → createWindow → makeContext → renderer.init
2. **共享头文件冻结协议**: renderer.h/image_loader.h/exif_parser.h 的修改不能破坏 Android 编译
3. **Vulkan 句柄类型不变**: VkDevice/VkInstance 等保持原始指针
4. **不引入外部依赖**: 除 Google Test 外，无 spdlog/fmt/Boost
5. **CMake source list 同步**: 每新增 .cpp 文件必须更新 CMakeLists.txt

## Open Questions
- [已确认]: 重构优先级 = A→B→C→D 全部
- [已确认]: TDD + Google Test
- [已确认]: Vulkan 保留并优化
- [已确认]: 非 ASCII 路径问题 → 文档标注已知限制，不作为重构内容
- [已确认]: 异步加载 → 标记为 out of scope（后续功能请求）

## Scope Boundaries
- INCLUDE: 
  - Wave 0: Google Test 基础设施 + 像素对比基线
  - Wave A: Bug修复（GLFW hints、saveImageFile、parseGPSStrings 死代码、文件对话框重复）
  - Wave B: 架构拆分（app.cpp → 最多4个文件，image_loader 职责分离）
  - Wave C: 代码质量（ImageData::pixels→vector、fprintf→LOG_ERROR宏、常量提取）
  - Wave D: 性能优化（色调映射分支外提、像素拷贝合并、纹理复用）
  - OpenCode skill 文件 (.opencode/skills/ultrahdr-viewer.md)
- EXCLUDE:
  - 新功能（异步加载、HDR10/scRGB 显示、PNG/JPEG 保存、EXIF 方向旋转）
  - Android 端重构（仅保证头文件兼容性）
  - 多线程、多窗口支持
  - 第三方日志库（spdlog/fmt）
  - UTF-8 路径修复（标注为已知限制）
