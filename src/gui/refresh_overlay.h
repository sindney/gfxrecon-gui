#pragma once
#include <imgui.h>
#include <imgui_internal.h>

namespace gfx {
// Scope the disabled state to the inspector body; tabs and event navigation stay usable.
class RefreshOverlay {
    bool active_;
    ImRect rect_;
public:
    explicit RefreshOverlay(bool active) : active_(active), rect_(ImGui::GetCurrentWindow()->InnerRect) { ImGui::BeginDisabled(active_); }
    ~RefreshOverlay() {
        ImGui::EndDisabled();
        if (!active_) return;
        auto* parent=ImGui::GetCurrentWindow();auto& dc=parent->DC;
        const auto cursor=dc.CursorPos,previous=dc.CursorPosPrevLine,max=dc.CursorMaxPos;
        const auto currentLine=dc.CurrLineSize,previousLine=dc.PrevLineSize;
        const auto currentBase=dc.CurrLineTextBaseOffset,previousBase=dc.PrevLineTextBaseOffset;
        const bool sameLine=dc.IsSameLine,setPosition=dc.IsSetPos;
        ImGui::SetCursorScreenPos(rect_.Min);
        ImGui::BeginChild("##refresh-overlay",rect_.GetSize(),ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNavFocus);
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(rect_.Min,rect_.Max,IM_COL32(8,11,15,155));
        const char* text="Refreshing...";const auto size=ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(rect_.GetCenter().x-size.x*.5f,rect_.GetCenter().y-size.y*.5f),IM_COL32(218,224,232,255),text);
        ImGui::InvisibleButton("##block-input",ImGui::GetContentRegionAvail());
        ImGui::EndChild();
        // This child floats above the body. Restore layout directly: the cursor
        // following a final item includes spacing beyond CursorMaxPos, so calling
        // SetCursorScreenPos(cursor) here incorrectly advertises a new item extent.
        dc.CursorPos=cursor;dc.CursorPosPrevLine=previous;dc.CursorMaxPos=max;
        dc.CurrLineSize=currentLine;dc.PrevLineSize=previousLine;
        dc.CurrLineTextBaseOffset=currentBase;dc.PrevLineTextBaseOffset=previousBase;
        dc.IsSameLine=sameLine;dc.IsSetPos=setPosition;
    }
};
}
