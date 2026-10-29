#include "gui/about.h"

#include <imgui.h>

namespace tpl {

static const char* kAboutPopupId = "About";

void OpenAboutPopup() { ImGui::OpenPopup(kAboutPopupId); }

void ShowAboutModal(const AboutInfo& info) {
    if (ImGui::BeginPopupModal(kAboutPopupId, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", info.appName.c_str());
        ImGui::Text("Version %s", info.version.c_str());
        ImGui::Separator();
        ImGui::Text("MIT License");
        ImGui::Spacing();
        ImGui::Text("Built with:");
        ImGui::BulletText("Dear ImGui (docking) - MIT");
        ImGui::BulletText("Dear ImGui Test Engine - MIT");
        ImGui::BulletText("SDL3 - Zlib");
        ImGui::BulletText("SFML - Zlib/png");
        ImGui::BulletText("ImGui-SFML - MIT");
        ImGui::BulletText("nativefiledialog-extended - Zlib");
        ImGui::BulletText("UI themes adapted from TheAncientOwl (imgui #707)");
        if (!info.lastOpenedFile.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("Last opened: %s", info.lastOpenedFile.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace tpl
