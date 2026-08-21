#include "mklib/mklib.h"

#if defined(_WIN32)
#define MKLIB_PLATFORM "Windows"
#elif defined(__linux__)
#define MKLIB_PLATFORM "Linux"
#else
#define MKLIB_PLATFORM "unsupported"
#endif

extern "C" {

uint32_t mklib_abi_version(void) {
    return MKLIB_ABI_VERSION;
}

mklib_status mklib_config_init(mklib_config *config) {
    if (config == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    *config = {};
    config->struct_size = sizeof(*config);
    config->abi_version = MKLIB_ABI_VERSION;
    return MKLIB_OK;
}

const char *mklib_platform_name(void) {
    return MKLIB_PLATFORM;
}

const char *mklib_status_string(mklib_status status) {
    switch (status) {
        case MKLIB_OK: return "ok";
        case MKLIB_INVALID_ARGUMENT: return "invalid argument";
        case MKLIB_ALREADY_RUNNING: return "already running";
        case MKLIB_NOT_RUNNING: return "not running";
        case MKLIB_PERMISSION_DENIED: return "permission denied";
        case MKLIB_PLATFORM_UNSUPPORTED: return "platform unsupported";
        case MKLIB_INTERNAL_ERROR: return "internal error";
        case MKLIB_TIMEOUT: return "timeout";
    }
    return "unknown status";
}

const char *mklib_access_status_string(mklib_access_status status) {
    switch (status) {
        case MKLIB_ACCESS_GRANTED: return "granted";
        case MKLIB_ACCESS_DENIED: return "denied";
        case MKLIB_ACCESS_UNKNOWN: return "unknown";
        case MKLIB_ACCESS_NOT_APPLICABLE: return "not applicable";
    }
    return "unknown";
}

mklib_access_status mklib_input_access_status(void) {
    return MKLIB_ACCESS_NOT_APPLICABLE;
}

mklib_status mklib_request_input_access(void) {
    return MKLIB_PLATFORM_UNSUPPORTED;
}

mklib_status mklib_create(const mklib_config *, mklib_handle **out_handle) {
    if (out_handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    *out_handle = nullptr;
    return MKLIB_PLATFORM_UNSUPPORTED;
}

mklib_status mklib_start(mklib_handle *) {
    return MKLIB_PLATFORM_UNSUPPORTED;
}

mklib_status mklib_stop(mklib_handle *) {
    return MKLIB_PLATFORM_UNSUPPORTED;
}

mklib_status mklib_destroy(mklib_handle **handle) {
    if (handle == nullptr || *handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    *handle = nullptr;
    return MKLIB_PLATFORM_UNSUPPORTED;
}

mklib_status mklib_get_devices(const mklib_handle *, mklib_device_info *, size_t,
                               size_t *out_count) {
    if (out_count == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    *out_count = 0;
    return MKLIB_PLATFORM_UNSUPPORTED;
}

mklib_status mklib_poll_event(mklib_handle *, mklib_event *, uint32_t) {
    return MKLIB_PLATFORM_UNSUPPORTED;
}

mklib_status mklib_get_dropped_event_count(const mklib_handle *, uint64_t *out_count) {
    if (out_count == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    *out_count = 0;
    return MKLIB_PLATFORM_UNSUPPORTED;
}

}
