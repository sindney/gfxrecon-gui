#include "platform/device_lock.h"

#include <cctype>
#include <map>
#include <mutex>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace gfx {
namespace {
struct Held { unsigned count = 0;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
#else
    int file = -1;
#endif
};
std::mutex mutex;
std::map<std::string, Held> held;
std::filesystem::path directory;
std::chrono::seconds wait{0};
std::string Safe(const std::string& serial) {
    std::string name;
    for (const char c : serial) name += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') ? c : '_';
    return name.empty() ? "local" : name;
}
bool TryLock(const std::filesystem::path& file, Held& entry) {
#ifdef _WIN32
    const auto handle = CreateFileW(file.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open device lock " + file.u8string());
    OVERLAPPED overlapped{};
    if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped)) { CloseHandle(handle); return false; }
    entry.file = handle;
#else
    const int handle = open(file.c_str(), O_RDWR | O_CREAT, 0644);
    if (handle < 0) throw std::runtime_error("Cannot open device lock " + file.u8string());
    if (flock(handle, LOCK_EX | LOCK_NB) != 0) { close(handle); return false; }
    entry.file = handle;
#endif
    return true;
}
} // namespace
void ConfigureDeviceLocks(const std::filesystem::path& root, std::chrono::seconds duration) {
    std::lock_guard<std::mutex> guard(mutex); directory = root; wait = duration;
}
DeviceLock::DeviceLock(const std::string& serial) : serial_(serial) {
    std::unique_lock<std::mutex> guard(mutex);
    if (directory.empty()) { serial_.clear(); return; } // locking not configured (unit tests, fixtures)
    auto& entry = held[serial_];
    if (entry.count) { ++entry.count; return; }
    std::filesystem::create_directories(directory);
    const auto file = directory / (Safe(serial_) + ".lock");
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (!TryLock(file, entry)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            held.erase(serial_);
            throw DeviceBusy(serial_, "Device " + serial_ + " is busy: another GfxReconGui or gfxrecon-cli operation is using it.");
        }
        guard.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(250)); guard.lock();
    }
    entry.count = 1;
}
DeviceLock::~DeviceLock() {
    if (serial_.empty()) return;
    std::lock_guard<std::mutex> guard(mutex);
    const auto found = held.find(serial_);
    if (found == held.end() || --found->second.count) return;
#ifdef _WIN32
    OVERLAPPED overlapped{}; UnlockFileEx(found->second.file, 0, 1, 0, &overlapped); CloseHandle(found->second.file);
#else
    flock(found->second.file, LOCK_UN); close(found->second.file);
#endif
    held.erase(found);
}
} // namespace gfx
