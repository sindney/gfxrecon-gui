#include "gui/workspace.h"
#include "shaders/diagnostics.h"

#include "core/log.h"
#include "core/settings.h"
#include "core/artifacts.h"
#include "analysis/cache.h"
#include "analysis/import.h"
#include "core/tools.h"
#include "shaders/decode.h"
#include "platform/device_lock.h"
#include "platform/process.h"
#include "shaders/syntax.h"
#include "shaders/interface.h"
#include "shaders/metadata.h"
#include "gui/fonts.h"
#include "gui/refresh_overlay.h"
#include "platform/dialogs.h"
#include <misc/cpp/imgui_stdlib.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>

namespace gfx {
namespace {
std::string ReadText(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + file.u8string());
    return {std::istreambuf_iterator<char>(input), {}};
}
void WriteText(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    if (!(out << text)) throw std::runtime_error("Cannot write " + file.u8string());
}
std::filesystem::path NewSession(const std::filesystem::path& root) {
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    auto path = root / std::to_string(stamp);
    if (!std::filesystem::create_directories(path)) throw std::runtime_error("Could not create a unique session directory");
    return path;
}
std::string ShaderKey(const ShaderStage& stage){return std::to_string(stage.module)+"|"+stage.stage+"|"+stage.entry+"|"+stage.specialization.dump();}
WorkspaceResult LoadShaderDocument(const std::filesystem::path& path, const ShaderStage& stage,
                             const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel) {
    const auto decoded = gfx::DecodeShader(path, stage, tools, cancel);
    WorkspaceResult r;
    r.shader = true;
    r.shaderInfo = decoded.stage;
    r.shaderPath = path.u8string();
    r.capturedSource = decoded.capturedSource;
    r.shaderMetadata = decoded.metadata.dump(2);
    r.reflection = decoded.reflection;
    r.text = decoded.glsl;
    r.assembly = decoded.assembly;
    r.message = "Shader loaded. GLSL is reconstructed. " + std::string(r.capturedSource.empty() ?
        "No embedded source was recorded; debug metadata remains inspectable." : "Embedded source is available under Captured source.");
    return r;
}
} // namespace
WorkspaceResult Workspace::ImportCapture(const std::filesystem::path& file, const std::filesystem::path& session,
                               const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel) {
    const auto imported = gfx::ImportCapture(file, session.parent_path() / "cache", tools, cancel);
    WorkspaceResult result;
    result.session = imported.directory; result.capture = imported.capture; result.message = imported.message;
    return result;
}
void Workspace::Init(const std::filesystem::path& dir) {
    settingsDir_ = dir;
    sessionsDir_ = dir / "sessions";
    tools_ = ResolveTools(dir, std::filesystem::u8path(tpl::ExeDirectory()));
    ConfigureDeviceLocks(dir / "locks");
    RefreshToolAvailability();
    const auto preferences = dir / "gfxrecon_preferences.json";
    if (std::filesystem::is_regular_file(preferences)) {
        try {
            const auto saved = Json::parse(ReadText(preferences));
            const int mode = saved.value("replay_mode", 0);
            validationReplay_=saved.value("validation_replay",false);phoneScreenOff_=saved.value("phone_screen_off",false);
            optimizeReplay_ = saved.value("optimize_replay", true);
            if (mode >= 0 && mode <= 2) replayMode_ = static_cast<ReplayMode>(mode);
            androidSerial_ = saved.value("android_serial", std::string{});
            captureMode_ = saved.value("capture_mode", 0) == 1 ? 1 : 0;
            capturePackage_ = saved.value("capture_package", std::string{});
            captureArguments_ = saved.value("capture_arguments", std::string{});
            captureFrames_ = saved.value("capture_frames", std::string("1-3"));
            captureStyle_ = saved.value("capture_style", 0); captureLength_ = saved.value("capture_length", 3);
            captureExecutable_ = saved.value("capture_executable", std::string{});
            captureWorkingDirectory_ = saved.value("capture_working_directory", std::string{});
            windowsArguments_ = saved.value("windows_arguments", std::string{});
        } catch (const std::exception& e) { LOGW("settings") << e.what(); }
    }
    RefreshRecoveries();
    editor_.SetLanguageDefinition(GlslLanguage());
    editor_.SetShowWhitespaces(false);
    editor_.SetReadOnly(true);
#ifdef _WIN32
    const auto windows = std::getenv("WINDIR");
    const auto font = std::filesystem::u8path(windows ? windows : "C:/Windows") / "Fonts/consola.ttf";
    if (std::filesystem::is_regular_file(font)) {
        editorFont_ = ImGui::GetIO().Fonts->AddFontFromFileTTF(font.u8string().c_str(), 15.0f * tpl::SystemDpiScale());
        const auto cjk = font.parent_path() / "msyh.ttc";
        if (std::filesystem::is_regular_file(cjk)) {
            ImFontConfig config; config.MergeMode = true;
            ImGui::GetIO().Fonts->AddFontFromFileTTF(cjk.u8string().c_str(), 15.0f * tpl::SystemDpiScale(), &config, ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());
        }
    }
#endif
}
void Workspace::SaveConfiguration() const {
    WriteText(settingsDir_ / "gfxrecon_tools.json", Json(tools_).dump(2));
    WriteText(settingsDir_ / "gfxrecon_preferences.json", Json{{"replay_mode", static_cast<int>(replayMode_)}, {"android_serial", androidSerial_}, {"optimize_replay",optimizeReplay_},{"validation_replay",validationReplay_},{"phone_screen_off",phoneScreenOff_},
        {"capture_mode", captureMode_}, {"capture_package", capturePackage_}, {"capture_arguments", captureArguments_},
        {"capture_frames", captureFrames_}, {"capture_style", captureStyle_}, {"capture_length", captureLength_}, {"capture_executable", captureExecutable_},
        {"capture_working_directory", captureWorkingDirectory_}, {"windows_arguments", windowsArguments_}}.dump(2));
}
void Workspace::SetReplayMode(ReplayMode mode) {
    if (replayMode_ != mode) { replayMode_ = mode;InvalidateDrawResources(); ++selectionGeneration_; }
}
void Workspace::Shutdown() {
    cancel_ = true;
    if (job_.valid()) { try { auto pending = job_.get(); pending.liveSession.reset(); } catch (...) {} }
    liveCapture_.reset();
    image_.Release();
    mesh_.Release();
    modifiedImage_.Release();
    differenceImage_.Release();
    ResetImageDocuments();
    for (auto& [frame, preview] : frameImages_) preview.image.Release();
    frameImages_.clear();
    try { SaveConfiguration(); } catch (const std::exception& e) { LOGE("settings") << e.what(); }
}
bool Workspace::Busy() const { return job_.valid(); }
void Workspace::Start(const std::string& name, std::function<WorkspaceResult()> work, JobKind kind, uint32_t frame) {
    if (Busy() && autoReplayRunning_) {
        deferredJob_ = DeferredJob{name, std::move(work), kind, frame};
        cancel_ = true; status_ = name + " queued; cancelling the previous preview."; return;
    }
    if (Busy()) { status_ = "Wait for or cancel the current job before starting another operation."; return; }
    cancel_ = false;
    jobExecution_=-1;
    jobFailed_ = false;
    jobName_ = name;
    status_ = name;
    jobPanels_ = {"Console"};
    jobFrame_ = frame;
    auto panels = [&](std::initializer_list<const char*> names) { for (const auto* panel : names) jobPanels_.emplace_back(panel); };
    switch (kind) {
    case JobKind::Import: panels({"Events", "Pipeline", "Frames"}); break;
    case JobKind::Shader: panels({"Shader", "Resources"}); break;
    case JobKind::Compile: panels({"Shader"}); break;
    case JobKind::Replay: panels({"Frames", "Image"}); break;
    case JobKind::Dump: panels({"Frames", "Image", "Resources", "Pipeline", "Shader", "Mesh"}); break;
    case JobKind::Resource: panels({"Resources", "Image"}); break;
    case JobKind::Capture: panels({"Frames", "Events", "Pipeline"}); break;
    case JobKind::Tools: break;
    }
    CompleteOperation(PanelState::Running, name);
    LOGI("jobs") << "Started: " << name; loggedStatus_ = status_;
    job_ = std::async(std::launch::async, std::move(work));
}
void Workspace::Poll() {
    if (!Busy() || job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const bool automatic = autoReplayRunning_; autoReplayRunning_ = false;
    try {
        auto result = job_.get();
        const bool validationErrors=result.validationReport.is_object()&&result.validationReport.value("errors",0)>0;
        if (cancel_ && !result.savedCapture) {
            status_ = "Operation cancelled.";
            CompleteOperation(PanelState::Inactive, status_); LOGI("jobs") << status_; loggedStatus_ = status_;
            if (captureInProgress_) { captureInProgress_ = false; captureStep_ = 2; captureError_ = status_; }
            RefreshRecoveries(); return;
        }
        if (result.selectionGeneration && result.selectionGeneration != selectionGeneration_) {
            status_ = "Job finished for a previous selection; its artifacts were retained.";
            CompleteOperation(PanelState::Inactive, status_); LOGI("jobs") << status_; loggedStatus_ = status_;
            return;
        }
        if (result.thumbnailOnly) {
            ImageViewer image; image.LoadBmp(result.replayImage);
            auto& preview = frameImages_[result.replayFrame]; preview.image.MakeThumbnail(image); image.Release();
            preview.target = result.replayTarget; preview.modified = !result.overrides.empty();
            CompleteOperation(PanelState::Success, result.message); LOGI("jobs") << result.message;
            return;
        }
        if(result.inspection){
            for(const auto& document:result.stageDocuments)shaderDocuments_[ShaderKey(document->shaderInfo)]=document;
            if(!HasUnsavedEdits()&&!result.stageDocuments.empty()){
                auto selected=result.stageDocuments.front();
                for(const auto& document:result.stageDocuments)if(document->shaderInfo.stage==autoShaderStage_)selected=document;
                ShowShaderDocument(*selected,false);
            }
        }
        if(result.unlockRequired){
            blockedReplay_=result;openReplayUnlock_=true;previewPaused_=true;
            if(validationReport_.is_object()&&validationReport_.value("status",std::string{})=="running")validationReport_={{"status","unavailable"},{"enabled",false},{"message",result.message}};
        }
        if(result.failed){if(!result.validationReport.is_null()){validationReport_=std::move(result.validationReport);resourceTab_=2;resourceTabRequest_=true;focusResources_=true;}status_=result.message;jobFailed_=true;CompleteOperation(PanelState::Failed,status_);LOGE("replay")<<status_;return;}
        if (result.capture) {
            mesh_.Release(); meshGeneration_ = meshRequestGeneration_ = 0; meshInstance_ = 0;
            focusFrames_ = !captureInProgress_;
            focusImage_=result.capture->source.extension()==".gfxr";
            capture_ = std::move(result.capture);
            session_ = result.session;
            selectedEvent_ = 0; frame_ = 1; inspectedPipeline_ = 0;drawVisualization_=0;
            displayedDraw_ = -1; pendingDraw_ = false;
            imageView_=0;InvalidateDrawResources();validationReport_=Json();
            shaderDocuments_.clear();executionStatuses_.clear();
            pendingFrame_ = result.deferReplay || capture_->source.extension() != ".gfxr" ? 0 : 1; thumbnailQueue_.clear(); failedFrames_.clear(); previewPaused_ = result.deferReplay;
            frameStatuses_.clear();
            for (const auto* panel : {"Shader", "Image", "Resources"}) panelStatuses_[panel] = {};
            filterDirty_ = true;
            shader_ = {}; glsl_.clear(); assembly_.clear(); editor_.SetText("");
            activeOverrides_.clear(); candidate_.clear(); hasComparison_ = false;
            image_.Release(); modifiedImage_.Release(); differenceImage_.Release();
            for (auto& [frame, preview] : frameImages_) preview.image.Release();
            frameImages_.clear();
            reflection_.clear(); lastReplayTarget_.clear();
            capturedSource_.clear(); shaderMetadata_.clear(); exportedGlsl_.clear(); exportedAssembly_.clear();
            resources_.clear(); resourceMode_ = 0; dumpBlock_ = 0;
            if (!capture_->executions.empty()) SelectExecution(0);
            else SelectEvent(0);
            // Import schedules one frame preview. Geometry readback starts only
            // after an explicit draw selection or Mesh/Reload request.
            meshRequestGeneration_ = selectionGeneration_;
        }
        if (!result.versions.empty()) versions_ = std::move(result.versions);
        if (!result.toolReady.empty()) toolReady_ = std::move(result.toolReady);
        if (result.devicesUpdated) devices_ = std::move(result.devices);
        if (result.deviceInfo) deviceInfo_ = std::move(result.deviceInfo);
        if (result.captureSetupReady) captureSetupReady_ = true;
        if (!result.completedCaptures.empty()) completedCaptures_ = std::move(result.completedCaptures);
        if (result.savedCapture) {
            const auto& saved=*result.savedCapture;
            const auto previous=CaptureSaveDestination(saved.source);
            savedCaptures_[saved.source]=saved.destination;
            if(capture_) {
                const auto current=std::filesystem::absolute(capture_->source).lexically_normal();
                if(current==saved.source||current==previous)capture_->source=saved.destination;
            }
            captureError_.clear();
        }
        if (result.liveSession) { liveCapture_ = std::move(result.liveSession); captureStep_ = 3; }
        if (!result.replayImage.empty() && !result.drawPreview && !result.meshPreview) image_.LoadBmp(result.replayImage);
        if(!result.indirectParameters.is_null())indirectParameters_=std::move(result.indirectParameters);
        if (result.replayResult) {
            if (!automatic && !result.meshPreview && !result.drawPreview) focusImage_ = true;
            if (result.drawPreview) { displayedDraw_ = result.dumpExecution; }
            else if (!result.meshPreview) displayedDraw_ = -1;
            lastReplayTarget_ = std::move(result.replayTarget);
            if (result.overrides != activeOverrides_) {
                mesh_.Release(); meshGeneration_ = meshRequestGeneration_ = 0;
                for (auto& [frame, preview] : frameImages_) preview.image.Release();
                frameImages_.clear(); thumbnailQueue_.clear(); failedFrames_.clear();
                frameStatuses_.clear();
                InvalidateDrawResources();
            }
            activeOverrides_ = std::move(result.overrides);
            hasComparison_ = !result.modifiedImage.empty();
            if(result.dumpExecution>=0){
                resources_=std::move(result.resources);dumpBlock_=result.dumpBlock;dumpExecution_=result.dumpExecution;imageView_=1;
                diagnosticResources_=std::move(result.diagnosticResources);diagnosticErrors_=std::move(result.diagnosticErrors);uvDetail_=std::move(result.uvDetail);
                diagnosticExecution_=result.dumpExecution;diagnosticSubdraw_=selectedSubdraw_;
                imageShownGeneration_=selectionGeneration_;
                if(!diagnosticResources_.empty())resources_=diagnosticResources_.at(diagnosticResources_.count(drawVisualization_)?drawVisualization_:0);
                drawHasInputs_=!result.drawPreview||result.descriptorImages;
                ResetImageDocuments();resourceMode_=0;
            }
        if(!result.validationReport.is_null())validationReport_=std::move(result.validationReport);
            resourceMetadata_ = Json(); resourcePage_ = {};
            resourceTab_ = result.inspection?3:resources_.empty()?0:1; resourceTabRequest_ = true;
            focusResources_ = !resources_.empty() && !result.meshPreview && !result.drawPreview;
            if (hasComparison_) {
                modifiedImage_.LoadBmp(result.modifiedImage);
                differenceImage_.AbsoluteDifference(image_, modifiedImage_);
            }
            if (result.replayFrame && !result.drawPreview && !result.meshPreview) {
                auto& preview = frameImages_[result.replayFrame];
                preview.image.MakeThumbnail(hasComparison_ ? modifiedImage_ : image_);
                preview.target = lastReplayTarget_; preview.modified = !activeOverrides_.empty();
                if (frameImages_.size() > 128) {
                    auto oldest = frameImages_.begin(); if (oldest->first == result.replayFrame) ++oldest;
                    oldest->second.image.Release(); frameImages_.erase(oldest);
                }
            }
        }
        if (!result.candidate.empty()) { candidate_ = result.candidate; candidateText_ = result.candidateText; }
        if(result.shaderReset){
            InvalidateDrawResources();mesh_.Release();meshGeneration_=meshRequestGeneration_=0;
            modifiedImage_.Release();differenceImage_.Release();hasComparison_=false;
            imageView_=0;imageTab_=-1;imageTabRequest_=true;resourceMode_=0;
            if(shader_.module==result.resetShaderModule)ResetShaderText();
        }
        if (!result.resourceImage.empty()) {
            try { if (result.resourceImage.extension() == ".png") resourceImage_.LoadPng(result.resourceImage); else resourceImage_.LoadBmp(result.resourceImage); resourceMode_ = 1; focusImage_ = true; }
            catch (const std::exception& error) { resourceImage_.Release(); status_ = error.what(); LOGE("resources") << status_; }
        }
        if (result.mesh) {
            try {
                mesh_.SetData(std::move(result.mesh)); meshGeneration_ = selectionGeneration_;
                meshRequestGeneration_=selectionGeneration_;
                LOGI("mesh") << "Geometry loaded: " << mesh_.InputVertexCount() << " input vertices, " << mesh_.ScreenVertexCount() << " post-VS positions. " << mesh_.Diagnostic();
            }
            catch (const std::exception& error) { meshDiagnostic_ = error.what(); LOGW("mesh") << meshDiagnostic_; }
        }
        if (!result.meshError.empty()) { meshDiagnostic_ = result.meshError; meshGeneration_ = 0;meshRequestGeneration_=selectionGeneration_; }
        if (result.resourcePage) { resourcePage_ = std::move(*result.resourcePage); resourceMode_ = 0; resourceTab_=4;resourceTabRequest_=true;focusResources_=true; }
        if(result.shader){shaderDocuments_[ShaderKey(result.shaderInfo)]=std::make_shared<WorkspaceResult>(result);ShowShaderDocument(result,true);}
        else if(!result.text.empty())deviceOutput_=result.text;
        status_ = result.message;
        if(validationErrors){resourceTab_=2;resourceTabRequest_=true;focusResources_=true;}
        CompleteOperation(validationErrors?PanelState::Failed:PanelState::Success, status_);
        if (captureInProgress_) { captureInProgress_ = false; captureStep_ = 4; }
        LOGI("jobs") << status_;
        loggedStatus_ = status_;
    } catch (const AndroidUnlockRequired& e) {
        WorkspaceResult blocked;blocked.unlockRequired=e.state;blocked.selectionGeneration=selectionGeneration_;
        blockedReplay_=std::move(blocked);openReplayUnlock_=true;previewPaused_=true;jobFailed_=true;status_=e.what();
        if(validationReport_.is_object()&&validationReport_.value("status",std::string{})=="running")validationReport_={{"status","unavailable"},{"enabled",false},{"message",status_}};
        CompleteOperation(PanelState::Failed,status_);LOGE("replay")<<status_;loggedStatus_=status_;
    } catch (const DeviceBusy& e) {
        // Another GUI/CLI process holds this phone; nothing on the device was touched.
        jobFailed_ = true; status_ = e.what(); captureError_ = status_;
        CompleteOperation(PanelState::Failed, status_);
        if (captureInProgress_) { captureInProgress_ = false; captureStep_ = 2; }
        LOGW("jobs") << status_; loggedStatus_ = status_;
    } catch (const std::exception& e) {
        jobFailed_ = !cancel_;
        status_ = e.what(); captureError_ = status_;
        if(validationReport_.is_object()&&validationReport_.value("status",std::string{})=="running")validationReport_={{"status","failed"},{"message",status_}};
        CompleteOperation(cancel_ ? PanelState::Inactive : PanelState::Failed, status_);
        if (automatic && !cancel_) failedFrames_[currentReplayFrame_] = status_;
        if (captureInProgress_) { captureInProgress_ = false; captureStep_ = 2; }
        if (cancel_) { LOGI("jobs") << status_; }
        else { LOGE("jobs") << status_; }
        loggedStatus_ = status_;
    }
    RefreshRecoveries();
}
bool Workspace::HasUnsavedEdits() const {
    return (glsl_ != originalGlsl_ && glsl_ != exportedGlsl_) || (assembly_ != originalAssembly_ && assembly_ != exportedAssembly_);
}
void Workspace::Open(const std::string& path) {
    if (HasUnsavedEdits()) { status_ = "Export or revert shader edits before opening another document."; return; }
    if (Busy() && autoReplayRunning_) {
        pendingOpen_ = path; deferredJob_.reset(); cancel_ = true; pendingFrame_ = 0; thumbnailQueue_.clear(); return;
    }
    auto file = std::filesystem::u8path(path);
    auto tools = tools_;
    const bool deferReplay = CaptureSessionActive();
    Start("Opening " + file.filename().u8string(), [this, file, tools, deferReplay] {
        WorkspaceResult r;
        r.session = NewSession(sessionsDir_);
        const auto ext = file.extension().u8string();
        if (ext == ".gfxr" || ext == ".jsonl") {
            r = ImportCapture(file, r.session, tools, &cancel_);
        } else if (ext == ".spv") {
            ShaderStage stage{0, "VK_SHADER_STAGE_FRAGMENT_BIT", "main"};
            r = LoadShaderDocument(file, stage, tools, &cancel_);
        } else if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".spvasm") {
            r.shader = true;
            r.shaderInfo = {0, ext == ".vert" ? "VK_SHADER_STAGE_VERTEX_BIT" :
                ext == ".comp" ? "VK_SHADER_STAGE_COMPUTE_BIT" : "VK_SHADER_STAGE_FRAGMENT_BIT", "main"};
            r.shaderPath = file.u8string();
            if (ext == ".spvasm") r.assembly = ReadText(file); else r.text = ReadText(file);
            r.message = "Opened shader document. Use Edit to enable changes.";
        } else throw std::runtime_error("Open a .gfxr, .jsonl, .spv, GLSL stage file, or .spvasm document");
        r.deferReplay = deferReplay;
        return r;
    }, file.extension() == ".gfxr" || file.extension() == ".jsonl" ? JobKind::Import : JobKind::Shader);
}
void Workspace::SelectEvent(size_t index, bool preserveImages) {
    if (!capture_ || index >= capture_->events.size()) return;
    selectedEvent_ = index;selectedSubdraw_=-1;indirectParameters_=Json();
    objectDetails_=Json();objectLabel_.clear();
    if(!preserveImages){resourceMode_=0;ResetImageDocuments();}
    pendingDraw_ = false;
    if (Busy() && autoReplayRunning_) cancel_ = true;
    selectedExecution_ = -1;
    inspectedPipeline_ = 0;
    ++selectionGeneration_;
    if (!HasUnsavedEdits()) {
        panelStatuses_["Shader"] = {};
        shader_ = {}; glsl_.clear(); assembly_.clear(); originalGlsl_.clear(); originalAssembly_.clear();
        reflection_.clear(); capturedSource_.clear(); shaderMetadata_.clear(); editor_.SetText(""); candidate_.clear();
        dirty_ = false;
    }
    try { arguments_ = capture_->ReadEvent(index).dump(2); }
    catch (const std::exception& e) { arguments_ = e.what(); }
}
void Workspace::SelectExecution(size_t index, bool preview) {
    if (!capture_ || index >= capture_->executions.size()) return;
    if(preview&&selectedExecution_>=0&&ExecutionState(selectedExecution_)==PanelState::Running)executionStatuses_[selectedExecution_]={PanelState::Inactive,"Superseded by a new selection"};
    if(!shader_.stage.empty())autoShaderStage_=shader_.stage;
    SelectEvent(capture_->executions[index].event,preview);
    selectedExecution_ = static_cast<int>(index);
    revealExecution_ = true;
    revealPipeline_=true;
    if (preview && !CaptureSessionActive() && capture_->source.extension() == ".gfxr") {
        imageView_=1;
        pendingDraw_ = true; drawRequestedAt_ = std::chrono::steady_clock::now();
        executionStatuses_[index]={PanelState::Running,"Queued draw inspection"};
        status_ = "Selected block " + std::to_string(capture_->events[selectedEvent_].block) + "; inspecting draw outputs.";
    }
}
void Workspace::SetDrawVisualization(int mode,int subdraw){
    if(!capture_||selectedExecution_<0||mode<0||mode>5||subdraw< -1)return;
    if(drawVisualization_==mode&&selectedSubdraw_==subdraw)return;
    drawVisualization_=mode;
    if(selectedSubdraw_==subdraw){
        if(imageShownGeneration_==selectionGeneration_&&diagnosticExecution_==selectedExecution_&&diagnosticSubdraw_==subdraw&&!diagnosticResources_.empty()){ShowDrawVisualization();return;}
        // A pending bundle contains every mode; mode changes do not supersede it.
        if(pendingDraw_||(Busy()&&autoReplayRunning_))return;
    }
    selectedSubdraw_=subdraw;++selectionGeneration_;imageView_=1;
    if(Busy()&&autoReplayRunning_)cancel_=true;
    if(capture_->source.extension()==".gfxr"){pendingDraw_=true;previewPaused_=false;drawRequestedAt_=std::chrono::steady_clock::now();executionStatuses_[selectedExecution_]={PanelState::Running,"Queued draw visualization"};}
}
void Workspace::ShowDrawVisualization(){
    const auto found=diagnosticResources_.find(drawVisualization_);
    resources_=found!=diagnosticResources_.end()?found->second:diagnosticResources_.at(0);
    ResetImageDocuments();resourceMode_=0;imageView_=1;
}
void Workspace::ProbeTools() {
    const auto tools = tools_;
    Start("Checking configured tools", [this, tools] {
        WorkspaceResult r;
        r.versions = ProbeToolVersions(tools, r.toolReady, &cancel_);
        r.message = "Tool checks complete. Offline navigation does not require replay.";
        return r;
    });
}
void Workspace::RefreshToolAvailability() {
    toolReady_ = ToolAvailability(tools_);
}
bool Workspace::ToolAvailable(const std::string& name) const {
    const auto found = toolReady_.find(name); return found != toolReady_.end() && found->second;
}
void Workspace::ShowShaderDocument(const WorkspaceResult& document,bool focus){
            shaderSelectionGeneration_ = selectionGeneration_;
            shader_ = document.shaderInfo;
            shaderPath_ = document.shaderPath;
            candidate_.clear();
            glsl_ = originalGlsl_ = document.text;
            assembly_ = originalAssembly_ = document.assembly;
            reflection_ = document.reflection;
            capturedSource_ = document.capturedSource; shaderMetadata_ = document.shaderMetadata;
            shaderEditLimitation_ = document.shaderEditLimitation;
            exportedGlsl_ = glsl_; exportedAssembly_ = assembly_;
            readOnly_ = true; dirty_ = false;
            representation_ = glsl_.empty() ? 1 : 0;
            editor_.SetLanguageDefinition(representation_ ? SpirvLanguage() : GlslLanguage());
            editor_.SetReadOnly(true);
            editor_.SetText(representation_ ? assembly_ : glsl_);
            resetShaderScroll_ = true;
            if(focus){focusShader_=true;resourceTab_=0;resourceTabRequest_=true;}
}
void Workspace::LoadShader(const ShaderStage& stage) {
    if (HasUnsavedEdits()) { status_ = "Export or revert current edits before selecting another shader."; return; }
    autoShaderStage_=stage.stage;
    const auto cached=shaderDocuments_.find(ShaderKey(stage));
    if(cached!=shaderDocuments_.end()){if(Busy()&&autoReplayRunning_&&jobExecution_<0)cancel_=true;ShowShaderDocument(*cached->second,true);return;}
    if(Busy()&&autoReplayRunning_&&jobExecution_==selectedExecution_){focusShader_=true;return;}
    const auto path = session_ / "shaders" / ("sh" + std::to_string(stage.module));
    if (!std::filesystem::is_regular_file(path)) {
        status_ = "No extracted binary for this module. Inline/library paths are not enabled.";
        panelStatuses_["Shader"] = {PanelState::Failed, status_}; jobFailed_ = true; return;
    }
    if (Busy() && !autoReplayRunning_) return;
    const auto tools = tools_;
    const auto generation = selectionGeneration_;
    const auto limitation = capture_ ? capture_->ModuleEditLimitation(stage.module) : "";
    Start("Reconstructing shader", [this, path, stage, tools, generation, limitation] {
        auto r = LoadShaderDocument(path, stage, tools, &cancel_); r.selectionGeneration = generation; r.shaderEditLimitation = limitation; return r;
    }, JobKind::Shader);
}
void Workspace::CompileShader() {
    if (representation_ >= 2) { status_ = "Choose editable GLSL or SPIR-V assembly before compiling."; return; }
    if (capture_ && shader_.module) {
        const auto reason = capture_->ModuleEditLimitation(shader_.module);
        if (!reason.empty()) { status_ = reason; return; }
    }
    const auto text = editor_.GetText();
    const auto tools = tools_;
    const auto shader = shader_;
    const bool assembly = representation_ == 1;
    const auto original = std::filesystem::u8path(shaderPath_);
    const auto generation = selectionGeneration_;
    Start("Compiling and validating candidate shader", [this, text, tools, shader, assembly, original, generation] {
        WorkspaceResult r;
        r.selectionGeneration = generation;
        auto revision = NewSession(sessionsDir_) / "candidate";
        std::filesystem::create_directories(revision);
        const auto source = revision / (assembly ? "shader.spvasm" : "shader.glsl");
        const auto binary = revision / ("sh" + std::to_string(shader.module));
        WriteText(source, text);
        gfx::CompileShader(source, shader, binary, tools, &cancel_);
        if (shader.module) CheckShaderInterface(original, binary, shader.entry);
        r.candidate = binary; r.candidateText = text;
        r.message = "Candidate compiled and validated: " + binary.u8string();
        return r;
    }, JobKind::Compile);
}
void Workspace::ResetShaderText(){
    glsl_=originalGlsl_;assembly_=originalAssembly_;exportedGlsl_=glsl_;exportedAssembly_=assembly_;
    editor_.SetText(representation_==0?glsl_:representation_==1?assembly_:representation_==2?capturedSource_:shaderMetadata_);
    editor_.SetReadOnly(true);readOnly_=true;dirty_=false;candidate_.clear();candidateText_.clear();resetShaderScroll_=true;
}
void Workspace::ResetShader(){
    if(Busy()||CaptureSessionActive())return;
    if(!capture_||capture_->source.extension()!=".gfxr"||!shader_.module){ResetShaderText();status_="Shader reset to original text.";return;}
    const auto capture=capture_;const auto tools=tools_;const auto mode=replayMode_;const auto serial=androidSerial_;
    const auto module=shader_.module;auto overrides=activeOverrides_;overrides.erase(module);
    const auto frame=frame_>0?static_cast<uint32_t>(frame_):selectedExecution_>=0?capture->executions[selectedExecution_].frame:1u;
    const auto generation=++selectionGeneration_;const auto optimize=optimizeReplay_;const auto validation=validationReplay_;const auto screenOff=phoneScreenOff_;
    pendingDraw_=false;pendingFrame_=0;
    Start("Resetting shader and replaying original module",[this,capture,tools,mode,serial,module,overrides,frame,generation,optimize,validation,screenOff]{
        WorkspaceResult r;r.selectionGeneration=generation;r.replayFrame=frame;r.retryReplay=true;r.retryShaderReset=true;r.resetShaderModule=module;
        try{
#ifdef IMGUI_TEMPLATE_TESTS
            if(std::getenv("GFXRECON_GUI_TEST_RESET_FAILURE"))throw std::runtime_error("Simulated reset replay failure");
            if(std::getenv("GFXRECON_GUI_TEST_LOCKED_REPLAY"))throw AndroidUnlockRequired(ParseAndroidLockState("KeyguardServiceDelegate\n showing=true\n secure=true","test-phone"));
#endif
            const auto target=ResolveReplayTarget(mode,capture->android,serial,tools.at("adb"),&cancel_);
            ReplayRequest request;request.capture=capture->source;request.output=NewSession(sessionsDir_);request.frame=frame;
            request.cacheDirectory=sessionsDir_.parent_path()/"replay-cache";request.expectedCaptureHash=capture->fingerprint;request.selectionGeneration=generation;
            request.overrides=overrides;request.optimize=optimize;request.validation=validation;request.screenOff=screenOff&&target.android;request.reuseArtifacts=false;
            if(validation)request.timeout=std::chrono::seconds(600);
            const auto result=ReplayCapture(request,target,tools,&cancel_);
            if(result.validationReport.is_object()&&result.validationReport.value("errors",0)>0){r.failed=true;r.message="Shader reset replay reported Vulkan validation errors; current edits were retained.";r.validationReport=result.validationReport;return r;}
            r.replayResult=true;r.shaderReset=true;r.replayImage=result.image;r.replayTarget=result.target;r.overrides=overrides;r.validationReport=result.validationReport;
            r.message="Shader reset to captured original and replayed. Artifacts: "+request.output.u8string();
        }catch(const AndroidUnlockRequired& error){r.failed=true;r.message=error.what();r.unlockRequired=error.state;}
        return r;
    },JobKind::Dump,frame);
}
void Workspace::Draw() {
    Poll(); PollLiveCapture(); DrawEvents(); DrawPipeline(); DrawShader(); DrawResources(); DrawMesh(); DrawFrames(); DrawTools();
    DrawCapture();
    DrawImages();
    DrawReplayUnlock();
    ScheduleFrameReplay();
}
void Workspace::DrawReplayUnlock(){
    if(openReplayUnlock_){ImGui::OpenPopup("Unlock Android device");openReplayUnlock_=false;}
    const float width=ImGui::GetFontSize()*30;
    ImGui::SetNextWindowSize(ImVec2(width,0),ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width,0),ImVec2(width,ImGui::GetMainViewport()->WorkSize.y*.9f));
    if(!ImGui::BeginPopupModal("Unlock Android device",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    if(!blockedReplay_||blockedReplay_->selectionGeneration!=selectionGeneration_){blockedReplay_.reset();ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
    const auto state=*blockedReplay_->unlockRequired;
    ImGui::TextWrapped("%s",state.known?("Phone "+state.serial+" is locked.").c_str():("Cannot confirm that phone "+state.serial+" is unlocked.").c_str());
    ImGui::TextWrapped("Unlock the phone to replay this capture. You can continue inspecting captured data while it is locked.");
    if(state.secureKnown&&state.secure)ImGui::TextDisabled("Secure lock: unlock on the phone.");
    else if(state.dismissAttempted)ImGui::TextWrapped("Automatic dismissal did not unlock the phone. Please unlock it manually.");
    if(blockedReplay_->retryReplay){
        ImGui::BeginDisabled(Busy());
        if(ImGui::Button("Retry replay")){
            const auto request=*blockedReplay_;blockedReplay_.reset();ImGui::CloseCurrentPopup();previewPaused_=false;
            if(request.retryShaderReset)ResetShader();
            else{ReplayFrame(request.retryDump,request.retryRevert,request.replayFrame,false,request.meshPreview,request.drawPreview,request.descriptorImages,true);
                autoReplayRunning_=Busy();currentReplayFrame_=request.replayFrame;}
        }
        ImGui::EndDisabled();ImGui::SameLine();
    }
    if(ImGui::Button("Continue inspecting")){blockedReplay_.reset();ImGui::CloseCurrentPopup();}
    ImGui::EndPopup();
}
void Workspace::ReplayFrame(bool dump, bool revert, uint32_t requestedFrame, bool thumbnailOnly, bool meshPreview, bool drawPreview, bool descriptorImages, bool fresh) {
    if (!capture_ || capture_->source.extension() != ".gfxr") { status_ = "Replay requires the original .gfxr capture."; return; }
    const auto capture = capture_;
    const auto tools = tools_;
    const auto mode = replayMode_;
    const auto serial = androidSerial_;
    const auto optimize = optimizeReplay_;
    const auto validation=validationReplay_;
    const auto screenOff=phoneScreenOff_;
    if(!drawPreview&&!meshPreview){imageView_=0;resourceMode_=0;imageTab_=-1;imageTabRequest_=true;}
    if(validation)validationReport_={{"status","running"}};
    const auto overrides = revert ? std::map<uint64_t, std::filesystem::path>{} : activeOverrides_;
    const auto generation = selectionGeneration_;
    const auto execution = selectedExecution_;
    const auto meshInstance = meshInstance_;
    const auto shaderDirectory = session_ / "shaders";
    const auto documents=shaderDocuments_;
    const auto subdraw=selectedSubdraw_;
    const auto frame = requestedFrame ? requestedFrame : dump && execution >= 0 ? capture->executions[execution].frame :
        frame_ > 0 ? static_cast<uint32_t>(frame_) : execution >= 0 ? capture->executions[execution].frame : 1u;
    if (dump && execution < 0) { status_ = "Select a submitted draw/dispatch occurrence before requesting its resources."; return; }
    Start(drawPreview ? "Fetching selected draw data" : dump ? "Replaying selected event resources" : "Rendering frame " + std::to_string(frame), [this, capture, tools, generation, execution, frame, dump, revert, mode, serial, optimize, validation, overrides, thumbnailOnly, meshInstance, shaderDirectory, meshPreview, drawPreview, descriptorImages,documents,screenOff,subdraw,fresh] {
        const auto queryStarted=std::chrono::steady_clock::now();
        WorkspaceResult r; r.selectionGeneration = generation;
        r.replayResult = true;
        r.retryReplay=true;r.retryDump=dump;r.retryRevert=revert;
        r.replayFrame = frame;
        r.thumbnailOnly = thumbnailOnly;
        r.meshPreview = meshPreview;
        r.drawPreview = drawPreview;
        r.inspection=drawPreview;r.descriptorImages=descriptorImages||drawPreview;
        std::string shaderErrors;
        if(drawPreview){
            const auto& event=capture->events[capture->executions.at(execution).event];
            const auto stages=capture->pipelines.find(event.pipeline);
            if(stages!=capture->pipelines.end())for(const auto& stage:stages->second){
                if(cancel_.load())throw std::runtime_error("Draw inspection cancelled");
                try{
                    const auto existing=documents.find(ShaderKey(stage));
                    if(existing!=documents.end())r.stageDocuments.push_back(existing->second);
                    else{
                        auto document=std::make_shared<WorkspaceResult>(LoadShaderDocument(shaderDirectory/("sh"+std::to_string(stage.module)),stage,tools,&cancel_));
                        document->shaderEditLimitation=capture->ModuleEditLimitation(stage.module);r.stageDocuments.push_back(std::move(document));
                    }
                }catch(const std::exception& error){if(cancel_.load())throw;shaderErrors+=stage.stage+": "+error.what()+"\n";}
            }
        }
        r.overrides = overrides;
        auto run = NewSession(sessionsDir_);
        const auto target = ResolveReplayTarget(mode, capture->android, serial, tools.at("adb"), &cancel_);
        ReplayRequest request; request.capture = capture->source; request.output = run; request.frame = frame;
        request.progress=[](const std::string& text){LOGI("replay")<<text;};
        request.expectedCaptureHash = capture->fingerprint; request.selectionGeneration = generation;
        request.cacheDirectory = sessionsDir_.parent_path() / "replay-cache";
        request.optimize = optimize;
        request.outputsOnly = drawPreview; request.reuseArtifacts = !revert&&!fresh;
        request.descriptorImages=descriptorImages&&!drawPreview;request.drawInspection=drawPreview;request.validation=validation;request.screenOff=screenOff&&target.android;
        if(validation)request.timeout=std::chrono::seconds(600);
        request.overrides = overrides; request.prefix = overrides.empty() ? "baseline" : "modified";
        if (dump) request.dump = capture->DumpRequest(static_cast<size_t>(execution));
        if(drawPreview&&capture->events[capture->executions[execution].event].name.rfind("vkCmdDraw",0)==0){
            request.visualization="original";request.indirectSubdraw=subdraw;request.visualizationPipeline=capture->events[capture->executions[execution].event].pipeline;
            if(subdraw>=0){
                for(const auto& stage:capture->pipelines.at(request.visualizationPipeline)){
                    const auto file=overrides.count(stage.module)?overrides.at(stage.module):shaderDirectory/("sh"+std::to_string(stage.module));
                    if(ShaderUsesDrawIndex(file))throw std::runtime_error("Individual indirect draws using gl_DrawID are not supported yet. Select All draws.");
                }request.indirectDrawIdSafe=true;
            }
        }
        ReplayResult result;
        try{
#ifdef IMGUI_TEMPLATE_TESTS
            if(std::getenv("GFXRECON_GUI_TEST_LOCKED_REPLAY"))throw AndroidUnlockRequired(ParseAndroidLockState("KeyguardServiceDelegate\n showing=true\n secure=true","test-phone"));
#endif
            result=ReplayCapture(request,target,tools,&cancel_);
        }catch(const AndroidUnlockRequired& error){r.failed=true;r.message=error.what();r.unlockRequired=error.state;return r;}
        catch(const std::exception& error){
            if((!validation&&!drawPreview)||cancel_.load())throw;
            if(validation){std::ifstream report(run/"validation.json");if(report)r.validationReport=Json::parse(report);
            else r.validationReport={{"status","failed"},{"message",error.what()}};}
            r.failed=true;r.message=error.what();return r;
        }
        r.replayImage = result.image; r.replayTarget = result.target;
        r.resources = result.resources;
        if(dump&&subdraw<0)r.indirectParameters=ReadDrawParameters(run,capture->events[capture->executions[execution].event].block);
        r.validationReport=result.validationReport;
        if (dump) r.dumpBlock = capture->events.at(capture->executions.at(static_cast<size_t>(execution)).event).block;
        if (dump) r.dumpExecution = execution;
        if (dump) {
            try {
                const auto& event = capture->events.at(capture->executions.at(static_cast<size_t>(execution)).event);
                const auto& stages = capture->pipelines.at(event.pipeline);
                const auto stage = std::find_if(stages.begin(), stages.end(), [](const auto& s) { return s.stage.find("VERTEX") != std::string::npos; });
                const auto binary = stage != stages.end() ? (overrides.count(stage->module) ? overrides.at(stage->module) : shaderDirectory / ("sh" + std::to_string(stage->module))) : std::filesystem::path{};
                r.mesh = std::make_shared<MeshData>(ReadMeshData(*capture, static_cast<size_t>(execution), run, binary, meshInstance, &cancel_));
                r.mesh->target = result.target;
                for(auto& record:r.mesh->records)if(record)record->target=result.target;
            } catch (const std::exception& error) { r.meshError = error.what(); LOGW("mesh") << error.what(); }
        }
        if(drawPreview&&request.visualizationPipeline){
            r.diagnosticResources[0]=result.resources;
            std::filesystem::path uv;
            try{
                const auto& stages=capture->pipelines.at(request.visualizationPipeline);
                const auto stage=std::find_if(stages.begin(),stages.end(),[](const auto& s){return s.stage.find("FRAGMENT")!=std::string::npos;});
                if(stage==stages.end())throw std::runtime_error("UV density unavailable: no fragment stage");
                if(!stage->specialization.is_null())throw std::runtime_error("UV density unavailable for specialized fragment interfaces");
                const auto binary=overrides.count(stage->module)?overrides.at(stage->module):shaderDirectory/("sh"+std::to_string(stage->module));
                const auto source=run/"uv-checker.frag";uv=run/"uv-checker.spv";
                {std::ofstream out(source);out<<UvCheckerboardSource(binary,stage->entry,r.uvDetail);}
                gfx::CompileShader(source,{0,"VK_SHADER_STAGE_FRAGMENT_BIT","main"},uv,tools,&cancel_);
            }catch(const std::exception& error){if(cancel_.load())throw;r.diagnosticErrors[4]=error.what();uv.clear();}
            const char* modes[]={"original","solid","wireframe","depth","uv","silhouette"};
            for(int diagnostic=1;diagnostic<=5;++diagnostic){
                if(cancel_.load())throw std::runtime_error("Draw diagnostic query cancelled");
                if(r.diagnosticErrors.count(diagnostic))continue;
                try{
                    for(const auto& stage:capture->pipelines.at(request.visualizationPipeline)){
                        const auto limitation=capture->ModuleEditLimitation(stage.module);
                        if(!limitation.empty())throw std::runtime_error("Draw visualization unavailable: "+limitation);
                    }
                    auto pass=request;pass.output=NewSession(sessionsDir_);pass.visualization=modes[diagnostic];
                    pass.outputsOnly=true;pass.drawInspection=false;pass.descriptorImages=false;
                    pass.uvShader=diagnostic==4?uv:std::filesystem::path{};
                    if(diagnostic==5){
                        auto color=[](const auto& assets){for(const auto& asset:assets)if(asset.metadata.value("category","")=="output"&&asset.metadata.value("aspect","")=="COLOR"&&asset.metadata.value("location",0u)==0&&asset.file.extension()==".bmp")return asset.file;return std::filesystem::path{};};
                        pass.outlineBaseImage=color(result.resources);
                        if(r.diagnosticResources.count(1))pass.outlineCoverageImage=color(r.diagnosticResources.at(1));
                    }
                    LOGI("replay")<<"Fetching draw diagnostic "<<modes[diagnostic];
                    const auto output=ReplayCapture(pass,target,tools,&cancel_);
                    auto& assets=r.diagnosticResources[diagnostic];
                    for(const auto& asset:result.resources)if(asset.metadata.value("category","")!="output")assets.push_back(asset);
                    for(const auto& asset:output.resources)if(asset.metadata.value("category","")=="output")assets.push_back(asset);
                }catch(const std::exception& error){if(cancel_.load())throw;r.diagnosticErrors[diagnostic]=error.what();LOGW("replay")<<modes[diagnostic]<<": "<<error.what();}
            }
        }
        r.message = "Replay artifacts: " + run.u8string() + " (" + result.target + ")";
        const auto elapsed=r.diagnosticResources.empty()?result.timings.value("elapsed_seconds",0.0):std::chrono::duration<double>(std::chrono::steady_clock::now()-queryStarted).count();
        r.message += " | " + std::to_string(elapsed) + " s";
        if(!r.diagnosticResources.empty())r.message+=" | "+std::to_string(r.diagnosticResources.size())+" cached display modes";
        if (result.timings.value("artifact_cache_hit", false)) r.message += " | verified cached output";
        if (result.timings.value("optimized_replay", false)) r.message += " | optimized replay copy";
        if (result.timings.value("optimizer_status", "").find("failed") == 0) r.message += " | optimizer failed; using original capture";
        if (result.timings.value("replay_update_required", false)) r.message += " | Install the bundled replay APK to enable pipeline-cache acceleration.";
        if(validation)r.message+=" | Vulkan validation: "+std::to_string(r.validationReport.value("errors",0))+" errors, "+std::to_string(r.validationReport.value("warnings",0))+" warnings";
        if(!shaderErrors.empty())r.message+=" | Shader data unavailable: "+shaderErrors;
        if(!r.meshError.empty())r.message+=" | Mesh: "+r.meshError;
        return r;
    }, dump ? JobKind::Dump : JobKind::Replay, frame);
    if(drawPreview&&Busy()){jobExecution_=execution;executionStatuses_[execution]={PanelState::Running,"Fetching pipeline, shaders, mesh and bound images"};}
}
void Workspace::ApplyCandidate() {
    imageView_=0;resourceMode_=0;imageTab_=-1;imageTabRequest_=true;
    if (!capture_ || capture_->source.extension() != ".gfxr" || !shader_.module || candidate_.empty() || candidateText_ != editor_.GetText()) {
        status_ = "Compile the current captured module before applying a replay override."; return;
    }
    const auto reason = capture_->ModuleEditLimitation(shader_.module);
    if (!reason.empty()) { status_ = reason; return; }
    const auto capture = capture_;
    const auto tools = tools_;
    const auto mode = replayMode_;
    const auto serial = androidSerial_;
    const auto optimize = optimizeReplay_;
    const auto generation = selectionGeneration_;
    const auto validation=validationReplay_;
    const auto screenOff=phoneScreenOff_;
    if(validation)validationReport_={{"status","running"}};
    auto overrides = activeOverrides_; overrides[shader_.module] = candidate_;
    const auto frame = frame_ > 0 ? static_cast<uint32_t>(frame_) : selectedExecution_ >= 0 ? capture->executions[selectedExecution_].frame : 1u;
    Start("Replaying baseline and modified shader", [this, capture, tools, generation, frame, overrides, mode, serial, optimize,validation,screenOff] {
        WorkspaceResult r; r.selectionGeneration = generation; r.replayResult = true; r.overrides = overrides; r.replayFrame = frame;
        const auto run = NewSession(sessionsDir_);
        const auto target = ResolveReplayTarget(mode, capture->android, serial, tools.at("adb"), &cancel_);
        r.replayTarget = target.Label();
        for (auto name : {"baseline", "modified"}) {
            auto dir = run / name; std::filesystem::create_directories(dir);
            ReplayRequest request; request.capture = capture->source; request.output = dir; request.frame = frame; request.prefix = name;
            request.expectedCaptureHash = capture->fingerprint; request.selectionGeneration = generation;
            request.cacheDirectory = sessionsDir_.parent_path() / "replay-cache";
            request.optimize = optimize;
            request.validation=validation;request.screenOff=screenOff&&target.android;
            if(validation)request.timeout=std::chrono::seconds(600);
            request.reuseArtifacts = false;
            if (std::string(name) == "modified") request.overrides = overrides;
            const auto result = ReplayCapture(request, target, tools, &cancel_);
            if(validation){
                if(r.validationReport.is_null())r.validationReport={{"status","complete"},{"enabled",true},{"errors",0},{"warnings",0},{"messages",Json::array()}};
                for(const auto key:{"errors","warnings"})r.validationReport[key]=r.validationReport[key].get<int>()+result.validationReport.value(key,0);
                for(auto message:result.validationReport.value("messages",Json::array())){message["message"]="["+std::string(name)+"] "+message.value("message",std::string{});r.validationReport["messages"].push_back(message);}
            }
            (std::string(name) == "baseline" ? r.replayImage : r.modifiedImage) = result.image;
        }
        AtomicJson(run / "manifest.json", Json{{"owner", "gfxrecon_gui"}, {"schema", 2}, {"complete", true},
            {"capture_sha256", capture->fingerprint}, {"selection_generation", generation}, {"source", capture->source.u8string()},
            {"target", target.Label()}, {"frame", frame}, {"override_count", overrides.size()},
            {"baseline", r.replayImage.u8string()}, {"modified", r.modifiedImage.u8string()},
            {"baseline_manifest_sha256", Sha256File(run / "baseline/manifest.json", &cancel_)},
            {"modified_manifest_sha256", Sha256File(run / "modified/manifest.json", &cancel_)}});
        r.message = "Baseline/modified replay complete. Revert and render baseline to remove overrides. Artifacts: " + run.u8string();
        return r;
    }, JobKind::Replay, frame);
}
void Workspace::DrawShader() {
    if (focusShader_) { ImGui::SetNextWindowFocus(); focusShader_ = false; }
    const bool visible = ImGui::Begin("Shader");
    if (!visible) { ImGui::End(); return; }
    if (glsl_.empty() && assembly_.empty() && shaderMetadata_.empty()) {
        { RefreshOverlay refresh(Refreshing("Shader"));
        ImGui::TextWrapped("%s",selectedExecution_>=0&&ExecutionState(selectedExecution_)==PanelState::Running?"Fetching selected draw shaders...":"Select a shader stage or open a .spv / GLSL / .spvasm file."); }
        ImGui::End(); return;
    }
    { RefreshOverlay refresh(Refreshing("Shader"));
    const char* formats[] = {"Reconstructed GLSL", "SPIR-V assembly", "Captured source", "Debug metadata"};
    int next = representation_;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##representation", &next, formats, 4)) {
        if (representation_ < 2) (representation_ ? assembly_ : glsl_) = editor_.GetText();
        representation_ = next;
        editor_.SetLanguageDefinition(next ? SpirvLanguage() : GlslLanguage());
        editor_.SetText(next == 0 ? glsl_ : next == 1 ? assembly_ : next == 2 ? capturedSource_ : shaderMetadata_);
        if (next >= 2) { readOnly_ = true; editor_.SetReadOnly(true); }
        resetShaderScroll_ = true;
    }
    ImGui::SetItemTooltip("Shader representation. GLSL is reconstructed from captured SPIR-V; captured source/debug metadata are separate views.");
    bool editing = !readOnly_;
    ImGui::BeginDisabled(representation_ >= 2);
    if (ImGui::Checkbox("Edit", &editing)) {
        readOnly_ = !editing;
        editor_.SetReadOnly(readOnly_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("More")) ImGui::OpenPopup("shader-actions");
    if (ImGui::BeginPopup("shader-actions")) {
    if (ImGui::Button("Copy")) ImGui::SetClipboardText(editor_.GetText().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Export")) {
        try {
            const auto ext = representation_ == 0 ? "glsl" : representation_ == 1 ? "spvasm" : representation_ == 2 ? "txt" : "json";
            if (const auto path = tpl::SaveFileDialog("Shader text", ext, ("shader." + std::string(ext)).c_str())) {
                const auto destination = std::filesystem::u8path(*path);
                if (std::filesystem::weakly_canonical(destination) == std::filesystem::weakly_canonical(std::filesystem::u8path(shaderPath_)) ||
                    (capture_ && std::filesystem::weakly_canonical(destination) == std::filesystem::weakly_canonical(capture_->source)))
                    throw std::runtime_error("Choose a separate export file to preserve the original capture and shader binary");
                WriteText(destination, editor_.GetText());
                if (representation_ == 0) exportedGlsl_ = editor_.GetText();
                if (representation_ == 1) exportedAssembly_ = editor_.GetText();
                status_ = "Shader text exported: " + *path;
            }
        } catch (const std::exception& error) { status_ = error.what(); }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(shaderPath_.empty() || (!shader_.module && std::filesystem::u8path(shaderPath_).extension() != ".spv"));
    if (ImGui::Button("Export binary")) {
        try {
            if (const auto path = tpl::SaveFileDialog("Captured SPIR-V", "spv", "captured.spv")) {
                const auto destination = std::filesystem::u8path(*path), original = std::filesystem::u8path(shaderPath_);
                if (std::filesystem::weakly_canonical(destination) == std::filesystem::weakly_canonical(original) ||
                    (capture_ && std::filesystem::weakly_canonical(destination) == std::filesystem::weakly_canonical(capture_->source)))
                    throw std::runtime_error("Choose a separate export file");
                std::filesystem::copy_file(original, destination, std::filesystem::copy_options::overwrite_existing);
                status_ = "Exact captured binary exported: " + *path;
            }
        } catch (const std::exception& error) { status_ = error.what(); }
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Revert text")) {
        glsl_ = originalGlsl_; assembly_ = originalAssembly_;
        editor_.SetText(representation_ == 0 ? glsl_ : representation_ == 1 ? assembly_ : representation_ == 2 ? capturedSource_ : shaderMetadata_); dirty_ = false;
        resetShaderScroll_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Top")) resetShaderScroll_ = true;
    ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(Busy()||!editor_.CanUndo());
    if(ImGui::Button("Undo"))editor_.Undo();
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Undo text edit (%s+Z).\nReplay changes only after Compile and Apply.",ImGui::GetIO().ConfigMacOSXBehaviors?"Cmd":"Ctrl");
    ImGui::SameLine();
    ImGui::BeginDisabled(Busy()||!editor_.CanRedo());
    if(ImGui::Button("Redo"))editor_.Redo();
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Redo text edit (%s+Y).\nReplay changes only after Compile and Apply.",ImGui::GetIO().ConfigMacOSXBehaviors?"Cmd":"Ctrl");
    ImGui::SameLine();
    ImGui::BeginDisabled(Busy()||CaptureSessionActive()||(readOnly_&&!dirty_&&candidate_.empty()&&!activeOverrides_.count(shader_.module)));
    if(ImGui::Button("Reset shader"))ResetShader();
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Restore the captured shader, clear text history and the candidate, and replay automatically.\nOther shader overrides are retained.");
    ImGui::SameLine();
    ImGui::BeginDisabled((Busy() && !autoReplayRunning_) || readOnly_ || representation_ >= 2 || !shaderEditLimitation_.empty() || !ToolAvailable(representation_ == 1 ? "assembler" : "compiler") || !ToolAvailable("validator"));
    if (ImGui::Button("Compile")) CompileShader();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Compile and validate the current shader candidate. %s", shaderEditLimitation_.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled((Busy() && !autoReplayRunning_) || !shader_.module || !shaderEditLimitation_.empty() || candidate_.empty() || candidateText_ != editor_.GetText());
    if (ImGui::Button("Apply")) ApplyCandidate();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Apply the validated candidate to fresh replay and compare against a matching baseline.");
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Find next").x - 28);
    ImGui::InputTextWithHint("##shader-search", "Find...", &shaderSearch_);
    ImGui::SameLine();
    if (ImGui::Button("Find next") && !shaderSearch_.empty()) {
        const auto lines = editor_.GetTextLines();
        const auto start = editor_.GetCursorPosition();
        for (size_t n = 0; n < lines.size(); ++n) {
            const size_t row = (static_cast<size_t>(start.mLine) + n) % lines.size();
            const auto pos = lines[row].find(shaderSearch_, n == 0 ? static_cast<size_t>(start.mColumn + 1) : 0);
            if (pos != std::string::npos) {
                editor_.SetCursorPosition({static_cast<int>(row), static_cast<int>(pos)});
                editor_.SetSelection({static_cast<int>(row), static_cast<int>(pos)}, {static_cast<int>(row), static_cast<int>(pos + shaderSearch_.size())});
                break;
            }
        }
    }
    const bool uses = shader_.module && capture_;
    const float referencesHeight = uses ? std::min(150.0f, ImGui::GetContentRegionAvail().y * .25f) : 0;
    const float editorHeight = std::max(60.0f, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() - referencesHeight);
    if (editorFont_) ImGui::PushFont(editorFont_);
    if (resetShaderScroll_) {
        editor_.SetCursorPosition({0, 0});
        editor_.SetSelection({0, 0}, {0, 0});
        ImGui::SetNextWindowScroll(ImVec2(0, 0));
        resetShaderScroll_ = false;
    }
    editor_.SetHandleKeyboardInputs(!Refreshing("Shader"));editor_.SetHandleMouseInputs(!Refreshing("Shader"));
    editor_.SetReadOnly(readOnly_||Refreshing("Shader"));
    editor_.Render("source-editor", ImVec2(0, editorHeight));
    editor_.SetReadOnly(readOnly_);
    if (editorFont_) ImGui::PopFont();
    if (editor_.IsTextChanged() && representation_ < 2) {
        (representation_ ? assembly_ : glsl_) = editor_.GetText();
        dirty_ = glsl_ != originalGlsl_ || assembly_ != originalAssembly_;
    }
    if (uses) {
        ImGui::BeginChild("pipeline-uses", ImVec2(0, referencesHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
        ImGui::TextDisabled("Pipelines");
        if (ImGui::BeginTable("references", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Pipeline"); ImGui::TableSetupColumn("Entry"); ImGui::TableHeadersRow();
            for (const auto& [pipeline, stages] : capture_->pipelines) for (const auto& stage : stages) if (stage.module == shader_.module) {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::PushID(stage.stage.c_str());
                const auto selectedPipeline = inspectedPipeline_ ? inspectedPipeline_ : capture_->events[selectedEvent_].pipeline;
                if (ImGui::Selectable(std::to_string(pipeline).c_str(), selectedPipeline == pipeline)) JumpToPipeline(pipeline);
                ImGui::SetItemTooltip("Inspect pipeline %llu\n%s | %s\nReplacement affects every use of module %llu", pipeline, stage.stage.c_str(), stage.entry.c_str(), shader_.module);
                ImGui::PopID(); ImGui::TableNextColumn(); ImGui::TextUnformatted(stage.entry.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }
    ImGui::TextDisabled("Module %llu  |  %s  |  %s", shader_.module, dirty_ ? "Modified" : "Original", readOnly_ ? "Read only" : "Editing");
    ImGui::SetItemTooltip("%s\n%s\n%s", shaderPath_.c_str(), shaderEditLimitation_.empty() ? "GLSL is generated by SPIRV-Cross. Changes affect all module uses." : shaderEditLimitation_.c_str(),
        capture_ && shaderSelectionGeneration_ != selectionGeneration_ ? "Document retained from a previous selection." : "Document matches the current selection.");
    }
    ImGui::End();
}
void Workspace::RefreshDevices() {
    const auto adb = tools_["adb"];
    Start("Discovering Android devices", [this, adb] {
        WorkspaceResult r; r.devices = QueryAndroidDevices(adb, &cancel_); r.devicesUpdated = true;
        r.message = "Device discovery complete."; return r;
    });
}
void Workspace::DrawTools() {
    if (showTools_) { ImGui::OpenPopup("Tools and devices"); showTools_ = false; }
    ImGui::SetNextWindowSize(ImVec2(760, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(760,0),ImVec2(760,ImGui::GetMainViewport()->WorkSize.y*.9f));
    if (!ImGui::BeginPopupModal("Tools and devices", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (ImGui::CollapsingHeader("Available operations", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto operation = [&](const char* label, bool ready, const char* dependency) {
            ImGui::Text("%s: %s", label, ready ? "Available" : "Unavailable");
            if (!ready) { ImGui::SameLine(); ImGui::TextDisabled("Requires %s", dependency); }
        };
        operation("Offline JSONL navigation", true, "");
        operation("GFXR import", ToolAvailable("convert"), "gfxrecon-convert");
        operation("Captured shader extraction", ToolAvailable("extract"), "gfxrecon-extract");
        operation("GLSL reconstruction / reflection", true, "");
        operation("SPIR-V assembly view", ToolAvailable("disassembler"), "spirv-dis");
        operation("GLSL editing", ToolAvailable("compiler") && ToolAvailable("validator"), "glslangValidator and spirv-val");
        operation("SPIR-V assembly editing", ToolAvailable("assembler") && ToolAvailable("validator"), "spirv-as and spirv-val");
        operation("Local replay", ToolAvailable("replay"), "gfxrecon-replay");
        operation("Android setup / replay", ToolAvailable("adb"), "ADB and a compatible installed replay APK");
        ImGui::TextDisabled("SPIRV-Cross: built in (300681bb). Transport: CLI / ADB files.");
        ImGui::TextDisabled("Experimental remote transport: probed separately; disabled in this release.");
    }
    if (ImGui::CollapsingHeader("Tool configuration")) {
        ImGui::BeginDisabled(Busy());
        for (auto& [name, path] : tools_) {
            if (ImGui::InputText(name.c_str(), &path)) { RefreshToolAvailability(); versions_.erase(name); }
            ImGui::SameLine(); ImGui::TextDisabled("%s", ToolAvailable(name) ? "Found" : "Missing");
            auto it = versions_.find(name);
            if (it != versions_.end()) ImGui::TextWrapped("%s", it->second.c_str());
        }
        ImGui::EndDisabled();
        if (ImGui::Button("Save tool paths")) {
            try { SaveConfiguration(); status_ = "Tool paths saved."; }
            catch (const std::exception& e) { status_ = e.what(); }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(Busy());
        if (ImGui::Button("Check versions")) ProbeTools();
        ImGui::EndDisabled();
    }
    ImGui::BeginDisabled(Busy() || tools_["adb"].empty());
    if (ImGui::Button("Refresh Android devices")) RefreshDevices();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(Busy());
    ImGui::TextUnformatted("Replay device");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##replay-device", androidSerial_.empty() ? "Automatic (single device)" : androidSerial_.c_str())) {
        if (ImGui::Selectable("Automatic (single device)", androidSerial_.empty())) { androidSerial_.clear(); ++selectionGeneration_; }
        for (const auto& device : devices_) {
            const auto label = device.serial + " (" + device.state + ")";
            ImGui::BeginDisabled(device.state != "device");
            if (ImGui::Selectable(label.c_str(), androidSerial_ == device.serial)) { androidSerial_ = device.serial; ++selectionGeneration_; }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("%s", device.details.c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (devices_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Automatic selects the only authorized phone. Refresh to choose a specific device.");
        ImGui::PopStyleColor();
    }
    if (!deviceOutput_.empty()) ImGui::TextUnformatted(deviceOutput_.c_str());
    for(const auto& path:displayRecoveries_){
        ImGui::PushID(path.u8string().c_str());ImGui::TextWrapped("Replay display needs recovery: %s",path.parent_path().filename().u8string().c_str());
        ImGui::BeginDisabled(Busy());
        if(ImGui::Button("Restore phone display")){
            const auto tools=tools_;Start("Restoring replay display",[this,path,tools]{
                const auto journal=Json::parse(ReadText(path));const auto serial=journal.at("serial").get<std::string>();
                ResolveReplayTarget(ReplayMode::Android,true,serial,tools.at("adb"),&cancel_);
                RestoreAndroidReplayDisplay({tools.at("adb"),serial,&cancel_},path);WorkspaceResult r;r.message="Phone display restored on "+serial;return r;
            });
        }ImGui::EndDisabled();ImGui::PopID();
    }
    for (const auto& path : recoveries_) {
        ImGui::PushID(path.u8string().c_str());
        ImGui::TextWrapped("Capture settings need recovery: %s", path.parent_path().filename().u8string().c_str());
        ImGui::BeginDisabled(Busy());
        if (ImGui::Button("Restore device settings")) {
            const auto tools = tools_;
            Start("Restoring device capture settings", [this, path, tools] {
                const auto journal = Json::parse(ReadText(path));
                const auto serial = journal.at("serial").get<std::string>();
                ResolveReplayTarget(ReplayMode::Android, true, serial, tools.at("adb"), &cancel_);
                RestoreAndroidCapture({tools.at("adb"), serial, &cancel_}, path);
                WorkspaceResult r; r.message = "Capture settings restored on " + serial; return r;
            });
        }
        ImGui::EndDisabled(); ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextWrapped("Open existing captures or standalone shader files from File > Open.");
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
} // namespace gfx
