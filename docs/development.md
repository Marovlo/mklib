# mklib 调研与开发文档

本文档记录 mklib 当前仍有效、已经核对或通过本地 SDK 验证的调研结论，并作为后续开发的帮助文档。临时实验日志不在这里长期保存；新的结论应在测试后更新本文档，而不是与旧结论并存。

## 1. 项目目标

mklib 面向本地多人游戏，解决同一台电脑接入多个键盘时，系统和游戏通常无法区分输入来源的问题。

一期目标是：

- Windows、macOS、Linux 支持多个物理键盘。
- 每个事件带物理设备 ID，游戏可以将设备绑定给不同玩家。
- 支持设备枚举、热插拔和设备元数据。
- 提供友好、稳定、低耦合的 API 和二次开发文档。
- 低延迟、低开销，设备之间不因为某一个设备或回调阻塞采集。
- macOS 优先完成和验证。

二期目标是：

- 区分多个物理鼠标。
- 游戏内维护多个逻辑光标。
- 不创建系统级虚拟鼠标，不要求桌面系统支持多个鼠标光标，不向系统注入鼠标事件。

## 2. 总体架构

```text
游戏
├── mklib C ABI / C++ wrapper
│   ├── 设备生命周期
│   ├── 设备身份和元数据
│   ├── 事件队列
│   ├── 回调和轮询
│   └── 权限状态
├── macOS IOHIDManager
├── Windows Raw Input
└── Linux evdev
```

mklib 核心采集层不依赖 SDL3。SDL3 是跨平台多媒体库，适合创建 Demo 窗口、渲染、计时和普通应用输入，但不是 mklib 的逐物理设备采集底座。

SDL3 从 3.2.0 起提供 `SDL_GetKeyboards` 和 `SDL_KeyboardID`，可以枚举键盘并给键盘事件附加设备 ID，但其 ID 只保证当前应用连接期间有效，重连会得到新 ID，列表还可能包含带键盘功能的虚拟设备或其他 HID 设备。因此 mklib 需要使用平台原生底层 API获取统一设备元数据。

## 3. API 设计

对外优先提供 C ABI：

```text
mklib_create
mklib_start
mklib_stop
mklib_destroy
mklib_get_devices
mklib_poll_event
mklib_input_access_status
mklib_request_input_access
```

事件包含：

- `device_id`：当前连接期间的物理设备 ID。
- `usage_page` 和 `usage`：HID 用途页和用途码。
- `type`：按下或释放。
- `value`：原始数值。
- `timestamp_ns`：纳秒时间戳。
- `repeat`：是否为重复事件。

设备信息包含：

- 厂商 ID、产品 ID。
- 设备厂商和产品名称。
- 传输方式，如 USB 或 Bluetooth。
- 序列号和位置 ID。
- 尽可能稳定的 `persistent_id`。

稳定 C ABI 的意义是：

```text
C++ / Objective-C++ 内部实现
          ↓
       mklib C ABI
          ↓
C、C++、Rust、C#、Python、Go、Swift 绑定
```

Xcode、Clang 和 CMake 与语言绑定不是同一个问题。macOS 不要求使用 Xcode IDE；Apple Clang 可通过 Xcode Command Line Tools 使用；CMake 只是推荐的跨平台构建系统。语言绑定主要依赖稳定的 C ABI。

## 4. macOS 方案

### 4.1 采集层

使用 `IOHIDManager`：

- 按 Generic Desktop / Keyboard Usage 匹配键盘。
- 使用 dispatch queue 接收异步设备事件。
- 注册设备匹配回调和设备移除回调。
- 注册输入值回调。
- 从 `IOHIDDeviceRef` 查询 VID、PID、产品、厂商、序列号、传输方式和 LocationID。
- 从 `IOHIDElementRef` 查询键盘 Usage Page 和 Usage。

当前实现采用串行 HID 采集队列，回调只做轻量的识别、入队和可选回调。公共 API 通过有容量上限的事件队列支持轮询，避免消费者不及时读取造成无界内存增长。

后续性能优化方向：

- 将用户回调从采集队列移出，避免用户代码阻塞 HID 回调。
- 为不同设备使用独立无锁或有界 SPSC 队列。
- 增加事件批量读取接口。
- 使用基准测试测量设备数、事件速率和回调耗时。

### 4.2 权限

macOS 不需要 root，也不需要为 mklib 安装内核驱动。监听键盘、鼠标等输入设备时，需要用户在系统设置中授予实际运行程序 Input Monitoring 权限：

```text
系统设置
→ 隐私与安全性
→ 输入监控
→ 允许游戏 App
```

权限归属于进程或 App，不归属于静态库文件。如果游戏把 mklib 静态链接进 `MyGame.app`，需要授权 `MyGame.app`；如果另起独立 helper 进程，helper 可能需要单独授权。

SDK 提供：

- `IOHIDCheckAccess(kIOHIDRequestTypeListenEvent)`：检查状态。
- `IOHIDRequestAccess(kIOHIDRequestTypeListenEvent)`：请求用户授权。

Demo 包含 `NSInputMonitoringUsageDescription`，并在没有权限时显示系统设置路径。

开发时应优先从最终 `.app` 启动测试。通过 Terminal 或 Xcode 启动时，TCC 权限归属和签名身份可能与最终游戏不同。

## 5. Windows 方案

使用 Windows Raw Input：

```text
RegisterRawInputDevices
        ↓
WM_INPUT
        ↓
RAWINPUTHEADER.hDevice
        ↓
设备元数据和键盘事件
```

Raw Input 可以区分同类型的多个设备，通常不需要管理员权限。

重要集成限制：同一个进程内，每种 Raw Input 设备类别只能注册到一个窗口。`RegisterRawInputDevices` 不应由库无条件调用，因为可能覆盖宿主游戏或引擎的 Raw Input 注册。

Windows 后端应提供两种接入模式：

```text
宿主窗口模式：mklib 接收宿主窗口的 WM_INPUT
转发模式：游戏自己读取 WM_INPUT，再调用 mklib_feed_raw_input
```

实现 Windows 后端前，需要先设计窗口消息生命周期、Raw Input 注册协商和设备到 mklib ID 的映射。

## 6. Linux 方案

使用 evdev 读取 `/dev/input/event*`：

- evdev 是 Linux 用户态消费输入事件的通用接口。
- 事件包含设备节点、事件类型、事件码和值。
- 通过设备能力过滤键盘。
- 通过 udev 或设备监听机制处理热插拔。

Linux 不要求游戏每次以 root 运行，但当前进程必须有设备节点读权限。不同发行版的默认权限不同，常见安全部署顺序为：

1. 优先使用桌面活动会话 ACL。
2. 安装针对输入设备的受控 udev 规则，例如活动用户访问标签。
3. 必要时使用受控用户组。
4. 不把长期 `sudo game` 作为产品方案。

开放全部键盘节点有隐私风险，因为键盘事件可能包含密码和其他敏感输入。Linux 文档需要单独说明发行版、X11、Wayland、Flatpak 和 Snap 差异。

## 7. 已知限制和风险

### 7.1 接入边界

不接入 mklib 的现有游戏不会自动获得多键盘能力。mklib 首期是游戏内输入库，不做系统级输入重映射、虚拟键盘或通用游戏注入器。

### 7.2 设备身份

以下对象不应作为永久身份：

- macOS `IOHIDDeviceRef`。
- Windows `hDevice`。
- Linux `/dev/input/eventX`。
- SDL3 `SDL_KeyboardID`。

设备重连后可能产生新 ID。无序列号的同型号设备需要通过按键确认、用户配置或物理端口信息辅助区分。

### 7.3 输入状态恢复

设备断开、睡眠、失焦和程序异常退出时，可能没有收到按键释放事件。库需要在设备移除和状态重置时清理该设备所有按下键，避免游戏角色持续移动。

### 7.4 键盘布局与文本输入

游戏控制使用物理 Usage 或扫描码，不使用布局相关字符。mklib 不负责 IME、中文输入、候选框、聊天文本和文本编辑。

### 7.5 硬件行为

软件无法消除键盘固件造成的限制，包括 6KRO、NKRO、Fn 键、蓝牙休眠、无线丢包和硬件级组合键冲突。

### 7.6 输入路径重复

游戏如果同时使用 mklib 事件和 SDL3、Cocoa 或引擎自己的普通键盘事件，可能同一次按键被处理两次。接入 mklib 的游戏应明确输入归属，Demo 只使用 mklib 事件控制玩家。

### 7.7 权限和平台差异

macOS 的 TCC 权限、Linux 的设备节点权限、Windows 的 Raw Input 注册都可能导致“设备可枚举但没有事件”或与宿主引擎冲突。权限检查、错误提示和诊断日志属于正式 API 的一部分。

## 8. 多鼠标二期边界

二期只采集多个物理鼠标并将事件带上设备 ID：

```text
鼠标 A → mklib → 游戏逻辑光标 A
鼠标 B → mklib → 游戏逻辑光标 B
```

游戏自己绘制多个光标，自己决定每个光标的点击和 UI 规则。系统桌面仍然只有一个光标，不创建虚拟鼠标，不使用 uinput、IOHIDPostEvent 或 Windows 注入接口。

## 9. 当前开发进度

### 已完成

- 创建 CMake 工程。
- 创建稳定 C ABI。
- 创建 macOS IOHIDManager 后端初版。
- 支持设备枚举、热插拔、键盘事件和权限状态。
- 创建 Cocoa 双玩家 Demo。
- 创建基础 API 测试。
- 固化 README 和本文档。

### 下一步

- 在真实 MacBook 内置键盘和蓝牙键盘上验证设备列表与事件来源。
- 验证不同启动方式下的 Input Monitoring 权限。
- 增加按键释放状态清理测试。
- 将用户回调与 HID 采集队列解耦。
- 增加设备元数据和事件吞吐性能基准。
- 开始 Windows Raw Input 后端设计。
- 开始 Linux evdev 后端设计。

## 10. 验证记录规范

后续每次实验只记录：

```text
日期：YYYY-MM-DD
平台和系统版本：
设备：
测试目标：
测试条件：
结果：
当前结论：
```

只有测试过且仍有效的结论进入本文档。旧结论过时后直接更新，不保留互相矛盾的版本。

## 11. 参考资料

- [SDL3 官方 Wiki](https://wiki.libsdl.org/)
- [SDL3 获取键盘列表](https://wiki.libsdl.org/SDL3/SDL_GetKeyboards)
- [SDL3 键盘 ID](https://wiki.libsdl.org/SDL_KeyboardID)
- [Apple 输入监控权限](https://support.apple.com/guide/mac-help/mchl4cedafb6/mac)
- [Apple IOHID 设备接口](https://developer.apple.com/documentation/iokit/iohiddevice_h_user-space)
- [Microsoft Raw Input 概览](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input)
- [Microsoft RegisterRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices)
- [Linux 输入子系统](https://www.kernel.org/doc/html/latest/input/input.html)
- [Linux udev](https://www.kernel.org/doc/man-pages/online/pages/man7/udev.7.html)
