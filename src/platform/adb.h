#pragma once

#include "platform/process.h"
#include <functional>
#include <stdexcept>

namespace gfx {
std::string QuoteAndroidArgument(const std::string& text);
std::string AndroidCommand(const std::vector<std::string>& words);
struct AdbConnection {
    const std::string executable, serial;
    const std::atomic_bool* cancel = nullptr;
    std::function<ProcessResult(const ProcessRequest&, const std::atomic_bool*)> transport;
    ProcessResult Run(std::vector<std::string> args, const std::filesystem::path& input = {},
                      const std::filesystem::path& output = {}, bool cleanup = false) const;
    ProcessResult TryShell(const std::vector<std::string>& words, bool cleanup = false) const;
    std::string Shell(const std::vector<std::string>& words, bool cleanup = false) const;
    void Put(const std::string& package, const std::filesystem::path& source, const std::string& remote) const;
    void Get(const std::string& package, const std::string& source, const std::filesystem::path& destination) const;
    void Stop(const std::string& package) const;
};
struct AndroidLockState {
    std::string serial;
    bool known=false,locked=true,secureKnown=false,secure=true,dismissAttempted=false;
};
class AndroidUnlockRequired : public std::runtime_error {
public:
    AndroidLockState state;
    explicit AndroidUnlockRequired(AndroidLockState value,const std::string& detail = {});
};
AndroidLockState ParseAndroidLockState(const std::string& policy,const std::string& serial);
AndroidLockState EnsureAndroidReplayReady(const AdbConnection& adb);
} // namespace gfx
