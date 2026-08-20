#ifndef MKLIB_MKLIB_H
#define MKLIB_MKLIB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(MKLIB_BUILD_SHARED)
#if defined(mklib_EXPORTS)
#define MKLIB_API __declspec(dllexport)
#else
#define MKLIB_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define MKLIB_API __attribute__((visibility("default")))
#else
#define MKLIB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mklib_handle mklib_handle;
typedef uint64_t mklib_device_id;

typedef enum mklib_status {
    MKLIB_OK = 0,
    MKLIB_INVALID_ARGUMENT = 1,
    MKLIB_ALREADY_RUNNING = 2,
    MKLIB_NOT_RUNNING = 3,
    MKLIB_PERMISSION_DENIED = 4,
    MKLIB_PLATFORM_UNSUPPORTED = 5,
    MKLIB_INTERNAL_ERROR = 6,
    MKLIB_TIMEOUT = 7
} mklib_status;

typedef enum mklib_access_status {
    MKLIB_ACCESS_GRANTED = 0,
    MKLIB_ACCESS_DENIED = 1,
    MKLIB_ACCESS_UNKNOWN = 2,
    MKLIB_ACCESS_NOT_APPLICABLE = 3
} mklib_access_status;

typedef enum mklib_device_event_type {
    MKLIB_DEVICE_ADDED = 1,
    MKLIB_DEVICE_REMOVED = 2
} mklib_device_event_type;

typedef enum mklib_input_event_type {
    MKLIB_KEY_DOWN = 1,
    MKLIB_KEY_UP = 2
} mklib_input_event_type;

typedef struct mklib_device_info {
    mklib_device_id id;
    uint16_t vendor_id;
    uint16_t product_id;
    uint32_t location_id;
    bool is_virtual;
    char transport[64];
    char manufacturer[128];
    char product[128];
    char serial_number[128];
    char persistent_id[256];
} mklib_device_info;

typedef struct mklib_event {
    mklib_input_event_type type;
    mklib_device_id device_id;
    uint16_t usage_page;
    uint16_t usage;
    int32_t value;
    uint64_t timestamp_ns;
    bool repeat;
} mklib_event;

typedef void (*mklib_device_callback)(const mklib_device_info *device,
                                       mklib_device_event_type type,
                                       void *user_data);
typedef void (*mklib_event_callback)(const mklib_event *event, void *user_data);

typedef struct mklib_config {
    mklib_device_callback device_callback;
    mklib_event_callback event_callback;
    void *user_data;
    size_t event_queue_capacity;
    bool request_input_access;
} mklib_config;

MKLIB_API const char *mklib_status_string(mklib_status status);
MKLIB_API const char *mklib_access_status_string(mklib_access_status status);

MKLIB_API mklib_access_status mklib_input_access_status(void);
MKLIB_API mklib_status mklib_request_input_access(void);

MKLIB_API mklib_status mklib_create(const mklib_config *config,
                                    mklib_handle **out_handle);
MKLIB_API mklib_status mklib_start(mklib_handle *handle);
MKLIB_API mklib_status mklib_stop(mklib_handle *handle);
MKLIB_API mklib_status mklib_destroy(mklib_handle **handle);

MKLIB_API mklib_status mklib_get_devices(const mklib_handle *handle,
                                         mklib_device_info *devices,
                                         size_t capacity,
                                         size_t *out_count);
MKLIB_API mklib_status mklib_poll_event(mklib_handle *handle,
                                        mklib_event *out_event,
                                        uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
