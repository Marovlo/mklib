#import <Cocoa/Cocoa.h>

#include "mklib/mklib.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace {

constexpr NSUInteger kMaxDevices = 32;
constexpr CGFloat kWindowWidth = 960.0;
constexpr CGFloat kWindowHeight = 620.0;

bool is_down(const mklib_event &event, uint16_t usage) {
    return event.type == MKLIB_KEY_DOWN && event.usage == usage;
}

struct Player {
    mklib_device_id device_id = 0;
    CGPoint position{0, 0};
    NSColor *color = nil;
    std::string persistent_id;
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
};

void reset_player_input(Player &player) {
    player.up = false;
    player.down = false;
    player.left = false;
    player.right = false;
}

void update_player_input(Player &player, const mklib_event &event) {
    const bool pressed = event.type == MKLIB_KEY_DOWN;
    switch (event.usage) {
        case 0x1A: player.up = pressed; break;
        case 0x16: player.down = pressed; break;
        case 0x04: player.left = pressed; break;
        case 0x07: player.right = pressed; break;
        default: break;
    }
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
        if (player.persistent_id == devices[index].persistent_id) {
            player.device_id = devices[index].id;
            return;
        }
    }
}

}  // namespace

@class DemoController;

@interface DemoAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) DemoController *controller;
@end

@interface DemoView : NSView
@property(nonatomic, assign) DemoController *controller;
@end

@interface DemoController : NSObject {
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
    bool _permission_pending;
    uint64_t _cocoa_event_count;
    unsigned short _last_cocoa_key_code;
    id _cocoa_monitor;
}
- (instancetype)init;
- (void)startInput;
- (void)stopInput;
- (void)recordCocoaEvent:(NSEvent *)event;
- (void)tick:(NSTimer *)timer;
- (void)refreshDevices;
- (void)swapPlayers;
- (void)drawInView:(NSView *)view;
@end

@implementation DemoView
- (BOOL)isFlipped {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)becomeFirstResponder {
    return YES;
}
- (void)keyDown:(NSEvent *)event {
    [self.controller recordCocoaEvent:event];
}
- (void)keyUp:(NSEvent *)event {
    [self.controller recordCocoaEvent:event];
}
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [self.controller drawInView:self];
}
@end

@implementation DemoController

- (instancetype)init {
    self = [super init];
    if (self != nil) {
        _players[0].position = CGPointMake(kWindowWidth * 0.28, kWindowHeight * 0.62);
        _players[1].position = CGPointMake(kWindowWidth * 0.72, kWindowHeight * 0.62);
        _players[0].color = [NSColor colorWithCalibratedRed:0.22 green:0.62 blue:1.0 alpha:1.0];
        _players[1].color = [NSColor colorWithCalibratedRed:1.0 green:0.38 blue:0.28 alpha:1.0];
        _status = @"正在初始化 mklib…";
        _permission_pending = mklib_input_access_status() != MKLIB_ACCESS_GRANTED;
        __weak DemoController *weak_self = self;
        _cocoa_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskKeyDown | NSEventMaskKeyUp)
                                                                  handler:^NSEvent *(NSEvent *event) {
            DemoController *strong_self = weak_self;
            if (strong_self != nil) {
                ++strong_self->_cocoa_event_count;
                strong_self->_last_cocoa_key_code = event.keyCode;
            }
            return event;
        }];

        mklib_config config{};
        config.event_queue_capacity = 4096;
        config.request_input_access = true;
        if (mklib_create(&config, &_handle) != MKLIB_OK || _handle == nullptr) {
            _status = @"创建 mklib 失败";
            return self;
        }
        const mklib_status start_status = mklib_start(_handle);
        if (start_status != MKLIB_OK) {
            _permission_denied = start_status == MKLIB_PERMISSION_DENIED;
            _status = [NSString stringWithFormat:@"mklib 启动失败：%s",
                       mklib_status_string(start_status)];
        } else {
            [self refreshDevices];
            _status = _permission_pending
                ? @"等待输入监控授权，授权后会自动重新连接"
                : @"已启动，先按任意键绑定玩家 1 和玩家 2";
        }
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
    if (_cocoa_monitor != nil) {
        [NSEvent removeMonitor:_cocoa_monitor];
    }
    if (_handle != nullptr) {
        mklib_destroy(&_handle);
    }
}

- (void)startInput {
    if (_handle == nullptr) {
        return;
    }
    const mklib_access_status access = mklib_input_access_status();
    _permission_pending = access != MKLIB_ACCESS_GRANTED;
    const mklib_status status = mklib_start(_handle);
    if (status == MKLIB_OK) {
        [self refreshDevices];
        _permission_denied = false;
        _status = _permission_pending
            ? @"等待输入监控授权，授权后会自动重新连接"
            : @"已启动，先按任意键绑定玩家 1 和玩家 2";
    } else if (status != MKLIB_ALREADY_RUNNING) {
        _permission_denied = status == MKLIB_PERMISSION_DENIED;
        _status = [NSString stringWithFormat:@"重新启动输入失败：%s",
                   mklib_status_string(status)];
    }
}

- (void)stopInput {
    if (_handle != nullptr) {
        mklib_stop(_handle);
        _device_count = 0;
        reset_player_input(_players[0]);
        reset_player_input(_players[1]);
    }
}

- (void)recordCocoaEvent:(NSEvent *)event {
    ++_cocoa_event_count;
    _last_cocoa_key_code = event.keyCode;
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
        if (event.type == MKLIB_KEY_DOWN && _players[0].device_id == 0) {
            set_player_device(_players[0], event.device_id, _devices, _device_count);
            _status = [NSString stringWithFormat:@"设备 %llu 已绑定给玩家 1",
                       (unsigned long long)event.device_id];
        } else if (event.type == MKLIB_KEY_DOWN &&
                   _players[1].device_id == 0 &&
                   event.device_id != _players[0].device_id) {
            set_player_device(_players[1], event.device_id, _devices, _device_count);
            _status = [NSString stringWithFormat:@"设备 %llu 已绑定给玩家 2",
                       (unsigned long long)event.device_id];
        } else if (is_down(event, 0x2B) &&
                   _players[0].device_id != 0 &&
                   _players[1].device_id != 0) {
            [self swapPlayers];
            _status = @"已交换两个玩家的键盘绑定";
        }

        Player *player = nullptr;
        if (event.device_id == _players[0].device_id && _players[0].device_id != 0) {
            player = &_players[0];
        } else if (event.device_id == _players[1].device_id && _players[1].device_id != 0) {
            player = &_players[1];
        }
        if (player != nullptr) {
            update_player_input(*player, event);
        }
    }
    constexpr CGFloat speed = 5.0;
    for (Player &player : _players) {
        if (player.up) {
            player.position.y -= speed;
        }
        if (player.down) {
            player.position.y += speed;
        }
        if (player.left) {
            player.position.x -= speed;
        }
        if (player.right) {
            player.position.x += speed;
        }
        player.position.x = MAX(80.0, MIN(kWindowWidth - 80.0, player.position.x));
        player.position.y = MAX(230.0, MIN(kWindowHeight - 70.0, player.position.y));
    }
    [self refreshDevices];
    NSView *view = [NSApp mainWindow].contentView;
    [view setNeedsDisplay:YES];
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
    [@"mklib · 多键盘本地多人 Demo" drawAtPoint:NSMakePoint(36, 28)
                                  withAttributes:titleAttributes];

    NSDictionary *bodyAttributes = @{
        NSFontAttributeName: [NSFont systemFontOfSize:14],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.78 alpha:1.0]
    };
    NSString *instructions = @"先按任意键绑定玩家 1，再按另一键盘任意键绑定玩家 2    两台键盘都绑定后按 Tab 交换    WASD：移动对象";
    [instructions drawAtPoint:NSMakePoint(38, 72) withAttributes:bodyAttributes];
    [_status drawAtPoint:NSMakePoint(38, 104) withAttributes:bodyAttributes];

    NSDictionary *deviceAttributes = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
    };
    NSString *deviceText = [NSString stringWithFormat:@"已发现键盘：%zu", _device_count];
    [deviceText drawAtPoint:NSMakePoint(38, 140) withAttributes:deviceAttributes];
    NSString *eventText = _has_last_event
        ? [NSString stringWithFormat:@"mklib 事件：%llu 设备:%llu 类型:%u UsagePage:0x%02x Usage:0x%02x 值:%d",
           (unsigned long long)_event_count,
           (unsigned long long)_last_event.device_id,
           _last_event.type,
           _last_event.usage_page,
           _last_event.usage,
           _last_event.value]
        : @"mklib 事件：0（请按任意物理键测试）";
    NSString *cocoaText = [NSString stringWithFormat:@"Cocoa 焦点事件：%llu，最近 keyCode：%hu",
                           (unsigned long long)_cocoa_event_count,
                           _last_cocoa_key_code];
    [eventText drawAtPoint:NSMakePoint(38, 158) withAttributes:deviceAttributes];
    [cocoaText drawAtPoint:NSMakePoint(38, 176) withAttributes:deviceAttributes];
    for (size_t index = 0; index < _device_count; ++index) {
        const auto &device = _devices[index];
        NSString *name = [NSString stringWithUTF8String:device.product[0] != '\0' ? device.product : "Unknown keyboard"];
        NSString *line = [NSString stringWithFormat:@"[%llu] %@  %s  %s  VID:%04x PID:%04x",
                          (unsigned long long)device.id, name, device.transport,
                          device.manufacturer, device.vendor_id, device.product_id];
        [line drawAtPoint:NSMakePoint(38, 200 + 18 * index) withAttributes:deviceAttributes];
    }

    NSRect arena = NSMakeRect(32, 260, kWindowWidth - 64, kWindowHeight - 288);
    [[NSColor colorWithCalibratedWhite:0.12 alpha:1.0] setFill];
    NSRectFill(arena);
    [[NSColor colorWithCalibratedWhite:0.22 alpha:1.0] setStroke];
    NSFrameRectWithWidth(arena, 1.0);

    for (size_t index = 0; index < _players.size(); ++index) {
        const Player &player = _players[index];
        [player.color setFill];
        NSRect circle = NSMakeRect(player.position.x - 32, player.position.y - 32, 64, 64);
        NSBezierPath *path = [NSBezierPath bezierPathWithOvalInRect:circle];
        [path fill];
        NSString *label = [NSString stringWithFormat:@"玩家 %zu\n键盘 %llu", index + 1,
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
        [@"请打开：系统设置 → 隐私与安全性 → 输入监控 → mklib Demo"
            drawAtPoint:NSMakePoint(38, kWindowHeight - 32) withAttributes:warningAttributes];
    }
    (void)view;
}

@end

@implementation DemoAppDelegate
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
        DemoAppDelegate *delegate = [[DemoAppDelegate alloc] init];
        application.delegate = delegate;

        DemoController *controller = [[DemoController alloc] init];
        delegate.controller = controller;
        DemoView *view = [[DemoView alloc] initWithFrame:NSMakeRect(0, 0, kWindowWidth, kWindowHeight)];
        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, kWindowWidth, kWindowHeight)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.title = @"mklib Demo";
        view.controller = controller;
        window.contentView = view;
        window.initialFirstResponder = view;
        [window center];
        [window makeKeyAndOrderFront:nil];
        [window makeFirstResponder:view];
        [application activateIgnoringOtherApps:YES];
        [application run];
    }
    return 0;
}
