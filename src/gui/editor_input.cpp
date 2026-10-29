#include "gui/editor_input.h"
#include <imgui_internal.h>
#include <algorithm>
namespace gfx {
namespace { bool composing = false, compositionEvent = false; }
void BeginEditorInputFrame() { compositionEvent = false; }
void SetEditorComposition(bool active) { compositionEvent |= composing || active; composing = active; }
bool BlockEditorCommands() { return composing || compositionEvent; }
void RequestEditorInput(ImVec2 caret, float height) {
    auto& data = GImGui->PlatformImeData;
    const auto* window = ImGui::GetCurrentWindow();
    data.WantVisible = true; data.WantTextInput = true;
    data.InputPos = ImVec2(std::clamp(caret.x, window->InnerRect.Min.x, window->InnerRect.Max.x),
                          std::clamp(caret.y, window->InnerRect.Min.y, std::max(window->InnerRect.Min.y, window->InnerRect.Max.y - height)));
    data.InputLineHeight = height;
    data.ViewportId = window->Viewport->ID;
}
}
