#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>

namespace gfx {
struct ProcessRequest {
    std::string executable;
    std::vector<std::string> arguments;
    std::string workingDirectory;
    std::map<std::string, std::string> environment;
    std::chrono::milliseconds timeout{120000};
    size_t outputLimit = 8 * 1024 * 1024;
    std::filesystem::path stdinFile;
    std::filesystem::path stdoutFile;
};
struct ProcessResult {
    int exitCode = -1;
    std::string output;
    bool cancelled = false;
    bool timedOut = false;
    bool outputTruncated = false;
    bool Success() const { return exitCode == 0 && !cancelled && !timedOut; }
};
// Throws on launch/setup failure. Never invokes a command shell.
ProcessResult RunProcess(const ProcessRequest& request, const std::atomic_bool* cancel = nullptr);
std::string FindExecutablePath(const std::string& name);
std::vector<std::string> NativeArguments();
std::vector<std::string> SplitProcessArguments(const std::string& arguments);
// A launched game outlives its capture session. Stop is always explicit.
class GameProcess {
public:
    explicit GameProcess(const ProcessRequest& request);
    ~GameProcess();
    bool Running() const;
    void Stop();
    void SetCaptureEnabled(bool enabled);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gfx
