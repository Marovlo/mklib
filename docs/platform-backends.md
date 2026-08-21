# 跨平台后端开发约定

## 1. 目录和构建边界

公共 C ABI 位于 `include/mklib/mklib.h`，平台采集实现放在 `src/` 的平台后端中：

```text
src/mklib.cpp                    当前 macOS IOHIDManager 后端
src/platform/unsupported.cpp     Windows/Linux 编译占位
src/platform/windows/            后续 Raw Input 后端
src/platform/linux/              后续 evdev 后端
```

CMake 根据目标平台选择后端：

- macOS 选择 `src/mklib.cpp`；
- 其他平台选择 `src/platform/unsupported.cpp`，公共 API 可以编译和链接，但采集接口返回 `MKLIB_PLATFORM_UNSUPPORTED`；
- Demo 只在 macOS 构建；库和基础测试可以在 Windows/Linux 先完成编译验证。

这种布局让平台代码不会泄漏到公共头文件，也避免在 Windows/Linux 开发阶段使用大量 `#ifdef` 改写业务层。

## 2. 后端必须遵守的统一语义

每个平台后端都必须提供相同的上层语义：

1. `start` 建立采集资源并发现当前设备；
2. 设备加入/移除产生设备回调；
3. 输入事件进入同一个有界事件队列；
4. 每个事件带临时 `device_id`、设备类别、原始用途/扫描码、值和单调时间戳；
5. `stop` 先停止新事件，再等待后端工作结束，最后释放设备和线程资源；
6. 设备移除时不得继续产生该设备的新事件；
7. 不把平台句柄直接暴露给公共 ABI；
8. 不注入系统输入，不创建系统级虚拟键盘或鼠标。

平台代码只负责采集和规范化，玩家绑定、长按状态、渲染和业务动作由调用方负责。

## 3. Windows 后端规划

推荐目录：

```text
src/platform/windows/raw_input_backend.h
src/platform/windows/raw_input_backend.cpp
src/platform/windows/window_message_bridge.h
src/platform/windows/window_message_bridge.cpp
```

实现重点：

- 使用 `RegisterRawInputDevices` 注册键盘和鼠标类别；
- 从宿主窗口的 `WM_INPUT` 读取 `RAWINPUT`；
- 使用 `RAWINPUTHEADER.hDevice` 区分物理设备；
- 使用 `GetRawInputDeviceInfo` 获取设备路径和元数据；
- 用设备路径、VID/PID、序列号等组合 `persistent_id`；
- 用 `WM_INPUT_DEVICE_CHANGE` 处理热插拔；
- 将扫描码转换为统一的 `usage_page`/`usage` 或明确记录平台扫描码来源；
- 在 DLL/静态库和宿主引擎之间明确窗口消息转发责任。

重要限制：同一个进程内，Raw Input 注册通常由一个窗口负责。库不能无条件创建隐藏窗口或覆盖宿主已有注册。推荐两种模式：

- 宿主窗口模式：调用方把窗口句柄交给 mklib，库提供 `mklib_windows_process_message` 处理 `WM_INPUT`；
- 转发模式：宿主自己读取 `WM_INPUT`，将原始数据喂给后端规范化层。

Windows 实现前应先确定上述模式中的 API 形态，再写设备采集代码，避免后续与 Unity、Unreal 或自有消息循环冲突。

## 4. Linux 后端规划

推荐目录：

```text
src/platform/linux/evdev_backend.h
src/platform/linux/evdev_backend.cpp
src/platform/linux/udev_monitor.h
src/platform/linux/udev_monitor.cpp
```

实现重点：

- 枚举并打开具有键盘、鼠标能力的 `/dev/input/event*`；
- 通过 `ioctl(EVIOCGBIT)` 和设备名称/总线信息筛选设备；
- 每个设备使用独立非阻塞 fd，或由一个 `epoll` 线程统一读取；
- 使用 `EV_KEY`、`EV_REL`、`EV_ABS` 映射为统一事件；
- 使用 `udev` 监听加入/移除并重新建立 fd；
- 设备节点路径只作当前连接句柄，不作永久身份；
- 记录 `ID_VENDOR_ID`、`ID_MODEL_ID`、`ID_SERIAL`、`ID_PATH` 等 udev 属性；
- 处理 fd 错误、设备断开和所有按下状态清理。

权限必须是产品设计的一部分：优先使用桌面会话 ACL 或受控 udev 规则，不要求用户长期以 root 运行游戏。Wayland/X11 不改变 evdev 读取层的设备权限问题，但沙箱环境可能禁止访问设备节点，需要单独的打包权限声明。

## 5. 测试分层

平台后端开发应按以下层次增加测试：

### 公共 ABI 测试

不依赖真实设备，覆盖：

- 配置初始化、ABI 版本和结构体大小；
- 空指针和错误参数；
- 未启动轮询；
- 句柄重复启动/停止/销毁；
- 非支持平台的明确错误。

### 规范化层测试

使用人工构造的平台原始报告，覆盖：

- 键盘按下/释放；
- 鼠标按钮、X/Y 相对移动；
- 时间戳和负方向增量；
- 重复事件；
- 设备加入、移除和状态清理。

### 平台集成测试

在真实系统和真实设备上覆盖：

- 多个同型号设备；
- USB、蓝牙、内置设备；
- 断开、重连、休眠、切后台；
- 权限拒绝和恢复；
- 高频鼠标移动下的队列丢弃和延迟。

### 性能指标

至少测量：

- 每秒事件吞吐；
- 采集线程到轮询线程的延迟；
- 队列满时的丢弃数量；
- 设备数量对 CPU 和内存的影响；
- 用户回调耗时对采集链路的影响。

## 6. 暂不做的内容

跨平台后端当前不应扩展为：

- 系统级多光标；
- 输入注入或键盘记录器；
- 文本输入/IME 统一层；
- 绕过平台权限的驱动或提权方案；
- 为每个游戏引擎制作强耦合适配层。
