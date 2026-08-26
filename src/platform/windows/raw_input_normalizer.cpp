#include "raw_input_normalizer.h"

namespace mklib_windows {
namespace {

uint16_t base_usage(uint16_t make_code) {
    switch (make_code) {
        case 0x01: return 0x29;
        case 0x02: return 0x1e;
        case 0x03: return 0x1f;
        case 0x04: return 0x20;
        case 0x05: return 0x21;
        case 0x06: return 0x22;
        case 0x07: return 0x23;
        case 0x08: return 0x24;
        case 0x09: return 0x25;
        case 0x0a: return 0x26;
        case 0x0b: return 0x27;
        case 0x0c: return 0x2d;
        case 0x0d: return 0x2e;
        case 0x0e: return 0x2a;
        case 0x0f: return 0x2b;
        case 0x10: return 0x14;
        case 0x11: return 0x1a;
        case 0x12: return 0x08;
        case 0x13: return 0x15;
        case 0x14: return 0x17;
        case 0x15: return 0x1c;
        case 0x16: return 0x18;
        case 0x17: return 0x0c;
        case 0x18: return 0x12;
        case 0x19: return 0x13;
        case 0x1a: return 0x2f;
        case 0x1b: return 0x30;
        case 0x1c: return 0x28;
        case 0x1d: return 0xe0;
        case 0x1e: return 0x04;
        case 0x1f: return 0x16;
        case 0x20: return 0x07;
        case 0x21: return 0x09;
        case 0x22: return 0x0a;
        case 0x23: return 0x0b;
        case 0x24: return 0x0d;
        case 0x25: return 0x0e;
        case 0x26: return 0x0f;
        case 0x27: return 0x33;
        case 0x28: return 0x34;
        case 0x29: return 0x35;
        case 0x2a: return 0xe1;
        case 0x2b: return 0x31;
        case 0x2c: return 0x1d;
        case 0x2d: return 0x1b;
        case 0x2e: return 0x06;
        case 0x2f: return 0x19;
        case 0x30: return 0x05;
        case 0x31: return 0x11;
        case 0x32: return 0x10;
        case 0x33: return 0x36;
        case 0x34: return 0x37;
        case 0x35: return 0x38;
        case 0x36: return 0xe5;
        case 0x37: return 0x55;
        case 0x38: return 0xe2;
        case 0x39: return 0x2c;
        case 0x3a: return 0x39;
        case 0x3b: return 0x3a;
        case 0x3c: return 0x3b;
        case 0x3d: return 0x3c;
        case 0x3e: return 0x3d;
        case 0x3f: return 0x3e;
        case 0x40: return 0x3f;
        case 0x41: return 0x40;
        case 0x42: return 0x41;
        case 0x43: return 0x42;
        case 0x44: return 0x43;
        case 0x45: return 0x53;
        case 0x46: return 0x47;
        case 0x47: return 0x5f;
        case 0x48: return 0x60;
        case 0x49: return 0x61;
        case 0x4a: return 0x56;
        case 0x4b: return 0x5c;
        case 0x4c: return 0x5d;
        case 0x4d: return 0x5e;
        case 0x4e: return 0x57;
        case 0x4f: return 0x59;
        case 0x50: return 0x5a;
        case 0x51: return 0x5b;
        case 0x52: return 0x62;
        case 0x53: return 0x63;
        case 0x57: return 0x44;
        case 0x58: return 0x45;
    }
    return 0;
}

uint16_t extended_usage(uint16_t make_code, bool e1) {
    if (e1 && make_code == 0x1d) {
        return 0x48;
    }
    switch (make_code) {
        case 0x1c: return 0x58;
        case 0x1d: return 0xe4;
        case 0x35: return 0x54;
        case 0x37: return 0x46;
        case 0x38: return 0xe6;
        case 0x47: return 0x4a;
        case 0x48: return 0x52;
        case 0x49: return 0x4b;
        case 0x4b: return 0x50;
        case 0x4d: return 0x4f;
        case 0x4f: return 0x4d;
        case 0x50: return 0x51;
        case 0x51: return 0x4e;
        case 0x52: return 0x49;
        case 0x53: return 0x4c;
    }
    return 0;
}

void append_mouse_button(uint16_t button_flags, uint16_t down_flag, uint16_t up_flag,
                         uint16_t usage, normalized_event *out_events, size_t capacity,
                         size_t *count) {
    if (*count >= capacity) {
        return;
    }
    if ((button_flags & down_flag) != 0) {
        out_events[*count].type = MKLIB_MOUSE_BUTTON_DOWN;
        out_events[*count].usage_page = 0x09;
        out_events[*count].usage = usage;
        out_events[*count].value = 1;
        ++*count;
    }
    if (*count >= capacity) {
        return;
    }
    if ((button_flags & up_flag) != 0) {
        out_events[*count].type = MKLIB_MOUSE_BUTTON_UP;
        out_events[*count].usage_page = 0x09;
        out_events[*count].usage = usage;
        out_events[*count].value = 0;
        ++*count;
    }
}

}  // namespace

bool normalize_keyboard(uint16_t make_code, uint16_t flags, normalized_event *out_event) {
    if (out_event == nullptr || make_code == 0) {
        return false;
    }
    const bool e0 = (flags & kKeyboardFlagE0) != 0;
    const bool e1 = (flags & kKeyboardFlagE1) != 0;
    const uint16_t usage = e0 || e1 ? extended_usage(make_code, e1) : base_usage(make_code);
    if (usage == 0) {
        return false;
    }
    out_event->type = (flags & kKeyboardFlagBreak) != 0 ? MKLIB_KEY_UP : MKLIB_KEY_DOWN;
    out_event->usage_page = 0x07;
    out_event->usage = usage;
    out_event->value = out_event->type == MKLIB_KEY_DOWN ? 1 : 0;
    out_event->repeat = false;
    return true;
}

size_t normalize_mouse(uint16_t flags, uint16_t button_flags, int16_t button_data,
                       int32_t last_x, int32_t last_y,
                       normalized_event *out_events, size_t capacity) {
    if (out_events == nullptr || capacity == 0 || (flags & kMouseMoveAbsolute) != 0) {
        return 0;
    }
    size_t count = 0;
    append_mouse_button(button_flags, kMouseButton1Down, kMouseButton1Up, 1,
                        out_events, capacity, &count);
    append_mouse_button(button_flags, kMouseButton2Down, kMouseButton2Up, 2,
                        out_events, capacity, &count);
    append_mouse_button(button_flags, kMouseButton3Down, kMouseButton3Up, 3,
                        out_events, capacity, &count);
    append_mouse_button(button_flags, kMouseButton4Down, kMouseButton4Up, 4,
                        out_events, capacity, &count);
    append_mouse_button(button_flags, kMouseButton5Down, kMouseButton5Up, 5,
                        out_events, capacity, &count);
    if ((button_flags & kMouseWheel) != 0 && button_data != 0 && count < capacity) {
        out_events[count].type = MKLIB_MOUSE_WHEEL;
        out_events[count].usage_page = 0x01;
        out_events[count].usage = 0x38;
        out_events[count].value = button_data;
        ++count;
    }
    if (last_x != 0 && count < capacity) {
        out_events[count].type = MKLIB_MOUSE_MOVE;
        out_events[count].usage_page = 0x01;
        out_events[count].usage = 0x30;
        out_events[count].value = last_x;
        ++count;
    }
    if (last_y != 0 && count < capacity) {
        out_events[count].type = MKLIB_MOUSE_MOVE;
        out_events[count].usage_page = 0x01;
        out_events[count].usage = 0x31;
        out_events[count].value = last_y;
        ++count;
    }
    return count;
}

}  // namespace mklib_windows
