#pragma once
#include <imgui.h>
namespace gfx {
void BeginEditorInputFrame();
void SetEditorComposition(bool active);
bool BlockEditorCommands();
void RequestEditorInput(ImVec2 caret, float lineHeight);
}
