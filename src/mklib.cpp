#include "mklib/mklib.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDUsageTables.h>
#include <IOKit/hid/IOHIDValue.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

constexpr size_t kDefaultQueueCapacity = 4096;
constexpr uint16_t kKeyboardUsagePage = kHIDPage_KeyboardOrKeypad;

struct DeviceRecord {
    IOHIDDeviceRef device = nullptr;
    mklib_device_info info{};
};

struct mklib_handle_impl {
    mklib_config config{};
    IOHIDManagerRef manager = nullptr;
    dispatch_queue_t queue = nullptr;
    mutable std::mutex mutex;
    std::condition_variable event_condition;
    std::deque<mklib_event> events;
    std::unordered_map<IOHIDDeviceRef, DeviceRecord> devices;
    uint64_t next_device_id = 1;
    size_t queue_capacity = kDefaultQueueCapacity;
    std::atomic<bool> running{false};
    bool manager_cancelled = false;
};

static_assert(sizeof(mklib_handle_impl) > 0, "implementation type must be complete");

mklib_handle_impl *impl(mklib_handle *handle) {
    return reinterpret_cast<mklib_handle_impl *>(handle);
}

const mklib_handle_impl *impl(const mklib_handle *handle) {
    return reinterpret_cast<const mklib_handle_impl *>(handle);
}

void copy_string(CFTypeRef value, char *destination, size_t capacity) {
    if (destination == nullptr || capacity == 0) {
        return;
    }
    destination[0] = '\0';
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) {
        return;
    }
    CFStringGetCString(static_cast<CFStringRef>(value), destination,
                       static_cast<CFIndex>(capacity), kCFStringEncodingUTF8);
}

template <typename Number>
Number copy_number(CFTypeRef value, Number fallback = 0) {
    if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID()) {
        return fallback;
    }
    Number result = fallback;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(value),
                          std::is_same<Number, uint16_t>::value ? kCFNumberSInt16Type
                                                                : kCFNumberSInt32Type,
                          &result)) {
        return fallback;
    }
    return result;
}

uint64_t absolute_time_to_ns(uint64_t timestamp) {
    static mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t info{};
        mach_timebase_info(&info);
        return info;
    }();
    if (timebase.denom == 0) {
        return 0;
    }
    return timestamp * static_cast<uint64_t>(timebase.numer) /
           static_cast<uint64_t>(timebase.denom);
}

mklib_device_info make_device_info(IOHIDDeviceRef device, mklib_device_id id) {
    mklib_device_info info{};
    info.id = id;
    info.vendor_id = copy_number<uint16_t>(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey)));
    info.product_id = copy_number<uint16_t>(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey)));
    info.location_id = copy_number<uint32_t>(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDLocationIDKey)));
    copy_string(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDTransportKey)), info.transport,
                sizeof(info.transport));
    copy_string(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDManufacturerKey)), info.manufacturer,
                sizeof(info.manufacturer));
    copy_string(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey)), info.product,
                sizeof(info.product));
    copy_string(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDSerialNumberKey)), info.serial_number,
                sizeof(info.serial_number));
    const std::string manufacturer = info.manufacturer;
    const std::string product = info.product;
    const std::string serial = info.serial_number;
    const std::string transport = info.transport;
    std::string persistent = manufacturer + ":" + product + ":" + serial + ":" + transport;
    if (serial.empty()) {
        persistent += ":location=" + std::to_string(info.location_id);
    }
    std::strncpy(info.persistent_id, persistent.c_str(), sizeof(info.persistent_id) - 1);
    info.persistent_id[sizeof(info.persistent_id) - 1] = '\0';
    info.is_virtual = transport == kIOHIDTransportVirtualValue;
    return info;
}

void emit_device_callback(mklib_handle_impl *state, const mklib_device_info &info,
                          mklib_device_event_type type) {
    if (state->config.device_callback != nullptr) {
        state->config.device_callback(&info, type, state->config.user_data);
    }
}

void emit_event_callback(mklib_handle_impl *state, const mklib_event &event) {
    if (state->config.event_callback != nullptr) {
        state->config.event_callback(&event, state->config.user_data);
    }
}

void device_matching_callback(void *context, IOReturn, void *, IOHIDDeviceRef device) {
    auto *state = static_cast<mklib_handle_impl *>(context);
    if (state == nullptr || device == nullptr || !state->running.load()) {
        return;
    }

    mklib_device_info info{};
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->devices.find(device) != state->devices.end()) {
            return;
        }
        info = make_device_info(device, state->next_device_id++);
        CFRetain(device);
        state->devices.emplace(device, DeviceRecord{device, info});
    }
    emit_device_callback(state, info, MKLIB_DEVICE_ADDED);
}

void device_removal_callback(void *context, IOReturn, void *, IOHIDDeviceRef device) {
    auto *state = static_cast<mklib_handle_impl *>(context);
    if (state == nullptr || device == nullptr) {
        return;
    }

    mklib_device_info info{};
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto iterator = state->devices.find(device);
        if (iterator != state->devices.end()) {
            info = iterator->second.info;
            state->devices.erase(iterator);
            removed = true;
        }
    }
    if (removed) {
        CFRelease(device);
        emit_device_callback(state, info, MKLIB_DEVICE_REMOVED);
    }
}

void input_value_callback(void *context, IOReturn, void *, IOHIDValueRef value) {
    auto *state = static_cast<mklib_handle_impl *>(context);
    if (state == nullptr || value == nullptr || !state->running.load()) {
        return;
    }

    IOHIDElementRef element = IOHIDValueGetElement(value);
    if (element == nullptr || IOHIDElementGetUsagePage(element) != kKeyboardUsagePage) {
        return;
    }
    const uint32_t usage = IOHIDElementGetUsage(element);
    if (usage == 0 || usage > UINT16_MAX) {
        return;
    }
    IOHIDDeviceRef device = IOHIDElementGetDevice(element);
    if (device == nullptr) {
        return;
    }

    mklib_event event{};
    event.device_id = 0;
    event.usage_page = kKeyboardUsagePage;
    event.usage = static_cast<uint16_t>(usage);
    event.value = static_cast<int32_t>(IOHIDValueGetIntegerValue(value));
    event.timestamp_ns = absolute_time_to_ns(IOHIDValueGetTimeStamp(value));
    event.type = event.value == 0 ? MKLIB_KEY_UP : MKLIB_KEY_DOWN;
    event.repeat = event.value > 1;

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto iterator = state->devices.find(device);
        if (iterator == state->devices.end()) {
            return;
        }
        event.device_id = iterator->second.info.id;
        if (state->events.size() >= state->queue_capacity) {
            state->events.pop_front();
        }
        state->events.push_back(event);
    }
    state->event_condition.notify_one();
    emit_event_callback(state, event);
}

void cancel_handler(mklib_handle_impl *state) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->manager_cancelled = true;
    state->event_condition.notify_all();
}

}  // namespace

struct mklib_handle : mklib_handle_impl {};

extern "C" {

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
    switch (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent)) {
        case kIOHIDAccessTypeGranted: return MKLIB_ACCESS_GRANTED;
        case kIOHIDAccessTypeDenied: return MKLIB_ACCESS_DENIED;
        case kIOHIDAccessTypeUnknown: return MKLIB_ACCESS_UNKNOWN;
    }
    return MKLIB_ACCESS_UNKNOWN;
}

mklib_status mklib_request_input_access(void) {
    return IOHIDRequestAccess(kIOHIDRequestTypeListenEvent)
               ? MKLIB_OK
               : MKLIB_PERMISSION_DENIED;
}

mklib_status mklib_create(const mklib_config *config, mklib_handle **out_handle) {
    if (out_handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    *out_handle = nullptr;
    auto *state = new (std::nothrow) mklib_handle();
    if (state == nullptr) {
        return MKLIB_INTERNAL_ERROR;
    }
    if (config != nullptr) {
        state->config = *config;
    }
    if (state->config.event_queue_capacity != 0) {
        state->queue_capacity = state->config.event_queue_capacity;
    }
    *out_handle = reinterpret_cast<mklib_handle *>(state);
    return MKLIB_OK;
}

mklib_status mklib_start(mklib_handle *handle) {
    if (handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    auto *state = impl(handle);
    if (state->running.exchange(true)) {
        return MKLIB_ALREADY_RUNNING;
    }
    if (state->config.request_input_access &&
        mklib_input_access_status() != MKLIB_ACCESS_GRANTED &&
        mklib_request_input_access() != MKLIB_OK) {
        state->running.store(false);
        return MKLIB_PERMISSION_DENIED;
    }

    state->manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);
    if (state->manager == nullptr) {
        state->running.store(false);
        return MKLIB_INTERNAL_ERROR;
    }

    int usage_page = kHIDPage_GenericDesktop;
    int usage = kHIDUsage_GD_Keyboard;
    const void *keys[] = {
        CFSTR(kIOHIDPrimaryUsagePageKey),
        CFSTR(kIOHIDPrimaryUsageKey)
    };
    const void *values[] = {
        CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage_page),
        CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage)
    };
    CFDictionaryRef matching = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
                                                  &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
    CFRelease(values[0]);
    CFRelease(values[1]);
    IOHIDManagerSetDeviceMatching(state->manager, matching);
    if (matching != nullptr) {
        CFRelease(matching);
    }

    state->queue = dispatch_queue_create("com.mklib.hid", DISPATCH_QUEUE_SERIAL);
    if (state->queue == nullptr) {
        CFRelease(state->manager);
        state->manager = nullptr;
        state->running.store(false);
        return MKLIB_INTERNAL_ERROR;
    }
    IOHIDManagerSetDispatchQueue(state->manager, state->queue);
    IOHIDManagerRegisterDeviceMatchingCallback(state->manager, device_matching_callback, state);
    IOHIDManagerRegisterDeviceRemovalCallback(state->manager, device_removal_callback, state);
    IOHIDManagerRegisterInputValueCallback(state->manager, input_value_callback, state);
    IOHIDManagerSetInputValueMatching(state->manager, nullptr);
    IOHIDManagerSetCancelHandler(state->manager, ^{
        cancel_handler(state);
    });
    IOHIDManagerActivate(state->manager);
    return MKLIB_OK;
}

mklib_status mklib_stop(mklib_handle *handle) {
    if (handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    auto *state = impl(handle);
    if (!state->running.exchange(false)) {
        return MKLIB_NOT_RUNNING;
    }
    if (state->manager != nullptr) {
        IOHIDManagerCancel(state->manager);
        dispatch_sync(state->queue, ^{
        });
        std::unordered_map<IOHIDDeviceRef, DeviceRecord> devices;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            devices.swap(state->devices);
            state->events.clear();
        }
        for (const auto &entry : devices) {
            CFRelease(entry.second.device);
        }
        CFRelease(state->manager);
        state->manager = nullptr;
    }
    if (state->queue != nullptr) {
        dispatch_release(state->queue);
        state->queue = nullptr;
    }
    return MKLIB_OK;
}

mklib_status mklib_destroy(mklib_handle **handle) {
    if (handle == nullptr || *handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    auto *state = impl(*handle);
    if (state->running.load()) {
        mklib_stop(*handle);
    }
    delete state;
    *handle = nullptr;
    return MKLIB_OK;
}

mklib_status mklib_get_devices(const mklib_handle *handle, mklib_device_info *devices,
                               size_t capacity, size_t *out_count) {
    if (handle == nullptr || out_count == nullptr || (capacity != 0 && devices == nullptr)) {
        return MKLIB_INVALID_ARGUMENT;
    }
    const auto *state = impl(handle);
    std::lock_guard<std::mutex> lock(state->mutex);
    *out_count = state->devices.size();
    const size_t copied = std::min(capacity, *out_count);
    size_t index = 0;
    for (const auto &entry : state->devices) {
        if (index >= copied) {
            break;
        }
        devices[index++] = entry.second.info;
    }
    return MKLIB_OK;
}

mklib_status mklib_poll_event(mklib_handle *handle, mklib_event *out_event, uint32_t timeout_ms) {
    if (handle == nullptr || out_event == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    auto *state = impl(handle);
    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->running.load()) {
        return MKLIB_NOT_RUNNING;
    }
    if (state->events.empty() && timeout_ms > 0) {
        state->event_condition.wait_for(lock, std::chrono::milliseconds(timeout_ms), [state] {
            return !state->events.empty() || !state->running.load() || state->manager_cancelled;
        });
    }
    if (state->events.empty()) {
        return state->running.load() ? MKLIB_TIMEOUT : MKLIB_NOT_RUNNING;
    }
    *out_event = state->events.front();
    state->events.pop_front();
    return MKLIB_OK;
}

}  // extern "C"
