#ifndef MKLIB_WINDOWS_DEMO_COMMON_H
#define MKLIB_WINDOWS_DEMO_COMMON_H

#include "mklib/mklib.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace mklib_demo_windows {

inline std::wstring wide(const std::string &value) {
    if (value.empty()) return L"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

inline void text(HDC dc, int x, int y, const std::wstring &value, COLORREF color = RGB(220, 226, 234), int height = 16) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    HFONT font = CreateFontW(height, 0, 0, 0, height >= 22 ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, font);
    TextOutW(dc, x, y, value.c_str(), static_cast<int>(value.size()));
    SelectObject(dc, old);
    DeleteObject(font);
}

inline void rect(HDC dc, int left, int top, int right, int bottom, COLORREF fill, COLORREF border = RGB(40, 48, 60)) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    Rectangle(dc, left, top, right, bottom);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

inline void circle(HDC dc, int x, int y, int radius, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ old = SelectObject(dc, brush);
    Ellipse(dc, x - radius, y - radius, x + radius, y + radius);
    SelectObject(dc, old);
    DeleteObject(brush);
}

class InputWindow {
public:
    InputWindow(const wchar_t *title, int width, int height, uint32_t mask)
        : title_(title), width_(width), height_(height), mask_(mask) {}
    virtual ~InputWindow() = default;

    int run(HINSTANCE instance) {
        instance_ = instance;
        const wchar_t *class_name = L"mklib_windows_demo_window";
        WNDCLASSW wc{};
        wc.hInstance = instance_;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &InputWindow::window_proc;
        wc.lpszClassName = class_name;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = nullptr;
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(0, class_name, title_.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, width_, height_, nullptr, nullptr, instance_, this);
        if (hwnd_ == nullptr) return 1;
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

protected:
    HWND hwnd() const { return hwnd_; }
    const std::vector<mklib_device_info> &devices() const { return devices_; }
    mklib_handle *input() const { return input_; }
    void set_status(const std::string &status) { status_ = status; }
    const std::string &status() const { return status_; }
    void invalidate() { InvalidateRect(hwnd_, nullptr, FALSE); }
    virtual void tick(double delta_seconds) = 0;
    virtual void paint(HDC dc, const RECT &client) = 0;

    virtual void on_input_event(const mklib_event &) {}
    virtual void on_device_change() {}

    void refresh_devices() {
        if (input_ == nullptr) return;
        size_t count = 0;
        if (mklib_get_devices(input_, nullptr, 0, &count) != MKLIB_OK) return;
        devices_.resize(count);
        if (count != 0) mklib_get_devices(input_, devices_.data(), count, &count);
        devices_.resize(count);
        on_device_change();
    }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        InputWindow *self = reinterpret_cast<InputWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto *create = reinterpret_cast<CREATESTRUCTW *>(lparam);
            self = static_cast<InputWindow *>(create->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self == nullptr ? DefWindowProcW(hwnd, message, wparam, lparam) : self->handle_message(message, wparam, lparam);
    }

    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case WM_CREATE: {
                mklib_config config{};
                mklib_config_init(&config);
                config.event_queue_capacity = 8192;
                config.device_kind_mask = mask_;
                if (mklib_create(&config, &input_) != MKLIB_OK || input_ == nullptr ||
                    mklib_windows_attach_window(input_, reinterpret_cast<mklib_windows_window_handle>(hwnd_),
                                                MKLIB_WINDOWS_ATTACH_REGISTER_RAW_INPUT |
                                                MKLIB_WINDOWS_ATTACH_INPUT_SINK) != MKLIB_OK ||
                    mklib_start(input_) != MKLIB_OK) {
                    status_ = "无法初始化 Windows Raw Input";
                } else {
                    status_ = "输入服务已启动";
                    refresh_devices();
                }
                timer_id_ = SetTimer(hwnd_, 1, 16, nullptr);
                return 0;
            }
            case WM_INPUT:
            case WM_INPUT_DEVICE_CHANGE: {
                if (input_ != nullptr) {
                    bool handled = false;
                    mklib_windows_process_message(input_, static_cast<uint32_t>(message),
                                                  static_cast<uintptr_t>(wparam), static_cast<intptr_t>(lparam), &handled);
                    if (message == WM_INPUT_DEVICE_CHANGE) refresh_devices();
                }
                return DefWindowProcW(hwnd_, message, wparam, lparam);
            }
            case WM_TIMER: {
                if (wparam == timer_id_) {
                    const auto now = std::chrono::steady_clock::now();
                    const double delta = (std::min)(0.05, std::chrono::duration<double>(now - last_tick_).count());
                    last_tick_ = now;
                    mklib_event event{};
                    while (input_ != nullptr && mklib_poll_event(input_, &event, 0) == MKLIB_OK) on_input_event(event);
                    refresh_devices();
                    tick(delta);
                    invalidate();
                }
                return 0;
            }
            case WM_KILLFOCUS:
            case WM_ACTIVATEAPP:
                if (input_ != nullptr) {
                    bool handled = false;
                    mklib_windows_process_message(input_, static_cast<uint32_t>(message),
                                                  static_cast<uintptr_t>(wparam), static_cast<intptr_t>(lparam), &handled);
                }
                return DefWindowProcW(hwnd_, message, wparam, lparam);
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT paint_struct{};
                HDC window_dc = BeginPaint(hwnd_, &paint_struct);
                RECT client{};
                GetClientRect(hwnd_, &client);
                const int width = client.right - client.left;
                const int height = client.bottom - client.top;
                HDC buffer_dc = CreateCompatibleDC(window_dc);
                HBITMAP buffer_bitmap = CreateCompatibleBitmap(window_dc, width, height);
                HGDIOBJ old_bitmap = SelectObject(buffer_dc, buffer_bitmap);
                paint(buffer_dc, client);
                BitBlt(window_dc, 0, 0, width, height, buffer_dc, 0, 0, SRCCOPY);
                SelectObject(buffer_dc, old_bitmap);
                DeleteObject(buffer_bitmap);
                DeleteDC(buffer_dc);
                EndPaint(hwnd_, &paint_struct);
                return 0;
            }
            case WM_DESTROY:
                if (timer_id_ != 0) KillTimer(hwnd_, timer_id_);
                if (input_ != nullptr) {
                    mklib_stop(input_);
                    mklib_windows_detach_window(input_);
                    mklib_destroy(&input_);
                }
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(hwnd_, message, wparam, lparam);
        }
    }

    std::wstring title_;
    int width_ = 0;
    int height_ = 0;
    uint32_t mask_ = 0;
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT_PTR timer_id_ = 0;
    mklib_handle *input_ = nullptr;
    std::vector<mklib_device_info> devices_;
    std::string status_;
    std::chrono::steady_clock::time_point last_tick_ = std::chrono::steady_clock::now();
};

inline const mklib_device_info *find_device(const std::vector<mklib_device_info> &devices, mklib_device_id id) {
    for (const auto &device : devices) if (device.id == id) return &device;
    return nullptr;
}

inline const char *kind_name(mklib_device_kind kind) {
    switch (kind) {
        case MKLIB_DEVICE_KEYBOARD: return "keyboard";
        case MKLIB_DEVICE_MOUSE: return "mouse";
        case MKLIB_DEVICE_TOUCHPAD: return "touchpad";
        default: return "unknown";
    }
}

inline std::wstring device_line(const mklib_device_info &device) {
    char buffer[512]{};
    std::snprintf(buffer, sizeof(buffer), "[%llu] %s %s %s VID:%04x PID:%04x",
                  static_cast<unsigned long long>(device.id),
                  device.product[0] ? device.product : "Unknown device", kind_name(device.kind),
                  device.transport, device.vendor_id, device.product_id);
    return wide(buffer);
}

}  // namespace mklib_demo_windows

#endif
