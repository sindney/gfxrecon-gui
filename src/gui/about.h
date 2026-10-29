#pragma once

#include <string>

namespace tpl {

struct AboutInfo {
    std::string appName;
    std::string version;
    // Last file picked via File->Open, shown as the NFD demo result.
    std::string lastOpenedFile;
};

// Renders the modal when open. Call every frame; open via ImGui::OpenPopup.
void ShowAboutModal(const AboutInfo& info);
void OpenAboutPopup();

} // namespace tpl
