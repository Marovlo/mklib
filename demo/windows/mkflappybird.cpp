#include "demo_common.h"

#include <windows.h>
#include <gdiplus.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <random>

#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;
using namespace mklib_demo_windows;

namespace {
constexpr int kHeader = 118;
constexpr int kFooter = 54;
constexpr double kBirdRadius = 15.0;
constexpr double kPipeWidth = 48.0;
constexpr double kPipeGap = 178.0;
constexpr double kBirdXRatio = 0.28;
constexpr double kGravity = 920.0;
constexpr double kFlapVelocity = -330.0;
constexpr double kPipeSpeed = 190.0;
constexpr double kBulletSpeed = kPipeSpeed * 3.0;
constexpr double kPipeInterval = 1.75;
constexpr double kShotInterval = 0.5;
constexpr uint16_t kSpace = 0x2c, kEnter = 0x28, kEscape = 0x29, kOne = 0x1e, kTwo = 0x1f;

struct Pipe { double x = 0; double gap_y = 0; bool scored = false; };
struct Bullet { double x = 0; double y = 0; bool active = false; };
struct Gift { double x = 0; double y = 0; bool active = false; };
struct Zone {
    mklib_device_id keyboard = 0, mouse = 0; double bird_y = 0, velocity = 0, pipe_time = 0, gift_time = 0;
    double next_gift = 1.4, last_shot = -kShotInterval; int score = 0; bool dead = false; uint32_t seed = 0;
    std::array<Pipe, 6> pipes{}; size_t pipe_count = 0; std::array<Bullet, 8> bullets{}; Gift gift{};
};
enum class Screen { Home, Binding, Countdown, Playing, Finished };

uint32_t random_next(uint32_t &seed) { seed = seed * 1664525u + 1013904223u; return seed; }
double random_gap(double height, uint32_t &seed) { const double min = 108, max = (std::max)(min + 10, height - 108); return min + (max - min) * (random_next(seed) % 10000) / 10000.0; }
double random_gift(double height, uint32_t &seed) { const double min = 60, max = (std::max)(min + 10, height - 60); return min + (max - min) * (random_next(seed) % 10000) / 10000.0; }
}

class FlappyDemo final : public InputWindow {
public:
    FlappyDemo() : InputWindow(L"mkflappybird", 1280, 800, MKLIB_DEVICE_MASK_KEYBOARD | MKLIB_DEVICE_MASK_MOUSE) {
        const wchar_t *file_names[] = {L"sky_background.png", L"bird_up.png", L"bird_down.png", L"bullet.png", L"gift.png"};
        Image **images[] = {&background_, &bird_up_, &bird_down_, &bullet_, &gift_};
        wchar_t module[MAX_PATH]{}; GetModuleFileNameW(nullptr, module, MAX_PATH);
        std::filesystem::path asset_dir = std::filesystem::path(module).parent_path() / L"assets";
        for (size_t i = 0; i < 5; ++i) *images[i] = new Image((asset_dir / file_names[i]).c_str());
    }
    ~FlappyDemo() override { delete background_; delete bird_up_; delete bird_down_; delete bullet_; delete gift_; }
protected:
    void on_input_event(const mklib_event &event) override {
        if (screen_ == Screen::Home) { if (event.type == MKLIB_KEY_DOWN && event.usage == kEnter) start_binding(); return; }
        if (event.type == MKLIB_KEY_DOWN && event.usage == kEscape) { reset_home(); return; }
        if (screen_ == Screen::Binding) {
            if (event.type == MKLIB_KEY_DOWN && (event.usage == kOne || event.usage == kTwo)) bind(event.device_id, MKLIB_DEVICE_KEYBOARD, event.usage == kOne ? 0 : 1);
            else if (event.type == MKLIB_MOUSE_BUTTON_DOWN && (event.usage == 1 || event.usage == 2)) bind(event.device_id, MKLIB_DEVICE_MOUSE, event.usage == 1 ? 0 : 1);
            return;
        }
        if (screen_ == Screen::Playing && event.type == MKLIB_KEY_DOWN && event.usage == kSpace) {
            for (auto &zone : zones_) if (zone.keyboard == event.device_id && !zone.dead) { zone.velocity = kFlapVelocity; break; }
        } else if (screen_ == Screen::Playing && event.type == MKLIB_MOUSE_BUTTON_DOWN && (event.usage == 1 || event.usage == 2)) {
            for (auto &zone : zones_) if (zone.mouse == event.device_id && !zone.dead && game_time_ - zone.last_shot >= kShotInterval) {
                for (auto &bullet : zone.bullets) if (!bullet.active) { bullet.x = zone_width_ * kBirdXRatio + kBirdRadius + 5; bullet.y = zone.bird_y; bullet.active = true; zone.last_shot = game_time_; break; }
                break;
            }
        } else if (screen_ == Screen::Finished && event.type == MKLIB_KEY_DOWN && event.usage == kEnter) reset_home();
    }
    void tick(double delta) override {
        if (screen_ == Screen::Binding) return;
        if (screen_ == Screen::Countdown) { countdown_ -= delta; if (countdown_ <= 0) start_game(); return; }
        if (screen_ != Screen::Playing) return;
        game_time_ += delta; bool all_dead = true;
        for (auto &zone : zones_) { update_zone(zone, delta); if (!zone.dead) all_dead = false; }
        if (all_dead) { screen_ = Screen::Finished; set_status("本局结束，按 Enter 回到大厅重新开始"); }
    }
    void paint(HDC dc, const RECT &client) override {
        const int width = client.right, height = client.bottom; draw_background(dc, width, height);
        if (screen_ == Screen::Home) { draw_home(dc, width, height); return; }
        if (screen_ == Screen::Binding) { draw_binding(dc, width, height); return; }
        if (screen_ == Screen::Finished) { draw_finished(dc, width, height); return; }
        draw_playing(dc, width, height);
        if (screen_ == Screen::Countdown) {
            Graphics graphics(dc);
            SolidBrush overlay(Color(96, 7, 59, 88));
            graphics.FillRectangle(&overlay, 0, 0, width, height);
            text(dc, width / 2 - 70, 300, L"准备起飞", RGB(255,255,255), 26);
            text(dc, width / 2 - 18, 355, std::to_wstring(static_cast<int>(std::ceil(countdown_))), RGB(255,209,102), 92);
        }
    }
private:
    Screen screen_ = Screen::Home; std::array<Zone, 2> zones_{}; double countdown_ = 3, game_time_ = 0, zone_width_ = 640; uint32_t seed_ = 0x4d4b4c42u;
    Image *background_ = nullptr, *bird_up_ = nullptr, *bird_down_ = nullptr, *bullet_ = nullptr, *gift_ = nullptr;
    void reset_home() { zones_ = {}; screen_ = Screen::Home; set_status("双区域双人对战：先绑定两台键盘和两只鼠标"); }
    void start_binding() { zones_ = {}; screen_ = Screen::Binding; set_status("键盘按 1/2 绑定左右区；鼠标按左键/右键绑定左右区"); }
    bool bound(mklib_device_id id) const { return zones_[0].keyboard == id || zones_[0].mouse == id || zones_[1].keyboard == id || zones_[1].mouse == id; }
    void bind(mklib_device_id id, mklib_device_kind kind, int index) { if (bound(id)) { set_status("这台设备已经绑定，请使用另一台设备"); return; } if (kind == MKLIB_DEVICE_KEYBOARD) zones_[index].keyboard = id; else zones_[index].mouse = id; if (zones_[0].keyboard && zones_[0].mouse && zones_[1].keyboard && zones_[1].mouse) { countdown_ = 3; screen_ = Screen::Countdown; set_status("两位玩家全部就绪，马上起飞"); } }
    void start_game() { zone_width_ = 640; for (size_t i = 0; i < zones_.size(); ++i) { auto &z = zones_[i]; z.bird_y = (800 - kHeader - kFooter) * .48; z.velocity = z.pipe_time = z.gift_time = 0; z.next_gift = 1.0 + (random_next(seed_) % 120) / 100.0; z.last_shot = -kShotInterval; z.score = 0; z.dead = false; z.pipe_count = 0; z.gift = {}; z.seed = seed_ + static_cast<uint32_t>(i * 977 + 101); z.bullets = {}; } game_time_ = 0; screen_ = Screen::Playing; set_status("键盘 Space 控制飞行；对应鼠标按键发射子弹（每 0.5 秒一次）"); }
    void update_zone(Zone &z, double delta) {
        if (z.dead) return; const double arena = 800 - kHeader - kFooter; z.velocity += kGravity * delta; z.bird_y += z.velocity * delta; z.pipe_time += delta; z.gift_time += delta;
        if (z.pipe_time >= kPipeInterval && z.pipe_count < z.pipes.size()) { z.pipe_time -= kPipeInterval; z.pipes[z.pipe_count++] = {zone_width_ + 90, random_gap(arena, z.seed), false}; }
        if (!z.gift.active && z.gift_time >= z.next_gift) { z.gift_time = 0; z.next_gift = 1.4 + (random_next(z.seed) % 180) / 100.0; z.gift = {zone_width_ + 70, random_gift(arena, z.seed), true}; }
        for (size_t i = 0; i < z.pipe_count; ++i) { auto &p = z.pipes[i]; p.x -= kPipeSpeed * delta; if (!p.scored && p.x + kPipeWidth < zone_width_ * kBirdXRatio) { p.scored = true; ++z.score; } }
        if (z.gift.active) { z.gift.x -= kPipeSpeed * delta; if (z.gift.x < -30) z.gift.active = false; }
        for (auto &b : z.bullets) if (b.active) { b.x += kBulletSpeed * delta; if (b.x > zone_width_ + 80) b.active = false; }
        if (z.gift.active) for (auto &b : z.bullets) if (b.active && std::abs(b.x - z.gift.x) < 30 && std::abs(b.y - z.gift.y) < 30) { b.active = false; z.gift.active = false; ++z.score; break; }
        size_t first = 0; while (first < z.pipe_count && z.pipes[first].x + kPipeWidth < -20) ++first; if (first) { for (size_t i = first; i < z.pipe_count; ++i) z.pipes[i-first] = z.pipes[i]; z.pipe_count -= first; }
        const double bird_x = zone_width_ * kBirdXRatio; if (z.bird_y - kBirdRadius < 0 || z.bird_y + kBirdRadius > arena) { z.dead = true; return; }
        for (size_t i = 0; i < z.pipe_count; ++i) { const auto &p = z.pipes[i]; if (bird_x + kBirdRadius > p.x && bird_x - kBirdRadius < p.x + kPipeWidth && (z.bird_y - kBirdRadius < p.gap_y - kPipeGap*.5 || z.bird_y + kBirdRadius > p.gap_y + kPipeGap*.5)) { z.dead = true; return; } }
    }
    void draw_background(HDC dc, int w, int h) {
        Graphics graphics(dc);
        if (background_ && background_->GetLastStatus() == Ok) {
            const double scale = (std::max)(static_cast<double>(w) / background_->GetWidth(), static_cast<double>(h) / background_->GetHeight());
            const int draw_width = static_cast<int>(background_->GetWidth() * scale);
            const int draw_height = static_cast<int>(background_->GetHeight() * scale);
            graphics.DrawImage(background_, (w - draw_width) / 2, (h - draw_height) / 2, draw_width, draw_height);
        } else {
            SolidBrush fallback(Color(255, 103, 213, 231));
            graphics.FillRectangle(&fallback, 0, 0, w, h);
        }
        SolidBrush tint(Color(72, 7, 59, 88));
        graphics.FillRectangle(&tint, 0, 0, w, h);
    }
    void draw_home(HDC dc, int w, int) { text(dc,42,30,L"MKLIB / ARCADE",RGB(255,255,255),18); text(dc,42,82,L"TWO-LANE / KEYBOARD + MOUSE",RGB(235,245,250),16); text(dc,w/2-120,190,L"飞吧，小鸟们",RGB(7,59,88),32); text(dc,w/2-230,245,L"固定双区域 · 键盘控制飞行 · 鼠标发射子弹收集礼物",RGB(7,59,88),16); rect(dc,140,300,w-140,580,RGB(7,59,88)); text(dc,220,360,L"两位玩家 · 两条航线",RGB(255,255,255),26); text(dc,220,445,L"左区  键盘 1 + 鼠标左键",RGB(255,202,85),17); text(dc,220,500,L"右区  键盘 2 + 鼠标右键",RGB(244,126,101),17); text(dc,w/2-120,650,L"按 Enter 开始绑定",RGB(255,209,102),18); }
    void draw_binding(HDC dc, int w, int h) { text(dc,42,30,L"MKLIB / ROOM SETUP",RGB(255,255,255),18); text(dc,42,105,L"连接两位玩家",RGB(255,255,255),30); text(dc,42,155,L"键盘按数字绑定区域，鼠标按对应按钮绑定区域",RGB(235,245,250),16); for (int i=0;i<2;++i) { int top=220+i*180; rect(dc,42,top,w-42,top+150,RGB(7,59,88)); text(dc,70,top+25,i==0?L"玩家 1 · 左区":L"玩家 2 · 右区",i==0?RGB(255,202,85):RGB(244,126,101),20); text(dc,70,top+70,zones_[i].keyboard?L"键盘已连接":(i==0?L"键盘按数字 1 绑定":L"键盘按数字 2 绑定"),RGB(255,255,255),15); text(dc,70,top+105,zones_[i].mouse?L"鼠标已连接":(i==0?L"鼠标按左键绑定":L"鼠标按右键绑定"),RGB(143,214,160),15); } text(dc,42,h-45,wide(status()),RGB(255,255,255),14); }
    void draw_playing(HDC dc, int w, int h) { int arena = h-kHeader-kFooter; for (int i=0;i<2;++i) { int left=i*zone_width_; rect(dc,left+1,kHeader,left+static_cast<int>(zone_width_)-2,h-kFooter,RGB(i?52:35,i?120:104,i?106:142)); auto &z=zones_[i]; text(dc,left+30,kHeader+25,i==0?L"左区 · PLAYER 1":L"右区 · PLAYER 2",RGB(255,255,255),14); text(dc,left+30,kHeader+48,std::to_wstring(z.score),i?RGB(244,126,101):RGB(255,202,85),22); for(size_t j=0;j<z.pipe_count;++j){auto&p=z.pipes[j];rect(dc,left+static_cast<int>(p.x),kHeader,left+static_cast<int>(p.x+kPipeWidth),kHeader+static_cast<int>(p.gap_y-kPipeGap*.5),RGB(35,123,97));rect(dc,left+static_cast<int>(p.x),kHeader+static_cast<int>(p.gap_y+kPipeGap*.5),left+static_cast<int>(p.x+kPipeWidth),kHeader+arena,RGB(35,123,97));} if(z.gift.active&&gift_&&gift_->GetLastStatus()==Ok){Graphics g(dc);g.DrawImage(gift_,left+static_cast<int>(z.gift.x-15),kHeader+static_cast<int>(z.gift.y-15),30,30);} for(auto&b:z.bullets)if(b.active&&bullet_&&bullet_->GetLastStatus()==Ok){Graphics g(dc);g.DrawImage(bullet_,left+static_cast<int>(b.x-16),kHeader+static_cast<int>(b.y-8),32,16);} Image*bird=z.velocity<=0?bird_up_:bird_down_;if(bird&&bird->GetLastStatus()==Ok){Graphics g(dc);g.DrawImage(bird,left+static_cast<int>(zone_width_*kBirdXRatio-22),kHeader+static_cast<int>(z.bird_y-22),44,44);} if(z.dead)text(dc,left+180,kHeader+arena/2,L"本区结束",RGB(255,255,255),20);} text(dc,34,65,wide(status()),RGB(255,255,255),13); }
    void draw_finished(HDC dc, int w, int) { text(dc,w/2-160,150,L"这一局，飞得很远",RGB(255,255,255),28); for(int i=0;i<2;++i){rect(dc,w/2-330+i*360,300,w/2-30+i*360,490,RGB(7,59,88));text(dc,w/2-240+i*360,350,i==0?L"PLAYER 1":L"PLAYER 2",RGB(255,255,255),15);text(dc,w/2-200+i*360,410,std::to_wstring(zones_[i].score),i?RGB(244,126,101):RGB(255,202,85),44);} text(dc,w/2-125,600,L"按 Enter 再来一局",RGB(255,209,102),18); }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) { GdiplusStartupInput input; ULONG_PTR token=0; GdiplusStartup(&token,&input,nullptr); FlappyDemo demo; int result=demo.run(instance); GdiplusShutdown(token); return result; }
