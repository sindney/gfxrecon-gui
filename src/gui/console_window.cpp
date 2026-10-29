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
    ImGui::Checkbox("Auto-scroll", &autoScroll_);
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
        std::vector<const LogRecord*> visible;
        for (const LogRecord& r : records) {
            if (!levelVisible_[(int)r.level])
                continue;
            if (filter_[0] && !strstr(r.formatted.c_str(), filter_))
                continue;
            visible.push_back(&r);
        }
        ImGuiListClipper clipper;
        clipper.Begin((int)visible.size(), ImGui::GetTextLineHeightWithSpacing());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const LogRecord& r = *visible[i];
                ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(r.level));
                ImGui::TextUnformatted(r.formatted.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace tpl
