#include "gui/editor_input.h"
#include <imgui_internal.h>
#include <algorithm>
namespace gfx {
namespace { bool composing = false, compositionEvent = false; }
void BeginEditorInputFrame() { compositionEvent = false; }
void SetEditorComposition(bool active) { compositionEvent |= composing || active; composing = active; }
bool BlockEditorCommands() { return composing || compositionEvent; }
void RequestEditorInput(ImVec2 caret, float height) {
    // This editor is a custom canvas, so ImGui has no InputText active ID to
    // suppress navigation/scrolling of its parent dock.
    const auto id = ImGui::GetID("##editor-keyboard");
    // A toolbar button owns the ID between mouse-down and mouse-up. Keeping
    // editor focus must not steal that click (or another text field's input).
    if (GImGui->ActiveId && GImGui->ActiveId != id) return;
    ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
    ImGui::KeepAliveID(id);
    GImGui->ActiveIdAllowOverlap = true;
    ImGui::SetActiveIdUsingAllKeyboardKeys();
    auto& data = GImGui->PlatformImeData;
    const auto* window = ImGui::GetCurrentWindow();
    data.WantVisible = true; data.WantTextInput = true;
    data.InputPos = ImVec2(std::clamp(caret.x, window->InnerRect.Min.x, window->InnerRect.Max.x),
                          std::clamp(caret.y, window->InnerRect.Min.y, std::max(window->InnerRect.Min.y, window->InnerRect.Max.y - height)));
    data.InputLineHeight = height;
    data.ViewportId = window->Viewport->ID;
}
}
