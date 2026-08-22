#include "mklib/mklib.h"
#if defined(_WIN32)
#include <windows.h>
#endif

#include <cassert>
#include <cstring>

int main() {
    assert(std::strcmp(mklib_status_string(MKLIB_OK), "ok") == 0);
    assert(std::strcmp(mklib_access_status_string(MKLIB_ACCESS_UNKNOWN), "unknown") == 0);

    assert(mklib_abi_version() == MKLIB_ABI_VERSION);
    assert(mklib_platform_name() != nullptr);

    mklib_config config{};
    assert(mklib_config_init(&config) == MKLIB_OK);
    config.event_queue_capacity = 8;

    mklib_handle *handle = nullptr;
    const char *platform = mklib_platform_name();
    if (std::strcmp(platform, "macOS") != 0 && std::strcmp(platform, "Windows") != 0) {
        assert(mklib_create(&config, &handle) == MKLIB_PLATFORM_UNSUPPORTED);
        assert(handle == nullptr);
        return 0;
    }
    assert(mklib_create(&config, &handle) == MKLIB_OK);
#if defined(_WIN32)
    assert(mklib_input_access_status() == MKLIB_ACCESS_NOT_APPLICABLE);
    assert(mklib_windows_attach_window(handle, 0, 0) == MKLIB_INVALID_ARGUMENT);
    assert(mklib_start(handle) == MKLIB_OK);
    bool handled = false;
    assert(mklib_windows_process_message(handle, WM_KILLFOCUS, 0, 0, &handled) == MKLIB_OK);
    assert(handled);
    assert(mklib_stop(handle) == MKLIB_OK);
#endif
    assert(handle != nullptr);

    size_t count = 99;
    assert(mklib_get_devices(handle, nullptr, 0, &count) == MKLIB_OK);
    assert(count == 0);

    uint64_t dropped_event_count = 99;
    assert(mklib_get_dropped_event_count(handle, &dropped_event_count) == MKLIB_OK);
    assert(dropped_event_count == 0);

    mklib_event event{};
    assert(mklib_poll_event(handle, &event, 0) == MKLIB_NOT_RUNNING);
    assert(mklib_destroy(&handle) == MKLIB_OK);
    assert(handle == nullptr);
    assert(mklib_destroy(&handle) == MKLIB_INVALID_ARGUMENT);
    return 0;
}
