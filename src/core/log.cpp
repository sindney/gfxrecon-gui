#include "core/log.h"

#include "platform/platform.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

#if PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace tpl {

namespace {

const char* LevelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "DBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "EROR";
    }
    return "????";
}

std::string Basename(const char* path) {
    std::string p(path);
    size_t pos = p.find_last_of("/\\");
    return pos == std::string::npos ? p : p.substr(pos + 1);
}

std::string Timestamp() {
    using namespace std::chrono;
    system_clock::time_point now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    int ms = (int)(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm;
#if PLATFORM_WINDOWS
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
}

} // namespace

Log& Log::Instance() {
    static Log instance;
    return instance;
}

bool Log::SetFileOutput(const std::string& path, bool append) {
    std::lock_guard lock(mutex_);
    file_.close();
    file_.open(path, std::ios::out | (append ? std::ios::app : std::ios::trunc));
    return file_.good();
}

void Log::ClearFileOutput() {
    std::lock_guard lock(mutex_);
    file_.close();
}

std::vector<LogRecord> Log::RecentRecords() const {
    std::lock_guard lock(mutex_);
    return {history_.begin(), history_.end()};
}

void Log::ClearHistory() {
    std::lock_guard lock(mutex_);
    history_.clear();
}

void Log::Publish(LogLevel level, const char* category, const char* file,
                  int line, const char* function, const std::string& message) {
    LogRecord record{level, category, Basename(file), function, line, message, {}};

    std::ostringstream header;
    header << "[" << Timestamp() << "][" << LevelTag(level) << "][" << category
           << "][t:" << std::this_thread::get_id() << "][" << record.file << ":"
           << line << "][" << function << "] ";
    record.formatted = header.str() + message;

    std::lock_guard lock(mutex_);
    auto& console = console_ ? *console_ : std::cout;
    console << record.formatted << "\n";
    console.flush();
    if (file_.is_open()) {
        file_ << record.formatted << "\n";
        file_.flush(); // flush per record so a crash loses at most this line
    }
#if PLATFORM_WINDOWS
    std::wstring wide(record.formatted.begin(), record.formatted.end());
    OutputDebugStringW((wide + L"\n").c_str());
#endif
    history_.push_back(std::move(record));
    if (history_.size() > kHistoryCapacity)
        history_.pop_front();
}

LogLine::LogLine(LogLevel level, const char* category, const char* file, int line,
                 const char* function)
    : level_(level), category_(category), file_(file), function_(function),
      line_(line) {}

LogLine::~LogLine() {
    Log::Instance().Publish(level_, category_, file_, line_, function_, stream_.str());
}

bool LevelEnabled(LogLevel level) {
    return (int)level <= (int)Log::Instance().MinLevel();
}

} // namespace tpl
