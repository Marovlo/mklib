#import <Cocoa/Cocoa.h>

#include "mklib/mklib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {

constexpr NSUInteger kZoneCount = 2;
constexpr NSUInteger kMaxDevices = 64;
constexpr CGFloat kWindowWidth = 1280.0;
constexpr CGFloat kWindowHeight = 800.0;
constexpr CGFloat kHeaderHeight = 118.0;
constexpr CGFloat kFooterHeight = 54.0;
constexpr CGFloat kBirdRadius = 15.0;
constexpr CGFloat kPipeWidth = 48.0;
constexpr CGFloat kPipeGap = 178.0;
constexpr CGFloat kBirdXRatio = 0.28;
constexpr CGFloat kGravity = 920.0;
constexpr CGFloat kFlapVelocity = -330.0;
constexpr CGFloat kPipeSpeed = 190.0;
constexpr CGFloat kBulletSpeed = kPipeSpeed * 3.0;
constexpr CGFloat kPipeInterval = 1.75;
constexpr CGFloat kShotInterval = 0.5;
constexpr CGFloat kGiftSize = kBirdRadius * 2.0;
constexpr uint16_t kSpaceUsage = 0x2c;
constexpr uint16_t kEnterUsage = 0x28;
constexpr uint16_t kEscapeUsage = 0x29;
constexpr uint16_t kNumberOneUsage = 0x1e;
constexpr uint16_t kNumberTwoUsage = 0x1f;
constexpr uint16_t kMouseLeftUsage = 1;
constexpr uint16_t kMouseRightUsage = 2;

struct Pipe {
    CGFloat x = 0.0;
    CGFloat gap_y = 0.0;
    bool scored = false;
};

struct Bullet {
    CGFloat x = 0.0;
    CGFloat y = 0.0;
    bool active = false;
};

struct Gift {
    CGFloat x = 0.0;
    CGFloat y = 0.0;
    bool active = false;
};

struct PlayerZone {
    mklib_device_id keyboard_id = 0;
    mklib_device_id mouse_id = 0;
    std::string keyboard_product;
    std::string mouse_product;
    CGFloat bird_y = 0.0;
    CGFloat velocity = 0.0;
    CGFloat pipe_time = 0.0;
    CGFloat gift_time = 0.0;
    CGFloat next_gift_time = 1.4;
    CGFloat last_shot_time = -kShotInterval;
    int score = 0;
    bool dead = false;
    uint32_t random_seed = 0;
    std::array<Pipe, 6> pipes{};
    size_t pipe_count = 0;
    std::array<Bullet, 8> bullets{};
    Gift gift{};
};

enum class Screen {
    home,
    binding,
    countdown,
    playing,
    finished
};

NSColor *color_from_hex(unsigned int value, CGFloat alpha = 1.0) {
    return [NSColor colorWithCalibratedRed:((value >> 16) & 0xff) / 255.0
                                     green:((value >> 8) & 0xff) / 255.0
                                      blue:(value & 0xff) / 255.0
                                     alpha:alpha];
}

void fill_rounded(NSRect rect, CGFloat radius, NSColor *color) {
    [color setFill];
    [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius] fill];
}

void stroke_rounded(NSRect rect, CGFloat radius, CGFloat width, NSColor *color) {
    [color setStroke];
    NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius];
    path.lineWidth = width;
    [path stroke];
}

void draw_text(NSString *text, NSPoint point, CGFloat size, NSColor *color,
               NSFontWeight weight = NSFontWeightRegular) {
    NSDictionary *attributes = @{
        NSFontAttributeName: [NSFont systemFontOfSize:size weight:weight],
        NSForegroundColorAttributeName: color
    };
    [text drawAtPoint:point withAttributes:attributes];
}

void draw_centered_text(NSString *text, NSRect rect, CGFloat size, NSColor *color,
                        NSFontWeight weight = NSFontWeightRegular) {
    NSDictionary *attributes = @{
        NSFontAttributeName: [NSFont systemFontOfSize:size weight:weight],
        NSForegroundColorAttributeName: color
    };
    NSSize text_size = [text sizeWithAttributes:attributes];
    [text drawAtPoint:NSMakePoint(NSMidX(rect) - text_size.width * 0.5,
                                  NSMidY(rect) - text_size.height * 0.5)
       withAttributes:attributes];
}

uint32_t next_random(uint32_t &seed) {
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

CGFloat random_gap(CGFloat height, uint32_t &seed) {
    const CGFloat minimum = 108.0;
    const CGFloat maximum = std::max(minimum + 10.0, height - 108.0);
    const CGFloat ratio = static_cast<CGFloat>(next_random(seed) % 10000u) / 10000.0;
    return minimum + (maximum - minimum) * ratio;
}

CGFloat random_gift_y(CGFloat height, uint32_t &seed) {
    const CGFloat minimum = 60.0;
    const CGFloat maximum = std::max(minimum + 10.0, height - 60.0);
    const CGFloat ratio = static_cast<CGFloat>(next_random(seed) % 10000u) / 10000.0;
    return minimum + (maximum - minimum) * ratio;
}

NSString *device_product(const std::string &value, NSString *fallback) {
    if (value.empty()) {
        return fallback;
    }
    NSString *product = [NSString stringWithUTF8String:value.c_str()];
    return product == nil ? fallback : product;
}

}  // namespace

@class FlappyController;

@interface FlappyView : NSView
@property(nonatomic, assign) FlappyController *controller;
@end

@interface FlappyController : NSObject {
    mklib_handle *_handle;
    NSTimer *_timer;
    NSImage *_background;
    NSImage *_bird_up;
    NSImage *_bird_down;
    NSImage *_bullet_image;
    NSImage *_gift_image;
    std::array<PlayerZone, kZoneCount> _zones;
    mklib_device_info _devices[kMaxDevices];
    size_t _device_count;
    Screen _screen;
    CGFloat _countdown;
    CGFloat _last_time;
    CGFloat _game_time;
    uint32_t _random_seed;
    NSString *_notice;
    bool _permission_denied;
}
- (void)startInput;
- (void)stopInput;
- (void)tick:(NSTimer *)timer;
- (void)refreshDevices;
- (void)handleEvent:(const mklib_event &)event;
- (void)handleClick:(NSPoint)point inSize:(NSSize)size;
- (void)startBinding;
- (void)startGame;
- (void)resetToHome;
- (void)updateZone:(PlayerZone &)zone width:(CGFloat)width height:(CGFloat)height delta:(CGFloat)delta;
- (void)drawInView:(NSView *)view;
@end

@implementation FlappyView
- (BOOL)isFlipped {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)becomeFirstResponder {
    return YES;
}
- (void)mouseDown:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    [_controller handleClick:point inSize:self.bounds.size];
}
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [_controller drawInView:self];
}
@end

@implementation FlappyController

- (instancetype)init {
    self = [super init];
    if (self != nil) {
        _screen = Screen::home;
        _random_seed = 0x4d4b4c42u;
        _last_time = static_cast<CGFloat>([NSDate timeIntervalSinceReferenceDate]);
        _notice = @"双区域双人对战：先绑定两台键盘和两只鼠标";
        NSBundle *bundle = [NSBundle mainBundle];
        _background = [[NSImage alloc] initWithContentsOfFile:[bundle pathForResource:@"sky_background" ofType:@"png"]];
        _bird_up = [[NSImage alloc] initWithContentsOfFile:[bundle pathForResource:@"bird_up" ofType:@"png"]];
        _bird_down = [[NSImage alloc] initWithContentsOfFile:[bundle pathForResource:@"bird_down" ofType:@"png"]];
        _bullet_image = [[NSImage alloc] initWithContentsOfFile:[bundle pathForResource:@"bullet" ofType:@"png"]];
        _gift_image = [[NSImage alloc] initWithContentsOfFile:[bundle pathForResource:@"gift" ofType:@"png"]];

        mklib_config config{};
        config.event_queue_capacity = 8192;
        config.request_input_access = true;
        config.device_kind_mask = MKLIB_DEVICE_MASK_KEYBOARD | MKLIB_DEVICE_MASK_MOUSE;
        if (mklib_create(&config, &_handle) != MKLIB_OK || _handle == nullptr) {
            _notice = @"无法初始化键盘和鼠标服务";
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
    if (_handle != nullptr) {
        mklib_destroy(&_handle);
    }
}

- (void)startInput {
    if (_handle == nullptr) {
        return;
    }
    const mklib_status status = mklib_start(_handle);
    if (status == MKLIB_OK || status == MKLIB_ALREADY_RUNNING) {
        _permission_denied = false;
        [self refreshDevices];
    } else {
        _permission_denied = status == MKLIB_PERMISSION_DENIED;
        _notice = [NSString stringWithFormat:@"输入服务启动失败：%s", mklib_status_string(status)];
    }
}

- (void)stopInput {
    if (_handle != nullptr) {
        mklib_stop(_handle);
        _device_count = 0;
    }
}

- (void)refreshDevices {
    if (_handle == nullptr) {
        return;
    }
    size_t count = 0;
    mklib_get_devices(_handle, _devices, kMaxDevices, &count);
    _device_count = std::min(count, kMaxDevices);
    for (PlayerZone &zone : _zones) {
        if (zone.keyboard_id != 0) {
            bool found = false;
            for (size_t index = 0; index < _device_count; ++index) {
                if (_devices[index].id == zone.keyboard_id &&
                    _devices[index].kind == MKLIB_DEVICE_KEYBOARD) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                zone.keyboard_id = 0;
                zone.keyboard_product.clear();
            }
        }
        if (zone.mouse_id != 0) {
            bool found = false;
            for (size_t index = 0; index < _device_count; ++index) {
                if (_devices[index].id == zone.mouse_id &&
                    _devices[index].kind == MKLIB_DEVICE_MOUSE) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                zone.mouse_id = 0;
                zone.mouse_product.clear();
            }
        }
    }
}

- (bool)deviceIsBound:(mklib_device_id)device_id {
    for (const PlayerZone &zone : _zones) {
        if (zone.keyboard_id == device_id || zone.mouse_id == device_id) {
            return true;
        }
    }
    return false;
}

- (void)bindDevice:(mklib_device_id)device_id kind:(mklib_device_kind)kind zone:(NSUInteger)zone_index {
    if (zone_index >= kZoneCount || [self deviceIsBound:device_id]) {
        _notice = @"这台设备已经绑定，请使用另一台设备";
        return;
    }
    for (size_t index = 0; index < _device_count; ++index) {
        if (_devices[index].id != device_id || _devices[index].kind != kind) {
            continue;
        }
        if (kind == MKLIB_DEVICE_KEYBOARD) {
            _zones[zone_index].keyboard_id = device_id;
            _zones[zone_index].keyboard_product = _devices[index].product;
        } else {
            _zones[zone_index].mouse_id = device_id;
            _zones[zone_index].mouse_product = _devices[index].product;
        }
        break;
    }
    bool ready = true;
    for (const PlayerZone &zone : _zones) {
        if (zone.keyboard_id == 0 || zone.mouse_id == 0) {
            ready = false;
            break;
        }
    }
    if (ready) {
        _countdown = 3.0;
        _screen = Screen::countdown;
        _notice = @"两位玩家全部就绪，马上起飞";
    } else {
        _notice = @"绑定成功，请继续连接另一台键盘和鼠标";
    }
}

- (void)handleEvent:(const mklib_event &)event {
    if (_screen == Screen::home) {
        if (event.type == MKLIB_KEY_DOWN && event.usage == kEnterUsage) {
            [self startBinding];
        }
        return;
    }
    if (event.type == MKLIB_KEY_DOWN && event.usage == kEscapeUsage && _screen != Screen::home) {
        [self resetToHome];
        return;
    }
    if (_screen == Screen::binding) {
        if (event.type == MKLIB_KEY_DOWN &&
            (event.usage == kNumberOneUsage || event.usage == kNumberTwoUsage)) {
            [self bindDevice:event.device_id kind:MKLIB_DEVICE_KEYBOARD
                         zone:event.usage == kNumberOneUsage ? 0 : 1];
        } else if (event.type == MKLIB_MOUSE_BUTTON_DOWN &&
                   (event.usage == kMouseLeftUsage || event.usage == kMouseRightUsage)) {
            [self bindDevice:event.device_id kind:MKLIB_DEVICE_MOUSE
                         zone:event.usage == kMouseLeftUsage ? 0 : 1];
        }
        return;
    }
    if (_screen == Screen::playing) {
        if (event.type == MKLIB_KEY_DOWN && event.usage == kSpaceUsage) {
            for (NSUInteger index = 0; index < kZoneCount; ++index) {
                if (_zones[index].keyboard_id == event.device_id && !_zones[index].dead) {
                    _zones[index].velocity = kFlapVelocity;
                    break;
                }
            }
        } else if (event.type == MKLIB_MOUSE_BUTTON_DOWN &&
                   (event.usage == kMouseLeftUsage || event.usage == kMouseRightUsage)) {
            for (NSUInteger index = 0; index < kZoneCount; ++index) {
                if (_zones[index].mouse_id == event.device_id && !_zones[index].dead &&
                    _game_time - _zones[index].last_shot_time >= kShotInterval) {
                    for (Bullet &bullet : _zones[index].bullets) {
                        if (!bullet.active) {
                            const CGFloat zone_width = [NSApp mainWindow].contentView.bounds.size.width / 2.0;
                            bullet.x = zone_width * kBirdXRatio + kBirdRadius + 5.0;
                            bullet.y = _zones[index].bird_y;
                            bullet.active = true;
                            _zones[index].last_shot_time = _game_time;
                            break;
                        }
                    }
                    break;
                }
            }
        }
    } else if (_screen == Screen::finished && event.type == MKLIB_KEY_DOWN && event.usage == kEnterUsage) {
        [self resetToHome];
    }
}

- (void)handleClick:(NSPoint)point inSize:(NSSize)size {
    if (_screen == Screen::home && point.y >= 510.0 && point.y <= 570.0 &&
        point.x >= size.width * 0.5 - 150.0 && point.x <= size.width * 0.5 + 150.0) {
        [self startBinding];
    }
}

- (void)startBinding {
    for (PlayerZone &zone : _zones) {
        zone = PlayerZone{};
    }
    _screen = Screen::binding;
    _notice = @"键盘按 1/2 绑定左右区；鼠标按左键/右键绑定左右区";
}

- (void)startGame {
    const CGFloat arena_height = kWindowHeight - kHeaderHeight - kFooterHeight;
    for (NSUInteger index = 0; index < kZoneCount; ++index) {
        PlayerZone &zone = _zones[index];
        zone.bird_y = arena_height * 0.48;
        zone.velocity = 0.0;
        zone.pipe_time = 0.0;
        zone.gift_time = 0.0;
        zone.next_gift_time = 1.0 + static_cast<CGFloat>((next_random(_random_seed) % 120) / 100.0);
        zone.last_shot_time = -kShotInterval;
        zone.score = 0;
        zone.dead = false;
        zone.pipe_count = 0;
        zone.gift = Gift{};
        for (Bullet &bullet : zone.bullets) {
            bullet = Bullet{};
        }
        zone.random_seed = _random_seed + static_cast<uint32_t>(index * 977u + 101u);
    }
    _game_time = 0.0;
    _screen = Screen::playing;
    _notice = @"键盘 Space 控制飞行；对应鼠标按键发射子弹（每 0.5 秒一次）";
}

- (void)resetToHome {
    _screen = Screen::home;
    for (PlayerZone &zone : _zones) {
        zone = PlayerZone{};
    }
    _notice = @"双区域双人对战：先绑定两台键盘和两只鼠标";
}

- (void)updateZone:(PlayerZone &)zone width:(CGFloat)width height:(CGFloat)height delta:(CGFloat)delta {
    if (zone.dead) {
        return;
    }
    zone.velocity += kGravity * delta;
    zone.bird_y += zone.velocity * delta;
    zone.pipe_time += delta;
    zone.gift_time += delta;
    if (zone.pipe_time >= kPipeInterval) {
        zone.pipe_time -= kPipeInterval;
        if (zone.pipe_count < zone.pipes.size()) {
            Pipe &pipe = zone.pipes[zone.pipe_count++];
            pipe.x = width + 90.0;
            pipe.gap_y = random_gap(height, zone.random_seed);
            pipe.scored = false;
        }
    }
    if (!zone.gift.active && zone.gift_time >= zone.next_gift_time) {
        zone.gift_time = 0.0;
        zone.next_gift_time = 1.4 + static_cast<CGFloat>(next_random(zone.random_seed) % 180) / 100.0;
        zone.gift.x = width + 70.0;
        zone.gift.y = random_gift_y(height, zone.random_seed);
        zone.gift.active = true;
    }
    for (size_t index = 0; index < zone.pipe_count; ++index) {
        Pipe &pipe = zone.pipes[index];
        pipe.x -= kPipeSpeed * delta;
        if (!pipe.scored && pipe.x + kPipeWidth < width * kBirdXRatio) {
            pipe.scored = true;
            ++zone.score;
        }
    }
    if (zone.gift.active) {
        zone.gift.x -= kPipeSpeed * delta;
        if (zone.gift.x < -kGiftSize) {
            zone.gift.active = false;
        }
    }
    for (Bullet &bullet : zone.bullets) {
        if (bullet.active) {
            bullet.x += kBulletSpeed * delta;
            if (bullet.x > width + 80.0) {
                bullet.active = false;
            }
        }
    }
    if (zone.gift.active) {
        for (Bullet &bullet : zone.bullets) {
            if (bullet.active && std::abs(bullet.x - zone.gift.x) < kGiftSize &&
                std::abs(bullet.y - zone.gift.y) < kGiftSize) {
                bullet.active = false;
                zone.gift.active = false;
                ++zone.score;
                break;
            }
        }
    }
    size_t first_live_pipe = 0;
    while (first_live_pipe < zone.pipe_count && zone.pipes[first_live_pipe].x + kPipeWidth < -20.0) {
        ++first_live_pipe;
    }
    if (first_live_pipe > 0) {
        for (size_t index = first_live_pipe; index < zone.pipe_count; ++index) {
            zone.pipes[index - first_live_pipe] = zone.pipes[index];
        }
        zone.pipe_count -= first_live_pipe;
    }
    const CGFloat bird_x = width * kBirdXRatio;
    if (zone.bird_y - kBirdRadius < 0.0 || zone.bird_y + kBirdRadius > height) {
        zone.dead = true;
        return;
    }
    for (const Bullet &bullet : zone.bullets) {
        if (bullet.active && std::abs(bullet.x - bird_x) < kBirdRadius &&
            std::abs(bullet.y - zone.bird_y) < kBirdRadius) {
            zone.dead = true;
            return;
        }
    }
    for (size_t index = 0; index < zone.pipe_count; ++index) {
        const Pipe &pipe = zone.pipes[index];
        const bool horizontal_hit = bird_x + kBirdRadius > pipe.x &&
                                    bird_x - kBirdRadius < pipe.x + kPipeWidth;
        const bool vertical_hit = zone.bird_y - kBirdRadius < pipe.gap_y - kPipeGap * 0.5 ||
                                  zone.bird_y + kBirdRadius > pipe.gap_y + kPipeGap * 0.5;
        if (horizontal_hit && vertical_hit) {
            zone.dead = true;
            return;
        }
    }
}

- (void)tick:(NSTimer *)timer {
    (void)timer;
    const CGFloat now = static_cast<CGFloat>([NSDate timeIntervalSinceReferenceDate]);
    CGFloat delta = now - _last_time;
    _last_time = now;
    if (delta < 0.0 || delta > 0.1) {
        delta = 0.016;
    }
    mklib_event event{};
    while (_handle != nullptr && mklib_poll_event(_handle, &event, 0) == MKLIB_OK) {
        [self handleEvent:event];
    }
    [self refreshDevices];
    if (_screen == Screen::countdown) {
        _countdown -= delta;
        if (_countdown <= 0.0) {
            [self startGame];
        }
    } else if (_screen == Screen::playing) {
        _game_time += delta;
        const NSSize size = [NSApp mainWindow].contentView.bounds.size;
        const CGFloat arena_height = size.height - kHeaderHeight - kFooterHeight;
        const CGFloat zone_width = size.width / 2.0;
        bool all_dead = true;
        for (NSUInteger index = 0; index < kZoneCount; ++index) {
            [self updateZone:_zones[index] width:zone_width height:arena_height delta:delta];
            if (!_zones[index].dead) {
                all_dead = false;
            }
        }
        if (all_dead) {
            _screen = Screen::finished;
            _notice = @"本局结束，按 Enter 回到大厅重新开始";
        }
    }
    [NSApp mainWindow].contentView.needsDisplay = YES;
}

- (void)drawBackground:(NSRect)bounds {
    if (_background != nil) {
        const NSSize image_size = _background.size;
        const CGFloat scale = std::max(bounds.size.width / image_size.width,
                                       bounds.size.height / image_size.height);
        const NSSize draw_size = NSMakeSize(image_size.width * scale, image_size.height * scale);
        const NSRect draw_rect = NSMakeRect(NSMidX(bounds) - draw_size.width * 0.5,
                                            NSMidY(bounds) - draw_size.height * 0.5,
                                            draw_size.width, draw_size.height);
        [_background drawInRect:draw_rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
    } else {
        [color_from_hex(0x67d5e7) setFill];
        NSRectFill(bounds);
    }
    [color_from_hex(0x073b58, 0.28) setFill];
    NSRectFill(bounds);
}

- (void)drawBrand:(NSRect)bounds subtitle:(NSString *)subtitle {
    fill_rounded(NSMakeRect(42.0, 30.0, 170.0, 34.0), 17.0, color_from_hex(0x0b4460, 0.82));
    draw_text(@"MKLIB  /  ARCADE", NSMakePoint(58.0, 40.0), 12.0, [NSColor whiteColor], NSFontWeightBold);
    draw_text(subtitle, NSMakePoint(42.0, 82.0), 15.0, color_from_hex(0xffffff, 0.84));
    (void)bounds;
}

- (void)drawHome:(NSRect)bounds {
    [self drawBackground:bounds];
    [self drawBrand:bounds subtitle:@"TWO-LANE  /  KEYBOARD + MOUSE"];
    draw_centered_text(@"飞吧，小鸟们", NSMakeRect(0.0, 120.0, bounds.size.width, 64.0),
                       32.0, color_from_hex(0x073b58), NSFontWeightBold);
    draw_centered_text(@"固定双区域 · 键盘控制飞行 · 鼠标发射子弹收集礼物", NSMakeRect(0.0, 190.0, bounds.size.width, 30.0),
                       15.0, color_from_hex(0x073b58, 0.78));
    NSRect panel = NSMakeRect(140.0, 270.0, bounds.size.width - 280.0, 280.0);
    fill_rounded(panel, 28.0, color_from_hex(0x073b58, 0.84));
    stroke_rounded(panel, 28.0, 1.0, color_from_hex(0xffffff, 0.24));
    draw_centered_text(@"两位玩家 · 两条航线", NSMakeRect(panel.origin.x, panel.origin.y + 34.0, panel.size.width, 36.0),
                       26.0, [NSColor whiteColor], NSFontWeightSemibold);
    fill_rounded(NSMakeRect(panel.origin.x + 46.0, panel.origin.y + 104.0, panel.size.width - 92.0, 62.0), 16.0,
                 color_from_hex(0xffffff, 0.10));
    draw_text(@"左区", NSMakePoint(panel.origin.x + 72.0, panel.origin.y + 126.0), 17.0, color_from_hex(0xffca55), NSFontWeightBold);
    draw_text(@"键盘 1  +  鼠标左键", NSMakePoint(panel.origin.x + 150.0, panel.origin.y + 128.0), 15.0, [NSColor whiteColor]);
    draw_text(@"Space 飞行 · 左键射击", NSMakePoint(panel.origin.x + 520.0, panel.origin.y + 128.0), 13.0, color_from_hex(0xffffff, 0.68));
    fill_rounded(NSMakeRect(panel.origin.x + 46.0, panel.origin.y + 178.0, panel.size.width - 92.0, 62.0), 16.0,
                 color_from_hex(0xffffff, 0.10));
    draw_text(@"右区", NSMakePoint(panel.origin.x + 72.0, panel.origin.y + 200.0), 17.0, color_from_hex(0xf47e65), NSFontWeightBold);
    draw_text(@"键盘 2  +  鼠标右键", NSMakePoint(panel.origin.x + 150.0, panel.origin.y + 202.0), 15.0, [NSColor whiteColor]);
    draw_text(@"Space 飞行 · 右键射击", NSMakePoint(panel.origin.x + 520.0, panel.origin.y + 202.0), 13.0, color_from_hex(0xffffff, 0.68));
    fill_rounded(NSMakeRect(bounds.size.width * 0.5 - 150.0, 596.0, 300.0, 54.0), 27.0, color_from_hex(0xffd166));
    draw_centered_text(@"按 Enter 开始绑定  →", NSMakeRect(bounds.size.width * 0.5 - 150.0, 596.0, 300.0, 54.0),
                       16.0, color_from_hex(0x073b58), NSFontWeightBold);
    draw_centered_text(@"需要 2 台键盘 + 2 只鼠标", NSMakeRect(0.0, 686.0, bounds.size.width, 24.0),
                       13.0, color_from_hex(0xffffff, 0.78));
}

- (void)drawBinding:(NSRect)bounds {
    [self drawBackground:bounds];
    [self drawBrand:bounds subtitle:@"ROOM SETUP  /  FOUR DEVICES"];
    draw_text(@"连接两位玩家", NSMakePoint(42.0, 126.0), 32.0, [NSColor whiteColor], NSFontWeightBold);
    draw_text(@"键盘按数字绑定区域，鼠标按对应按钮绑定区域", NSMakePoint(42.0, 168.0), 15.0, color_from_hex(0xffffff, 0.78));
    const CGFloat top = 218.0;
    const CGFloat gap = 18.0;
    const CGFloat card_height = (bounds.size.height - top - 126.0 - gap) / 2.0;
    const unsigned int player_colors[] = {0xffca55, 0xf47e65};
    for (NSUInteger index = 0; index < kZoneCount; ++index) {
        const NSRect card = NSMakeRect(42.0, top + (card_height + gap) * static_cast<CGFloat>(index),
                                       bounds.size.width - 84.0, card_height);
        const bool keyboard_bound = _zones[index].keyboard_id != 0;
        const bool mouse_bound = _zones[index].mouse_id != 0;
        fill_rounded(card, 18.0, color_from_hex(0x073b58, 0.76));
        stroke_rounded(card, 18.0, 1.0, color_from_hex(0xffffff, 0.22));
        fill_rounded(NSMakeRect(card.origin.x + 18.0, card.origin.y + 14.0, 58.0, card.size.height - 28.0), 14.0,
                     color_from_hex(player_colors[index], 0.92));
        draw_centered_text(index == 0 ? @"左" : @"右",
                           NSMakeRect(card.origin.x + 18.0, card.origin.y + 14.0, 58.0, card.size.height - 28.0),
                           25.0, color_from_hex(0x073b58), NSFontWeightBold);
        draw_text([NSString stringWithFormat:@"玩家 %zu  ·  %@区", index + 1, index == 0 ? @"左" : @"右"],
                  NSMakePoint(card.origin.x + 96.0, card.origin.y + 18.0), 18.0, [NSColor whiteColor], NSFontWeightBold);
        NSString *keyboard_text = keyboard_bound ? device_product(_zones[index].keyboard_product, @"键盘已连接") :
            [NSString stringWithFormat:@"键盘按数字 %zu 绑定", index + 1];
        NSString *mouse_text = mouse_bound ? device_product(_zones[index].mouse_product, @"鼠标已连接") :
            (index == 0 ? @"鼠标按左键绑定" : @"鼠标按右键绑定");
        draw_text(keyboard_text, NSMakePoint(card.origin.x + 96.0, card.origin.y + 54.0), 14.0,
                  keyboard_bound ? color_from_hex(player_colors[index]) : color_from_hex(0xffffff, 0.68));
        draw_text(mouse_text, NSMakePoint(card.origin.x + 96.0, card.origin.y + 80.0), 14.0,
                  mouse_bound ? color_from_hex(0x8fd6a0) : color_from_hex(0xffffff, 0.68));
        draw_text(keyboard_bound && mouse_bound ? @"READY" : @"WAITING",
                  NSMakePoint(card.origin.x + card.size.width - 116.0, card.origin.y + 48.0), 13.0,
                  keyboard_bound && mouse_bound ? color_from_hex(0x8fd6a0) : color_from_hex(0xffffff, 0.58),
                  NSFontWeightBold);
    }
    fill_rounded(NSMakeRect(42.0, bounds.size.height - 86.0, bounds.size.width - 84.0, 44.0), 14.0, color_from_hex(0x073b58, 0.56));
    draw_text(_notice, NSMakePoint(62.0, bounds.size.height - 72.0), 14.0, color_from_hex(0xffffff, 0.82));
    draw_text(@"按 Esc 返回大厅", NSMakePoint(bounds.size.width - 164.0, bounds.size.height - 72.0), 13.0, color_from_hex(0xffffff, 0.64));
}

- (void)drawImage:(NSImage *)image center:(NSPoint)center size:(CGFloat)size angle:(CGFloat)angle {
    if (image == nil) {
        return;
    }
    NSImageView *image_view = nil;
    (void)image_view;
    NSAffineTransform *transform = [NSAffineTransform transform];
    [transform translateXBy:center.x yBy:center.y];
    [transform rotateByDegrees:angle];
    [transform translateXBy:-center.x yBy:-center.y];
    [transform concat];
    [image drawInRect:NSMakeRect(center.x - size * 0.5, center.y - size * 0.5, size, size)
             fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
    [transform invert];
    [transform concat];
}

- (void)drawPlaying:(NSRect)bounds {
    [self drawBackground:bounds];
    const CGFloat arena_top = kHeaderHeight;
    const CGFloat arena_height = bounds.size.height - kHeaderHeight - kFooterHeight;
    const CGFloat zone_width = bounds.size.width / 2.0;
    const unsigned int player_colors[] = {0xffca55, 0xf47e65};
    for (NSUInteger index = 0; index < kZoneCount; ++index) {
        const CGFloat left = zone_width * static_cast<CGFloat>(index);
        const NSRect zone_rect = NSMakeRect(left + 1.0, arena_top, zone_width - 2.0, arena_height);
        fill_rounded(zone_rect, 16.0, color_from_hex(0x073b58, 0.22));
        if (index > 0) {
            [[color_from_hex(0xffffff, 0.40) colorWithAlphaComponent:0.40] setStroke];
            NSBezierPath *separator = [NSBezierPath bezierPath];
            [separator moveToPoint:NSMakePoint(left, arena_top + 16.0)];
            [separator lineToPoint:NSMakePoint(left, arena_top + arena_height - 16.0)];
            separator.lineWidth = 2.0;
            [separator stroke];
        }
        PlayerZone &zone = _zones[index];
        fill_rounded(NSMakeRect(left + 14.0, arena_top + 14.0, zone_width - 28.0, 58.0), 15.0,
                     color_from_hex(0x073b58, 0.72));
        draw_text([NSString stringWithFormat:@"%@区  ·  PLAYER %zu", index == 0 ? @"左" : @"右", index + 1],
                  NSMakePoint(left + 30.0, arena_top + 25.0), 13.0, [NSColor whiteColor], NSFontWeightBold);
        draw_text([NSString stringWithFormat:@"%d", zone.score], NSMakePoint(left + 30.0, arena_top + 42.0),
                  22.0, color_from_hex(player_colors[index]), NSFontWeightBold);
        draw_text(@"SPACE 飞行", NSMakePoint(left + zone_width - 104.0, arena_top + 24.0),
                  11.0, color_from_hex(0xffffff, 0.68), NSFontWeightBold);
        draw_text(index == 0 ? @"LEFT 射击" : @"RIGHT 射击", NSMakePoint(left + zone_width - 104.0, arena_top + 42.0),
                  11.0, color_from_hex(0xffd166, 0.86), NSFontWeightBold);
        for (size_t pipe_index = 0; pipe_index < zone.pipe_count; ++pipe_index) {
            const Pipe &pipe = zone.pipes[pipe_index];
            fill_rounded(NSMakeRect(left + pipe.x, arena_top, kPipeWidth,
                                    pipe.gap_y - kPipeGap * 0.5), 10.0, color_from_hex(0x237b61));
            fill_rounded(NSMakeRect(left + pipe.x, arena_top + pipe.gap_y + kPipeGap * 0.5,
                                    kPipeWidth, arena_height - pipe.gap_y - kPipeGap * 0.5), 10.0, color_from_hex(0x237b61));
            fill_rounded(NSMakeRect(left + pipe.x - 6.0, arena_top + pipe.gap_y - kPipeGap * 0.5 - 10.0,
                                    kPipeWidth + 12.0, 18.0), 8.0, color_from_hex(0x2f9673));
            fill_rounded(NSMakeRect(left + pipe.x - 6.0, arena_top + pipe.gap_y + kPipeGap * 0.5 - 8.0,
                                    kPipeWidth + 12.0, 18.0), 8.0, color_from_hex(0x2f9673));
        }
        if (zone.gift.active && _gift_image != nil) {
            [_gift_image drawInRect:NSMakeRect(left + zone.gift.x - kGiftSize * 0.5,
                                                arena_top + zone.gift.y - kGiftSize * 0.5,
                                                kGiftSize, kGiftSize)
                           fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
        }
        for (const Bullet &bullet : zone.bullets) {
            if (bullet.active && _bullet_image != nil) {
                [_bullet_image drawInRect:NSMakeRect(left + bullet.x - 16.0, arena_top + bullet.y - 8.0, 32.0, 16.0)
                                fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
            }
        }
        const CGFloat bird_x = left + zone_width * kBirdXRatio;
        const CGFloat bird_y = arena_top + zone.bird_y;
        NSImage *bird = zone.velocity <= 0.0 ? _bird_up : _bird_down;
        [self drawImage:bird center:NSMakePoint(bird_x, bird_y) size:kBirdRadius * 3.0
                   angle:zone.velocity <= 0.0 ? -10.0 : 16.0];
        if (zone.dead) {
            fill_rounded(NSMakeRect(left + 20.0, arena_top + arena_height * 0.46,
                                    zone_width - 40.0, 104.0), 18.0, color_from_hex(0x073b58, 0.86));
            draw_centered_text(@"本区结束", NSMakeRect(left + 20.0, arena_top + arena_height * 0.48,
                                                         zone_width - 40.0, 30.0),
                               20.0, [NSColor whiteColor], NSFontWeightBold);
            draw_centered_text([NSString stringWithFormat:@"最终分数  %d", zone.score],
                               NSMakeRect(left + 20.0, arena_top + arena_height * 0.48 + 38.0,
                                          zone_width - 40.0, 24.0),
                               14.0, color_from_hex(player_colors[index]), NSFontWeightBold);
        }
    }
    fill_rounded(NSMakeRect(0.0, 0.0, bounds.size.width, kHeaderHeight - 12.0), 0.0, color_from_hex(0x073b58, 0.56));
    draw_text(@"mkflappybird", NSMakePoint(34.0, 28.0), 22.0, [NSColor whiteColor], NSFontWeightBold);
    draw_text(_notice, NSMakePoint(34.0, 67.0), 13.0, color_from_hex(0xffffff, 0.76));
    draw_text(@"Esc 退出本局", NSMakePoint(bounds.size.width - 112.0, 45.0), 12.0, color_from_hex(0xffffff, 0.66));
    fill_rounded(NSMakeRect(0.0, bounds.size.height - kFooterHeight, bounds.size.width, kFooterHeight), 0.0, color_from_hex(0x073b58, 0.74));
    draw_centered_text(@"礼物 +1 · 子弹速度为小鸟航线速度的 3 倍 · 每 0.5 秒可射击一次",
                       NSMakeRect(0.0, bounds.size.height - kFooterHeight, bounds.size.width, kFooterHeight),
                       13.0, color_from_hex(0xffffff, 0.72));
}

- (void)drawCountdown:(NSRect)bounds {
    [self drawPlaying:bounds];
    [color_from_hex(0x073b58, 0.38) setFill];
    NSRectFill(bounds);
    draw_centered_text(@"准备起飞", NSMakeRect(0.0, 280.0, bounds.size.width, 38.0), 24.0,
                       [NSColor whiteColor], NSFontWeightSemibold);
    draw_centered_text([NSString stringWithFormat:@"%d", static_cast<int>(ceil(_countdown))],
                       NSMakeRect(0.0, 326.0, bounds.size.width, 110.0), 92.0,
                       color_from_hex(0xffd166), NSFontWeightBold);
}

- (void)drawFinished:(NSRect)bounds {
    [self drawBackground:bounds];
    [self drawBrand:bounds subtitle:@"ROUND COMPLETE  /  LOCAL LEADERBOARD"];
    draw_centered_text(@"这一局，飞得很远", NSMakeRect(0.0, 136.0, bounds.size.width, 48.0),
                       28.0, [NSColor whiteColor], NSFontWeightBold);
    draw_centered_text(@"每个分区的成绩都已保存到本局结果", NSMakeRect(0.0, 190.0, bounds.size.width, 24.0),
                       14.0, color_from_hex(0xffffff, 0.76));
    const CGFloat card_width = 300.0;
    const CGFloat gap = 24.0;
    const CGFloat total_width = card_width * 2.0 + gap;
    const CGFloat left = (bounds.size.width - total_width) * 0.5;
    const unsigned int player_colors[] = {0xffca55, 0xf47e65};
    for (NSUInteger index = 0; index < kZoneCount; ++index) {
        const CGFloat x = left + (card_width + gap) * static_cast<CGFloat>(index);
        fill_rounded(NSMakeRect(x, 274.0, card_width, 188.0), 22.0, color_from_hex(0x073b58, 0.84));
        fill_rounded(NSMakeRect(x + 18.0, 292.0, 54.0, 42.0), 12.0, color_from_hex(player_colors[index]));
        draw_centered_text(index == 0 ? @"左" : @"右", NSMakeRect(x + 18.0, 292.0, 54.0, 42.0),
                           21.0, color_from_hex(0x073b58), NSFontWeightBold);
        draw_text([NSString stringWithFormat:@"PLAYER %zu", index + 1], NSMakePoint(x + 88.0, 306.0),
                  14.0, color_from_hex(0xffffff, 0.72), NSFontWeightBold);
        draw_centered_text([NSString stringWithFormat:@"%d", _zones[index].score],
                           NSMakeRect(x, 346.0, card_width, 58.0), 44.0,
                           color_from_hex(player_colors[index]), NSFontWeightBold);
        draw_centered_text(@"POINTS", NSMakeRect(x, 412.0, card_width, 20.0), 11.0,
                           color_from_hex(0xffffff, 0.58), NSFontWeightBold);
    }
    fill_rounded(NSMakeRect(bounds.size.width * 0.5 - 160.0, 548.0, 320.0, 54.0), 27.0, color_from_hex(0xffd166));
    draw_centered_text(@"按 Enter 再来一局  →", NSMakeRect(bounds.size.width * 0.5 - 160.0, 548.0, 320.0, 54.0),
                       16.0, color_from_hex(0x073b58), NSFontWeightBold);
}

- (void)drawInView:(NSView *)view {
    const NSRect bounds = view.bounds;
    if (_screen == Screen::home) {
        [self drawHome:bounds];
    } else if (_screen == Screen::binding) {
        [self drawBinding:bounds];
    } else if (_screen == Screen::countdown) {
        [self drawCountdown:bounds];
    } else if (_screen == Screen::playing) {
        [self drawPlaying:bounds];
    } else {
        [self drawFinished:bounds];
    }
    if (_permission_denied) {
        fill_rounded(NSMakeRect(32.0, bounds.size.height - 92.0, bounds.size.width - 64.0, 38.0), 12.0,
                     color_from_hex(0xa62d45, 0.88));
        draw_centered_text(@"请在 系统设置 → 隐私与安全性 → 输入监控 中允许当前 Demo",
                           NSMakeRect(32.0, bounds.size.height - 92.0, bounds.size.width - 64.0, 38.0),
                           13.0, [NSColor whiteColor], NSFontWeightSemibold);
    }
}

@end

@interface FlappyAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) FlappyController *controller;
@end

@implementation FlappyAppDelegate
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
        FlappyAppDelegate *delegate = [[FlappyAppDelegate alloc] init];
        application.delegate = delegate;
        FlappyController *controller = [[FlappyController alloc] init];
        delegate.controller = controller;
        FlappyView *view = [[FlappyView alloc] initWithFrame:NSMakeRect(0.0, 0.0, kWindowWidth, kWindowHeight)];
        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0.0, 0.0, kWindowWidth, kWindowHeight)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.title = @"mkflappybird";
        window.minSize = NSMakeSize(900.0, 650.0);
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
