#import <Cocoa/Cocoa.h>

#include "mklib/mklib.h"

#include <array>
#include <cstdio>
#include <cstring>
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
};

}  // namespace

@class DemoController;

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
    bool _permission_denied;
}
- (instancetype)init;
- (void)tick:(NSTimer *)timer;
- (void)refreshDevices;
- (void)swapPlayers;
- (void)drawInView:(NSView *)view;
@end

@implementation DemoView
- (BOOL)isFlipped {
    return YES;
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

        mklib_config config{};
        config.event_queue_capacity = 4096;
        config.request_input_access = true;
        if (mklib_create(&config, &_handle) != MKLIB_OK || _handle == nullptr) {
            _status = @"创建 mklib 失败";
            return self;
        }
        if (mklib_start(_handle) != MKLIB_OK) {
            _permission_denied = true;
            _status = @"没有输入监控权限，请在系统设置中允许 mklib Demo";
        } else {
            _status = @"已启动，按任意键查看设备来源";
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
    if (_handle != nullptr) {
        mklib_destroy(&_handle);
    }
}

- (void)refreshDevices {
    if (_handle == nullptr) {
        return;
    }
    size_t count = 0;
    mklib_get_devices(_handle, _devices, kMaxDevices, &count);
    _device_count = count > kMaxDevices ? kMaxDevices : count;
}

- (void)tick:(NSTimer *)timer {
    (void)timer;
    mklib_event event{};
    while (_handle != nullptr && mklib_poll_event(_handle, &event, 0) == MKLIB_OK) {
        if (is_down(event, 0x1E)) {
            _players[0].device_id = event.device_id;
            _status = [NSString stringWithFormat:@"设备 %llu 已绑定给玩家 1",
                       (unsigned long long)event.device_id];
        } else if (is_down(event, 0x1F)) {
            _players[1].device_id = event.device_id;
            _status = [NSString stringWithFormat:@"设备 %llu 已绑定给玩家 2",
                       (unsigned long long)event.device_id];
        } else if (is_down(event, 0x2B)) {
            [self swapPlayers];
            _status = @"已交换两个玩家的键盘绑定";
        }

        Player *player = nullptr;
        if (event.device_id == _players[0].device_id && _players[0].device_id != 0) {
            player = &_players[0];
        } else if (event.device_id == _players[1].device_id && _players[1].device_id != 0) {
            player = &_players[1];
        }
        if (player != nullptr && event.type == MKLIB_KEY_DOWN) {
            constexpr CGFloat speed = 5.0;
            switch (event.usage) {
                case 0x1A: player->position.y -= speed; break;
                case 0x16: player->position.y += speed; break;
                case 0x04: player->position.x -= speed; break;
                case 0x07: player->position.x += speed; break;
                default: break;
            }
            player->position.x = MAX(80.0, MIN(kWindowWidth - 80.0, player->position.x));
            player->position.y = MAX(230.0, MIN(kWindowHeight - 70.0, player->position.y));
        }
    }
    [self refreshDevices];
    [[NSApp mainWindow] displayIfNeeded];
}

- (void)swapPlayers {
    std::swap(_players[0].device_id, _players[1].device_id);
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
    NSString *instructions = @"按 1：将当前按键的键盘绑定给玩家 1    按 2：绑定给玩家 2    按 Tab：交换绑定    WASD：移动对应对象";
    [instructions drawAtPoint:NSMakePoint(38, 72) withAttributes:bodyAttributes];
    [_status drawAtPoint:NSMakePoint(38, 104) withAttributes:bodyAttributes];

    NSDictionary *deviceAttributes = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.65 alpha:1.0]
    };
    NSString *deviceText = [NSString stringWithFormat:@"已发现键盘：%zu", _device_count];
    [deviceText drawAtPoint:NSMakePoint(38, 140) withAttributes:deviceAttributes];
    for (size_t index = 0; index < _device_count; ++index) {
        const auto &device = _devices[index];
        NSString *name = [NSString stringWithUTF8String:device.product[0] != '\0' ? device.product : "Unknown keyboard"];
        NSString *line = [NSString stringWithFormat:@"[%llu] %@  %s  %s  VID:%04x PID:%04x",
                          (unsigned long long)device.id, name, device.transport,
                          device.manufacturer, device.vendor_id, device.product_id];
        [line drawAtPoint:NSMakePoint(38, 164 + 18 * index) withAttributes:deviceAttributes];
    }

    NSRect arena = NSMakeRect(32, 224, kWindowWidth - 64, kWindowHeight - 252);
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

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];

        DemoController *controller = [[DemoController alloc] init];
        DemoView *view = [[DemoView alloc] initWithFrame:NSMakeRect(0, 0, kWindowWidth, kWindowHeight)];
        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, kWindowWidth, kWindowHeight)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.title = @"mklib Demo";
        view.controller = controller;
        window.contentView = view;
        [window center];
        [window makeKeyAndOrderFront:nil];
        [application activateIgnoringOtherApps:YES];
        [application run];
    }
    return 0;
}
