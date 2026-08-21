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

当前实现采用串行 HID 采集队列，先完成轻量的识别和入队；用户回调通过独立串行回调队列异步执行，避免用户代码阻塞 HID 采集。公共 API 通过有容量上限的事件队列支持轮询，避免消费者不及时读取造成无界内存增长。

后续性能优化方向：

- 将用户回调从采集队列移出，避免用户代码阻塞 HID 回调。
- 为不同设备使用独立无锁或有界 SPSC 队列。
- 增加事件批量读取接口。
- 使用基准测试测量设备数、事件速率和回调耗时。

### 4.2 macOS HID 正确用法与已确认的坑

本项目当前使用 `IOHIDManager` 监听物理键盘。它与 Cocoa 的窗口键盘事件是两条独立链路：

```text
物理键盘
├── AppKit / NSEvent / keyDown:       → 当前窗口的焦点事件
└── IOHIDManager / HID value callback → mklib 的逐设备事件
```

因此，Cocoa 窗口是否获得焦点不会决定 `IOHIDManager` 是否能收到物理键盘事件。Cocoa `keyDown:` 适合普通窗口输入，但系统通常不会为它提供可靠的物理键盘来源区分；需要区分多台键盘时，必须使用 HID 层。

当前已在真实 macOS 设备上验证的 Manager 调用顺序是：

```text
创建 IOHIDManager
→ 设置设备匹配条件
→ 设置串行 dispatch queue
→ 注册设备匹配、设备移除和输入值回调
→ 设置取消处理器
→ IOHIDManagerOpen
→ IOHIDManagerActivate
→ IOHIDManagerCopyDevices 获取当前设备快照
```

停止时应使用对应的逆向生命周期：

```text
IOHIDManagerCancel
→ 等待 HID dispatch queue 中的工作完成
→ IOHIDManagerClose
→ 释放 IOHIDManager 和 dispatch queue
```

关键规则：

- 设备枚举成功不等于输入回调成功；必须分别验证设备数量和实际输入事件。
- 不要把窗口焦点、Cocoa `NSEvent` 计数和 mklib HID 事件计数混为一谈。
- 当前输入值匹配不额外限制 Usage Page，由回调读取 `IOHIDElementRef` 的 Usage Page/Usage 后再过滤；过窄的输入匹配字典可能导致设备能枚举但输入回调不触发。
- 多设备匹配使用 `IOHIDManagerSetDeviceMatchingMultiple`，不能简单把键盘匹配替换成鼠标匹配，否则会丢失已有键盘设备。
- 当前设备类别优先按产品名中的 `Trackpad`/`Touchpad` 和 Primary Usage 区分：Generic Desktop/Keyboard、Generic Desktop/Mouse、Digitizer/Touch Pad。因为部分 MacBook 内置触控板在 IOKit 中暴露为 `Apple Internal Keyboard / Trackpad` 的 Generic Desktop/Mouse，不能只依赖 Primary Usage；`mklib_device_info.kind` 记录类别，`mklib_config.device_kind_mask` 控制本次句柄需要监听的类别；掩码为 0 时保持旧行为，只监听键盘。
- 鼠标按钮使用 Button Usage Page，X/Y 移动使用 Generic Desktop 的 X/Y Usage；触控板还可能使用 Digitizer/Tip Switch 表示按压，需要转换为鼠标按钮事件。触摸板只有在 macOS 暴露相对鼠标式 X/Y 报告时才能直接用于本阶段逻辑光标。绝对触摸坐标、多点联系人、手势和系统光标不属于当前 API。
- `IOHIDDeviceRef` 只作为当前连接的句柄使用，不作为永久设备身份；设备身份应由 VID/PID、LocationID、序列号等元数据组合生成。
- `Input Monitoring` 是访问底层 HID 输入的授权资格，不是应用之间的独占锁；CodeBuddy 与 mklib Demo 同时获得授权不会互相抢占。
- 应用可以在前台激活时启动 mklib，在切到其他应用时停止 mklib；这不会取消已经授予的权限，但可以避免程序在后台主动监听。
- 权限归宿是最终运行的宿主 App，不是静态库文件。开发调试时应固定 `.app` 路径、Bundle ID、签名身份和启动方式；Ad Hoc 重编译可能导致 macOS TCC 重新要求授权。
- 通过 `open` 或 Finder 启动 App 时，`fprintf(stderr, ...)` 不会回到发起命令的终端；应使用 `log stream`/`log show`，或直接运行 `Contents/MacOS/mklib_demo`。
- Demo 曾出现“拖动窗口跨屏幕后计数才更新”。根因是事件和状态已经在内存中更新，但绘制代码只调用 `displayIfNeeded`，没有先调用 `setNeedsDisplay:YES`；这是 AppKit 重绘问题，不是 HID 事件延迟。

推荐排错顺序：

```text
1. 检查 Input Monitoring 权限
2. 检查 IOHIDManager 是否成功 Open/Activate
3. 检查当前匹配设备数量
4. 检查 HID 输入值回调是否收到首个事件
5. 检查 mklib 事件队列是否能被轮询
6. 最后检查 Demo 的视图重绘和玩家绑定逻辑
```

### 4.3 权限

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

多鼠标开发从 `main` 创建 `feature/multi-mouse` 分支继续，当前第一版只采集多个物理鼠标/触摸板并将事件带上设备 ID：

```text
鼠标 A ─┐
鼠标 B ─┼→ mklib → 游戏逻辑光标 A/B
触摸板 ─┘
```

游戏自己绘制多个光标，自己决定每个光标的点击和 UI 规则。系统桌面仍然只有一个光标，不创建虚拟鼠标，不使用 uinput、IOHIDPostEvent 或 Windows 注入接口。

公共 API 新增：

- `mklib_device_kind`：区分键盘、鼠标和触摸板；
- `mklib_config.device_kind_mask`：选择本次句柄监听的设备类别，0 保持原有键盘默认行为；
- `MKLIB_MOUSE_BUTTON_DOWN/UP`：鼠标按钮事件；
- `MKLIB_MOUSE_MOVE`：相对 X/Y 移动事件，轴由 `usage` 表示，增量由 `value` 表示。

`build/mklib_mouse_demo.app` 当前暂时限定为两个独立物理鼠标：用鼠标按钮绑定两个玩家，用 Tab 交换控制，并用相对 X/Y 移动控制两个逻辑小球。当前 MacBook 触摸板虽然可能被系统指针使用，但没有向本项目的 `IOHIDManager` 事件链路提供可用的按钮/移动事件，因此暂不纳入 Demo 控制。库层保留触摸板分类和兼容代码，后续再研究 macOS 专用触控板接口；本阶段不承诺多点触摸、绝对坐标、手势或所有内置触摸板。

Demo 的鼠标捕获行为：

- 第一次有效鼠标按钮绑定后，调用 `CGAssociateMouseAndMouseCursorPosition(false)` 解开物理鼠标与系统光标的关联，并调用 `NSCursor hide` 隐藏光标；
- HID 鼠标移动继续通过 mklib 控制 Demo 内的逻辑小球；
- 收到键盘 `Escape` 后，恢复鼠标与系统光标的关联、显示系统光标，并停止处理鼠标移动；
- 切到后台或 Demo 退出时也会自动恢复系统光标，避免全局鼠标状态残留；
- 再次按鼠标按钮可以重新捕获。该功能只影响 Demo 的系统光标状态，不创建虚拟鼠标。


## 9. 当前开发进度

### 已完成

- 创建 CMake 工程。
- 创建稳定 C ABI。
- 创建 macOS IOHIDManager 后端初版。
- 支持设备枚举、热插拔、键盘事件和权限状态。
- 创建 Cocoa 双玩家键盘 Demo。
- 在 `feature/multi-mouse` 分支增加键盘、鼠标、触摸板的设备类别匹配和独立 Mouse Demo 初版。
- 创建基础 API 测试。
- 固化 README 和本文档。

### 下一步

- 在真实 MacBook 内置键盘和蓝牙键盘上验证设备列表与事件来源。
- 验证不同启动方式下的 Input Monitoring 权限。
- 增加按键释放状态清理测试。
- 将用户回调与 HID 采集队列解耦。（已完成：回调使用独立串行队列。）
- 增加设备元数据和事件吞吐性能基准。
- 开始 Windows Raw Input 后端设计。（已完成目录和统一语义占位，真实采集待实现。）
- 开始 Linux evdev 后端设计。（已完成目录和统一语义占位，真实采集待实现。）
- 详细二次开发契约见 `docs/api.md`，跨平台约束见 `docs/platform-backends.md`。

### 9.1 macOS 阶段性验证记录

截至当前阶段，macOS 后端已经在真实设备上验证了以下链路：

```text
多个物理键盘
    → IOHIDManager 设备枚举
    → 带 device_id 的 HID 按键事件
    → mklib 有界事件队列
    → Demo 轮询、自动绑定玩家和 Tab 交换
```

已观察到的结果：

- Demo 能枚举 MacBook 内置键盘、蓝牙键盘和 USB 接收器键盘。
- 第一次由任意键盘产生的按键可以绑定玩家 1；另一台键盘产生按键后可以绑定玩家 2。
- 两台键盘绑定后，任意键盘的 Tab 可以交换玩家位置。
- `mklib` 事件计数能够增加，说明当前 IOHIDManager 采集方案和 Input Monitoring 权限链路是可行的。
- CodeBuddy 与 Demo 同时出现在 Input Monitoring 列表不会形成输入抢占；该权限不是应用之间的独占锁。
- Demo 应在前台启动 mklib，在切到其他应用时停止 mklib；Input Monitoring 权限仍是访问底层 HID 事件的资格，不等于程序必须持续后台监听。

已确认的 Demo 显示层问题：

- 事件轮询和状态更新发生在 `NSTimer` 中，但原先只调用 `displayIfNeeded`，没有将视图标记为需要重绘。
- 因此 mklib 实际已经持续收到事件，计数和玩家状态在内存中更新；拖动窗口、跨屏幕或其他系统操作触发 AppKit 重绘后，积累的状态才一次性显示出来。
- 这个现象不是 Cocoa 焦点、屏幕切换或 HID 事件延迟，而是 Demo 忘记调用 `setNeedsDisplay` 导致的界面刷新问题。
- 修复方式是在每次 `tick:` 完成状态更新后，对内容视图调用 `setNeedsDisplay:YES`。这只修复显示刷新，不改变 mklib 采集链路。

诊断时应区分两类计数：

- `mklib 事件`：来自 IOHIDManager 的逐设备事件，验证物理键盘采集是否成功。
- `Cocoa 焦点事件`：Demo 窗口自己的 AppKit 事件，用于辅助判断窗口焦点；它不是 mklib 的输入来源。当前 Demo 同时统计本地事件监视器和视图事件，因此一次完整的按下/释放可能计数 4 次：本地监视器收到 `keyDown`/`keyUp` 各一次，`DemoView` 的 `keyDown:`/`keyUp:` 又各收到一次。不应将其与 mklib 计数直接比较。
- `mklib 事件`：当前是 HID value callback 入队次数，不是用户意义上的“按键动作次数”。一次物理操作可能包含按下、释放、HID 报告重复或设备上报的多个值；原始 HID 层也不保证像 AppKit 一样提供文字输入的自动重复。不能用 mklib 事件总数直接实现长按移动。
- Demo 的长按处理应维护每台已绑定键盘的 WASD 按键状态：收到 `KEY_DOWN` 置位、收到 `KEY_UP` 清位，然后在每个渲染 tick 按状态移动。切到后台停止 mklib 时必须清空按键状态，避免释放事件丢失造成卡键。

启动和日志注意事项：

- 从可执行文件直接运行时，`fprintf(stderr, ...)` 会回到当前终端。
- 使用 `open` 启动 App 时，标准输出不会回到发出命令的终端；应使用 `log stream`/`log show` 查看统一日志，或直接运行 App 内的可执行文件。
- macOS TCC 权限与最终 App 的 Bundle 身份、签名和启动上下文有关，开发验证应固定使用同一个 `.app` 路径和启动方式。

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
