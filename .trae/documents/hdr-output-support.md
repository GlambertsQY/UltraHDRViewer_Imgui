# HDR 显示输出支持（scRGB + HDR10）

## Context

此前修复了 HDR 解码（真 HDR 数据，1.0 = 203 nits，高光 >1.0），但显示链路只能输出色调映射后的 SDR（OpenGL GL_RGBA8 / Vulkan B8G8R8A8_UNORM swapchain）。用户要求在 HDR 显示器上能输出真 HDR 画面；开发机为 SDR 屏，需保证优雅回退且 SDR 行为完全不变。

**决策（用户已确认）**：支持 scRGB（R16G16B16A16_SFLOAT + EXTENDED_SRGB_LINEAR）与 HDR10（A2B10G10R10 + ST2084，PQ 编码）；单一"HDR 输出"开关，默认关，优先 scRGB 回退 HDR10；OpenGL 后端不支持（Windows WGL 无 HDR swapchain 机制），仅留 stub。

## 数据约定

HDRData 线性 float RGBA，1.0 = 203 nits。scRGB 输出 × 203/80（scRGB 1.0=80nits）；HDR10 输出 ×203 得 nits → BT.709→BT.2020 → PQ(÷10000) 编码。

## 任务步骤

### 1. 基础设施（无 Vulkan 依赖，可单测）

- **新建 `src/half_float.h`**：`halfToFloat()` 从 [image_loader.cpp](file:///d:/Projects/UltraHDRViewer_Imgui/src/image_loader.cpp#L18-L38) 移入（inline），新增 `floatToHalf()`。image_loader.cpp 改为 include 此头。
- **新建 `src/colorspace.h`**（全 inline）：`srgbToLinear()`、`pqEncode()`/`pqDecode()`（ST2084 常量 m1=0.1593017578125、m2=78.84375、c1=0.8359375、c2=18.8515625、c3=18.6875）、BT.709→BT.2020 矩阵（BT.2087 标准值 0.6274039/0.3292830/0.0433...，行和=1）、常量 `HDR_WHITE_NITS=203`、`SCRGB_WHITE_NITS=80`、`SCRGB_SCALE=2.5375`。
- **新建 `tests/colorspace_test.cpp`**：锚点断言（见"验证"节）。GLOB 自动收录，无需改 tests/CMakeLists.txt。

### 2. 渲染器接口层（`src/renderer.h`）

- 将 `HDRData` 从 image_loader.h 移入（image_loader.h 保留 `#include "renderer.h"`，现有代码不破）。
- 新增 `enum class HdrMode { Off, ScRGB, HDR10 };` 及虚函数：
  ```cpp
  virtual bool hdrOutputSupported() const = 0;
  virtual bool setHdrOutput(bool enable) = 0;   // 返回实际是否进入 HDR
  virtual HdrMode activeHdrMode() const = 0;     // Off=未激活
  virtual void* createTextureHDR(const HDRData& image) = 0;
  virtual void updateTextureHDR(void* texture, const HDRData& image) = 0;
  virtual void setHdrDrawRect(float x0, float y0, float x1, float y1) = 0; // 帧缓冲像素
  ```
- `renderer_gl.h/.cpp`：全部 stub（false / nullptr / 空操作）。

### 3. Vulkan 探测（`src/renderer_vk.cpp`）

- `createInstance`（cpp:95-139）：用 `vkEnumerateInstanceExtensionProperties` 探测后按需追加 `VK_EXT_swapchain_colorspace`（不探测直接加会导致老驱动启动失败）。
- `createSwapchain`（cpp:207-256）：提取 `querySurfaceFormats()`，置 `m_hdrScRGBSupported/m_hdr10Supported`；`chooseSurfaceFormat()` 按 `m_hdrMode` 选 (R16G16B16A16_SFLOAT, EXTENDED_SRGB_LINEAR) / (A2B10G10R10_UNORM_PACK32, HDR10_ST2084) / 现有 SDR 对；目标格式消失（换屏）时自动回落 SDR 并置 `m_hdrMode=Off`。

### 4. Vulkan HDR 资源（`src/renderer_vk.h/.cpp`）

- **新成员**（renderer_vk.h）：支持标志、`HdrMode m_hdrMode`、HDR 描述符集布局/管线布局/管线、`m_hdrDrawRect[4]+valid`、HdrPush 结构（rectMin/rectMax/fbSize/mode/whiteNits/refWhite，约 40B push constants）。
- **Shader**：新建 `src/shaders/hdr_quad.vert`（无顶点输入 6 顶点，push constant 像素矩形→NDC，映射同 ImGui `px/fb*2-1` 无 Y 翻转——已核实 imgui_impl_vulkan.cpp:466-471 视口为正高度）与 `hdr_quad.frag`（mode 0: ×whiteNits/refWhite；mode 1: ×whiteNits→矩阵→PQ，alpha 强制 1.0）。用本机 `C:\dev\VulkanSDK\1.4.357.0\Bin\glslc.exe -mfmt=c` 离线编译为 uint32 数组头文件提交（imgui_impl_vulkan 同款模式），构建期不依赖 glslc。
- **管线**：triangle-list、无 vertex input、viewport/scissor 为 dynamic state、关闭 blending、renderPass=m_renderPass。
- **HDR 纹理**：泛化 `uploadTextureData(tex, data, w, h, fmt, byteSize)`（R8 路径变薄封装不破）；`createTextureHDR` 做 float→half 后以 `VK_FORMAT_R16G16B16A16_SFLOAT`、w*h*8 字节上传；描述符集不走 ImGui_ImplVulkan_AddTexture，用自有 `m_hdrDescSetLayout`（binding 0, COMBINED_IMAGE_SAMPLER, FRAGMENT）从现有 m_descriptorPool 分配+vkUpdateDescriptorSets，销毁时 vkFreeDescriptorSets。顺带修复：现有 `destroyTexture` 补 `ImGui_ImplVulkan_RemoveTexture`（SDR 描述符集泄漏）。
- **setHdrOutput 重建序列**（契约：app 先销毁自家纹理）：
  ```
  vkDeviceWaitIdle → 毁 HDR 纹理 → ImGui_ImplVulkan_Shutdown →
  cleanupSwapchain → vkDestroyRenderPass → createSwapchain(按 m_hdrMode) →
  createRenderPass → createFrameResources/CommandBuffers/SyncObjects →
  (HDR? createHdrPipeline : destroyHdrPipeline) → ImGui_ImplVulkan_Init(新 renderPass)
  ```
  仅 resize 的 recreateSwapchain 不动 render pass/管线（格式不变）。
- **render() 录制点**：BeginRenderPass（cpp:448）后、RenderDrawData（cpp:450）前：若 HDR 激活且 rect 有效且有 HDR 纹理 → bind 管线/动态 viewport+scissor/push constants/描述符集 → `vkCmdDraw(6)`（先画图像，ImGui UI 后叠加）。

### 5. App/UI 接线

- **app.h**：`m_hdrOutputWanted`、`m_hdrTexture`、`m_savedStyle`（ImVec4 数组）；`onHdrToggle(bool)`、`bakeHdrStyle()/restoreStyle()`。
- **app.cpp**：
  - `onHdrToggle(true)`：前置 `m_hdrData && hdrOutputSupported()`；先销毁 SDR 纹理 → `setHdrOutput(true)` → 成功则 `createTextureHDR` + 烘焙 style（scRGB: `srgbToLinear(c)×2.5375`；HDR10: `pqEncode(M·(srgbToLinear(c)×203))`，alpha 不动）；失败回滚。关闭路径对称。
  - `renderImagePanel`：HDR 激活时跳过棋盘格与 `AddImage`，改为 `setHdrDrawRect(ip, ie)`（乘 `ImGuiIO::DisplayFramebufferScale`）。
  - `run()` 每帧对账：`m_hdrOutputWanted && activeHdrMode()==Off`（换屏回落）→ 清标志、恢复 style、重建 SDR 纹理。
  - HDR 激活时 `loadImage`/`reapplyToneMapping` 走 `createTextureHDR/updateTextureHDR`。
- **ui_panels.h/.cpp**：`renderMenuBar` View 菜单加 "HDR 输出" 勾选项（`enabled = hdrSupported && hasHDR`），状态区显示 `scRGB / HDR10 / HDR不可用`；签名追加参数与 `std::function<void(bool)>` 回调。`renderControlPanel`：HDR 激活时禁用色调映射组并提示。

### 6. 顺带修复（本任务触到的既有问题）

- `destroyTexture` 补 `ImGui_ImplVulkan_RemoveTexture`（描述符集泄漏）。

## 不做什么

- 不改 OpenGL 后端 HDR（WGL 无机制）；不改 Android（EGL HDR 是另一课题，另行处理）；不 patch deps/imgui shader（用 style 烘焙近似）；不实现 Windows SDR 白电平滑块读取（用 203nits 常量，留调整口）。

## 风险与对策

1. **ImGui 持有旧 render pass** → 换格式必须 Shutdown+Init，已列入重建序列；app 先毁纹理。
2. **NDC Y 方向** → 复用 ImGui 正高度视口映射，已核实，勿套用全屏三角形惯写法。
3. **实例扩展兼容性** → 探测后按需启用。
4. **HDR10 UI 混合失真** → style 烘焙为逐顶点近似，字体边缘轻微光晕可接受；HDR 模式跳过棋盘格（其 IM_COL32 硬编码色不经 style）。
5. **显示器热切换** → 每次重建重查格式，失效自动回落 + app 每帧对账。
6. **FP16 纹理内存翻倍** → 复用现有 maxImageDimension2D 检查。

## 验证

**单测**（tests/colorspace_test.cpp + 现有测试）：
- `srgbToLinear(0.04045)≈0.0031308`、`srgbToLinear(0.5)≈0.2140411`
- `floatToHalf(1.0)=0x3C00`、全 65535 half 值 round-trip
- `pqEncode(0)=0`、`pqEncode(1000)≈0.751827`、`pqEncode(10000)=1.0`（1e-3 容差）、`pqDecode∘pqEncode` 恒等
- BT.709→2020 矩阵行和=1、白点恒等、六基色映射分量≥0
- 现有 8 个测试保持通过（2 个缺素材失败为存量问题）

**SDR 开发机回归**：
```bat
cmake --preset vs2026-vulkan
cmake --build out/build/vs2026-vulkan --config Debug
ctest --test-dir out/build/vs2026-vulkan -C Debug --output-on-failure
out\build\vs2026-vulkan\bin\Debug\UltraHDRViewer.exe --vulkan synthetic_uhdr_test.jpg
```
- 日志显示 HDR 不可用、菜单项灰色、SDR 画面与之前一致（RenderDoc 抓帧：swapchain 仍 B8G8R8A8+SRGB_NONLINEAR）
- vs2026-opengl 构建+运行行为完全不变

**HDR 屏人工清单**（留给有 HDR 屏时执行）：
1. Windows 开 HDR → 启动日志显示 scRGB 可用
2. 开"HDR 输出"→ 高光细节可见、UI 亮度正常、状态显示 scRGB
3. 关回 SDR 无残留；窗口拖到 SDR 屏自动回落
4. （可选）临时注释 scRGB 优先级验证 HDR10 路径：白 203nits ≈ PQ 0.58

## 关键文件

- `src/renderer_vk.cpp` / `src/renderer_vk.h`（主体）
- `src/renderer.h`（接口 + HDRData）
- `src/app.cpp` / `src/app.h`（状态与流程）
- `src/ui_panels.cpp` / `src/ui_panels.h`（菜单）
- `src/colorspace.h` / `src/half_float.h`（新建，可测）
- `src/shaders/hdr_quad.vert/.frag` + 生成的 SPIR-V 头（新建）
- `tests/colorspace_test.cpp`（新建）
- `src/renderer_gl.h/.cpp`（stub）、`src/image_loader.h/.cpp`（HDRData 移出、halfToFloat 移出）
