#pragma once

#include <optional>
#include <string>

namespace tpl {

// Each call is self-contained: NFD_Init/NFD_Quit (RAII) and ImGui input-state
// cleanup are handled inside. Returns std::nullopt when cancelled.
std::optional<std::string> OpenFileDialog(const char* filterName,
                                          const char* filterSpec);
std::optional<std::string> SaveFileDialog(const char* name, const char* extensions, const char* defaultName, const char* defaultDirectory = nullptr);
#ifdef IMGUI_TEMPLATE_TESTS
using SaveDialogOverride = std::optional<std::string>(*)(const char*,const char*,const char*,const char*);
void SetSaveDialogOverrideForTests(SaveDialogOverride callback);
#endif

} // namespace tpl
