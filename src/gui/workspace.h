#pragma once

#include "analysis/capture.h"
#include "gui/image_viewer.h"
#include "platform/android_display.h"
#include "gui/mesh_viewer.h"
#include "replay/replay.h"
#include "capture/capture.h"
#include "capture/live_capture.h"
#include "capture/save_capture.h"
#include <TextEditor.h>
#include <atomic>
#include <future>
#include <functional>
#include <map>
#include <optional>
#include <mutex>
#include <set>

namespace gfx {
enum class PanelState { Inactive, Running, Success, Failed };
enum class JobKind { Import, Shader, Compile, Replay, Dump, Resource, Capture, Tools };
struct PanelStatus { PanelState state = PanelState::Inactive; std::string detail; };
struct WorkspaceResult {
    std::shared_ptr<Capture> capture;
    std::filesystem::path session;
    std::map<std::string, std::string> versions;
    std::string text;
    std::string assembly;
    std::string reflection;
    std::string capturedSource, shaderMetadata;
    std::string shaderEditLimitation;
    std::string message;
    bool shader = false;
    ShaderStage shaderInfo;
    std::string shaderPath;
    std::filesystem::path replayImage;
    uint64_t selectionGeneration = 0;
    std::filesystem::path candidate;
    std::string candidateText;
    std::filesystem::path modifiedImage;
    std::map<uint64_t, std::filesystem::path> overrides;
    bool replayResult = false;
    bool thumbnailOnly = false;
    bool meshPreview = false;
    bool drawPreview = false;
    bool descriptorImages = false;
    bool inspection = false;
    std::vector<std::shared_ptr<WorkspaceResult>> stageDocuments;
    Json validationReport;
    Json indirectParameters;
    bool failed = false;
    std::optional<AndroidLockState> unlockRequired;
    bool retryReplay=false,retryDump=false,retryRevert=false;
    uint32_t replayFrame = 0;
    std::string replayTarget;
    std::vector<AndroidDevice> devices;
    bool devicesUpdated = false;
    std::optional<AndroidInfo> deviceInfo;
    std::vector<ResourceArtifact> resources;
    uint64_t dumpBlock = 0;
    int dumpExecution = -1;
    std::optional<ResourcePage> resourcePage;
    std::filesystem::path resourceImage;
    bool captureSetupReady = false;
    bool deferReplay = false;
    std::shared_ptr<LiveCaptureSession> liveSession;
    std::shared_ptr<MeshData> mesh;
    std::string meshError;
    std::map<std::string, bool> toolReady;
    std::optional<CaptureSaveResult> savedCapture;
    std::vector<std::filesystem::path> completedCaptures;
};
class Workspace {
public:
    void Init(const std::filesystem::path& settingsDirectory);
    void Shutdown();
    void Draw();
    void Open(const std::string& path);
    void SelectEvent(size_t index);
    void SelectExecution(size_t index, bool preview = false);
    void LoadShader(const ShaderStage& stage);
    bool Busy() const;
    bool HasPendingWork() const { return pendingDraw_ || pendingFrame_ || deferredJob_.has_value() || !pendingOpen_.empty(); }
    const std::shared_ptr<Capture>& CurrentCapture() const { return capture_; }
    size_t SelectedEvent() const { return selectedEvent_; }
    const std::string& Status() const { return status_; }
    const TextEditor& Editor() const { return editor_; }
    const ImageViewer& ModifiedImage() const { return modifiedImage_; }
    const ImageViewer& BaselineImage() const { return image_; }
    const std::string& LastReplayTarget() const { return lastReplayTarget_; }
    const std::vector<ResourceArtifact>& Resources() const { return resources_; }
    const std::filesystem::path& ActiveCaptureSession() const { return captureSession_; }
    const std::map<std::string, std::string>& ConfiguredTools() const { return tools_; }
    int SelectedExecution() const { return selectedExecution_; }
    int DisplayedExecution() const { return displayedDraw_; }
    PanelState ExecutionState(size_t index)const{const auto state=executionStatuses_.find(index);return state==executionStatuses_.end()?PanelState::Inactive:state->second.state;}
    size_t ShaderDocumentCount()const{return shaderDocuments_.size();}
    const ImageViewer& SelectedImage() const {return resourceImage_;}
    const Json& ValidationReport() const {return validationReport_;}
#ifdef IMGUI_TEMPLATE_TESTS
    void ConfigureReplayFixture(const std::string& path){tools_["replay"]=path;RefreshToolAvailability();}
    void LoadMeshFixture(std::shared_ptr<MeshData> data){const auto generation=selectionGeneration_;Start("Loading mesh fixture",[data,generation]{WorkspaceResult r;r.mesh=data;r.meshPreview=true;r.selectionGeneration=generation;return r;});}
    void LoadDrawFixture(const std::filesystem::path& directory){const auto generation=selectionGeneration_;const auto execution=selectedExecution_;const auto block=capture_->events[selectedEvent_].block;Start("Loading draw fixture",[directory,generation,execution,block]{WorkspaceResult r;r.replayResult=true;r.drawPreview=true;r.descriptorImages=true;r.resources=CollectResources(directory);r.selectionGeneration=generation;r.dumpExecution=execution;r.dumpBlock=block;return r;});}
#endif
    void SetReplayMode(ReplayMode mode);
    void SetDrawVisualization(int mode,int subdraw=-1);
    int DrawVisualization()const{return drawVisualization_;}
    int SelectedSubdraw()const{return selectedSubdraw_;}
    bool PhoneScreenOff()const{return phoneScreenOff_;}
    void RequestCapture();
    void RequestTools() { showTools_ = true; }
    void SaveCapture(bool saveAs = false);
    bool CanSaveCapture() const { return capture_ && capture_->source.extension()==".gfxr" && (!Busy()||autoReplayRunning_); }
    void ProbeTools();
    bool HasUnsavedEdits() const;
    void SelectFrame(uint32_t frame, bool render = true);
    void JumpToPipeline(uint64_t pipeline);
    uint32_t SelectedFrame() const { return static_cast<uint32_t>(frame_); }
    size_t ThumbnailCount() const { return frameImages_.size(); }
    uint64_t InspectedPipeline() const { return inspectedPipeline_; }
    void CancelJob();
    void DrawPanelStatuses();
    PanelState StateFor(const std::string& panel) const;
    PanelState FrameStateFor(uint32_t frame) const;
    bool Refreshing(const std::string& panel) const;
    size_t ImageDocumentCount()const{return imageTabs_.size();}
    int PipelineStage()const{return pipelineView_;}
    const ResourcePage& SelectedBuffer()const{return resourcePage_;}
    const Json& SelectedObject()const{return objectDetails_;}
    const MeshViewer& Mesh() const { return mesh_; }
    bool CaptureSessionActive() const { return liveCapture_ && liveCapture_->Active(); }
    void EndCaptureSession(bool closeGame = false) { if (liveCapture_) liveCapture_->End(closeGame); }
    std::optional<LiveCaptureState> LiveState() const { return liveCapture_ ? std::optional<LiveCaptureState>(liveCapture_->State()) : std::nullopt; }
private:
    void Start(const std::string& name, std::function<WorkspaceResult()> work, JobKind kind = JobKind::Tools, uint32_t frame = 0);
    void CompleteOperation(PanelState state, const std::string& detail);
    void Poll();
    void DrawEvents();
    void DrawPipeline();
    void DrawShader();
    void DrawResources();
    void DrawMesh();
    void DrawTools();
    void DrawFrames();
    void DrawFrameActions(uint32_t frame);
    void CompileShader();
    void ReplayFrame(bool dump = false, bool revert = false, uint32_t requestedFrame = 0, bool thumbnailOnly = false, bool meshPreview = false, bool drawPreview = false, bool descriptorImages = false, bool fresh = false);
    void DrawImages();
    void DrawReplayUnlock();
    void ResetImageDocuments();
    void InvalidateDrawResources();
    void DrawBoundImages(const char* id,bool compact = false);
    void DrawStageImages(const std::string& stage,bool outputs);
    void DrawStageSamplers(const ShaderStage& stage,const Execution* execution);
    void ShowShaderDocument(const WorkspaceResult& document,bool focus);
    void ScheduleFrameReplay();
    void ApplyCandidate();
    void SaveConfiguration() const;
    void RefreshDevices();
    void DrawCapture();
    void DrawBuffer();
    void OpenResource(const ResourceArtifact& artifact, uint64_t offset = 0,bool activate = true);
    void StartCapture();
    void StartLiveCapture();
    void DrawLiveCapture();
    void DrawCompletedCapture(const std::filesystem::path& file);
    void RequestCaptureSave(const std::filesystem::path& source, bool saveAs);
    std::filesystem::path CaptureSaveDestination(const std::filesystem::path& source);
    void PollLiveCapture();
    void InspectCaptureDevice();
    void InstallReplayApk(bool sample = false);
    void CheckCaptureSetup();
    void SetCaptureProgress(const std::string& phase);
    void RefreshRecoveries();
    static WorkspaceResult ImportCapture(const std::filesystem::path& file, const std::filesystem::path& session,
                                         const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel);
    void RefreshToolAvailability();
    bool ToolAvailable(const std::string& name) const;
    std::filesystem::path settingsDir_, sessionsDir_, session_;
    std::map<std::string, std::string> tools_, versions_;
    std::map<std::string, bool> toolReady_;
    std::future<WorkspaceResult> job_;
    std::optional<WorkspaceResult> blockedReplay_;
    bool openReplayUnlock_=false;
    struct DeferredJob { std::string name; std::function<WorkspaceResult()> work; JobKind kind; uint32_t frame; };
    std::optional<DeferredJob> deferredJob_;
    std::atomic_bool cancel_{false};
    std::string jobName_, status_ = "Open a GFXReconstruct capture or shader to begin.";
    std::string loggedStatus_;
    std::map<std::string, PanelStatus> panelStatuses_;
    std::map<uint32_t, PanelStatus> frameStatuses_;
    std::map<size_t,PanelStatus> executionStatuses_;
    int jobExecution_ = -1;
    bool revealPipeline_ = false;
    std::map<std::string,std::shared_ptr<WorkspaceResult>> shaderDocuments_;
    std::string autoShaderStage_ = "VK_SHADER_STAGE_FRAGMENT_BIT";
    uint32_t jobFrame_ = 0;
    std::vector<std::string> jobPanels_;
    std::shared_ptr<Capture> capture_;
    size_t selectedEvent_ = 0;
    std::string arguments_;
    int frame_ = 0;
    int selectedExecution_ = -1;
    bool submittedOnly_ = true;
    uint64_t selectionGeneration_ = 1;
    bool onlyDraws_ = false;
    bool showTools_ = false;
    ImGuiTextFilter eventFilter_;
    std::vector<size_t> filteredRows_;
    bool filterDirty_ = true;
    TextEditor editor_;
    ImFont* editorFont_ = nullptr;
    std::string glsl_, assembly_, originalGlsl_, originalAssembly_, reflection_;
    std::string capturedSource_, shaderMetadata_, exportedGlsl_, exportedAssembly_;
    std::string shaderEditLimitation_;
    ShaderStage shader_;
    int representation_ = 0;
    bool readOnly_ = true;
    bool dirty_ = false;
    bool resetShaderScroll_ = false;
    std::string shaderPath_, deviceOutput_, shaderSearch_;
    ImageViewer image_;
    MeshViewer mesh_;
    bool focusMesh_ = false;
    uint64_t meshGeneration_ = 0, meshRequestGeneration_ = 0;
    uint32_t meshInstance_ = 0;
    std::string meshDiagnostic_;
    ImageViewer modifiedImage_;
    ImageViewer differenceImage_;
    int comparisonMode_ = 0;
    std::filesystem::path candidate_;
    std::string candidateText_;
    std::map<uint64_t, std::filesystem::path> activeOverrides_;
    bool hasComparison_ = false;
    ReplayMode replayMode_ = ReplayMode::Automatic;
    bool optimizeReplay_ = true;
    bool validationReplay_ = false;
    bool phoneScreenOff_ = false;
    Json validationReport_;
    std::string androidSerial_, lastReplayTarget_;
    std::vector<AndroidDevice> devices_;
    std::optional<AndroidInfo> deviceInfo_;
    bool showCapture_ = false, captureInProgress_ = false, captureSetupReady_ = false;
    int captureStep_ = 0;
    std::string captureError_, capturePhase_;
    std::mutex capturePhaseMutex_;
    std::filesystem::path captureSession_;
    std::vector<std::filesystem::path> completedCaptures_;
    std::map<std::filesystem::path,std::filesystem::path> savedCaptures_;
    std::shared_ptr<LiveCaptureSession> liveCapture_;
    int captureStyle_ = 0;
    int captureLength_ = 3;
    std::string liveCaptureDetail_;
    int captureMode_ = 0;
    std::string capturePackage_, captureArguments_, captureFrames_ = "1-3";
    std::string captureExecutable_, captureWorkingDirectory_, windowsArguments_;
    std::vector<std::filesystem::path> recoveries_,displayRecoveries_;
    std::vector<ResourceArtifact> resources_;
    ResourcePage resourcePage_;
    Json resourceMetadata_,bufferMetadata_,objectDetails_;
    std::string objectLabel_;
    ImageViewer resourceImage_;
    std::vector<ResourceArtifact> imageTabs_;
    int imageTab_ = -1;
    bool imageTabRequest_ = false;
    int imageSidebar_ = 1;
    std::map<std::filesystem::path,ImageViewer> assetThumbnails_;
    std::set<std::filesystem::path> failedThumbnails_;
    int imageView_ = 0;
    bool drawHasInputs_ = false;
    std::filesystem::path selectedDrawImage_;
    uint64_t imageShownGeneration_ = 0;
    uint64_t resourceOffset_ = 0, dumpBlock_ = 0;
    int dumpExecution_ = -1;
    uint64_t shaderSelectionGeneration_ = 0;
    int resourceMode_ = 0, resourceTab_ = 0, scalarType_ = 4, bufferColumns_ = 4, bufferStride_ = 16;
    bool resourceTabRequest_ = false;
    int pipelineView_ = 6;
    using ExecutionTree = std::map<uint32_t, std::map<uint64_t, std::map<std::string, std::vector<size_t>>>>;
    ExecutionTree executionTree_;
    bool revealExecution_ = false;
    bool focusShader_ = false, focusPipeline_ = false, autoReplayRunning_ = false;
    bool focusImage_ = false, focusResources_ = false;
    bool focusFrames_ = true;
    bool previewPaused_ = false, jobFailed_ = false;
    std::string pendingOpen_;
    uint64_t inspectedPipeline_ = 0;
    uint32_t pendingFrame_ = 0, currentReplayFrame_ = 0;
    bool pendingDraw_ = false;
    std::chrono::steady_clock::time_point drawRequestedAt_;
    int displayedDraw_ = -1;
    int drawVisualization_=0,selectedSubdraw_=-1;
    Json indirectParameters_;
    std::vector<uint32_t> thumbnailQueue_;
    std::map<uint32_t, std::string> failedFrames_;
    struct FramePreview { ImageViewer image; std::string target; bool modified = false; };
    std::map<uint32_t, FramePreview> frameImages_;
};
} // namespace gfx
