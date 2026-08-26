# mklib 二次开发 API 指南

## 1. 适用范围

`mklib` 为游戏或本地多人应用提供按物理设备区分的输入事件。当前已实现 macOS 和 Windows Raw Input 后端；Linux 仍为可编译占位后端。

库不负责：

- 修改系统键盘布局、IME 或文本输入；
- 创建系统级多光标；
- 注入键盘、鼠标事件；
- 自动改造没有接入 `mklib` 的游戏。

公共接口是 C ABI，头文件为 `include/mklib/mklib.h`。C++、C、Rust、C#、Python 等语言应以该头文件为边界制作绑定。

## 2. 生命周期

标准顺序如下：

```cpp
mklib_handle *handle = nullptr;
mklib_config config{};
mklib_config_init(&config);
config.device_kind_mask = MKLIB_DEVICE_MASK_KEYBOARD;

mklib_status status = mklib_create(&config, &handle);
if (status == MKLIB_OK) {
    status = mklib_start(handle);
}

if (handle != nullptr) {
    mklib_stop(handle);
    mklib_destroy(&handle);
}
```

约定：

- `mklib_create` 只创建句柄，不访问输入设备；
- `mklib_start` 建立平台采集后端并枚举当前设备；
- `mklib_stop` 停止采集、唤醒等待中的轮询、清空待消费事件并释放后端资源；
- `mklib_destroy` 释放句柄，并将调用方指针置为 `nullptr`；
- 同一个句柄不能并发执行 `start`、`stop` 或 `destroy`；库会串行化 `start` 和 `stop`，调用方仍应保证销毁期间没有其他线程使用句柄；
- `stop` 后可以再次 `start`，设备的临时 `id` 可能改变；
- `destroy` 之后不能再访问原句柄。

## 3. 配置

推荐使用 `mklib_config_init` 初始化配置，而不是依赖结构体零初始化：

```cpp
mklib_config config{};
if (mklib_config_init(&config) != MKLIB_OK) {
    return false;
}
config.event_queue_capacity = 4096;
config.request_input_access = true;
config.device_kind_mask = MKLIB_DEVICE_MASK_KEYBOARD |
                          MKLIB_DEVICE_MASK_MOUSE;
```

`struct_size` 和 `abi_version` 用于后续 ABI 演进。库会根据 `struct_size` 读取配置，未知的保留字段必须保持为 0。当前 ABI 版本可通过 `mklib_abi_version()` 查询。

配置字段：

| 字段 | 说明 |
| --- | --- |
| `device_callback` | 设备添加/移除回调，可为 `nullptr` |
| `event_callback` | 输入事件回调，可为 `nullptr` |
| `user_data` | 原样传给回调的指针，库不拥有其生命周期 |
| `event_queue_capacity` | 轮询队列容量，0 使用默认值 4096 |
| `request_input_access` | 启动时请求平台输入权限 |
| `device_kind_mask` | 设备类别掩码；0 保持默认，只监听键盘 |

### 3.1 Windows 窗口消息接入

Windows 的窗口接入 API 与其他公共 API 一样位于 `include/mklib/mklib.h`，不改变 `mklib_config` 布局和现有 ABI 版本。库不会创建隐藏窗口、子类化窗口或自动接管宿主的窗口过程。

宿主必须在创建窗口后、`mklib_start` 前调用 `mklib_windows_attach_window`，传入窗口句柄的整数值（`HWND` 转为 `uintptr_t`）。如果宿主已经注册 Raw Input，传入 flags 为 0；宿主窗口过程收到消息后调用：

```cpp
bool handled = false;
mklib_windows_process_message(
    handle, static_cast<uint32_t>(message),
    static_cast<uintptr_t>(wparam), static_cast<intptr_t>(lparam), &handled);
```

宿主仍拥有窗口消息的最终处理权。对 `WM_INPUT`，调用库处理后仍应按宿主窗口框架要求调用 `DefWindowProc`；库只读取消息并生成 mklib 事件，不注入或修改系统输入。对 `WM_INPUT_DEVICE_CHANGE` 也应转发消息，以获得加入/移除通知。窗口过程不要在 `mklib` 回调中停止或销毁句柄。

如果宿主没有自己的 Raw Input 注册，可以显式使用 `MKLIB_WINDOWS_ATTACH_REGISTER_RAW_INPUT`（可与 `MKLIB_WINDOWS_ATTACH_INPUT_SINK` 组合）让库为该窗口调用 `RegisterRawInputDevices`。这是一次明确的所有权选择，不会在 `start` 时无条件覆盖宿主注册。库停止时只撤销自己注册的类别；宿主自有注册应由宿主负责。若引擎已经占用键盘或鼠标类别，必须使用 flags 为 0 的转发模式，不能再次注册同一类别。

窗口必须在 `detach` 或 `stop` 前保持有效；窗口销毁后不得再转发消息。失焦时转发 `WM_KILLFOCUS`/`WM_ACTIVATEAPP`，或者由宿主调用 `mklib_windows_reset_input_state`，库会清空各设备的内部按下集合，不伪造释放消息。设备移除和 `stop` 也会清空对应状态。

## 4. 设备与身份

`mklib_get_devices` 获取当前句柄已监听的设备：

设备回调和事件回调都是可选的；如果同时启用回调和轮询，它们共享同一事件队列，回调不会产生第二份轮询事件。

```cpp
mklib_device_info devices[64]{};
size_t device_count = 0;
mklib_get_devices(handle, devices, 64, &device_count);
```

当 `capacity` 不足时，`out_count` 仍返回设备总数，数组只填充前 `capacity` 项。可先传 `devices = nullptr, capacity = 0` 查询数量。

字段含义：

- `id`：当前 `mklib` 运行期间的临时 ID；Windows 上由 `RAWINPUTHEADER.hDevice` 映射而来，`hDevice` 只作当前连接句柄；
- `persistent_id`：由平台元数据组合的尽可能稳定的身份，用于重连恢复；Windows 上优先包含 Raw Input 设备路径、VID/PID 以及 SetupAPI 可取得的厂商/产品信息，不保证在没有序列号的同型号设备之间唯一；
- `kind`：键盘、鼠标、触控板或未知类别；
- `vendor_id`、`product_id`、`location_id`：平台设备元数据；
- 字符串字段：始终保证以 `\\0` 结尾，内容可能为空；
- `is_virtual`：平台能够识别为虚拟设备时为真。

游戏应保存 `persistent_id`，不要把 `id`、macOS `IOHIDDeviceRef`、Windows `hDevice` 或 Linux `eventX` 当作永久身份。

## 5. 事件消费

### 5.1 轮询

```cpp
mklib_event event{};
while (mklib_poll_event(handle, &event, 16) == MKLIB_OK) {
    if (event.type == MKLIB_KEY_DOWN) {
        const uint16_t physical_usage = event.usage;
        const mklib_device_id source = event.device_id;
        (void)physical_usage;
        (void)source;
    }
}
```

`timeout_ms` 仅在队列为空时生效：

- `0`：立即返回；
- 大于 `0`：最多等待指定毫秒数；
- `MKLIB_OK`：得到一个事件；
- `MKLIB_TIMEOUT`：等待超时；
- `MKLIB_NOT_RUNNING`：句柄未启动或已经停止。

队列达到容量时丢弃最旧事件，不会无限增长。游戏应在自己的主循环或输入线程中及时消费；高频鼠标移动场景应根据实际速率调整容量。可用 `mklib_get_dropped_event_count` 查询累计丢弃数量，用于诊断容量是否不足。

### 5.2 回调

回调在库内部独立的串行回调队列上执行，不在 macOS HID 采集队列上直接执行。设备枚举发生在 `mklib_start` 的采集初始化过程中，但回调本身是异步排队的，可能在 `mklib_start` 返回后执行。回调不会并行执行，但会按事件顺序排队。回调参数只在本次回调期间有效，调用方需要保存数据时必须复制结构体内容。

回调应快速返回，不应在回调中执行渲染、阻塞 I/O 或等待游戏主线程。不要在回调中调用 `mklib_stop` 或 `mklib_destroy`；库会拒绝从回调队列直接停止，建议设置原子标志，在游戏主循环中停止和销毁。回调中的 `mklib_poll_event` 可能与回调队列无关，但不建议把两种消费方式同时用于同一类业务事件，否则会产生重复或分流语义。

轮询和回调可以同时启用，但两者消费的是同一个有界事件源：回调不会额外复制到另一条轮询队列。

## 6. 事件字段

- `device_id`：事件来源设备；
- `usage_page`、`usage`：原始 HID 用途页和用途码；
- `value`：原始值。键盘/按钮通常为 0 或非 0，鼠标 X/Y 为相对位移增量；
- `timestamp_ns`：平台单调时钟转换后的纳秒时间戳，适合排序和计算间隔，不应直接当作 Unix 时间；
- `repeat`：当前 macOS 后端不生成系统文字重复事件，暂为 `false`；游戏长按应维护按下状态并在自己的帧循环中更新；
- 键盘控制应使用物理 Usage 或平台扫描码，不应把 `usage` 直接当作当前键盘布局下的字符。

鼠标事件约定：

- `MKLIB_MOUSE_BUTTON_DOWN/UP`：Button Usage Page，`usage` 通常为 1 左键、2 右键、3 中键；
- `MKLIB_MOUSE_MOVE`：Generic Desktop Usage Page，`usage` 为 0x30 X 或 0x31 Y，`value` 为相对增量；
- `MKLIB_MOUSE_WHEEL`：Generic Desktop Usage Page，`usage` 为 0x38 Wheel，`value` 为滚轮增量；
- 当前不承诺绝对触摸坐标、多点触摸、手势和系统级多光标。

## 7. 设备回调与释放状态

设备移除回调表示设备不再可用。由于设备断开、休眠或切后台可能缺少按键释放事件，游戏必须在收到移除通知、应用失焦或 `mklib_stop` 后清理该设备的所有按下状态。库不会替游戏维护 WASD、动作键或业务层状态。

## 8. 错误处理

所有返回值都应检查。常见状态：

- `MKLIB_INVALID_ARGUMENT`：空指针、容量和缓冲区不匹配等参数错误；
- `MKLIB_ALREADY_RUNNING`：重复启动；
- `MKLIB_NOT_RUNNING`：未启动或已停止；
- `MKLIB_PERMISSION_DENIED`：平台输入权限不足；
- `MKLIB_PLATFORM_UNSUPPORTED`：当前平台后端尚未实现；
- `MKLIB_INTERNAL_ERROR`：平台对象或内部资源创建失败；
- `MKLIB_TIMEOUT`：轮询在指定时间内没有事件。

用 `mklib_status_string` 转换给用户显示，用 `mklib_platform_name` 显示当前编译后端平台。

## 9. 平台权限

- macOS：最终宿主 App 需要 Input Monitoring 权限；权限属于 App，不属于静态库文件；
- Windows：Raw Input 通常不要求管理员权限；宿主必须负责窗口句柄生命周期、`WM_INPUT`/`WM_INPUT_DEVICE_CHANGE` 转发和是否调用 `RegisterRawInputDevices` 的选择。Raw Input 注册按进程和设备类别受窗口归属约束，库不会无条件覆盖引擎注册；访问受限桌面、远程会话或设备驱动限制可能导致设备不可用。Windows 没有与 macOS Input Monitoring 等价的 mklib 权限请求流程，`mklib_input_access_status` 返回适用状态。
- Linux：计划使用 evdev，进程需要输入设备节点的读取权限，部署优先采用活动会话 ACL 或受控 udev 规则，不建议长期使用 root。

## 10. 接入建议

1. 启动时创建并启动一个句柄；
2. 读取设备列表，向用户展示产品、VID/PID 和 `persistent_id`；
3. 用第一次有效按键/鼠标按钮绑定玩家；
4. 事件只更新输入状态，角色移动和渲染放在游戏帧循环；
5. 设备移除、失焦和停止时清理所有按下状态；
6. 游戏只选择一条主要消费路径，避免同时处理 mklib、SDL、Cocoa 或引擎的同一份普通输入事件。
