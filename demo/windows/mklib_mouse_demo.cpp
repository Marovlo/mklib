#include "demo_common.h"

#include <array>
#include <utility>

using namespace mklib_demo_windows;

class MouseDemo final : public InputWindow {
public:
    MouseDemo() : InputWindow(L"mklib Windows Mouse Demo", 960, 650, MKLIB_DEVICE_MASK_KEYBOARD | MKLIB_DEVICE_MASK_MOUSE) {}
protected:
    void on_input_event(const mklib_event &event) override {
        if (event.type == MKLIB_KEY_DOWN && event.usage == 0x29) { capture_ = false; set_status("鼠标已释放，按鼠标按钮可重新捕获"); return; }
        if (event.type == MKLIB_KEY_DOWN && event.usage == 0x2b && players_[0].device && players_[1].device) { std::swap(players_[0].device, players_[1].device); set_status("已交换两个玩家的鼠标绑定"); }
        if (event.type == MKLIB_MOUSE_BUTTON_DOWN && event.usage <= 8) {
            if (players_[0].device == 0) { players_[0].device = event.device_id; set_status("第一只鼠标已绑定给玩家 1"); }
            else if (players_[1].device == 0 && players_[0].device != event.device_id) { players_[1].device = event.device_id; set_status("第二只鼠标已绑定给玩家 2"); }
            capture_ = true;
        }
        if (!capture_ || event.type != MKLIB_MOUSE_MOVE) return;
        for (auto &player : players_) if (player.device == event.device_id) {
            if (event.usage == 0x30) player.x += event.value; if (event.usage == 0x31) player.y += event.value;
            player.x = std::clamp(player.x, 70, 890); player.y = std::clamp(player.y, 230, 570);
        }
    }
    void tick(double) override {}
    void paint(HDC dc, const RECT &client) override {
        rect(dc, 0, 0, client.right, client.bottom, RGB(14, 18, 25), RGB(14, 18, 25));
        text(dc, 32, 24, L"mklib · Windows 多鼠标 Demo", RGB(245, 248, 252), 26);
        text(dc, 34, 66, L"鼠标按钮绑定两个玩家；按 Tab 交换；按 Esc 释放逻辑捕获", RGB(180, 190, 204));
        text(dc, 34, 94, wide(status()), RGB(130, 210, 170));
        text(dc, 34, 126, wide(std::string("已发现输入设备：") + std::to_string(devices().size())), RGB(150, 160, 174));
        int y = 150; for (const auto &device : devices()) { text(dc, 34, y, device_line(device), RGB(150, 160, 174), 14); y += 20; }
        rect(dc, 32, 235, client.right - 32, client.bottom - 32, RGB(27, 34, 45));
        circle(dc, players_[0].x, players_[0].y, 34, RGB(55, 158, 245)); circle(dc, players_[1].x, players_[1].y, 34, RGB(242, 93, 83));
        text(dc, players_[0].x - 42, players_[0].y + 42, wide("玩家 1 / 鼠标 " + std::to_string(players_[0].device)), RGB(230, 235, 242), 14);
        text(dc, players_[1].x - 42, players_[1].y + 42, wide("玩家 2 / 鼠标 " + std::to_string(players_[1].device)), RGB(230, 235, 242), 14);
        text(dc, 34, client.bottom - 22, capture_ ? L"逻辑鼠标捕获：开启（不会移动系统光标）" : L"逻辑鼠标捕获：关闭", RGB(160, 170, 185), 14);
    }
private:
    struct Player { mklib_device_id device = 0; int x = 260; int y = 380; };
    std::array<Player, 2> players_{};
    bool capture_ = false;
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) { MouseDemo demo; return demo.run(instance); }
