#include "gui/workspace.h"
#include "platform/dialogs.h"
#include "core/artifacts.h"
#include "core/log.h"
#include <fstream>

namespace gfx {
std::filesystem::path Workspace::CaptureSaveDestination(const std::filesystem::path& file) {
    const auto source=std::filesystem::absolute(file).lexically_normal();
    const auto inserted=savedCaptures_.try_emplace(source,std::filesystem::path{});
    if(inserted.second)try {
        std::ifstream in(settingsDir_/"capture-saves"/(Sha256Text(source.u8string())+".json"));
        const auto record=Json::parse(in);
        if(record.value("owner","")=="gfxrecon_gui"&&record.value("complete",false)&&record.at("source")==source.u8string())
            inserted.first->second=std::filesystem::u8path(record.at("destination").get<std::string>());
    }catch(...){}
    return inserted.first->second.empty()?source:inserted.first->second;
}
void Workspace::SaveCapture(bool saveAs) {
    if(capture_&&capture_->source.extension()==".gfxr")RequestCaptureSave(capture_->source,saveAs);
}
void Workspace::RequestCaptureSave(const std::filesystem::path& file,bool saveAs) {
    const auto source=std::filesystem::absolute(file).lexically_normal();
    auto destination=source;
    auto previous=CaptureSaveDestination(source);
    std::error_code pathError;
    if(!std::filesystem::is_regular_file(previous,pathError))previous=source;
    if(saveAs) {
        const auto chosen=tpl::SaveFileDialog("GFXReconstruct capture","gfxr",previous.filename().u8string().c_str(),previous.parent_path().u8string().c_str());
        if(!chosen)return;
        destination=std::filesystem::u8path(*chosen);
    }
    std::string expected;
    if(capture_&&(std::filesystem::absolute(capture_->source).lexically_normal()==source||std::filesystem::absolute(capture_->source).lexically_normal()==previous))expected=capture_->fingerprint;
    else try {
        auto completed=source;completed.replace_extension(".json");std::ifstream in(completed);const auto metadata=Json::parse(in);
        if(metadata.value("complete",false))expected=metadata.value("sha256","");
    }catch(...){}
    const auto ledger=settingsDir_/"capture-saves"/(Sha256Text(source.u8string())+".json");
    pathError.clear();
    const bool replaceExisting=saveAs&&std::filesystem::exists(destination,pathError);
    Start("Saving capture",[this,source,destination,expected,replaceExisting,ledger]{
        WorkspaceResult result;result.savedCapture=SaveCaptureFile(source,destination,expected,replaceExisting,&cancel_);
        const auto& saved=*result.savedCapture;
        try {
            std::filesystem::create_directories(ledger.parent_path());
            AtomicJson(ledger,Json{{"owner","gfxrecon_gui"},{"schema",1},{"complete",true},
                {"source",source.u8string()},{"destination",saved.destination.u8string()},{"sha256",saved.sha256}});
        }catch(const std::exception& e){LOGW("capture")<<"Capture saved; cannot remember its destination: "<<e.what();}
        result.message="Capture saved: "+saved.destination.u8string();return result;
    });
}
void Workspace::DrawCompletedCapture(const std::filesystem::path& file) {
    const auto source=std::filesystem::absolute(file).lexically_normal();
    const auto destination=CaptureSaveDestination(source);
    std::error_code pathError;
    const bool saved=!savedCaptures_.at(source).empty()&&std::filesystem::is_regular_file(destination,pathError);
    const auto display=saved?destination:source;
    ImGui::PushID(source.u8string().c_str());
    if(ImGui::BeginTable("capture-file",2,ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Capture",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions",ImGuiTableColumnFlags_WidthFixed,250);
        ImGui::TableNextRow();ImGui::TableNextColumn();
        ImGui::TextWrapped("%s",display.filename().u8string().c_str());
        ImGui::SetItemTooltip("%s\nDefault copy: %s",display.u8string().c_str(),source.u8string().c_str());
        ImGui::TableNextColumn();ImGui::BeginDisabled(Busy()&&!autoReplayRunning_);
        if(ImGui::Button("Open")){Open(display.u8string());ImGui::CloseCurrentPopup();focusFrames_=true;}
        ImGui::SameLine();ImGui::BeginDisabled(saved);
        if(ImGui::Button(saved?"Saved":"Save"))RequestCaptureSave(source,false);
        ImGui::EndDisabled();ImGui::SetItemTooltip("Keep this capture in its default location:\n%s",source.u8string().c_str());
        ImGui::SameLine();if(ImGui::Button("Save As..."))RequestCaptureSave(source,true);
        ImGui::SetItemTooltip("Choose a folder and filename. The default session copy is retained; shader edits are exported separately.");
        ImGui::EndDisabled();ImGui::EndTable();
    }
    ImGui::PopID();
}
}
