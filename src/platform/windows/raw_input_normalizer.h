#ifndef MKLIB_WINDOWS_RAW_INPUT_NORMALIZER_H
#define MKLIB_WINDOWS_RAW_INPUT_NORMALIZER_H

#include "mklib/mklib.h"

#include <cstddef>
#include <cstdint>

namespace mklib_windows {

constexpr uint16_t kKeyboardFlagBreak = 0x0001u;
constexpr uint16_t kKeyboardFlagE0 = 0x0002u;
constexpr uint16_t kKeyboardFlagE1 = 0x0004u;
constexpr uint16_t kMouseMoveAbsolute = 0x0001u;
constexpr uint16_t kMouseButton1Down = 0x0001u;
constexpr uint16_t kMouseButton1Up = 0x0002u;
constexpr uint16_t kMouseButton2Down = 0x0004u;
constexpr uint16_t kMouseButton2Up = 0x0008u;
constexpr uint16_t kMouseButton3Down = 0x0010u;
constexpr uint16_t kMouseButton3Up = 0x0020u;
constexpr uint16_t kMouseButton4Down = 0x0040u;
constexpr uint16_t kMouseButton4Up = 0x0080u;
constexpr uint16_t kMouseButton5Down = 0x0100u;
constexpr uint16_t kMouseButton5Up = 0x0200u;
constexpr size_t kMaxNormalizedEvents = 8;

struct normalized_event {
    mklib_input_event_type type = MKLIB_KEY_DOWN;
    uint16_t usage_page = 0;
    uint16_t usage = 0;
    int32_t value = 0;
    bool repeat = false;
};

bool normalize_keyboard(uint16_t make_code, uint16_t flags, normalized_event *out_event);

size_t normalize_mouse(uint16_t flags, uint16_t button_flags, int16_t button_data,
                       int32_t last_x, int32_t last_y,
                       normalized_event *out_events, size_t capacity);

}  // namespace mklib_windows

#endif
