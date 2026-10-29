#include "gui/command_palette.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace tpl {
namespace {

bool ContainsIgnoreCase(const std::string& text, const char* filter) {
    const std::string_view query(filter);
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        }) != text.end() || query.empty();
}

} // namespace

std::optional<int> CommandPalette::Draw(
    bool& open, const std::vector<CommandPaletteEntry>& entries) {
    if (!open)
        return std::nullopt;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float unit = ImGui::GetFontSize();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowSize(ImVec2(std::min(36.0f * unit, viewport->WorkSize.x),
                                   std::min(24.0f * unit, viewport->WorkSize.y * 0.8f)),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                  viewport->WorkPos.y + viewport->WorkSize.y * 0.12f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    if (!ImGui::Begin("Command Palette", &open,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return std::nullopt;
    }

    if (ImGui::IsWindowAppearing()) {
        filter_[0] = '\0';
        selected_ = 0;
        scrollToSelection_ = true;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##filter", "Type to filter commands...",
                                 filter_, sizeof(filter_))) {
        selected_ = 0;
        scrollToSelection_ = true;
    }

    std::vector<const CommandPaletteEntry*> filtered;
    for (const auto& entry : entries)
        if (ContainsIgnoreCase(entry.label, filter_))
            filtered.push_back(&entry);

    const int count = static_cast<int>(filtered.size());
    selected_ = std::clamp(selected_, 0, std::max(0, count - 1));
    std::optional<int> command;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            open = false;
        } else if (count > 0) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                selected_ = (selected_ + 1) % count;
                scrollToSelection_ = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                selected_ = (selected_ + count - 1) % count;
                scrollToSelection_ = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
                command = filtered[selected_]->id;
        }
    }

    ImGui::TextDisabled("Up/Down: select   Enter: run   Esc: close");
    ImGui::Separator();
    if (ImGui::BeginChild("commands", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        if (filtered.empty())
            ImGui::TextDisabled("No matching commands");
        ImGuiListClipper clipper;
        clipper.Begin(count);
        if (scrollToSelection_ && count > 0)
            clipper.IncludeItemByIndex(selected_);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                ImGui::PushID(filtered[i]->id);
                if (ImGui::Selectable(filtered[i]->label.c_str(), i == selected_))
                    command = filtered[i]->id;
                ImGui::PopID();
                if (i == selected_ && scrollToSelection_) {
                    ImGui::SetScrollHereY(0.5f);
                    scrollToSelection_ = false;
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
    if (command)
        open = false;
    return command;
}

} // namespace tpl
