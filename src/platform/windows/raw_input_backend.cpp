#include "mklib/mklib.h"
#include "raw_input_normalizer.h"

#include <windows.h>
#include <setupapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <deque>
#include <iterator>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr size_t kDefaultQueueCapacity = 4096;
constexpr uint16_t kGenericDesktopUsagePage = 0x01;
constexpr uint16_t kKeyboardUsage = 0x06;
constexpr uint16_t kMouseUsage = 0x02;

enum class callback_kind {
    device,
    event
};

struct callback_item {
    callback_kind kind = callback_kind::event;
    mklib_device_info device{};
    mklib_device_event_type device_type = MKLIB_DEVICE_ADDED;
    mklib_event event{};
};

struct device_record {
    mklib_device_info info{};
    std::unordered_set<uint16_t> pressed_keys;
    std::unordered_set<uint16_t> pressed_buttons;
};

struct mklib_handle_impl {
    mklib_config config{};
    mutable std::mutex lifecycle_mutex;
    mutable std::mutex mutex;
    std::condition_variable event_condition;
    std::deque<mklib_event> events;
    std::unordered_map<HANDLE, device_record> devices;
    uint64_t dropped_event_count = 0;
    uint64_t next_device_id = 1;
    size_t queue_capacity = kDefaultQueueCapacity;
    std::atomic<bool> running{false};
    HWND window = nullptr;
    uint32_t window_flags = 0;
    bool registered_by_library = false;
    std::mutex callback_mutex;
    std::condition_variable callback_condition;
    std::deque<callback_item> callback_items;
    std::thread callback_thread;
    bool callback_stop = false;
    std::thread::id callback_thread_id;
};

struct mklib_handle : mklib_handle_impl {};

mklib_handle_impl *impl(mklib_handle *handle) {
    return reinterpret_cast<mklib_handle_impl *>(handle);
}

const mklib_handle_impl *impl(const mklib_handle *handle) {
    return reinterpret_cast<const mklib_handle_impl *>(handle);
}

uint32_t device_kind_bit(mklib_device_kind kind) {
    switch (kind) {
        case MKLIB_DEVICE_KEYBOARD: return MKLIB_DEVICE_MASK_KEYBOARD;
        case MKLIB_DEVICE_MOUSE: return MKLIB_DEVICE_MASK_MOUSE;
        case MKLIB_DEVICE_TOUCHPAD: return MKLIB_DEVICE_MASK_TOUCHPAD;
        case MKLIB_DEVICE_UNKNOWN: return 0;
    }
    return 0;
}

mklib_device_kind device_kind_for_raw_type(DWORD type) {
    if (type == RIM_TYPEKEYBOARD) {
        return MKLIB_DEVICE_KEYBOARD;
    }
    if (type == RIM_TYPEMOUSE) {
        return MKLIB_DEVICE_MOUSE;
    }
    return MKLIB_DEVICE_UNKNOWN;
}

std::string utf8_string(const std::wstring &value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(),
                        static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

void copy_string(const std::string &value, char *destination, size_t capacity) {
    if (destination == nullptr || capacity == 0) {
        return;
    }
    const size_t length = std::min(value.size(), capacity - 1);
    std::memcpy(destination, value.data(), length);
    destination[length] = '\0';
}

std::wstring upper_string(std::wstring value) {
    for (wchar_t &character : value) {
        character = static_cast<wchar_t>(std::towupper(character));
    }
    return value;
}

bool parse_hex_token(const std::wstring &path, const wchar_t *token, uint16_t *out_value) {
    const std::wstring upper_path = upper_string(path);
    const std::wstring upper_token = upper_string(token);
    const size_t position = upper_path.find(upper_token);
    if (position == std::wstring::npos || position + upper_token.size() + 4 > upper_path.size()) {
        return false;
    }
    uint16_t value = 0;
    for (size_t index = 0; index < 4; ++index) {
        const wchar_t character = upper_path[position + upper_token.size() + index];
        uint16_t digit = 0;
        if (character >= L'0' && character <= L'9') {
            digit = static_cast<uint16_t>(character - L'0');
        } else if (character >= L'A' && character <= L'F') {
            digit = static_cast<uint16_t>(character - L'A' + 10);
        } else {
            return false;
        }
        value = static_cast<uint16_t>((value << 4) | digit);
    }
    *out_value = value;
    return true;
}

uint32_t path_hash(const std::wstring &path) {
    uint32_t result = 2166136261u;
    for (wchar_t character : path) {
        result ^= static_cast<uint32_t>(character);
        result *= 16777619u;
    }
    return result;
}

std::wstring raw_device_path(HANDLE device) {
    UINT character_count = 0;
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &character_count) == UINT(-1) ||
        character_count == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(static_cast<size_t>(character_count) + 1, L'\0');
    UINT copied = character_count + 1;
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, buffer.data(), &copied) == UINT(-1)) {
        return {};
    }
    return std::wstring(buffer.data());
}

bool path_matches_instance(const std::wstring &path, const std::wstring &instance_id) {
    std::wstring normalized_instance = upper_string(instance_id);
    for (wchar_t &character : normalized_instance) {
        if (character == L'\\') {
            character = L'#';
        }
    }
    return upper_string(path).find(normalized_instance) != std::wstring::npos;
}

std::wstring setup_property(HDEVINFO device_info_set, SP_DEVINFO_DATA *device_info,
                            DWORD property) {
    DWORD type = 0;
    DWORD size = 0;
    SetupDiGetDeviceRegistryPropertyW(device_info_set, device_info, property, &type,
                                      nullptr, 0, &size, nullptr);
    if (size == 0 || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return {};
    }
    std::vector<BYTE> buffer(size + sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(device_info_set, device_info, property, &type,
                                           buffer.data(), static_cast<DWORD>(buffer.size()),
                                           &size, nullptr)) {
        return {};
    }
    return std::wstring(reinterpret_cast<wchar_t *>(buffer.data()));
}

void find_setup_properties(const std::wstring &path, std::string *manufacturer,
                           std::string *product) {
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
                                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return;
    }
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA data{};
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(set, index, &data)) {
            break;
        }
        wchar_t instance_id[512]{};
        if (!SetupDiGetDeviceInstanceIdW(set, &data, instance_id,
                                         static_cast<DWORD>(sizeof(instance_id) / sizeof(instance_id[0])), nullptr)) {
            continue;
        }
        if (!path_matches_instance(path, instance_id)) {
            continue;
        }
        std::wstring manufacturer_value = setup_property(set, &data, SPDRP_MFG);
        std::wstring product_value = setup_property(set, &data, SPDRP_FRIENDLYNAME);
        if (product_value.empty()) {
            product_value = setup_property(set, &data, SPDRP_DEVICEDESC);
        }
        *manufacturer = utf8_string(manufacturer_value);
        *product = utf8_string(product_value);
        break;
    }
    SetupDiDestroyDeviceInfoList(set);
}

mklib_device_info make_device_info(HANDLE device, mklib_device_id id,
                                   mklib_device_kind kind, const std::wstring &path) {
    mklib_device_info info{};
    info.id = id;
    info.kind = kind;
    info.location_id = path_hash(path);
    info.is_virtual = upper_string(path).find(L"ROOT#") != std::wstring::npos ||
                      upper_string(path).find(L"VIRTUAL") != std::wstring::npos;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    parse_hex_token(path, L"VID_", &vendor_id);
    parse_hex_token(path, L"PID_", &product_id);
    info.vendor_id = vendor_id;
    info.product_id = product_id;
    const std::wstring upper_path = upper_string(path);
    const std::string transport = upper_path.find(L"BTH#") != std::wstring::npos
        ? "Bluetooth"
        : upper_path.find(L"USB#") != std::wstring::npos ? "USB" : "HID";
    copy_string(transport, info.transport, sizeof(info.transport));
    std::string manufacturer;
    std::string product;
    find_setup_properties(path, &manufacturer, &product);
    copy_string(manufacturer, info.manufacturer, sizeof(info.manufacturer));
    copy_string(product, info.product, sizeof(info.product));
    const std::string path_string = utf8_string(path);
    copy_string("windows:" + path_string, info.persistent_id, sizeof(info.persistent_id));
    (void)device;
    return info;
}

bool get_raw_device_type(HANDLE device, mklib_device_kind *out_kind) {
    RID_DEVICE_INFO info{};
    info.cbSize = sizeof(info);
    UINT size = sizeof(info);
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &info, &size) == UINT(-1)) {
        return false;
    }
    *out_kind = device_kind_for_raw_type(info.dwType);
    return *out_kind != MKLIB_DEVICE_UNKNOWN;
}

uint64_t timestamp_ns() {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart <= 0) {
        return 0;
    }
    const uint64_t seconds = static_cast<uint64_t>(counter.QuadPart / frequency.QuadPart);
    const uint64_t remainder = static_cast<uint64_t>(counter.QuadPart % frequency.QuadPart);
    return seconds * 1000000000ull + remainder * 1000000000ull /
        static_cast<uint64_t>(frequency.QuadPart);
}

void enqueue_callback(mklib_handle_impl *state, const callback_item &item) {
    if (state->config.device_callback == nullptr && state->config.event_callback == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state->callback_mutex);
        if (!state->callback_stop) {
            state->callback_items.push_back(item);
        }
    }
    state->callback_condition.notify_one();
}

void callback_loop(mklib_handle_impl *state) {
    {
        std::lock_guard<std::mutex> lock(state->callback_mutex);
        state->callback_thread_id = std::this_thread::get_id();
    }
    for (;;) {
        callback_item item{};
        {
            std::unique_lock<std::mutex> lock(state->callback_mutex);
            state->callback_condition.wait(lock, [state] {
                return state->callback_stop || !state->callback_items.empty();
            });
            if (state->callback_items.empty() && state->callback_stop) {
                break;
            }
            item = state->callback_items.front();
            state->callback_items.pop_front();
        }
        if (item.kind == callback_kind::device) {
            if (state->config.device_callback != nullptr) {
                state->config.device_callback(&item.device, item.device_type,
                                              state->config.user_data);
            }
        } else if (state->config.event_callback != nullptr) {
            state->config.event_callback(&item.event, state->config.user_data);
        }
    }
}

void start_callback_thread(mklib_handle_impl *state) {
    if (state->config.device_callback == nullptr && state->config.event_callback == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state->callback_mutex);
        state->callback_stop = false;
        state->callback_items.clear();
        state->callback_thread_id = std::thread::id();
    }
    state->callback_thread = std::thread(callback_loop, state);
}

void stop_callback_thread(mklib_handle_impl *state) {
    if (!state->callback_thread.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state->callback_mutex);
        state->callback_stop = true;
    }
    state->callback_condition.notify_all();
    state->callback_thread.join();
    std::lock_guard<std::mutex> lock(state->callback_mutex);
    state->callback_thread_id = std::thread::id();
}

void emit_device_callback(mklib_handle_impl *state, const mklib_device_info &info,
                          mklib_device_event_type type) {
    callback_item item{};
    item.kind = callback_kind::device;
    item.device = info;
    item.device_type = type;
    enqueue_callback(state, item);
}

void emit_event_callback(mklib_handle_impl *state, const mklib_event &event) {
    callback_item item{};
    item.kind = callback_kind::event;
    item.event = event;
    enqueue_callback(state, item);
}

void enqueue_event(mklib_handle_impl *state, mklib_event event) {
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->running.load()) {
            return;
        }
        if (state->events.size() >= state->queue_capacity) {
            state->events.pop_front();
            ++state->dropped_event_count;
        }
        state->events.push_back(event);
    }
    state->event_condition.notify_one();
    emit_event_callback(state, event);
}

bool add_device(mklib_handle_impl *state, HANDLE device) {
    if (device == nullptr) {
        return false;
    }
    mklib_device_kind kind = MKLIB_DEVICE_UNKNOWN;
    if (!get_raw_device_type(device, &kind)) {
        return false;
    }
    const uint32_t mask = state->config.device_kind_mask == 0
        ? MKLIB_DEVICE_MASK_KEYBOARD
        : state->config.device_kind_mask;
    if ((mask & device_kind_bit(kind)) == 0) {
        return false;
    }
    const std::wstring path = raw_device_path(device);
    mklib_device_info info{};
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->running.load() || state->devices.find(device) != state->devices.end()) {
            return false;
        }
        info = make_device_info(device, state->next_device_id++, kind, path);
        state->devices.emplace(device, device_record{info, {}, {}});
    }
    emit_device_callback(state, info, MKLIB_DEVICE_ADDED);
    return true;
}

bool remove_device(mklib_handle_impl *state, HANDLE device) {
    if (device == nullptr) {
        return false;
    }
    mklib_device_info info{};
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto iterator = state->devices.find(device);
        if (iterator == state->devices.end()) {
            return false;
        }
        info = iterator->second.info;
        state->devices.erase(iterator);
    }
    emit_device_callback(state, info, MKLIB_DEVICE_REMOVED);
    return true;
}

bool process_keyboard(mklib_handle_impl *state, HANDLE device, const RAWKEYBOARD &keyboard) {
    mklib_windows::normalized_event normalized{};
    const uint16_t flags = static_cast<uint16_t>(
        (keyboard.Flags & RI_KEY_BREAK ? mklib_windows::kKeyboardFlagBreak : 0) |
        (keyboard.Flags & RI_KEY_E0 ? mklib_windows::kKeyboardFlagE0 : 0) |
        (keyboard.Flags & RI_KEY_E1 ? mklib_windows::kKeyboardFlagE1 : 0));
    if (!mklib_windows::normalize_keyboard(keyboard.MakeCode, flags, &normalized)) {
        return false;
    }
    mklib_event event{};
    event.type = normalized.type;
    event.device_id = 0;
    event.usage_page = normalized.usage_page;
    event.usage = normalized.usage;
    event.value = normalized.value;
    event.timestamp_ns = timestamp_ns();
    event.repeat = normalized.repeat;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto iterator = state->devices.find(device);
        if (iterator == state->devices.end() || !state->running.load()) {
            return false;
        }
        event.device_id = iterator->second.info.id;
        if (event.type == MKLIB_KEY_DOWN) {
            iterator->second.pressed_keys.insert(event.usage);
        } else {
            iterator->second.pressed_keys.erase(event.usage);
        }
    }
    enqueue_event(state, event);
    return true;
}

bool process_mouse(mklib_handle_impl *state, HANDLE device, const RAWMOUSE &mouse) {
    mklib_windows::normalized_event normalized[mklib_windows::kMaxNormalizedEvents]{};
    const size_t count = mklib_windows::normalize_mouse(
        mouse.usFlags, mouse.usButtonFlags, mouse.usButtonData, mouse.lLastX, mouse.lLastY,
        normalized, mklib_windows::kMaxNormalizedEvents);
    if (count == 0) {
        return false;
    }
    mklib_device_id device_id = 0;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto iterator = state->devices.find(device);
        if (iterator == state->devices.end() || !state->running.load()) {
            return false;
        }
        device_id = iterator->second.info.id;
    }
    for (size_t index = 0; index < count; ++index) {
        mklib_event event{};
        event.type = normalized[index].type;
        event.device_id = device_id;
        event.usage_page = normalized[index].usage_page;
        event.usage = normalized[index].usage;
        event.value = normalized[index].value;
        event.timestamp_ns = timestamp_ns();
        event.repeat = false;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            const auto iterator = state->devices.find(device);
            if (iterator == state->devices.end() || !state->running.load()) {
                return index != 0;
            }
            if (event.type == MKLIB_MOUSE_BUTTON_DOWN) {
                iterator->second.pressed_buttons.insert(event.usage);
            } else if (event.type == MKLIB_MOUSE_BUTTON_UP) {
                iterator->second.pressed_buttons.erase(event.usage);
            }
        }
        enqueue_event(state, event);
    }
    return true;
}

bool process_raw_input_buffer(mklib_handle_impl *state, const RAWINPUT *raw_input) {
    if (raw_input == nullptr || raw_input->header.hDevice == nullptr) {
        return false;
    }
    if (raw_input->header.dwType == RIM_TYPEKEYBOARD) {
        return process_keyboard(state, raw_input->header.hDevice, raw_input->data.keyboard);
    }
    if (raw_input->header.dwType == RIM_TYPEMOUSE) {
        return process_mouse(state, raw_input->header.hDevice, raw_input->data.mouse);
    }
    return false;
}

bool process_raw_input(mklib_handle_impl *state, HRAWINPUT raw_handle) {
    UINT size = 0;
    if (GetRawInputData(raw_handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == UINT(-1) ||
        size == 0) {
        return false;
    }
    std::vector<BYTE> buffer(size);
    if (GetRawInputData(raw_handle, RID_INPUT, buffer.data(), &size,
                        sizeof(RAWINPUTHEADER)) == UINT(-1)) {
        return false;
    }
    return process_raw_input(state, reinterpret_cast<const RAWINPUT *>(buffer.data()));
}

bool register_raw_input(mklib_handle_impl *state) {
    if (state->window == nullptr || state->registered_by_library) {
        return true;
    }
    const uint32_t mask = state->config.device_kind_mask == 0
        ? MKLIB_DEVICE_MASK_KEYBOARD
        : state->config.device_kind_mask;
    RAWINPUTDEVICE registrations[2]{};
    UINT count = 0;
    const DWORD flags = RIDEV_DEVNOTIFY |
        ((state->window_flags & MKLIB_WINDOWS_ATTACH_INPUT_SINK) != 0 ? RIDEV_INPUTSINK : 0);
    if ((mask & MKLIB_DEVICE_MASK_KEYBOARD) != 0) {
        registrations[count].usUsagePage = kGenericDesktopUsagePage;
        registrations[count].usUsage = kKeyboardUsage;
        registrations[count].dwFlags = flags;
        registrations[count].hwndTarget = state->window;
        ++count;
    }
    if ((mask & MKLIB_DEVICE_MASK_MOUSE) != 0) {
        registrations[count].usUsagePage = kGenericDesktopUsagePage;
        registrations[count].usUsage = kMouseUsage;
        registrations[count].dwFlags = flags;
        registrations[count].hwndTarget = state->window;
        ++count;
    }
    if (count == 0) {
        return true;
    }
    if (!RegisterRawInputDevices(registrations, count, sizeof(RAWINPUTDEVICE))) {
        return false;
    }
    state->registered_by_library = true;
    return true;
}

void unregister_raw_input(mklib_handle_impl *state) {
    if (!state->registered_by_library) {
        return;
    }
    const uint32_t mask = state->config.device_kind_mask == 0
        ? MKLIB_DEVICE_MASK_KEYBOARD
        : state->config.device_kind_mask;
    RAWINPUTDEVICE registrations[2]{};
    UINT count = 0;
    if ((mask & MKLIB_DEVICE_MASK_KEYBOARD) != 0) {
        registrations[count].usUsagePage = kGenericDesktopUsagePage;
        registrations[count].usUsage = kKeyboardUsage;
        registrations[count].dwFlags = RIDEV_REMOVE;
        ++count;
    }
    if ((mask & MKLIB_DEVICE_MASK_MOUSE) != 0) {
        registrations[count].usUsagePage = kGenericDesktopUsagePage;
        registrations[count].usUsage = kMouseUsage;
        registrations[count].dwFlags = RIDEV_REMOVE;
        ++count;
    }
    if (count != 0) {
        RegisterRawInputDevices(registrations, count, sizeof(RAWINPUTDEVICE));
    }
    state->registered_by_library = false;
}

void enumerate_devices(mklib_handle_impl *state) {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) {
        return;
    }
    std::vector<RAWINPUTDEVICELIST> devices(count);
    if (count != 0 && GetRawInputDeviceList(devices.data(), &count,
                                            sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) {
        return;
    }
    for (UINT index = 0; index < count; ++index) {
        add_device(state, devices[index].hDevice);
    }
}

void clear_device_state(mklib_handle_impl *state) {
    std::lock_guard<std::mutex> lock(state->mutex);
    for (auto &entry : state->devices) {
        entry.second.pressed_keys.clear();
        entry.second.pressed_buttons.clear();
    }
}

}  // namespace

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
    return "Windows";
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
    return MKLIB_OK;
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
        if (config->struct_size != 0 && config->struct_size <
            offsetof(mklib_config, device_kind_mask) + sizeof(config->device_kind_mask)) {
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
    *out_handle = state;
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
        state->events.clear();
        state->devices.clear();
    }
    if ((state->window_flags & MKLIB_WINDOWS_ATTACH_REGISTER_RAW_INPUT) != 0 &&
        !register_raw_input(state)) {
        state->running.store(false);
        return MKLIB_INTERNAL_ERROR;
    }
    start_callback_thread(state);
    enumerate_devices(state);
    return MKLIB_OK;
}

mklib_status mklib_stop(mklib_handle *handle) {
    if (handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    auto *state = impl(handle);
    {
        std::lock_guard<std::mutex> lock(state->callback_mutex);
        if (state->callback_thread_id == std::this_thread::get_id()) {
            return MKLIB_INTERNAL_ERROR;
        }
    }
    std::lock_guard<std::mutex> lifecycle_lock(state->lifecycle_mutex);
    if (!state->running.exchange(false)) {
        return MKLIB_NOT_RUNNING;
    }
    state->event_condition.notify_all();
    clear_device_state(state);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->devices.clear();
        state->events.clear();
    }
    unregister_raw_input(state);
    stop_callback_thread(state);
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
            return !state->events.empty() || !state->running.load();
        });
    }
    if (state->events.empty()) {
        return state->running.load() ? MKLIB_TIMEOUT : MKLIB_NOT_RUNNING;
    }
    *out_event = state->events.front();
    state->events.pop_front();
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

mklib_status mklib_windows_attach_window(mklib_handle *handle,
                                         mklib_windows_window_handle window_handle,
                                         uint32_t flags) {
    if (handle == nullptr || window_handle == 0 ||
        (flags & ~(MKLIB_WINDOWS_ATTACH_REGISTER_RAW_INPUT |
                   MKLIB_WINDOWS_ATTACH_INPUT_SINK)) != 0) {
        return MKLIB_INVALID_ARGUMENT;
    }
    auto *state = impl(handle);
    std::lock_guard<std::mutex> lifecycle_lock(state->lifecycle_mutex);
    if (state->running.load() || !IsWindow(reinterpret_cast<HWND>(window_handle))) {
        return state->running.load() ? MKLIB_ALREADY_RUNNING : MKLIB_INVALID_ARGUMENT;
    }
    state->window = reinterpret_cast<HWND>(window_handle);
    state->window_flags = flags;
    return MKLIB_OK;
}

mklib_status mklib_windows_detach_window(mklib_handle *handle) {
    if (handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    auto *state = impl(handle);
    std::lock_guard<std::mutex> lifecycle_lock(state->lifecycle_mutex);
    if (state->running.load()) {
        return MKLIB_ALREADY_RUNNING;
    }
    state->window = nullptr;
    state->window_flags = 0;
    state->registered_by_library = false;
    return MKLIB_OK;
}

mklib_status mklib_windows_process_message(mklib_handle *handle, uint32_t message,
                                           uintptr_t wparam, intptr_t lparam,
                                           bool *out_handled) {
    if (handle == nullptr || out_handled == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    *out_handled = false;
    auto *state = impl(handle);
    std::lock_guard<std::mutex> lifecycle_lock(state->lifecycle_mutex);
    if (!state->running.load()) {
        return MKLIB_NOT_RUNNING;
    }
    if (message == WM_INPUT) {
        *out_handled = true;
        process_raw_input(state, reinterpret_cast<HRAWINPUT>(lparam));
        return MKLIB_OK;
    }
    if (message == WM_INPUT_DEVICE_CHANGE) {
        *out_handled = true;
        const HANDLE device = reinterpret_cast<HANDLE>(lparam);
        if (wparam == GIDC_ARRIVAL) {
            add_device(state, device);
        } else if (wparam == GIDC_REMOVAL) {
            remove_device(state, device);
        }
        return MKLIB_OK;
    }
    if (message == WM_KILLFOCUS ||
        (message == WM_ACTIVATEAPP && wparam == FALSE)) {
        *out_handled = true;
        clear_device_state(state);
        return MKLIB_OK;
    }
    return MKLIB_OK;
}

mklib_status mklib_windows_reset_input_state(mklib_handle *handle) {
    if (handle == nullptr) {
        return MKLIB_INVALID_ARGUMENT;
    }
    auto *state = impl(handle);
    std::lock_guard<std::mutex> lifecycle_lock(state->lifecycle_mutex);
    if (!state->running.load()) {
        return MKLIB_NOT_RUNNING;
    }
    clear_device_state(state);
    return MKLIB_OK;
}

}
