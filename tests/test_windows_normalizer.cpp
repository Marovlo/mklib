#include "raw_input_normalizer.h"

#include <cassert>

int main() {
    mklib_windows::normalized_event event{};
    assert(mklib_windows::normalize_keyboard(0x1e, 0, &event));
    assert(event.type == MKLIB_KEY_DOWN);
    assert(event.usage_page == 0x07);
    assert(event.usage == 0x04);
    assert(event.value == 1);

    assert(mklib_windows::normalize_keyboard(
        0x1d, mklib_windows::kKeyboardFlagBreak | mklib_windows::kKeyboardFlagE0,
        &event));
    assert(event.type == MKLIB_KEY_UP);
    assert(event.usage == 0xe4);
    assert(event.value == 0);

    assert(mklib_windows::normalize_keyboard(
        0x1d, mklib_windows::kKeyboardFlagE1, &event));
    assert(event.usage == 0x48);
    assert(!mklib_windows::normalize_keyboard(0, 0, &event));

    mklib_windows::normalized_event mouse_events[mklib_windows::kMaxNormalizedEvents]{};
    const size_t mouse_count = mklib_windows::normalize_mouse(
        0, mklib_windows::kMouseButton1Down | mklib_windows::kMouseButton2Up,
        0, -12, 7, mouse_events, mklib_windows::kMaxNormalizedEvents);
    assert(mouse_count == 4);
    assert(mouse_events[0].type == MKLIB_MOUSE_BUTTON_DOWN);
    assert(mouse_events[0].usage == 1);
    assert(mouse_events[1].type == MKLIB_MOUSE_BUTTON_UP);
    assert(mouse_events[1].usage == 2);
    assert(mouse_events[2].type == MKLIB_MOUSE_MOVE);
    assert(mouse_events[2].usage == 0x30);
    assert(mouse_events[2].value == -12);
    assert(mouse_events[3].usage == 0x31);
    assert(mouse_events[3].value == 7);

    assert(mklib_windows::normalize_mouse(
        0, 0, 120, 0, 0, mouse_events,
        mklib_windows::kMaxNormalizedEvents) == 1);
    assert(mouse_events[0].type == MKLIB_MOUSE_WHEEL);
    assert(mouse_events[0].usage_page == 0x01);
    assert(mouse_events[0].usage == 0x38);
    assert(mouse_events[0].value == 120);

    assert(mklib_windows::normalize_mouse(
        mklib_windows::kMouseMoveAbsolute, 0, 0, 1, 1, mouse_events,
        mklib_windows::kMaxNormalizedEvents) == 0);
    return 0;
}
