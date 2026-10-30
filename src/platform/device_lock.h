#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace gfx {
// Thrown when another process owns the device; CLI maps it to error code `device_busy`.
class DeviceBusy : public std::runtime_error {
public:
    std::string serial;
    DeviceBusy(const std::string& device, const std::string& detail) : std::runtime_error(detail), serial(device) {}
};
// Process-wide configuration: <settings>/locks and how long a contending operation waits.
void ConfigureDeviceLocks(const std::filesystem::path& directory, std::chrono::seconds wait = std::chrono::seconds(0));
// Exclusive per-serial host lock held across processes (GUI and CLI) for device-mutating work.
// Reentrant within one process so nested operations on the same device do not deadlock.
class DeviceLock {
public:
    explicit DeviceLock(const std::string& serial);
    ~DeviceLock();
    DeviceLock(const DeviceLock&) = delete;
    DeviceLock& operator=(const DeviceLock&) = delete;
private:
    std::string serial_;
};
} // namespace gfx
