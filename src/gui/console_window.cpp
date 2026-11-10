#include "gui/console_window.h"

#include <imgui.h>

#include <cstring>

namespace tpl {

namespace {

const char* LevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Error: return "Error";
    case LogLevel::Warn:  return "Warn";
    case LogLevel::Info:  return "Info";
    case LogLevel::Debug: return "Debug";
    }
    return "?";
}

ImVec4 LevelColor(LogLevel level) {
    switch (level) {
    case LogLevel::Error: return {0.95f, 0.35f, 0.35f, 1.0f};
    case LogLevel::Warn:  return {0.95f, 0.80f, 0.35f, 1.0f};
    case LogLevel::Info:  return {0.85f, 0.85f, 0.85f, 1.0f};
    case LogLevel::Debug: return {0.50f, 0.70f, 0.95f, 1.0f};
    }
    return {1, 1, 1, 1};
}

} // namespace

void ConsoleWindow::Draw(bool* open) {
    const bool visible = ImGui::Begin("Console", open);
    if (!visible) {
        ImGui::End();
        return;
    }

    // Level filter as a multi-select dropdown (checkable items stay open).
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::BeginCombo("##levels", "Levels")) {
        for (int i = 0; i < 4; ++i)
            ImGui::Selectable(LevelName((LogLevel)i), &levelVisible_[i],
                              ImGuiSelectableFlags_DontClosePopups);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const bool resumeScrolling = ImGui::Checkbox("Auto-scroll", &autoScroll_) && autoScroll_;
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        Log::Instance().ClearHistory();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##filter", "Filter...", filter_, sizeof(filter_));

    ImGui::Separator();

    if (ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        // Pre-filter, then clip the visible subset: the clipper asserts when a
        // step submits no items, which filtering inside the loop would cause.
        // Keep the snapshot alive in `records` for the whole draw.
        std::vector<LogRecord> records = Log::Instance().RecentRecords();
        struct Line { LogLevel level; const char* begin; const char* end; };
        std::vector<Line> visible;
        for (const LogRecord& r : records) {
            if (!levelVisible_[(int)r.level])
                continue;
            if (filter_[0] && !strstr(r.formatted.c_str(), filter_))
                continue;
            // The clipper needs equal-height rows, not records: an ADB error
            // can contain many physical lines. Filter whole records so their
            // continuation lines keep the same visibility and severity.
            const char* begin = r.formatted.data();
            const char* end = begin + r.formatted.size();
            for (;;) {
                const char* newline = static_cast<const char*>(memchr(begin, '\n', end - begin));
                const char* lineEnd = newline ? newline : end;
                if (lineEnd > begin && lineEnd[-1] == '\r') --lineEnd;
                visible.push_back({r.level, begin, lineEnd});
                if (!newline) break;
                begin = newline + 1;
            }
        }
        const bool follow = autoScroll_ && (resumeScrolling || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f);
        ImGuiListClipper clipper;
        clipper.Begin((int)visible.size(), ImGui::GetTextLineHeightWithSpacing());
        // Submit the final row even when a large append put it outside the
        // viewport; SetScrollHereY must target that row rather than a clipped item.
        if (follow && !visible.empty()) clipper.IncludeItemByIndex((int)visible.size() - 1);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& line = visible[i];
                ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(line.level));
                if (line.begin == line.end) ImGui::TextUnformatted(" ");
                else ImGui::TextUnformatted(line.begin, line.end);
                ImGui::PopStyleColor();
            }
        }
        if (follow && !visible.empty())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace tpl
