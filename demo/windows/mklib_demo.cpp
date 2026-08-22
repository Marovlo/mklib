#include "demo_common.h"

#include <array>
#include <utility>

using namespace mklib_demo_windows;

namespace {
struct Player { mklib_device_id device = 0; int x = 260; int y = 380; bool up = false; bool down = false; bool left = false; bool right = false; };
constexpr uint16_t kTab = 0x2b;
void update(Player &player, const mklib_event &event) {
    if (event.type != MKLIB_KEY_DOWN && event.type != MKLIB_KEY_UP) return;
    const bool down = event.type == MKLIB_KEY_DOWN;
    switch (event.usage) {
        case 0x1a: player.up = down; break;
        case 0x16: player.down = down; break;
        case 0x04: player.left = down; break;
        case 0x07: player.right = down; break;
        default: break;
    }
}
}

class KeyboardDemo final : public InputWindow {
public:
    KeyboardDemo() : InputWindow(L"mklib Windows Demo", 960, 650, MKLIB_DEVICE_MASK_KEYBOARD) {}
protected:
    void on_input_event(const mklib_event &event) override {
        if (event.type == MKLIB_KEY_DOWN && event.usage == kTab && players_[0].device != 0 && players_[1].device != 0) {
            std::swap(players_[0].device, players_[1].device);
            set_status("已交换两个玩家的键盘绑定");
        } else if (event.type == MKLIB_KEY_DOWN && players_[0].device == 0) {
            players_[0].device = event.device_id; set_status("第一台键盘已绑定给玩家 1");
        } else if (event.type == MKLIB_KEY_DOWN && players_[1].device == 0 && event.device_id != players_[0].device) {
            players_[1].device = event.device_id; set_status("第二台键盘已绑定给玩家 2");
        }
        for (auto &player : players_) if (player.device == event.device_id) update(player, event);
    }
    void tick(double delta) override {
        const int speed = static_cast<int>(260.0 * delta);
        for (auto &player : players_) {
            if (player.up) player.y -= speed; if (player.down) player.y += speed;
            if (player.left) player.x -= speed; if (player.right) player.x += speed;
            player.x = std::clamp(player.x, 70, 890); player.y = std::clamp(player.y, 230, 570);
        }
    }
    void paint(HDC dc, const RECT &client) override {
        rect(dc, 0, 0, client.right, client.bottom, RGB(14, 18, 25), RGB(14, 18, 25));
        text(dc, 32, 24, L"mklib · Windows 双键盘 Demo", RGB(245, 248, 252), 26);
        text(dc, 34, 66, L"两台键盘首次按键分别绑定玩家；按 Tab 交换；WASD 移动对象", RGB(180, 190, 204));
        text(dc, 34, 94, wide(status()), RGB(130, 210, 170));
        text(dc, 34, 126, wide(std::string("已发现键盘：") + std::to_string(devices().size())), RGB(150, 160, 174));
        int y = 150; for (const auto &device : devices()) { text(dc, 34, y, device_line(device), RGB(150, 160, 174), 14); y += 20; }
        rect(dc, 32, 235, client.right - 32, client.bottom - 32, RGB(27, 34, 45));
        circle(dc, players_[0].x, players_[0].y, 34, RGB(55, 158, 245)); circle(dc, players_[1].x, players_[1].y, 34, RGB(242, 93, 83));
        text(dc, players_[0].x - 42, players_[0].y + 42, wide("玩家 1 / 键盘 " + std::to_string(players_[0].device)), RGB(230, 235, 242), 14);
        text(dc, players_[1].x - 42, players_[1].y + 42, wide("玩家 2 / 键盘 " + std::to_string(players_[1].device)), RGB(230, 235, 242), 14);
    }
private: std::array<Player, 2> players_{};
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) { KeyboardDemo demo; return demo.run(instance); }
