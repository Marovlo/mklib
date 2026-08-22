# mklib

`mklib` 是一个面向本地多人游戏的跨平台物理输入设备库，目标是让游戏能够区分多个键盘，并将不同键盘分配给不同玩家。

当前版本已实现 macOS `IOHIDManager` 和 Windows Raw Input 逐设备采集后端；Linux 仍为可编译占位后端。

## 解决的问题

系统和大多数游戏通常把多个键盘合并为一个逻辑键盘，因此两个键盘无法自然地控制两个玩家。`mklib` 不修改系统键盘模型，而是直接在游戏进程中提供带物理设备 ID 的输入事件：

```text
物理键盘 A ─┐
            ├─ mklib ── 游戏内玩家 1 / 玩家 2 / 任意游戏对象
物理键盘 B ─┘
```

现有游戏不会因为安装 mklib 自动获得多键盘能力，游戏需要主动接入 mklib API。

## 当前状态

- 已实现 macOS 键盘设备枚举。
- 已实现 macOS 键盘热插拔通知。
- 已实现按物理键盘区分的按键按下和释放事件。
- 已实现设备元数据：VID、PID、厂商、产品、传输方式、序列号和位置 ID。
- 已实现输入监控权限查询和请求接口。
- 已实现 C 风格 opaque handle、extern "C" ABI、ABI 版本和配置结构体版本字段，便于 C++、C#、Rust、Python 等语言绑定；正式发布仍需按版本维护 ABI 兼容性。
- 已实现无第三方 UI 依赖的 macOS Cocoa 双玩家、多鼠标和 Flappy Bird Demo。
- 已实现无第三方 UI 依赖的 Windows Win32 双键盘、多鼠标和 Flappy Bird Demo；Windows GUI 使用双缓冲，Flappy Bird 复用 macOS 资源。
- 已实现 Windows Raw Input 键盘/鼠标、设备热插拔、消息转发和显式窗口注册模式；Linux 仍返回 `MKLIB_PLATFORM_UNSUPPORTED`。
- 多鼠标开发已在 `feature/multi-mouse` 分支开始，采用游戏内多个逻辑鼠标，不创建系统级虚拟鼠标。

## 构建

环境要求：

- macOS 10.15 或更高版本，或 Windows 10/11。
- macOS 使用 Apple Clang；Windows 使用 Visual Studio 2022 的 MSVC；不要求使用 IDE。
- CMake 3.25 或更高版本。

```bash
cmake -S . -B build -DMKLIB_BUILD_DEMO=ON -DMKLIB_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Demo 位于 macOS 构建目录的 App Bundle 中；Windows 构建目录的 Debug/Release 子目录中：

```text
macOS:  build/mklib_demo.app
        build/mklib_mouse_demo.app
        build/mkflappybird.app
Windows: build/mklib_demo.exe
         build/mklib_mouse_demo.exe
         build/mkflappybird.exe
```

Windows 构建示例：

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 -DMKLIB_BUILD_DEMO=ON -DMKLIB_BUILD_MOUSE_DEMO=ON -DMKLIB_BUILD_FLAPPYBIRD_DEMO=ON -DMKLIB_BUILD_TESTS=ON
cmake --build build-win --config Debug
ctest --test-dir build-win -C Debug --output-on-failure
```

Windows Demo 的核心操作与 macOS 版本一致：双键盘 Demo 使用首次按键绑定和 WASD 移动；多鼠标 Demo 使用鼠标按钮绑定、相对移动和 Tab/Esc；Flappy Bird 使用 Enter 进入绑定、键盘 1/2 和鼠标左右键分配区域，进入游戏后用 Space 飞行、对应鼠标按键发射子弹，包含管道、礼物、碰撞、计分和结束状态。Windows Flappy Bird 构建时会自动从 `demo/macos/mkflappybird/assets` 复制 `sky_background.png`、`bird_up.png`、`bird_down.png`、`bullet.png` 和 `gift.png` 到输出目录的 `assets` 子目录。Windows Demo 不注入系统输入，也不移动系统光标。

macOS 建议直接启动 App，而不是长期从命令行启动：

```bash
open build/mklib_demo.app
open build/mklib_mouse_demo.app
open build/mkflappybird.app
```

## Demo 用法

首次启动时，macOS 可能要求输入监控权限。请打开：

```text
系统设置 → 隐私与安全性 → 输入监控 → 当前启动的 Demo（mklib Demo、mklib Mouse Demo 或 mkflappybird）
```

Demo 中：

- 第一次按下任意键：将产生该按键的物理键盘绑定给玩家 1。
- 之后由另一台键盘第一次按下任意键：将该物理键盘绑定给玩家 2。
- 两台键盘都绑定后，按 `Tab`：交换两个玩家的键盘绑定。
- 玩家 1 和玩家 2 都使用各自键盘上的 `WASD` 移动对应对象。
- 页面上会显示当前发现的键盘、设备 ID、厂商、传输方式和 VID/PID。

设备 ID 只保证当前连接期间有效。键盘断开后重新连接，可能得到新的 ID；如果两个键盘型号完全相同且没有序列号，Demo 使用按键绑定来消除识别歧义。

### mkflappybird Demo

`mkflappybird` 是双区域键盘+鼠标 Flappy Bird Demo；macOS 位于 `build/mkflappybird.app`，Windows 位于 `build-win/Debug/mkflappybird.exe`：

- 启动后进入主页，固定左右两个区域，按 `Enter` 进入绑定；
- 左区域：一台键盘按数字键 `1` 绑定，再用一只鼠标按左键绑定；
- 右区域：另一台键盘按数字键 `2` 绑定，再用另一只鼠标按右键绑定；同一设备不能绑定多个区域；
- 四个设备都绑定后自动倒计时 3 秒；
- 游戏中，绑定键盘按 `Space` 控制对应小鸟飞行，绑定鼠标按键发射水平子弹；
- 子弹每 0.5 秒最多发射一次，速度为障碍物移动速度的 3 倍；
- 子弹击中随机出现的礼物后，该礼物消失并为对应区域加 1 分；
- 小鸟撞到管道、边界或未消失的子弹时，该区域结束，其他区域继续；
- 全部区域结束后按 `Enter` 回到大厅重新开始。

### 多鼠标 Demo

多鼠标 Demo 位于 macOS 的 `build/mklib_mouse_demo.app` 或 Windows 的 `build-win/Debug/mklib_mouse_demo.exe`：

- 库仍能枚举键盘、鼠标和 HID 触摸板，但当前 Demo 暂时只接收键盘和两个独立鼠标；
- 第一次按下鼠标按钮绑定玩家 1，另一台鼠标按下按钮后绑定玩家 2；
- 任意键盘按 `Tab` 交换两个玩家的鼠标控制；
- 鼠标移动控制对应小球；
- macOS Demo 在完成绑定后隐藏并解关联系统光标，按 `Esc` 恢复系统光标并停止小球控制；Windows Demo 使用逻辑捕获，不移动系统光标，按 `Esc` 关闭逻辑捕获，再次按鼠标按钮可以重新捕获。

当前 MacBook 触摸板在本机没有产生可供 mklib 使用的 HID 按钮/移动事件，因此暂不作为 Mouse Demo 的控制设备。库层保留触摸板分类和事件兼容代码，后续再单独研究 macOS 专用触控板接口；本阶段不实现多点触摸、手势和系统级多光标。

## 最小接入示例

```cpp
#include <mklib/mklib.h>

mklib_handle *input = nullptr;
mklib_config config{};
if (mklib_config_init(&config) != MKLIB_OK) {
    return false;
}
config.event_queue_capacity = 4096;

if (mklib_create(&config, &input) != MKLIB_OK) {
    return false;
}
if (mklib_start(input) != MKLIB_OK) {
    mklib_destroy(&input);
    return false;
}

mklib_event event{};
while (mklib_poll_event(input, &event, 16) == MKLIB_OK) {
    if (event.type == MKLIB_KEY_DOWN) {
        (void)event.device_id;
        (void)event.usage;
    }
}

mklib_destroy(&input);
```

推荐游戏控制使用 HID Usage 或物理扫描码，而不是依赖当前键盘布局产生的字符。文本输入、IME 和聊天框应由游戏自己的文本输入模块处理。

## 设计原则

- 核心采集层不依赖 SDL3。
- Demo 可以使用 SDL3 或平台原生 UI；当前 macOS Demo 使用 Cocoa，Windows Demo 使用 Win32/GDI+，减少依赖并明确展示 mklib 的事件来源。
- 对外暴露稳定 C ABI，内部实现可以使用 C++ 和平台原生 API。
- 每个物理设备有独立 ID、元数据和事件来源。
- 回调和轮询两种消费方式都支持。
- 事件队列有容量上限，队列满时丢弃最旧事件，避免无界内存增长。
- 不默认独占或拦截系统输入，避免破坏用户正常输入和其他应用。
- 二期多鼠标只提供多个游戏内逻辑光标，不创建系统级虚拟鼠标。

## 平台路线

### macOS

使用 `IOHIDManager` 直接枚举和监听键盘设备。通常不需要 root，但需要用户对实际游戏 App 授予 Input Monitoring 权限。权限归宿主游戏 App，而不是静态库文件。

### Windows

使用 Raw Input 和 `WM_INPUT`，通过 `RAWINPUTHEADER.hDevice` 区分物理键盘和鼠标。Windows 窗口接入 API 与其他公共 API 一样声明在 `include/mklib/mklib.h` 中。宿主默认自行调用 `RegisterRawInputDevices`，在窗口过程把 `WM_INPUT` 和 `WM_INPUT_DEVICE_CHANGE` 转发到 `mklib_windows_process_message`；如果没有现成注册，也可以显式使用 `MKLIB_WINDOWS_ATTACH_REGISTER_RAW_INPUT`。库不创建隐藏窗口、不覆盖已有引擎注册、不注入系统输入。

窗口句柄必须覆盖 `attach` 到 `stop` 的整个生命周期，窗口销毁后先停止并分离句柄。`WM_INPUT` 处理后宿主仍按框架要求调用 `DefWindowProc`。失焦时转发 `WM_KILLFOCUS`/`WM_ACTIVATEAPP` 或调用 `mklib_windows_reset_input_state`。Windows 通常不需要管理员权限，也没有需要由 mklib 请求的系统级输入权限；远程桌面、受限桌面和驱动限制可能影响设备可见性。

### Linux

使用 evdev 读取 `/dev/input/event*`，通常不要求游戏每次以 root 运行，但要求当前用户拥有设备节点读权限。发行版部署应优先使用活动会话 ACL 或受控 udev 规则，不建议默认开放所有输入设备或要求用户长期使用 sudo。

## 已知限制

- 现有不接入 mklib 的游戏无法自动获得多键盘能力。
- 设备句柄、`eventX`、Windows `hDevice` 和当前连接 ID 都不是永久设备身份。
- 同型号、无序列号设备的永久区分能力有限，需要按键确认或用户配置。
- 蓝牙键盘可能有休眠、延迟和重连问题。
- 键盘硬件的按键冲突、NKRO、Fn 键固件处理无法由软件完全解决。
- 设备断开、休眠或失焦时可能缺少释放事件，库必须清理该设备的按下状态。
- Linux 不同发行版、X11、Wayland 和沙箱的设备权限模型不同。
- SDL3 本身支持键盘设备 ID，但 mklib 需要更底层、跨平台统一的物理设备元数据，因此不把 SDL3 作为核心采集层。

## 分期规划

### 一期

- macOS 首版可运行。
- Windows Raw Input 后端。
- Linux evdev 后端。
- 统一设备身份、热插拔、按键状态清理和性能基准。
- C API、C++ 包装、二次开发文档。

### 二期

- 按物理鼠标区分位移、按钮和滚轮。
- 游戏内部维护多个逻辑光标。
- 不创建系统级虚拟鼠标，不向系统注入鼠标事件。

## 目录结构

```text
include/mklib/mklib.h                                  公共 C ABI 和 Windows 窗口消息 API
src/platform/macos/macos_backend.cpp                   macOS IOHIDManager 后端
src/platform/windows/                                   Windows Raw Input 后端
src/platform/unsupported/unsupported_backend.cpp       其他平台占位后端
demo/macos/mklib_demo/main.mm                          Cocoa 双玩家 Demo
demo/macos/mklib_demo/Info.plist                       双玩家 Bundle 配置
demo/macos/mklib_mouse_demo/mouse_main.mm              Cocoa 多鼠标 Demo
demo/macos/mklib_mouse_demo/MouseInfo.plist            多鼠标 Bundle 配置
demo/macos/mkflappybird/main.mm                        Cocoa 双区域键盘+鼠标 Flappy Bird Demo
demo/macos/mkflappybird/Info.plist                     Flappy Bird Bundle 配置
demo/macos/mkflappybird/assets/sky_background.png      Flappy Bird 背景素材
demo/windows/demo_common.h                              Win32 Demo 双缓冲窗口基础层
demo/windows/mklib_demo.cpp                             Windows 双键盘 Demo
demo/windows/mklib_mouse_demo.cpp                       Windows 多鼠标 Demo
demo/windows/mkflappybird.cpp                           Windows 双区域键盘+鼠标 Flappy Bird Demo
tests/test_api.cpp                                      公共 API 测试
tests/test_windows_normalizer.cpp                      Windows 规范化测试
docs/api.md                                             二次开发 API、生命周期和线程契约
docs/platform-backends.md                               跨平台后端开发约定
docs/development.md                                     调研与开发文档
CMakeLists.txt                                          构建配置与安装包
```

## 二次开发与安装

详细接入契约见 [`docs/api.md`](docs/api.md)，各平台后端目录和实现约束见 [`docs/platform-backends.md`](docs/platform-backends.md)。

构建共享库并安装 CMake package：

```bash
cmake -S . -B build -DMKLIB_BUILD_SHARED=ON -DMKLIB_BUILD_DEMO=OFF
cmake --build build
cmake --install build --prefix "$PWD/install"
```

下游 CMake 项目可以使用：

```cmake
find_package(mklib CONFIG REQUIRED)
target_link_libraries(game PRIVATE mklib::mklib)
```

## 许可证

当前仓库尚未选定最终开源许可证。正式发布前需要补充许可证文件和第三方依赖声明。
