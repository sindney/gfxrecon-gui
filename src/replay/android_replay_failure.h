#pragma once
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>

namespace gfx {
// Attribute an abort to the latest native exit, not an unrelated/stale crash in
// Android's shared crash buffer. Missing identity/time evidence stays unknown.
inline std::string AndroidNativeCrashReason(const std::string& exitInfo, const std::string& logcat,
                                           const std::string& package) {
    try {
        const auto begin = exitInfo.find("ApplicationExitInfo #0:");
        if (begin == std::string::npos) return {};
        const auto end = exitInfo.find("ApplicationExitInfo #1:", begin);
        const auto latest = exitInfo.substr(begin, end == std::string::npos ? end : end - begin);
        if (latest.find("reason=5 ") == std::string::npos) return {};
        std::smatch match;
        if (!std::regex_search(latest, match, std::regex(R"(\bpid=([0-9]+))"))) return {};
        const auto expectedPid = std::stoull(match[1]);
        if (!std::regex_search(latest, match, std::regex(R"(timestamp=([0-9-]{10} [0-9:]{8}))"))) return {};
        const auto timestamp = [](const std::string& text) {
            std::tm value{}; std::istringstream input(text.substr(0, 19));
            input >> std::get_time(&value, "%Y-%m-%d %H:%M:%S");
            if (input.fail()) return std::time_t(-1);
            value.tm_isdst = -1; return std::mktime(&value);
        };
        const auto expectedTime = timestamp(match[1]);
        if (!expectedPid || expectedTime == std::time_t(-1)) return {};
        const auto trim = [](std::string text) {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return std::string{};
            return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
        };
        uint64_t pid = 0; std::time_t when = -1; bool correctPackage = false;
        std::string abort, result;
        const auto retain = [&] {
            if (pid == expectedPid && correctPackage && when != std::time_t(-1) &&
                std::abs(std::difftime(expectedTime, when)) <= 3 && !abort.empty()) result = abort.substr(0, 4096);
        };
        const std::regex crashPid(R"(\bpid:\s*([0-9]+),\s*tid:)");
        std::istringstream stream(logcat); std::string line;
        while (std::getline(stream, line)) {
            if (line.find("*** *** ***") != std::string::npos) {
                retain(); pid = 0; when = -1; correctPackage = false; abort.clear();
            }
            auto at = line.find("Timestamp: ");
            if (at != std::string::npos) when = timestamp(line.substr(at + 11));
            at = line.find("Cmdline: ");
            if (at != std::string::npos) correctPackage = trim(line.substr(at + 9)) == package;
            if (std::regex_search(line, match, crashPid)) pid = std::stoull(match[1]);
            at = line.find("Abort message: ");
            if (at != std::string::npos) {
                abort = trim(line.substr(at + 15));
                if (abort.size() >= 2 && abort.front() == '\'' && abort.back() == '\'') abort = abort.substr(1, abort.size() - 2);
            }
        }
        retain(); return result;
    } catch (...) { return {}; }
}
} // namespace gfx
