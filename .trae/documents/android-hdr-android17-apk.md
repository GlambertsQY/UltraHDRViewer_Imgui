# Android 端功能完善 + HDR 直显 + Android 17 APK 输出

## 1. 目标与范围

1. 修复 Android 端 HDR 解码（当前只解出 SDR，和桌面端已修复的假 HDR bug 同源）。
2. 新增 HDR10 直显：EGL 10-bit 窗口 Surface + BT.2020/PQ，FP16 纹理 + PQ 着色器，HDR 高光不再被色调映射压平。
3. 补齐 Android 基础功能：触摸交互（点击/拖动/双指缩放/双击）、SAF 文件打开、分享/查看 Intent、屏幕密度适配、横竖屏自适应、前后台生命周期、渲染线程。
4. 升级到 Android 17（compileSdk/targetSdk 37）、AGP 8.9.2 / Gradle 8.11.1 / JDK 17 / NDK r28，满足 16 KB page size 要求。
5. 在本机自动安装缺失的 JDK/SDK/NDK，并输出可安装 APK（debug），在 HDR 真机上验证。

明确不做：Vulkan Android 后端、Dolby Vision/HLG 输出、图片保存/导出、桌面端行为改动。

---

## 2. 现状分析（已核实）

### 2.1 解码仍是假 HDR

`android/app/src/main/cpp/image_loader_android.cpp#L89-L131`：
- 使用 `UHDR_IMG_FMT_32bppRGBA8888` + `UHDR_CT_SRGB`，libultrahdr 只返回 SDR rendition，增益图不生效。
- 随后把 8-bit SDR 除以 255 塞进 `HDRData`，HDR 最大值恒 ≤ 1.0，后续 HDR 输出/色调映射都没有真实高光数据。
- 桌面端已在 `src/image_loader.cpp#L59-L113` 改为 `UHDR_IMG_FMT_64bppRGBAHalfFloat` + `UHDR_CT_LINEAR`，这是本任务必须移植的修复。

### 2.2 Android 原生层不可用/不完整

`android/app/src/main/cpp/native.cpp`：
- 基于 SDL2 + `SDL_CreateWindowFrom(surface)`，SDL 在非 SDLActivity 下拿不到 Android 触摸事件；当前没有任何触摸转发，UI 实际不可交互。
- `mainLoop()` 在 `nativeOnResume` 里直接跑在 Java UI 线程上（`native.cpp#L352-L358`），会阻塞 Looper，触摸/生命周期事件无法处理。
- 没有 HDR 输出路径，纹理固定 RGBA8。
- 没有屏幕密度缩放，ImGui 字体在手机上过小。

### 2.3 Java 层功能缺口

`android/app/src/main/java/com/uhdrviewer/MainActivity.java`：
- 用 `MediaStore.Images.Media.DATA` 取路径（`L85-L97`），Android 10+ 分区存储下通常返回 null，打开图片会失败。
- 没有 `ACTION_VIEW` / `ACTION_SEND` Intent 处理，README 声称的“从文件管理器/分享打开”不成立。
- 没有触摸事件转发、没有 HDR 能力查询、没有沉浸式全屏/刘海适配。

### 2.4 构建配置过旧

- `android/build.gradle`：AGP 7.4.2、compileSdk/targetSdk 34。
- `android/app/build.gradle`：NDK 23.1.7779620、Java 11、四个 ABI。
- `android/gradle/wrapper/gradle-wrapper.properties`：Gradle 7.6；仓库里还有一个 130 MB 的 `android/gradle/gradle-8.4-bin.zip`。
- `AndroidManifest.xml`：`package` 属性（AGP 8 需移除）、强制 `userLandscape`、声明已不需要的存储权限。
- 本机未安装 JDK / Android SDK / NDK / adb（已确认），但 `dl.google.com` 与 Adoptium 网络可达。

### 2.5 Android 17 相关约束（已查官方文档）

- Android 17 = API 37，需 `compileSdk=37`、`targetSdk=37`，AGP ≥ 8.9.0-rc01。
- targetSdk 35+ 必须支持 16 KB page size；官方建议 AGP ≥ 8.5.1 + NDK r28+。
- Android 17 对大屏（sw ≥ 600dp）忽略应用的方向/可调整大小限制，UI 必须横竖屏自适应。
- 本应用不涉及 RemoteViews、MessageQueue 反射、网络/音频/短信，Android 17 行为变更无额外阻断项。

---

## 3. 技术决策（已与用户确认）

| 决策 | 选择 |
|---|---|
| HDR 路线 | EGL + GLES HDR10（BT.2020/PQ，10-bit Surface），不移植 Vulkan |
| Android 窗口/输入 | 移除 Android 端 SDL2，改用原生 EGL + Java 触摸转发 + 渲染线程 |
| 构建环境 | 自动安装 JDK 17、Android SDK 37、NDK r28、CMake 到项目内 `.tools/` |
| 验证设备 | 用户有 HDR 真机，安装 APK 做运行验证 |
| 最低版本 | minSdk 24（HDR 能力运行时探测，不支持时自动回退 SDR） |
| 输出物 | `android/app/build/outputs/apk/debug/app-debug.apk`（可直接安装） |

版本矩阵：
- JDK 17（Temurin）
- AGP 9.1.1，Gradle 9.3.1（AGP 9.1 是官方支持 API 37 的最低稳定版本）
- compileSdk / targetSdk 37，SDK Platform `platforms/android-37.0`，Build-Tools 36.0.0/37.0.0
- NDK 28.2.13676358，CMake 3.22.1
- ABI：`arm64-v8a`、`x86_64`（移除 32 位 ABI，现代 HDR 设备均为 64 位）

依赖补丁（`deps/` 为 clone 生成，补丁已写入 `clone_deps.bat`）：
- `deps/turbojpeg`：允许 `TURBOJPEG_IS_SUBPROJECT` 时通过 `add_subdirectory()` 集成。
- `deps/libultrahdr`：支持 `JPEG_PREBUILT_LIB` / `JPEG_PREBUILT_INCLUDE`，避免 Android 端触发 ExternalProject 或 `find_package(JPEG)` 失败。

---

## 4. 变更清单

### 4.1 构建与工程配置

#### `android/build.gradle`
- AGP 版本改为 `9.1.1`。
- `ext`：`compileSdkVersion = 37`、`targetSdkVersion = 37`、`minSdkVersion = 24`，新增 `ndkVersion = "28.2.13676358"`。

#### `android/app/build.gradle`
- `ndkVersion rootProject.ext.ndkVersion`。
- `compileOptions` 改为 Java 17。
- `abiFilters` 改为 `'arm64-v8a', 'x86_64'`。
- CMake 版本保持 `3.22.1`。
- 保留 `minifyEnabled true`，配合 `proguard-rules.pro` 保留 JNI 方法。

#### `android/gradle/wrapper/gradle-wrapper.properties`
- `distributionUrl` 改为 `gradle-9.3.1-bin.zip`。

#### `android/gradle/gradle-8.4-bin.zip`
- 删除该 130 MB 过时离线包（已被 `.gitignore` 排除构建产物，不由构建使用）。

#### `android/gradle.properties`
- 增加 `org.gradle.jvmargs=-Xmx4096m -Dfile.encoding=UTF-8`。

#### `android/app/src/main/AndroidManifest.xml`
- 移除 `package="com.uhdrviewer"`（改用 namespace）。
- 移除 `READ_EXTERNAL_STORAGE` / `READ_MEDIA_IMAGES`（改用 SAF，无需权限）。
- `application` 增加 `android:largeHeap="true"`（HDR float 数据占用较大）。
- `activity`：
  - `android:screenOrientation` 改为 `fullUser`，`configChanges` 增加 `screenLayout|smallestScreenSize|density|uiMode`。
  - 保留 `exported="true"`。
  - 增加 `ACTION_VIEW`（`image/*`）与 `ACTION_SEND`（`image/*`）intent-filter。
  - `launchMode="singleTop"`，配合 `onNewIntent`。

#### `android/app/proguard-rules.pro`
- 增加 `-keepclasseswithmembernames class * { native <methods>; }` 与 `MainActivity` 保留规则。

#### `.gitignore`
- 增加 `.tools/`（本机工具链目录）。

#### 新增 `android/setup_android_sdk.ps1`
- 解析 `https://dl.google.com/android/repository/repository2-3.xml` 获取最新 `commandlinetools-win` 包并解压到 `.tools/android-sdk/cmdline-tools/latest`。
- 下载并解压 Temurin JDK 17 到 `.tools/jdk-17`。
- `sdkmanager` 安装：`platform-tools`、`platforms;android-37`、`build-tools;37.0.0`、`ndk;28.2.13676358`、`cmake;3.22.1`，并接受 licenses。
- 生成 `android/local.properties`（`sdk.dir` 指向 `.tools/android-sdk`，并写入 `ndk.dir`）。
- 输出后续构建命令。脚本可重复执行（已存在则跳过）。

### 4.2 Java 层

#### 重写 `android/app/src/main/java/com/uhdrviewer/MainActivity.java`
- SurfaceView 保持全屏，`setOnTouchListener` 转发所有 `MotionEvent` 到 native（坐标用原始像素）。
- `onCreate`：
  - 沉浸式全屏：API 30+ 用 `Window.setDecorFitsSystemWindows(false)` + `WindowInsetsController.hide(statusBars|navigationBars)`；低版本用 `SYSTEM_UI_FLAG_IMMERSIVE_STICKY` 组合。
  - 读取 `Display.getHdrCapabilities()`：`HDR_TYPE_HDR10` 是否存在、`getDesiredMaxLuminance()`，传给 native。
  - 读取 `resources.displayMetrics.density`，传给 native。
  - 处理启动 Intent（`ACTION_VIEW` / `ACTION_SEND`）。
- 文件打开：
  - `ACTION_OPEN_DOCUMENT` + `CATEGORY_OPENABLE`、`type="image/*"`。
  - 通过 `ContentResolver.openInputStream(uri)` 复制到 `getCacheDir()/import_<ts>.<ext>`，再把本地路径传给 native；彻底规避 `MediaStore.DATA`。
- `onNewIntent`：复用同一 URI 导入流程。
- 每 100 ms（仅 resumed 状态）轮询 `nativeConsumeOpenRequest(handle)`，为 native UI 的 “Open Image” 按钮触发系统文件选择器（避免从渲染线程直接 `startActivity`）。
- 生命周期：`surfaceCreated` 创建 native 实例并启动渲染线程；`surfaceDestroyed` 停止并释放；`onPause/onResume` 只设置暂停标志；`onDestroy` 释放。
- `nativeOnCreate` 新签名：`(Surface, width, height, density, hdr10Supported, maxLuminanceNits)`。
- 新增 native 声明：`nativeOnTouch(handle, action, pointerCount, float[] xs, float[] ys)`、`nativeConsumeOpenRequest(handle)`。

### 4.3 Native 层

#### `android/app/src/main/cpp/image_loader_android.cpp`（修复真 HDR 解码）
- 删除 `<jni.h>` 依赖（该文件不使用 JNI，便于主机端单测）。
- 解码参数改为：
  - `uhdr_dec_set_out_img_format(dec, UHDR_IMG_FMT_64bppRGBAHalfFloat)`
  - `uhdr_dec_set_out_color_transfer(dec, UHDR_CT_LINEAR)`
- 校验 `decoded->fmt`；按行把 half 转 float，写入 `HDRData`（处理 stride padding）。
- 新增静态 `halfToFloat()`（与桌面 `src/half_float.h` 逻辑一致，Android 保持独立）。
- SDR 显示图改为由真 HDR 数据调用现有 `applyToneMap(..., DEFAULT_EXPOSURE, DEFAULT_GAMMA, 0)` 生成。
- 计算 `HDRData::maxNits`（`max(r,g,b) * 203`），供 HDR 元数据使用。
- 非 Ultra HDR 仍回退 `loadRegularImage()`（SDR only，无 HDR 纹理）。

#### `android/app/src/main/cpp/hdr_types.h`
- `HDRData` 增加 `float maxNits = 0.0f;`。
- 其余结构不变（`ImageData` 已是 `std::vector`）。

#### 新增 `android/app/src/main/cpp/egl_context.h` / `egl_context.cpp`
职责：EGL 初始化、HDR/SDR 配置选择、Surface 创建/重建、能力探测。
- `init(ANativeWindow*, bool hdrRequested, float maxLuminanceNits)`：
  - `eglGetDisplay(EGL_DEFAULT_DISPLAY)`、`eglInitialize`、`eglBindAPI(EGL_OPENGL_ES_API)`。
  - 查询 `EGL_EXTENSIONS`：`EGL_EXT_gl_colorspace_bt2020_pq`、`EGL_EXT_surface_SMPTE2086_metadata`、`EGL_EXT_surface_CTA861_3_metadata`。
  - HDR 配置优先：`EGL_RED_SIZE 10 / GREEN 10 / BLUE 10 / ALPHA 2`、`EGL_COLOR_COMPONENT_TYPE_EXT=FIXED`、`EGL_RENDERABLE_TYPE=EGL_OPENGL_ES3_BIT`。
  - HDR 不可用或 10-bit 配置为空 → SDR 8-bit 配置，`hdrActive=false`。
  - `eglCreateWindowSurface` 时 HDR 附加 `EGL_GL_COLORSPACE_KHR = EGL_GL_COLORSPACE_BT2020_PQ_EXT`。
  - 创建 GLES3 上下文（`EGL_CONTEXT_CLIENT_VERSION=3`），`eglMakeCurrent`。
  - HDR 激活时用 `eglSurfaceAttrib` 设置 BT.2020 原色/D65 白点、`EGL_SMPTE2086_MAX_LUMINANCE_EXT`（显示峰值亮度）、`EGL_CTA861_3_MAX_CONTENT_LIGHT_LEVEL_EXT`（内容峰值 = min(maxContentNits, display max)）；扩展缺失时跳过并记录日志。
- 提供 `recreateSurface(ANativeWindow*)`、`destroy()`、`swapBuffers()`、`isHdrActive()`、`hdrSupported()`。
- 所有 EGL/GL 调用只在渲染线程执行。

#### 新增 `android/app/src/main/cpp/hdr_gles.h` / `hdr_gles.cpp`
职责：HDR 图像绘制与 ImGui 颜色烘焙。
- 内嵌 GLSL ES 3.0 顶点/片元着色器（字符串常量，不新增 shader 编译步骤）：
  - 顶点：像素矩形 → NDC，UV 以图像左上角为原点。
  - 片元：`linear709 = texture(...).rgb * exposure` → 乘 203 nits → BT.709→BT.2020 矩阵 → `/10000` → ST2084 PQ 编码 → 输出。
- 管理 RGBA16F 纹理、VAO/VBO、program、uniform 位置；提供 `createTexture/updateTexture/draw(x0,y0,x1,y1,viewportW,H,exposure)`。
- 提供 ImGui SDR 离屏 FBO：ImGui 先渲染到 RGBA8、透明背景的 FBO，再用全屏着色器逐像素 `sRGB → BT.2020 → PQ（UI 白点 400 nits）` 合成到 HDR 画面，避免在 PQ 空间做颜色混合导致 UI 发灰/不饱和。

#### 重写 `android/app/src/main/cpp/native.cpp`
- 移除所有 SDL include/调用/`imgui_impl_sdl2`。
- 结构：
  - `AndroidApp`：EGL 状态、渲染线程、`std::atomic<bool> running/paused`、待加载结果（mutex 保护）、纹理句柄、SDR/HDR 尺寸、UI 状态（zoom/offset/fit/exposure/gamma/mode）、HDR 开关与能力、密度。
  - JNI：`nativeOnCreate`、`nativeOnSurfaceChanged`、`nativeOnResume`、`nativeOnPause`、`nativeOnDestroy`、`nativeOnLoadImage`、`nativeOnTouch`、`nativeConsumeOpenRequest`。
- 渲染线程：
  - 初始化 EGL → 创建 ImGui context → `ImGui_ImplOpenGL3_Init("#version 300 es")`。
  - 按密度加载字体：`ImFontConfig.SizePixels = 13 * density`，`style.ScaleAllSizes(density)`。
  - 每帧：`eglMakeCurrent` → `glViewport` → clear → （HDR 激活且有待绘制矩形时用 `hdr_gles` 画 HDR quad）→ `ImGui_ImplOpenGL3_NewFrame/ImGui::NewFrame` → UI → `ImGui::Render` + `RenderDrawData` → `eglSwapBuffers`。
  - 空闲时（无输入/无动画）限制到约 30 FPS，拖动/滑动期间不限制，降低功耗。实现：记录脏标志 + `std::this_thread::sleep_for`。
  - 收到暂停/重建 Surface 时释放 EGLSurface 并重建。
- 图像加载：
  - `nativeOnLoadImage` 启动后台线程读取/解码（`loadImageFile` → 失败则 `loadRegularImage`），结果放入待上传槽位；渲染线程在下一帧上传纹理，避免阻塞 UI 线程。
  - 纹理：SDR 用现有 tone map 后的 RGBA8；HDR 用 `HDRData` 上传 RGBA16F。
- 触摸映射（`nativeOnTouch`）：
  - 单指 DOWN/MOVE/UP → `io.AddMousePosEvent` + `AddMouseButtonEvent(0, ...)`；在图像区域内拖动时平移（`offsetX/Y`，`fitToWindow=false`）。
  - 双指 MOVE → 按两指距离比例调整 `zoom`（围绕双指中心），同时发送鼠标松开事件避免误触按钮。
  - 单击（位移 < 阈值、时长 < 250 ms）：无图时请求打开；双击（300 ms 内）→ 重置为 fit。
  - OPEN_REQUEST 原子标志供 Java 轮询。
- UI（沿用现有面板内容并扩展）：
  - 横屏：左侧控制面板（宽度 `min(280, width*0.32)`）；竖屏：控制面板放在底部（高度 `min(280, height*0.38)`），图像区域占据剩余空间。
  - 控件：Open Image、HDR Output 勾选（`hdrSupported && hdrData`）、色调映射模式/曝光/gamma（HDR 激活时禁用 gamma、提示“HDR 直显”）、Fit/Zoom/Reset、EXIF、状态栏（分辨率、SDR/HDR、HDR10、最大亮度）。
  - HDR 激活时图像窗口加 `ImGuiWindowFlags_NoBackground`，跳过棋盘格与 `AddImage`，改由 `hdr_gles` 绘制；SDR 时保持 `AddImage`。
  - HDR 开关切换时：销毁/创建 HDR 纹理、烘焙/恢复 ImGui 样式；失败自动回退 SDR。

#### `android/app/src/main/cpp/CMakeLists.txt`
- 移除 SDL2 `add_subdirectory`、SDL include/link 与 `imgui_impl_sdl2.cpp`。
- `imgui_lib` 保留 `imgui_impl_opengl3.cpp`，定义 `IMGUI_IMPL_OPENGL_ES3`。
- `APP_SOURCES` 增加 `egl_context.cpp`、`hdr_gles.cpp`。
- 链接 `EGL`、`GLESv3`、`android`、`log`、`uhdr`、`dl`。
- 为所有目标添加 `-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384`（NDK r28 已默认对齐，显式声明以防回退）。
- 设置 `CMAKE_POLICY_VERSION_MINIMUM 3.5`（libjpeg-turbo 3.0.1 旧 CMake 策略）。
- 删除 Android CMake 内的 GTest `FetchContent` 与 `uhdrviewer_android_test` 目标（交叉编译产物无法在主机运行，且会拖慢每次 Gradle 构建）；测试改由桌面 CMake 的独立主机测试目标承担（见 4.4）。

### 4.4 测试与文档

#### 新增 `tests/android_loader_test.cpp` + 修改 `tests/CMakeLists.txt`
- 新增独立可执行目标 `AndroidLoaderTests`（避免与桌面同名符号冲突）：
  - 源文件：`tests/android_loader_test.cpp`、`../android/app/src/main/cpp/image_loader_android.cpp`、`../android/app/src/main/cpp/exif_parser_android.cpp`、`../android/app/src/main/cpp/stb_image_impl.cpp`、`../android/app/src/main/cpp/tonemap_android_test.cpp`、`../android/app/src/main/cpp/exif_android_test.cpp`。
  - 链接 `GTest::gtest_main`、`uhdr`；include Android cpp 目录、`UHDR_DIR`、`DEPS_DIR`。
- 新测试 `AndroidLoaderTest.TrueHDRDecode`：
  - 若 `synthetic_uhdr_test.jpg` 不存在则 `GTEST_SKIP()`（提示先运行 `HdrRoundTripTest`）。
  - 断言 `loadImageFile` 成功、`outHDR != nullptr`、HDR 最大分量 > 1.0（回归假 HDR bug）、`maxNits > 203`、SDR 输出像素数正确。
- 保留并复用现有 Android tone map/EXIF 测试文件。

#### `README.md`
- 更新 Android 构建章节：JDK 17、SDK 37、NDK r28、`setup_android_sdk.ps1`、`gradlew assembleDebug`、APK 路径。
- 更新功能列表：Android HDR10 直显、触摸手势、SAF 打开、Android 17 支持。
- 更新项目结构：移除“Android via SDL2”的描述，补充 `egl_context`、`hdr_gles`。

---

## 5. 关键实现细节（执行时按此实现，不再做选择）

### 5.1 EGL 配置与 HDR 判定顺序

1. 查询 EGL 扩展字符串；`bt2020_pq` 与 10-bit 配置必须同时可用才尝试 HDR。
2. Java 侧 `HdrCapabilities` 必须包含 `HDR_TYPE_HDR10`，否则直接 SDR。
3. 优先创建 HDR Surface；`eglCreateWindowSurface` 或 `eglMakeCurrent` 失败 → 销毁并以 SDR 配置重建，`hdrActive=false`。
4. HDR 元数据设置失败只记录警告，不降级（surface 仍为 PQ）。
5. 日志统一使用 `__android_log_print`，tag `UHDRViewer`，打印扩展列表、配置位深、HDR 激活结果、最大亮度。

### 5.2 HDR 色彩数学

- `HDRData`：线性 BT.709，1.0 = 203 nits。
- 输出：`nits = rgb * 203 * exposure` → BT.709→BT.2020 → `pqEncode(nits/10000)`。
- UI：ImGui 先渲染到 SDR RGBA8 离屏缓冲；合成阶段逐像素 `sRGB → linear ×400 nits → BT.2020 → PQ`，保留 SDR 的混色与饱和度观感。
- 曝光滑杆在 HDR 模式下只作用于 shader uniform，不重跑 CPU tone map。

### 5.3 内存与性能

- HDR float 数据约为 `w*h*16` 字节；`largeHeap` 已开启。
- 纹理上传在渲染线程；解码在后台线程；两者通过 mutex + swap 交接，避免 UI 卡顿。
- 纹理上传后释放/保留 HDRData 的决策：保留（切换 HDR 开关/重新加载需要），但重新加载新图时先释放旧数据。

### 5.4 Android 17 适配

- targetSdk 37、compileSdk 37。
- 大屏忽略方向锁定 → 不依赖固定横屏，UI 自适应。
- Edge-to-edge 默认开启 → 使用沉浸式全屏并让 SurfaceView 全屏绘制，UI 内边距按 density 留白。
- 16 KB page size：NDK r28 + AGP 8.9.2 + 显式链接参数。

---

## 6. 实施顺序

### Wave 1：工具链与构建配置
1. 编写并执行 `android/setup_android_sdk.ps1`，安装 JDK/SDK/NDK/CMake，生成 `local.properties`。
2. 升级 Gradle/AGP/SDK/NDK 配置，删除过时 Gradle 包，更新 manifest/proguard/gitignore。
3. 先跑一次 `gradlew assembleDebug`，记录升级后基线错误（预期来自 NDK/CMake 与旧代码）。

### Wave 2：解码修复 + 主机回归测试
4. 修复 `image_loader_android.cpp`，增加 `HDRData::maxNits`。
5. 增加 `AndroidLoaderTests` 并运行，确认 HDR max > 1.0。

### Wave 3：Native 渲染重写
6. 新增 `egl_context.*`、`hdr_gles.*`。
7. 重写 `native.cpp`（渲染线程、EGL/GLES、HDR UI、触摸）。
8. 更新 Android `CMakeLists.txt`，移除 SDL/GTest。

### Wave 4：Java 层完善
9. 重写 `MainActivity.java`（触摸、SAF、Intent、HDR 能力、沉浸式、轮询）。
10. 更新 manifest（Intent filter、权限、方向、largeHeap）。

### Wave 5：构建与验证
11. `gradlew assembleDebug` 产出 APK。
12. 静态校验：targetSdk 37、16 KB 对齐、ABI、SO 存在。
13. 真机安装，按第 7 节清单验证 HDR/交互/文件打开/生命周期。

### Wave 6：文档收尾
14. 更新 README；清理临时日志/构建产物说明。

---

## 7. 验证清单

### 7.1 构建与产物

```powershell
cd android
.\gradlew.bat assembleDebug
# 产物：android/app/build/outputs/apk/debug/app-debug.apk
```

```powershell
# targetSdk / compileSdk / native libs
.tools\android-sdk\build-tools\37.0.0\aapt2.exe dump badging app\build\outputs\apk\debug\app-debug.apk

# 16 KB zip 对齐
.tools\android-sdk\build-tools\37.0.0\zipalign.exe -c -P 16 -v 4 app\build\outputs\apk\debug\app-debug.apk

# SO 段对齐（每个 LOAD 应为 2**14）
.tools\android-sdk\ndk\28.2.13676358\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-objdump.exe -p lib\arm64-v8a\libuhdrviewer.so | Select-String LOAD
```

### 7.2 主机单元测试

```powershell
cmake --preset vs2026-opengl
cmake --build out/build/vs2026-opengl --config Debug
ctest --test-dir out/build/vs2026-opengl -C Debug --output-on-failure
```
- 新增 `AndroidLoaderTest.TrueHDRDecode` 通过（HDR max > 1.0）。
- 现有测试无新增失败。

### 7.3 真机功能验证（HDR 设备）

```powershell
adb install -r android\app\build\outputs\apk\debug\app-debug.apk
adb logcat -s UHDRViewer
```

必须逐项确认：
1. 启动进入沉浸式全屏，画面无黑屏/闪烁。
2. 点击 “Open Image” 打开相册图片；选择 Ultra HDR 图片成功显示。
3. 从文件管理器/分享面板用 `ACTION_VIEW`/`ACTION_SEND` 打开图片成功。
4. 单指拖动平移；双指缩放围绕中心；双击回到 fit；缩放/平移不跳变。
5. 曝光/色调映射滑杆可操作，画面实时变化。
6. HDR 开关可切换；日志显示 `HDR10 active`、10-bit 配置、BT2020/PQ；高光有细节（不是整片死白），UI 亮度正常。
7. 关闭 HDR 回到 SDR，UI 颜色恢复正常（样式已恢复），画面与桌面端 SDR 一致。
8. 横竖屏旋转后布局自适应（左侧/底部面板切换），触摸坐标仍准确。
9. 切后台再回前台、锁屏解锁后渲染恢复，无 EGL 崩溃/黑屏。
10. 打开大图（≥12MP）不 OOM，切换图片无纹理泄漏。
11. 退出应用无 crash；`adb logcat` 无 EGL/GL 错误。

### 7.4 SDR 设备/模拟器（若有）

- 启动日志显示 `HDR10 unsupported`，HDR 勾选禁用，SDR 路径与原行为一致，无崩溃。

---

## 8. 假设与风险

### 已确认假设
- HDR 设备支持 `EGL_EXT_gl_colorspace_bt2020_pq` 且存在 10-bit EGL 配置；不支持时自动 SDR，功能不崩。
- HDR 只做 HDR10（PQ/BT.2020），不做 HLG/Dolby Vision。
- 移除 Android 端 SDL2 不影响桌面构建；`deps/sdl2` 保留在依赖脚本中，避免改动 clone 流程。
- 只产出 debug APK；release 签名不在本次范围。

### 风险与对策
1. **EGL 10-bit 配置在部分设备为空** → 回退 SDR 并记录日志；真机验证时优先确认。
2. **NDK r28 与 libjpeg-turbo/libultrahdr 旧 CMake 冲突** → 设置 `CMAKE_POLICY_VERSION_MINIMUM 3.5`，必要时关闭警告为错误。
3. **AGP/Gradle 下载失败** → 工具链脚本使用官方源；失败时保留可重试路径与明确错误。
4. **ImGui 样式烘焙后 8-bit 精度损失** → 与桌面 HDR10 路径相同的近似策略；UI 亮度以 203 nits 为基准，若真机观感异常可调整 `UI_WHITE_NITS`。
5. **大图内存** → `largeHeap` + 只在需要时保留 HDR 数据；若仍 OOM，后续可改为 half 存储（不在本次范围）。
6. **Android 17 大屏忽略方向限制** → 已按横竖屏自适应设计。
