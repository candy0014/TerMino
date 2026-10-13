#include "tetr/input/keyboard_monitor.h"

#ifdef __linux__

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {
struct DetectedKeyboard {
    std::string path;
};

struct MonitoredKeyboard {
    std::string path;
    int fd = -1;
    std::unordered_set<int> pressed;
    bool monotonicEventClock = false;
};

std::vector<DetectedKeyboard> discover_keyboards() {
    std::ifstream input("/proc/bus/input/devices");
    std::vector<DetectedKeyboard> result;
    if (!input) return result;

    std::string handlers;
    std::string line;
    auto finish_block = [&] {
        std::istringstream stream(handlers);
        std::string token;
        bool keyboard = false;
        std::string eventToken;
        while (stream >> token) {
            if (token == "kbd") keyboard = true;
            if (token.rfind("event", 0) == 0 && token.size() > 5)
                eventToken = token;
        }
        if (keyboard && !eventToken.empty())
            result.push_back({"/dev/input/" + eventToken});
        handlers.clear();
    };

    while (std::getline(input, line)) {
        if (line.empty()) {
            finish_block();
            continue;
        }
        if (line.rfind("H: Handlers=", 0) == 0) handlers = line.substr(12);
    }
    finish_block();

    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.path < rhs.path;
    });
    result.erase(std::unique(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.path == rhs.path;
    }), result.end());
    return result;
}

bool would_block(int error) {
    if (error == EAGAIN) return true;
#if EWOULDBLOCK != EAGAIN
    if (error == EWOULDBLOCK) return true;
#endif
    return false;
}
} // namespace

KeyboardMonitor::KeyboardMonitor() : running(false) {}

KeyboardMonitor::~KeyboardMonitor() {
    stop();
}

void KeyboardMonitor::init() {
    stop();
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        keyPressCounts.clear();
        focus_.reset();
    }
    running = true;
    monitorThread = std::thread(&KeyboardMonitor::eventLoop, this);
}

void KeyboardMonitor::stop() {
    running = false;
    if (monitorThread.joinable()) monitorThread.join();
    std::lock_guard<std::mutex> lock(stateMutex);
    keyPressCounts.clear();
    focus_.reset();
}

bool KeyboardMonitor::keydown(int keycode) {
    return focus_.key_down(keycode);
}

void KeyboardMonitor::eventLoop() {
    std::vector<MonitoredKeyboard> devices;

    auto release_device = [&](MonitoredKeyboard& device) {
        std::lock_guard<std::mutex> lock(stateMutex);
        for (int code : device.pressed) {
            auto it = keyPressCounts.find(code);
            if (it == keyPressCounts.end()) continue;
            if (--it->second <= 0) {
                keyPressCounts.erase(it);
                focus_.update(code, false);
            }
        }
        device.pressed.clear();
    };

    auto close_device = [&](MonitoredKeyboard& device) {
        release_device(device);
        if (device.fd >= 0) close(device.fd);
        device.fd = -1;
    };

    auto refresh_devices = [&] {
        const auto detected = discover_keyboards();
        std::set<std::string> wanted;
        for (const auto& keyboard : detected) wanted.insert(keyboard.path);

        for (auto it = devices.begin(); it != devices.end();) {
            if (wanted.count(it->path) != 0) {
                ++it;
                continue;
            }
            close_device(*it);
            it = devices.erase(it);
        }

        std::set<std::string> opened;
        for (const auto& device : devices) opened.insert(device.path);
        for (const auto& keyboard : detected) {
            if (opened.count(keyboard.path) != 0) continue;
            const int fd = open(keyboard.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0) {
                const int clock = CLOCK_MONOTONIC;
                const bool monotonic = ioctl(fd, EVIOCSCLOCKID, &clock) == 0;
                devices.push_back({keyboard.path, fd, {}, monotonic});
            }
        }
    };

    auto handle_key = [&](MonitoredKeyboard& device, const input_event& event) {
        if (event.type != EV_KEY) return;
        const int code = event.code;
        const auto time = device.monotonicEventClock
            ? std::chrono::steady_clock::time_point(std::chrono::seconds(event.time.tv_sec)+
                std::chrono::microseconds(event.time.tv_usec))
            : std::chrono::steady_clock::now();
        if (event.value == 0) {
            if (device.pressed.erase(code) == 0) return;
            std::lock_guard<std::mutex> lock(stateMutex);
            auto it = keyPressCounts.find(code);
            if (it != keyPressCounts.end() && --it->second <= 0) {
                keyPressCounts.erase(it);
                focus_.update(code, false, time);
            }
            return;
        }
        // EV_KEY value 1 is press and 2 is kernel autorepeat. Both mean the
        // key is down; only the first event from this device increments the
        // aggregate count.
        if (device.pressed.insert(code).second) {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (++keyPressCounts[code] == 1) focus_.update(code, true, time);
        }
    };

    refresh_devices();
    auto nextRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(1);

    while (running) {
        terminalInput_.poll();
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextRefresh) {
            refresh_devices();
            nextRefresh = now + std::chrono::seconds(1);
        }

        if (devices.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::vector<pollfd> pollfds;
        pollfds.reserve(devices.size());
        for (const auto& device : devices)
            pollfds.push_back({device.fd, POLLIN, 0});

        const int ready = poll(pollfds.data(), pollfds.size(), 10);
        terminalInput_.poll(); // Focus loss must precede publishing key events.
        if (ready < 0) {
            if (errno == EINTR) continue;
            // A persistent poll error must not turn the monitor thread into a
            // tight CPU loop. Device discovery will retry on the next cycle.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (ready == 0) continue;

        std::vector<std::size_t> dead;
        for (std::size_t i = 0; i < pollfds.size(); ++i) {
            if (pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                dead.push_back(i);
                continue;
            }
            if (!(pollfds[i].revents & POLLIN)) continue;

            while (true) {
                input_event events[64];
                const ssize_t n = read(devices[i].fd, events, sizeof(events));
                if (n > 0) {
                    for (std::size_t index=0; index<static_cast<std::size_t>(n)/sizeof(input_event); ++index)
                        handle_key(devices[i], events[index]);
                    continue;
                }
                if (n < 0 && would_block(errno)) break;
                if (n < 0 && (errno == EINTR)) continue;
                if (n == 0 || n < 0) dead.push_back(i);
                break;
            }
        }

        std::sort(dead.begin(), dead.end());
        dead.erase(std::unique(dead.begin(), dead.end()), dead.end());
        for (auto it = dead.rbegin(); it != dead.rend(); ++it) {
            if (*it >= devices.size()) continue;
            close_device(devices[*it]);
            devices.erase(devices.begin() + static_cast<std::ptrdiff_t>(*it));
        }
    }

    for (auto& device : devices) close_device(device);
}

#endif
