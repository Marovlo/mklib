#include "mklib/mklib.h"

#include <cassert>
#include <cstring>

int main() {
    assert(std::strcmp(mklib_status_string(MKLIB_OK), "ok") == 0);
    assert(std::strcmp(mklib_access_status_string(MKLIB_ACCESS_UNKNOWN), "unknown") == 0);

    mklib_config config{};
    config.event_queue_capacity = 8;

    mklib_handle *handle = nullptr;
    assert(mklib_create(&config, &handle) == MKLIB_OK);
    assert(handle != nullptr);

    size_t count = 99;
    assert(mklib_get_devices(handle, nullptr, 0, &count) == MKLIB_OK);
    assert(count == 0);

    mklib_event event{};
    assert(mklib_poll_event(handle, &event, 0) == MKLIB_NOT_RUNNING);
    assert(mklib_destroy(&handle) == MKLIB_OK);
    assert(handle == nullptr);
    assert(mklib_destroy(&handle) == MKLIB_INVALID_ARGUMENT);
    return 0;
}
