#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

#include "mklib/mklib.h"

#include <array>
#include <string>
#include <utility>

namespace {

constexpr NSUInteger kMaxDevices = 32;
constexpr CGFloat kWindowWidth = 960.0;
constexpr CGFloat kWindowHeight = 620.0;
constexpr uint16_t kTabUsage = 0x2B;
constexpr uint16_t kEscapeUsage = 0x29;
constexpr uint16_t kMouseButtonUsageMin = 1;
constexpr uint16_t kMouseButtonUsageMax = 8;

struct Player {
    mklib_device_id device_id = 0;
    std::string persistent_id;
    CGPoint position{0, 0};
    NSColor *color = nil;
};

bool is_mouse_kind(mklib_device_kind kind) {
    return kind == MKLIB_DEVICE_MOUSE;
}

void set_player_device(Player &player, mklib_device_id device_id,
                       const mklib_device_info *devices, size_t device_count) {
    player.device_id = device_id;
    player.persistent_id.clear();
    for (size_t index = 0; index < device_count; ++index) {
        if (devices[index].id == device_id) {
            player.persistent_id = devices[index].persistent_id;
            return;
        }
    }
}

void restore_player_device(Player &player, const mklib_device_info *devices,
                           size_t device_count) {
    if (player.persistent_id.empty()) {
        return;
    }
    player.device_id = 0;
    for (size_t index = 0; index < device_count; ++index) {
        if (player.persistent_id == devices[index].persistent_id &&
            is_mouse_kind(devices[index].kind)) {
            player.device_id = devices[index].id;
            return;
        }
    }
}

}  // namespace

@class MouseDemoController;

@interface MouseDemoView : NSView
@property(nonatomic, assign) MouseDemoController *controller;
@end

@interface MouseDemoController : NSObject {
    mklib_handle *_handle;
    NSTimer *_timer;
    std::array<Player, 2> _players;
    mklib_device_info _devices[kMaxDevices];
    size_t _device_count;
    NSString *_status;
    uint64_t _event_count;
    mklib_event _last_event;
    bool _has_last_event;
    bool _permission_denied;
    bool _mouse_capture_active;
}
- (void)startInput;
- (void)stopInput;
- (void)beginMouseCapture;
- (void)endMouseCapture;
- (void)tick:(NSTimer *)timer;
- (void)refreshDevices;
- (void)swapPlayers;
- (void)drawInView:(NSView *)view;
@end

@implementation MouseDemoView
- (BOOL)isFlipped {
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [_controller drawInView:self];
}
@end

@implementation MouseDemoController

- (instancetype)init {
    self = [super init];
    if (self != nil) {
        _players[0].position = CGPointMake(kWindowWidth * 0.28, kWindowHeight * 0.62);
        _players[1].position = CGPointMake(kWindowWidth * 0.72, kWindowHeight * 0.62);
        _players[0].color = [NSColor colorWithCalibratedRed:0.22 green:0.62 blue:1.0 alpha:1.0];
        _players[1].color = [NSColor colorWithCalibratedRed:1.0 green:0.38 blue:0.28 alpha:1.0];
        _status = @"正在初始化 mklib…";

        mklib_config config{};
        config.event_queue_capacity = 8192;
        config.request_input_access = true;
        config.device_kind_mask = MKLIB_DEVICE_MASK_KEYBOARD | MKLIB_DEVICE_MASK_MOUSE;
        if (mklib_create(&config, &_handle) != MKLIB_OK || _handle == nullptr) {
            _status = @"创建 mklib 失败";
            return self;
        }
        [self startInput];
        _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                                   target:self
                                                 selector:@selector(tick:)
                                                 userInfo:nil
                                                  repeats:YES];
    }
    return self;
}

- (void)dealloc {
    [_timer invalidate];
    [self endMouseCapture];
    if (_handle != nullptr) {
        mklib_destroy(&_handle);
    }
}

- (void)startInput {
    if (_handle == nullptr) {
        return;
    }
    const mklib_status status = mklib_start(_handle);
    if (status == MKLIB_OK) {
        _permission_denied = false;
        [self refreshDevices];
        _status = @"按鼠标按钮绑定两个玩家，捕获后按 Esc 释放系统光标";
    } else if (status != MKLIB_ALREADY_RUNNING) {
        _permission_denied = status == MKLIB_PERMISSION_DENIED;
        _status = [NSString stringWithFormat:@"mklib 启动失败：%s",
                   mklib_status_string(status)];
    }
}

- (void)stopInput {
    [self endMouseCapture];
    if (_handle != nullptr) {
        mklib_stop(_handle);
        _device_count = 0;
        _players[0].device_id = 0;
        _players[1].device_id = 0;
    }
}

- (void)beginMouseCapture {
    if (_mouse_capture_active) {
        return;
    }
    if (CGAssociateMouseAndMouseCursorPosition(false) == kCGErrorSuccess) {
        [NSCursor hide];
        _mouse_capture_active = true;
        _status = @"鼠标已捕获，按 Esc 显示系统光标并停止小球控制";
    }
}

- (void)endMouseCapture {
    if (!_mouse_capture_active) {
        return;
    }
    CGAssociateMouseAndMouseCursorPosition(true);
    [NSCursor unhide];
    _mouse_capture_active = false;
    _status = @"鼠标已释放，按鼠标按钮可重新捕获";
}

- (void)refreshDevices {
    if (_handle == nullptr) {
        return;
    }
    size_t count = 0;
    mklib_get_devices(_handle, _devices, kMaxDevices, &count);
    _device_count = count > kMaxDevices ? kMaxDevices : count;
    restore_player_device(_players[0], _devices, _device_count);
    restore_player_device(_players[1], _devices, _device_count);
}

- (void)tick:(NSTimer *)timer {
    (void)timer;
    mklib_event event{};
    while (_handle != nullptr && mklib_poll_event(_handle, &event, 0) == MKLIB_OK) {
        _last_event = event;
        _has_last_event = true;
        ++_event_count;

        if (event.type == MKLIB_KEY_DOWN && event.usage == kEscapeUsage) {
            [self endMouseCapture];
            continue;
        }

        if (event.type == MKLIB_KEY_DOWN && event.usage == kTabUsage &&
            _players[0].device_id != 0 && _players[1].device_id != 0) {
            [self swapPlayers];
            _status = @"已交换两个玩家的鼠标控制";
        }

        if (event.type == MKLIB_MOUSE_BUTTON_DOWN &&
            event.usage >= kMouseButtonUsageMin &&
            event.usage <= kMouseButtonUsageMax) {
            size_t device_index = 0;
            while (device_index < _device_count &&
                   _devices[device_index].id != event.device_id) {
                ++device_index;
            }
            if (device_index < _device_count && is_mouse_kind(_devices[device_index].kind)) {
                if (_players[0].device_id == 0) {
                    set_player_device(_players[0], event.device_id, _devices, _device_count);
                    _status = [NSString stringWithFormat:@"设备 %llu 已绑定给玩家 1",
                               (unsigned long long)event.device_id];
                } else if (_players[1].device_id == 0 &&
                           event.device_id != _players[0].device_id) {
                    set_player_device(_players[1], event.device_id, _devices, _device_count);
                    _status = [NSString stringWithFormat:@"设备 %llu 已绑定给玩家 2",
                               (unsigned long long)event.device_id];
                }
                [self beginMouseCapture];
            }
        }

        Player *player = nullptr;
        if (event.device_id == _players[0].device_id && _players[0].device_id != 0) {
            player = &_players[0];
        } else if (event.device_id == _players[1].device_id && _players[1].device_id != 0) {
            player = &_players[1];
        }
        if (_mouse_capture_active && player != nullptr && event.type == MKLIB_MOUSE_MOVE) {
            if (event.usage == 0x30) {
                player->position.x += static_cast<CGFloat>(event.value);
            } else if (event.usage == 0x31) {
                player->position.y += static_cast<CGFloat>(event.value);
            }
            player->position.x = MAX(80.0, MIN(kWindowWidth - 80.0, player->position.x));
            player->position.y = MAX(250.0, MIN(kWindowHeight - 70.0, player->position.y));
        }
    }
    [self refreshDevices];
    [[NSApp mainWindow].contentView setNeedsDisplay:YES];
}

- (void)swapPlayers {
    std::swap(_players[0].device_id, _players[1].device_id);
    std::swap(_players[0].persistent_id, _players[1].persistent_id);
}

- (void)drawInView:(NSView *)view {
    [[NSColor colorWithCalibratedWhite:0.055 alpha:1.0] setFill];
    NSRectFill(view.bounds);

    NSDictionary *titleAttributes = @{
        NSFontAttributeName: [NSFont systemFontOfSize:26 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor whiteColor]
    };
    [@"mklib · 多鼠标 Demo" drawAtPoint:NSMakePoint(36, 28)
                         withAttributes:titleAttributes];

    NSDictionary *bodyAttributes = @{
        NSFontAttributeName: [NSFont systemFontOfSize:14],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.78 alpha:1.0]
    };
    [@"按鼠标按钮绑定两个玩家    按 Tab 交换控制    捕获后按 Esc 释放光标并停止控制"
        drawAtPoint:NSMakePoint(38, 72) withAttributes:bodyAttributes];
    [_status drawAtPoint:NSMakePoint(38, 104) withAttributes:bodyAttributes];

    NSDictionary *deviceAttributes = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
    };
    NSString *deviceText = [NSString stringWithFormat:@"已发现输入设备：%zu", _device_count];
    [deviceText drawAtPoint:NSMakePoint(38, 140) withAttributes:deviceAttributes];
    NSString *eventText = _has_last_event
        ? [NSString stringWithFormat:@"mklib 事件：%llu 设备:%llu 类型:%u UsagePage:0x%02x Usage:0x%02x 值:%d",
           (unsigned long long)_event_count,
           (unsigned long long)_last_event.device_id,
           _last_event.type,
           _last_event.usage_page,
           _last_event.usage,
           _last_event.value]
        : @"mklib 事件：0（请按鼠标按钮或移动鼠标）";
    [eventText drawAtPoint:NSMakePoint(38, 158) withAttributes:deviceAttributes];

    for (size_t index = 0; index < _device_count; ++index) {
        const auto &device = _devices[index];
        NSString *name = [NSString stringWithUTF8String:device.product[0] != '\0'
            ? device.product : "Unknown device"];
        NSString *kind = device.kind == MKLIB_DEVICE_MOUSE ? @"mouse"
            : device.kind == MKLIB_DEVICE_TOUCHPAD ? @"touchpad"
            : device.kind == MKLIB_DEVICE_KEYBOARD ? @"keyboard" : @"unknown";
        NSString *line = [NSString stringWithFormat:@"[%llu] %@  %@  %s  %s  VID:%04x PID:%04x",
                          (unsigned long long)device.id, name, kind, device.transport,
                          device.manufacturer, device.vendor_id, device.product_id];
        [line drawAtPoint:NSMakePoint(38, 182 + 18 * index)
            withAttributes:deviceAttributes];
    }

    NSRect arena = NSMakeRect(32, 250, kWindowWidth - 64, kWindowHeight - 278);
    [[NSColor colorWithCalibratedWhite:0.12 alpha:1.0] setFill];
    NSRectFill(arena);
    [[NSColor colorWithCalibratedWhite:0.22 alpha:1.0] setStroke];
    NSFrameRectWithWidth(arena, 1.0);

    for (size_t index = 0; index < _players.size(); ++index) {
        const Player &player = _players[index];
        [player.color setFill];
        NSRect circle = NSMakeRect(player.position.x - 32, player.position.y - 32, 64, 64);
        [[NSBezierPath bezierPathWithOvalInRect:circle] fill];
        NSString *label = [NSString stringWithFormat:@"玩家 %zu\n鼠标 %llu", index + 1,
                           (unsigned long long)player.device_id];
        NSDictionary *labelAttributes = @{
            NSFontAttributeName: [NSFont systemFontOfSize:13 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: [NSColor whiteColor]
        };
        [label drawAtPoint:NSMakePoint(player.position.x - 34, player.position.y + 42)
            withAttributes:labelAttributes];
    }

    if (_permission_denied) {
        NSDictionary *warningAttributes = @{
            NSFontAttributeName: [NSFont systemFontOfSize:16 weight:NSFontWeightSemibold],
            NSForegroundColorAttributeName: [NSColor systemRedColor]
        };
        [@"请打开：系统设置 → 隐私与安全性 → 输入监控 → mklib Mouse Demo"
            drawAtPoint:NSMakePoint(38, kWindowHeight - 32)
            withAttributes:warningAttributes];
    }
    (void)view;
}

@end

@interface MouseDemoAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) MouseDemoController *controller;
@end

@implementation MouseDemoAppDelegate
- (void)applicationDidBecomeActive:(NSNotification *)notification {
    (void)notification;
    [_controller startInput];
}

- (void)applicationDidResignActive:(NSNotification *)notification {
    (void)notification;
    [_controller stopInput];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    [_controller stopInput];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)application {
    (void)application;
    return YES;
}
@end

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        MouseDemoAppDelegate *delegate = [[MouseDemoAppDelegate alloc] init];
        application.delegate = delegate;
        MouseDemoController *controller = [[MouseDemoController alloc] init];
        delegate.controller = controller;

        MouseDemoView *view = [[MouseDemoView alloc]
            initWithFrame:NSMakeRect(0, 0, kWindowWidth, kWindowHeight)];
        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, kWindowWidth, kWindowHeight)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.title = @"mklib Mouse Demo";
        view.controller = controller;
        window.contentView = view;
        [window center];
        [window makeKeyAndOrderFront:nil];
        [application activateIgnoringOtherApps:YES];
        [application run];
    }
    return 0;
}
