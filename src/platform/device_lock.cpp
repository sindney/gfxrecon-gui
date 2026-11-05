#include "platform/device_lock.h"

#include <cctype>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
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
struct FileLock {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
#else
    int file = -1;
#endif
};
struct Held { unsigned count = 0; std::vector<FileLock> files; };
std::mutex mutex;
std::map<std::string, Held> held;
std::filesystem::path directory;
std::chrono::seconds wait{0};
std::string Safe(const std::string& serial) {
    std::string name;
    for (const char c : serial) name += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') ? c : '_';
    return name.empty() ? "local" : name;
}
bool TryLock(const std::filesystem::path& file, FileLock& entry) {
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
void Release(FileLock& entry) {
#ifdef _WIN32
    OVERLAPPED overlapped{}; UnlockFileEx(entry.file, 0, 1, 0, &overlapped); CloseHandle(entry.file);
#else
    flock(entry.file, LOCK_UN); close(entry.file);
#endif
}
} // namespace
void ConfigureDeviceLocks(const std::filesystem::path& root, std::chrono::seconds duration) {
    std::lock_guard<std::mutex> guard(mutex); directory = root; wait = duration;
}
DeviceLock::DeviceLock(const std::string& serial, const std::atomic_bool* cancel) : serial_(serial) {
    std::unique_lock<std::mutex> guard(mutex);
    if (directory.empty()) { serial_.clear(); return; } // locking not configured (unit tests, fixtures)
    const auto existing = held.find(serial_);
    if (existing != held.end() && existing->second.count) { ++existing->second.count; return; }
    std::filesystem::create_directories(directory);
    const auto common = std::filesystem::temp_directory_path() / "gfxrecon-gui-device-locks";
    std::filesystem::create_directories(common);
    const auto file = directory / (Safe(serial_) + ".lock");
    const auto commonFile = common / (Safe(serial_) + ".lock");
    const auto deadline = std::chrono::steady_clock::now() + wait;
    for (;;) {
        if (cancel && cancel->load()) throw std::runtime_error("Device lock cancelled");
        auto& entry = held[serial_];
        if (entry.count) { ++entry.count; return; }
        bool acquired = false;
        try {
            FileLock global;
            if (TryLock(commonFile, global)) {
                entry.files.push_back(global);
                FileLock local;
                if (file == commonFile || TryLock(file, local)) {
                    if (file != commonFile) entry.files.push_back(local);
                    acquired = true;
                }
            }
        } catch (...) {
            for (auto& item : entry.files) Release(item);
            held.erase(serial_); throw;
        }
        if (acquired) { entry.count = 1; return; }
        for (auto& item : entry.files) Release(item);
        entry.files.clear();
        held.erase(serial_);
        if (std::chrono::steady_clock::now() >= deadline) {
            throw DeviceBusy(serial_, "Device " + serial_ + " is busy: another GfxReconGui or gfxrecon-cli operation is using it.");
        }
        guard.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(250)); guard.lock();
    }
}
DeviceLock::~DeviceLock() {
    if (serial_.empty()) return;
    std::lock_guard<std::mutex> guard(mutex);
    const auto found = held.find(serial_);
    if (found == held.end() || --found->second.count) return;
    for (auto& item : found->second.files) Release(item);
    held.erase(found);
}
} // namespace gfx
