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
#include <cstdio>
#include <os/log.h>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace {

constexpr size_t kDefaultQueueCapacity = 4096;
constexpr uint16_t kKeyboardUsagePage = kHIDPage_KeyboardOrKeypad;
constexpr uint16_t kGenericDesktopUsagePage = kHIDPage_GenericDesktop;
constexpr uint16_t kButtonUsagePage = kHIDPage_Button;
char kCallbackQueueKey;

uint32_t device_kind_bit(mklib_device_kind kind) {
    switch (kind) {
        case MKLIB_DEVICE_KEYBOARD: return MKLIB_DEVICE_MASK_KEYBOARD;
        case MKLIB_DEVICE_MOUSE: return MKLIB_DEVICE_MASK_MOUSE;
        case MKLIB_DEVICE_TOUCHPAD: return MKLIB_DEVICE_MASK_TOUCHPAD;
        case MKLIB_DEVICE_UNKNOWN: return 0;
    }
    return 0;
}

struct DeviceRecord {
    IOHIDDeviceRef device = nullptr;
    mklib_device_info info{};
};

struct mklib_handle_impl {
    mklib_config config{};
    IOHIDManagerRef manager = nullptr;
    dispatch_queue_t queue = nullptr;
    dispatch_queue_t callback_queue = nullptr;
    mutable std::mutex lifecycle_mutex;
    mutable std::mutex mutex;
    std::condition_variable event_condition;
    std::deque<mklib_event> events;
    std::unordered_map<IOHIDDeviceRef, DeviceRecord> devices;
    uint64_t dropped_event_count = 0;
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

mklib_device_kind classify_device(IOHIDDeviceRef device) {
    if (device == nullptr) {
        return MKLIB_DEVICE_UNKNOWN;
    }
    const uint32_t usage_page = copy_number<uint32_t>(
        IOHIDDeviceGetProperty(device, CFSTR(kIOHIDPrimaryUsagePageKey)));
    const uint32_t usage = copy_number<uint32_t>(
        IOHIDDeviceGetProperty(device, CFSTR(kIOHIDPrimaryUsageKey)));
    CFTypeRef product_value = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
    if (product_value != nullptr && CFGetTypeID(product_value) == CFStringGetTypeID()) {
        CFStringRef product = static_cast<CFStringRef>(product_value);
        if (CFStringFind(product, CFSTR("Trackpad"), 0).location != kCFNotFound ||
            CFStringFind(product, CFSTR("Touchpad"), 0).location != kCFNotFound) {
            return MKLIB_DEVICE_TOUCHPAD;
        }
    }
    if (usage_page == kHIDPage_GenericDesktop && usage == kHIDUsage_GD_Keyboard) {
        return MKLIB_DEVICE_KEYBOARD;
    }
    if (usage_page == kHIDPage_GenericDesktop && usage == kHIDUsage_GD_Mouse) {
        return MKLIB_DEVICE_MOUSE;
    }
    if (usage_page == kHIDPage_Digitizer && usage == kHIDUsage_Dig_TouchPad) {
        return MKLIB_DEVICE_TOUCHPAD;
    }
    return MKLIB_DEVICE_UNKNOWN;
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

mklib_device_info make_device_info(IOHIDDeviceRef device, mklib_device_id id,
                                   mklib_device_kind kind) {
    mklib_device_info info{};
    info.id = id;
    info.kind = kind;
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
    if (state->config.device_callback == nullptr || state->callback_queue == nullptr) {
        return;
    }
    const mklib_device_callback callback = state->config.device_callback;
    const mklib_device_info callback_info = info;
    void *user_data = state->config.user_data;
    dispatch_async(state->callback_queue, ^{
        callback(&callback_info, type, user_data);
    });
}

void emit_event_callback(mklib_handle_impl *state, const mklib_event &event) {
    if (state->config.event_callback == nullptr || state->callback_queue == nullptr) {
        return;
    }
    const mklib_event_callback callback = state->config.event_callback;
    const mklib_event callback_event = event;
    void *user_data = state->config.user_data;
    dispatch_async(state->callback_queue, ^{
        callback(&callback_event, user_data);
    });
}

void device_matching_callback(void *context, IOReturn, void *, IOHIDDeviceRef device) {
    auto *state = static_cast<mklib_handle_impl *>(context);
    if (state == nullptr || device == nullptr || !state->running.load()) {
        return;
    }

    const mklib_device_kind kind = classify_device(device);
    const uint32_t kind_mask = state->config.device_kind_mask == 0
        ? MKLIB_DEVICE_MASK_KEYBOARD
        : state->config.device_kind_mask;
    if ((kind_mask & device_kind_bit(kind)) == 0) {
        return;
    }

    mklib_device_info info{};
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->devices.find(device) != state->devices.end()) {
            return;
        }
        info = make_device_info(device, state->next_device_id++, kind);
        CFRetain(device);
        state->devices.emplace(device, DeviceRecord{device, info});
    }
    emit_device_callback(state, info, MKLIB_DEVICE_ADDED);
}

void copy_matching_device_callback(const void *value, void *context) {
    auto device = static_cast<IOHIDDeviceRef>(const_cast<void *>(value));
    device_matching_callback(context, 0, nullptr, device);
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
    if (element == nullptr) {
        return;
    }
    IOHIDDeviceRef device = IOHIDElementGetDevice(element);
    if (device == nullptr) {
        return;
    }

    const uint32_t usage_page = IOHIDElementGetUsagePage(element);
    const uint32_t usage = IOHIDElementGetUsage(element);
    if (usage == 0 || usage > UINT16_MAX) {
        return;
    }
    const int32_t value_number = static_cast<int32_t>(IOHIDValueGetIntegerValue(value));

    mklib_event event{};
    event.usage_page = static_cast<uint16_t>(usage_page);
    event.usage = static_cast<uint16_t>(usage);
    event.value = value_number;
    event.timestamp_ns = absolute_time_to_ns(IOHIDValueGetTimeStamp(value));
    event.repeat = false;

    mklib_device_kind kind = MKLIB_DEVICE_UNKNOWN;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto iterator = state->devices.find(device);
        if (iterator == state->devices.end()) {
            return;
        }
        event.device_id = iterator->second.info.id;
        kind = iterator->second.info.kind;
    }

    if (kind == MKLIB_DEVICE_KEYBOARD && usage_page == kKeyboardUsagePage) {
        event.type = value_number == 0 ? MKLIB_KEY_UP : MKLIB_KEY_DOWN;
    } else if ((kind == MKLIB_DEVICE_MOUSE || kind == MKLIB_DEVICE_TOUCHPAD) &&
               usage_page == kButtonUsagePage && usage <= 32) {
        event.type = value_number == 0 ? MKLIB_MOUSE_BUTTON_UP : MKLIB_MOUSE_BUTTON_DOWN;
    } else if (kind == MKLIB_DEVICE_TOUCHPAD &&
               usage_page == kHIDPage_Digitizer &&
               usage == kHIDUsage_Dig_TipSwitch) {
        event.type = value_number == 0 ? MKLIB_MOUSE_BUTTON_UP : MKLIB_MOUSE_BUTTON_DOWN;
    } else if ((kind == MKLIB_DEVICE_MOUSE || kind == MKLIB_DEVICE_TOUCHPAD) &&
               usage_page == kGenericDesktopUsagePage &&
               (usage == kHIDUsage_GD_X || usage == kHIDUsage_GD_Y)) {
        event.type = MKLIB_MOUSE_MOVE;
    } else {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->events.size() >= state->queue_capacity) {
            state->events.pop_front();
            ++state->dropped_event_count;
        }
        state->events.push_back(event);
    }
    state->event_condition.notify_one();
    static std::atomic<bool> reported_first_event{false};
    if (!reported_first_event.exchange(true)) {
        std::fprintf(stderr, "[mklib] received first input: device=%llu page=0x%04x usage=0x%04x value=%d\n",
                     static_cast<unsigned long long>(event.device_id),
                     event.usage_page, event.usage, event.value);
        os_log(OS_LOG_DEFAULT, "mklib received first input: device=%llu page=0x%04x usage=0x%04x value=%d",
               static_cast<unsigned long long>(event.device_id),
               event.usage_page, event.usage, event.value);
    }
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

uint32_t mklib_abi_version(void) {
    return MKLIB_ABI_VERSION;
}

mklib_status mklib_config_init(mklib_config *config) {
    if (config == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    std::memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->abi_version = MKLIB_ABI_VERSION;
    return MKLIB_OK;
}

const char *mklib_platform_name(void) {
    return "macOS";
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
    mklib_config_init(&state->config);
    if (config != nullptr) {
        if (config->struct_size != 0 && config->struct_size < offsetof(mklib_config, device_kind_mask) + sizeof(config->device_kind_mask)) {
            delete state;
            return MKLIB_INVALID_ARGUMENT;
        }
        if (config->abi_version != 0 && config->abi_version != MKLIB_ABI_VERSION) {
            delete state;
            return MKLIB_INVALID_ARGUMENT;
        }
        const size_t copy_size = config->struct_size == 0
            ? sizeof(mklib_config)
            : std::min(static_cast<size_t>(config->struct_size), sizeof(mklib_config));
        std::memcpy(&state->config, config, copy_size);
        state->config.struct_size = sizeof(mklib_config);
        state->config.abi_version = MKLIB_ABI_VERSION;
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
    std::lock_guard<std::mutex> lifecycle_lock(state->lifecycle_mutex);
    if (state->running.exchange(true)) {
        return MKLIB_ALREADY_RUNNING;
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->manager_cancelled = false;
        state->events.clear();
    }
    if (state->config.request_input_access &&
        mklib_input_access_status() != MKLIB_ACCESS_GRANTED) {
        mklib_request_input_access();
        if (mklib_input_access_status() != MKLIB_ACCESS_GRANTED) {
            state->running.store(false);
            return MKLIB_PERMISSION_DENIED;
        }
    }

    state->manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);
    if (state->manager == nullptr) {
        state->running.store(false);
        return MKLIB_INTERNAL_ERROR;
    }

    const uint32_t kind_mask = state->config.device_kind_mask == 0
        ? MKLIB_DEVICE_MASK_KEYBOARD
        : state->config.device_kind_mask;
    CFMutableArrayRef matching = CFArrayCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    const auto append_matching = [matching](int usage_page, int usage) {
        const void *keys[] = {
            CFSTR(kIOHIDPrimaryUsagePageKey),
            CFSTR(kIOHIDPrimaryUsageKey)
        };
        const void *values[] = {
            CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage_page),
            CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage)
        };
        CFDictionaryRef dictionary = CFDictionaryCreate(
            kCFAllocatorDefault, keys, values, 2,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        CFRelease(values[0]);
        CFRelease(values[1]);
        if (dictionary != nullptr) {
            CFArrayAppendValue(matching, dictionary);
            CFRelease(dictionary);
        }
    };
    if ((kind_mask & MKLIB_DEVICE_MASK_KEYBOARD) != 0) {
        append_matching(kHIDPage_GenericDesktop, kHIDUsage_GD_Keyboard);
    }
    if ((kind_mask & (MKLIB_DEVICE_MASK_MOUSE | MKLIB_DEVICE_MASK_TOUCHPAD)) != 0) {
        append_matching(kHIDPage_GenericDesktop, kHIDUsage_GD_Mouse);
    }
    if ((kind_mask & MKLIB_DEVICE_MASK_TOUCHPAD) != 0) {
        append_matching(kHIDPage_Digitizer, kHIDUsage_Dig_TouchPad);
    }
    IOHIDManagerSetDeviceMatchingMultiple(state->manager, matching);
    if (matching != nullptr) {
        CFRelease(matching);
    }

    state->queue = dispatch_queue_create("com.mklib.hid", DISPATCH_QUEUE_SERIAL);
    if (state->config.device_callback != nullptr || state->config.event_callback != nullptr) {
        state->callback_queue = dispatch_queue_create("com.mklib.callbacks", DISPATCH_QUEUE_SERIAL);
    }
    if (state->queue == nullptr ||
        ((state->config.device_callback != nullptr || state->config.event_callback != nullptr) &&
         state->callback_queue == nullptr)) {
        if (state->queue != nullptr) {
            dispatch_release(state->queue);
            state->queue = nullptr;
        }
        if (state->callback_queue != nullptr) {
            dispatch_release(state->callback_queue);
            state->callback_queue = nullptr;
        }
        CFRelease(state->manager);
        state->manager = nullptr;
        state->running.store(false);
        return MKLIB_INTERNAL_ERROR;
    }
    if (state->callback_queue != nullptr) {
        dispatch_queue_set_specific(state->callback_queue, &kCallbackQueueKey, state, nullptr);
    }
    IOHIDManagerSetDispatchQueue(state->manager, state->queue);
    IOHIDManagerRegisterDeviceMatchingCallback(state->manager, device_matching_callback, state);
    IOHIDManagerRegisterDeviceRemovalCallback(state->manager, device_removal_callback, state);
    IOHIDManagerRegisterInputValueCallback(state->manager, input_value_callback, state);
    IOHIDManagerSetInputValueMatching(state->manager, nullptr);
    IOHIDManagerSetCancelHandler(state->manager, ^{
        cancel_handler(state);
    });
    const IOReturn open_status = IOHIDManagerOpen(state->manager, kIOHIDOptionsTypeNone);
    std::fprintf(stderr, "[mklib] opened HID manager: status=0x%08x\n",
                 static_cast<unsigned int>(open_status));
    os_log(OS_LOG_DEFAULT, "mklib opened HID manager: status=0x%08x",
           static_cast<unsigned int>(open_status));
    if (open_status != kIOReturnSuccess) {
        IOHIDManagerCancel(state->manager);
        dispatch_sync(state->queue, ^{
        });
        CFRelease(state->manager);
        state->manager = nullptr;
        dispatch_release(state->queue);
        state->queue = nullptr;
        dispatch_release(state->callback_queue);
        state->callback_queue = nullptr;
        state->running.store(false);
        return MKLIB_INTERNAL_ERROR;
    }
    IOHIDManagerActivate(state->manager);
    CFSetRef matching_devices = IOHIDManagerCopyDevices(state->manager);
    const long matched_count = matching_devices == nullptr ? 0L : CFSetGetCount(matching_devices);
    std::fprintf(stderr, "[mklib] activated HID manager; matched devices=%ld\n", matched_count);
    os_log(OS_LOG_DEFAULT, "mklib activated HID manager; matched devices=%ld", matched_count);
    if (matching_devices != nullptr) {
        CFSetApplyFunction(matching_devices, copy_matching_device_callback, state);
        CFRelease(matching_devices);
    }
    return MKLIB_OK;
}

mklib_status mklib_stop(mklib_handle *handle) {
    if (handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    auto *state = impl(handle);
    if (dispatch_get_specific(&kCallbackQueueKey) == state) {
        return MKLIB_INTERNAL_ERROR;
    }
    std::lock_guard<std::mutex> lifecycle_lock(state->lifecycle_mutex);
    if (!state->running.exchange(false)) {
        return MKLIB_NOT_RUNNING;
    }
    state->event_condition.notify_all();
    if (state->manager != nullptr) {
        IOHIDManagerCancel(state->manager);
        dispatch_sync(state->queue, ^{
        });
        std::unordered_map<IOHIDDeviceRef, DeviceRecord> devices;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            devices.swap(state->devices);
            state->events.clear();
            state->manager_cancelled = false;
        }
        for (const auto &entry : devices) {
            CFRelease(entry.second.device);
        }
        IOHIDManagerClose(state->manager, kIOHIDOptionsTypeNone);
        CFRelease(state->manager);
        state->manager = nullptr;
    }
    if (state->queue != nullptr) {
        dispatch_release(state->queue);
        state->queue = nullptr;
    }
    if (state->callback_queue != nullptr) {
        dispatch_sync(state->callback_queue, ^{
        });
        dispatch_release(state->callback_queue);
        state->callback_queue = nullptr;
    }
    return MKLIB_OK;
}

mklib_status mklib_destroy(mklib_handle **handle) {
    if (handle == nullptr || *handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    mklib_handle *public_handle = *handle;
    auto *state = impl(public_handle);
    if (state->running.load()) {
        const mklib_status stop_status = mklib_stop(public_handle);
        if (stop_status != MKLIB_OK && stop_status != MKLIB_NOT_RUNNING) {
            return stop_status;
        }
    }
    delete public_handle;
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

mklib_status mklib_get_dropped_event_count(const mklib_handle *handle, uint64_t *out_count) {
    if (handle == nullptr || out_count == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    const auto *state = impl(handle);
    std::lock_guard<std::mutex> lock(state->mutex);
    *out_count = state->dropped_event_count;
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
