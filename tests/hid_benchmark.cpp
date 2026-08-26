#include "mklib/mklib.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__unix__)
#include <sys/resource.h>
#endif

namespace {

struct CpuUsage {
    double user_seconds = 0.0;
    double system_seconds = 0.0;
};

CpuUsage read_cpu_usage() {
#if defined(__unix__)
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return {
        static_cast<double>(usage.ru_utime.tv_sec) + usage.ru_utime.tv_usec / 1e6,
        static_cast<double>(usage.ru_stime.tv_sec) + usage.ru_stime.tv_usec / 1e6,
    };
#else
    return {};
#endif
}

const char *event_type_name(mklib_input_event_type type) {
    switch (type) {
        case MKLIB_KEY_DOWN: return "key_down";
        case MKLIB_KEY_UP: return "key_up";
        case MKLIB_MOUSE_BUTTON_DOWN: return "mouse_button_down";
        case MKLIB_MOUSE_BUTTON_UP: return "mouse_button_up";
        case MKLIB_MOUSE_MOVE: return "mouse_move";
        case MKLIB_MOUSE_WHEEL: return "mouse_wheel";
    }
    return "unknown";
}

} // namespace

int main(int argc, char **argv) {
    const double duration_seconds = argc > 1 ? std::atof(argv[1]) : 10.0;
    if (duration_seconds <= 0.0) {
        std::fprintf(stderr, "usage: %s [duration_seconds]\n", argv[0]);
        return 2;
    }

    mklib_config config{};
    if (mklib_config_init(&config) != MKLIB_OK) {
        std::fprintf(stderr, "mklib_config_init failed\n");
        return 1;
    }
    config.event_queue_capacity = 4096;
    config.request_input_access = true;
    config.device_kind_mask = MKLIB_DEVICE_MASK_ALL;

    mklib_handle *handle = nullptr;
    const mklib_status create_status = mklib_create(&config, &handle);
    if (create_status != MKLIB_OK || handle == nullptr) {
        std::fprintf(stderr, "mklib_create failed: %s\n", mklib_status_string(create_status));
        return 1;
    }

    const mklib_status start_status = mklib_start(handle);
    if (start_status != MKLIB_OK) {
        std::fprintf(stderr, "mklib_start failed: %s\n", mklib_status_string(start_status));
        mklib_destroy(&handle);
        return 1;
    }

    size_t device_count = 0;
    mklib_get_devices(handle, nullptr, 0, &device_count);
    std::printf("mklib HID benchmark: %.1f seconds, devices=%zu\n", duration_seconds, device_count);
    std::printf("Move the mouse or press buttons during the measurement if desired.\n");
    std::fflush(stdout);

    uint64_t event_counts[7]{};
    uint64_t total_events = 0;
    const CpuUsage cpu_start = read_cpu_usage();
    const auto wall_start = std::chrono::steady_clock::now();
    const auto deadline = wall_start + std::chrono::duration<double>(duration_seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        mklib_event event{};
        const mklib_status status = mklib_poll_event(handle, &event, 50);
        if (status == MKLIB_OK) {
            ++total_events;
            const int type = static_cast<int>(event.type);
            if (type >= 0 && type < 7) {
                ++event_counts[type];
            }
        } else if (status != MKLIB_TIMEOUT) {
            std::fprintf(stderr, "mklib_poll_event stopped: %s\n", mklib_status_string(status));
            break;
        }
    }
    const auto wall_end = std::chrono::steady_clock::now();
    const CpuUsage cpu_end = read_cpu_usage();

    uint64_t dropped = 0;
    mklib_get_dropped_event_count(handle, &dropped);
    mklib_stop(handle);
    mklib_destroy(&handle);

    const double wall_seconds = std::chrono::duration<double>(wall_end - wall_start).count();
    const double cpu_seconds = (cpu_end.user_seconds - cpu_start.user_seconds) +
                               (cpu_end.system_seconds - cpu_start.system_seconds);
    const double cpu_percent = wall_seconds > 0.0 ? cpu_seconds / wall_seconds * 100.0 : 0.0;
    std::printf("wall_seconds=%.3f cpu_user=%.6f cpu_system=%.6f cpu_percent=%.2f\n",
                wall_seconds,
                cpu_end.user_seconds - cpu_start.user_seconds,
                cpu_end.system_seconds - cpu_start.system_seconds,
                cpu_percent);
    std::printf("total_events=%llu dropped_events=%llu\n",
                static_cast<unsigned long long>(total_events),
                static_cast<unsigned long long>(dropped));
    for (int type = MKLIB_KEY_DOWN; type <= MKLIB_MOUSE_WHEEL; ++type) {
        std::printf("%s=%llu\n", event_type_name(static_cast<mklib_input_event_type>(type)),
                    static_cast<unsigned long long>(event_counts[type]));
    }
    return 0;
}
