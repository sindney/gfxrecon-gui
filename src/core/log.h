#pragma once

#include <atomic>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace tpl {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3 };

struct LogRecord {
    LogLevel level;
    std::string category;
    std::string file;      // basename only
    std::string function;
    int line;
    std::string message;   // raw streamed message
    std::string formatted; // full formatted line
};

class Log {
public:
    static Log& Instance();

    void SetMinLevel(LogLevel level) { minLevel_.store(level); }
    LogLevel MinLevel() const { return minLevel_.load(); }

    bool SetFileOutput(const std::string& path, bool append);
    // Console echo target; the CLI routes logs to stderr to keep stdout machine-readable.
    void SetConsoleStream(std::ostream* stream) { std::lock_guard lock(mutex_); console_ = stream; }
    void ClearFileOutput();

    // Ring buffer the console window reads.
    std::vector<LogRecord> RecentRecords() const;
    void ClearHistory();

    void Publish(LogLevel level, const char* category, const char* file,
                 int line, const char* function, const std::string& message);

private:
    Log() = default;

    static constexpr size_t kHistoryCapacity = 5000;

    mutable std::mutex mutex_;
    std::atomic<LogLevel> minLevel_{LogLevel::Info};
    std::ofstream file_;
    std::ostream* console_ = nullptr; // null: std::cout
    std::deque<LogRecord> history_;
};

// RAII stream record: message is published from the destructor.
class LogLine {
public:
    LogLine(LogLevel level, const char* category, const char* file, int line,
            const char* function);
    ~LogLine();

    template <typename T>
    LogLine& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }
    LogLine& operator<<(std::ostream& (*manip)(std::ostream&)) {
        stream_ << manip;
        return *this;
    }

private:
    LogLevel level_;
    const char* category_;
    const char* file_;
    const char* function_;
    int line_;
    std::ostringstream stream_;
};

bool LevelEnabled(LogLevel level);

} // namespace tpl

#define LOGD(category) \
    if (::tpl::LevelEnabled(::tpl::LogLevel::Debug)) \
    ::tpl::LogLine(::tpl::LogLevel::Debug, category, __FILE__, __LINE__, __func__)
#define LOGI(category) \
    if (::tpl::LevelEnabled(::tpl::LogLevel::Info)) \
    ::tpl::LogLine(::tpl::LogLevel::Info, category, __FILE__, __LINE__, __func__)
#define LOGW(category) \
    if (::tpl::LevelEnabled(::tpl::LogLevel::Warn)) \
    ::tpl::LogLine(::tpl::LogLevel::Warn, category, __FILE__, __LINE__, __func__)
#define LOGE(category) \
    if (::tpl::LevelEnabled(::tpl::LogLevel::Error)) \
    ::tpl::LogLine(::tpl::LogLevel::Error, category, __FILE__, __LINE__, __func__)
