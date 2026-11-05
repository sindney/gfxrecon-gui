#include "gui/workspace.h"
#include "analysis/performance_units.h"
#include "replay/perf_session.h"
#include "analysis/performance_series.h"
#include "core/log.h"
#include "platform/dialogs.h"
#include <implot.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>

namespace gfx {
namespace {
namespace fs = std::filesystem;
const Json& Object(const Json& parent, const char* key) {
    static const Json empty = Json::object();
    const auto at = parent.find(key); return at != parent.end() && at->is_object() ? *at : empty;
}
const Json& Array(const Json& parent, const char* key) {
    static const Json empty = Json::array();
    const auto at = parent.find(key); return at != parent.end() && at->is_array() ? *at : empty;
}
const char* SwapchainName(int mode) { return mode == 1 ? "virtual" : mode == 2 ? "captured" : "offscreen"; }
std::string Number(const Json& value) {
    if (!value.is_number()) return "—";
    if (value.is_number_integer() || value.is_number_unsigned()) return value.dump();
    std::ostringstream out; out.precision(6); out << value.get<double>(); return out.str();
}
Json Statistic(const Json& metric, const char* variant, const char* key) {
    const auto it = metric.find(variant);
    return it != metric.end() && it->is_object() ? it->value(key, Json{}) : Json{};
}
PerformanceDisplayScale MetricScale(const Json& metric){
    double magnitude=0;
    for(const auto* variant:{"baseline","modified"})for(const auto* field:{"mean","min","max","p95","stddev"}){
        const auto value=Statistic(metric,variant,field);if(value.is_number())magnitude=std::max(magnitude,std::abs(value.get<double>()));
    }
    return PerformanceScale(metric.at("unit").get<std::string>(),magnitude);
}
std::string ScaledNumber(const Json& value,const PerformanceDisplayScale& scale){
    return scale.unit=="ms"&&scale.divisor==1?Number(value):value.is_number()?FormatPerformanceValue(value.get<double>(),scale):"—";
}
double SortNumber(const Json& metric, const char* variant, const char* key) {
    const auto value = Statistic(metric, variant, key);
    return value.is_number() ? value.get<double>() : -std::numeric_limits<double>::infinity();
}
uint64_t UnsignedField(const Json& object, const char* key) {
    const auto at = object.find(key);
    if (at == object.end()) return 0;
    if (at->is_number_unsigned()) return at->get<uint64_t>();
    if (at->is_number_integer() && at->get<int64_t>() >= 0) return static_cast<uint64_t>(at->get<int64_t>());
    return 0;
}
double FiniteField(const Json& object, const char* key, double fallback = 0) {
    const auto at = object.find(key);
    if (at == object.end() || !at->is_number()) return fallback;
    const auto value = at->get<double>(); return std::isfinite(value) ? value : fallback;
}
std::string Status(const Json& object) {
    const auto at = object.find("status"); return at != object.end() && at->is_string() ? at->get<std::string>() : std::string{};
}
std::string CompactText(const std::string& text){
    std::istringstream input(text);std::string result,word;
    while(input>>word){if(!result.empty())result+=' ';result+=word;}
    return result;
}
void BeginPerfTooltip(){
    const float width=std::min(ImGui::GetFontSize()*34,ImGui::GetMainViewport()->WorkSize.x*.85f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width,0),ImVec2(width,FLT_MAX));
    ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width-ImGui::GetStyle().WindowPadding.x*2);
}
void EndPerfTooltip(){ImGui::PopTextWrapPos();ImGui::EndTooltip();}
void DeviceInfoLabel(){
    ImGuiContext& g=*GImGui;
    const float size=ImGui::GetFrameHeight();const auto origin=ImGui::GetCursorScreenPos();
    const ImRect bounds(origin,ImVec2(origin.x+size,origin.y+size));const auto id=ImGui::GetID("##perf-device-info");
    ImGui::ItemSize(bounds);
    if(ImGui::ItemAdd(bounds,id)){
        // A passive phone symbol, independent of platform emoji font support.
        auto* draw=ImGui::GetWindowDrawList();const auto color=ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImVec2 a(origin.x+size*.28f,origin.y+size*.13f),b(origin.x+size*.72f,origin.y+size*.87f);
        draw->AddRect(a,b,color,2,0,1.5f);
        draw->AddLine(ImVec2(a.x+2,b.y-size*.15f),ImVec2(b.x-2,b.y-size*.15f),color);
        draw->AddCircleFilled(ImVec2(origin.x+size*.5f,b.y-size*.075f),1,color);
    }
    IMGUI_TEST_ENGINE_ITEM_INFO(id,"##perf-device-info",g.LastItemData.StatusFlags);
}
struct PlotWindow { uint64_t start = 0, end = 0; bool Valid() const { return start && end > start; } };
PlotWindow CounterWindow(const Json& run) {
    const auto& gpu = Object(run, "gpu");
    // A rejected native window vetoes even stale numeric collection endpoints.
    if (gpu.contains("counter_window")) {
        const auto& window = Object(gpu, "counter_window");
        if (Status(window) != "available" && Status(window) != "valid") return {};
        const PlotWindow result{UnsignedField(window, "start_boot_ns"), UnsignedField(window, "end_boot_ns")};
        return result.Valid() ? result : PlotWindow{};
    }
    // Older complete reports keep their validated window on the collection.
    const auto& collection = Object(run, "counters"); const auto status = Status(collection);
    if (status != "complete" && status != "available" && status != "sparse" && status != "valid") return {};
    const PlotWindow result{UnsignedField(collection, "window_begin_boot_ns"), UnsignedField(collection, "window_end_boot_ns")};
    return result.Valid() ? result : PlotWindow{};
}
double RelativeTime(uint64_t time, uint64_t origin) {
    return static_cast<double>((static_cast<long double>(time) - origin) / 1e6L);
}
double WindowLengthMs(const Json& run) {
    const auto window = CounterWindow(run);
    if (window.Valid()) return RelativeTime(window.end, window.start);
    double elapsed = 0;
    for (const auto& sample : Array(Object(run, "gpu"), "samples"))
        if (sample.value("valid", false)) elapsed = std::max(elapsed, FiniteField(sample, "elapsed_ms"));
    return std::max(elapsed, 0.001);
}
PerformanceSeries SeriesFor(const Json& run, const Json& metric, bool fit, double low, double high, size_t budget) {
    std::vector<double> x, y;
    const auto window = CounterWindow(run);
    if (metric.at("key") == "gpu_time") {
        for (const auto& sample : Array(Object(run, "gpu"), "samples")) {
            if (!sample.value("valid", false) || sample.value("phase", "measurement") != "measurement") continue;
            const auto elapsed = FiniteField(sample, "elapsed_ms", -1);
            if (elapsed < 0) continue;
            x.push_back(0.0); y.push_back(elapsed);
        }
    } else {
        if (!window.Valid()) return ReducePerformanceSeries(x, y, budget);
        const auto& collection = Object(run, "counters");
        for (const auto& counter : Array(collection, "counters")) {
            if (counter.at("id") != metric.at("counter_id")) continue;
            const auto requested = std::max<uint64_t>(1, std::min<uint64_t>(1000000000, UnsignedField(collection, "period_ns")));
            const auto observedGap = UnsignedField(Object(counter, "summary"), "gap_threshold_ns");
            const auto gapLimit = observedGap ? observedGap : std::max<uint64_t>(requested * 4, 3000000);
            uint64_t previous = 0;
            for (const auto& point : Array(counter, "samples")) {
                const auto time = UnsignedField(point, "timestamp_boot_ns");
                const auto value = FiniteField(point, "value", std::numeric_limits<double>::quiet_NaN());
                if (time < window.start || time > window.end || !std::isfinite(value)) continue;
                const auto ms = RelativeTime(time, window.start);
                if (previous && time - previous > gapLimit) { x.push_back(ms); y.push_back(std::numeric_limits<double>::quiet_NaN()); }
                x.push_back(ms); y.push_back(value); previous = time;
            }
        }
    }
    if (!fit && high > low && !x.empty()) {
        auto first = std::lower_bound(x.begin(), x.end(), low), end = std::upper_bound(x.begin(), x.end(), high);
        const auto visible = size_t(end - first);
        if (first != x.begin()) --first;
        if (end != x.end()) ++end;
        const auto beginIndex = size_t(first - x.begin()), endIndex = size_t(end - x.begin());
        auto reduced = ReducePerformanceSeries(std::vector<double>(x.begin() + beginIndex, x.begin() + endIndex),
            std::vector<double>(y.begin() + beginIndex, y.begin() + endIndex), budget);
        reduced.sourcePoints = visible; return reduced;
    }
    return ReducePerformanceSeries(x, y, budget);
}
std::string Coverage(const Json& result, const Json& metric) {
    if (!metric.contains("counter_id")) return "valid GPU samples";
    double minimum = 1; bool found = false;
    for (const auto& run : result.at("runs")) {
        if (!CounterWindow(run).Valid()) continue;
        const auto& collection = Object(run, "counters");
        for (const auto& counter : Array(collection, "counters"))
            if (counter.at("id") == metric.at("counter_id")) {
                minimum = std::min(minimum, FiniteField(Object(counter, "summary"), "coverage_estimate")); found = true;
            }
    }
    char buffer[48]; std::snprintf(buffer, sizeof(buffer), "%.1f%% estimate", minimum * 100);
    return found ? buffer : "unavailable";
}
void PlotMetric(const Json& result, const Json& metric, bool fit,
                double& low, double& high, size_t& reducedPoints, std::map<std::string, PerformanceSeries>& cache) {
    const auto title = metric.at("name").get<std::string>();
    const auto scale=MetricScale(metric);
    const auto& runs = Array(result, "runs");
    const size_t budget = std::max<size_t>(8, 1024 / std::max<size_t>(1, runs.size()));
    const auto getSeries = [&](const Json& run) -> const PerformanceSeries& {
        std::ostringstream key; key.precision(7);
        key << metric.at("key").get<std::string>() << '/' << run.value("variant", "baseline") << '/' << run.value("index", 1)
            << '/' << fit << '/' << low << '/' << high << '/' << budget;
        const auto found = cache.find(key.str()); if (found != cache.end()) return found->second;
        if (cache.size() > 256) cache.clear();
        auto series=SeriesFor(run, metric, fit, low, high, budget);
        for(auto& value:series.y)value/=scale.divisor;
        return cache.emplace(key.str(),std::move(series)).first->second;
    };
    ImGui::PushID(metric.at("key").get<std::string>().c_str());
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.02f, 0.10f));
    const float height=std::max(110.0f,165.0f-ImGui::GetTextLineHeightWithSpacing());
    if (ImPlot::BeginPlot(title.c_str(), ImVec2(-1, height), ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes(nullptr, scale.unit.c_str());
        ImPlot::SetupAxisLinks(ImAxis_X1, &low, &high);
        if (fit) ImPlot::SetupAxisLimits(ImAxis_X1, low, high, ImPlotCond_Always);
        // Keep the full metric range visible; wheel/pan input changes time only.
        double minimum=std::numeric_limits<double>::infinity(),maximum=-minimum;
        for(const auto* variant:{"baseline","modified"})for(const auto* field:{"min","max","mean"}){
            const auto value=Statistic(metric,variant,field);
            if(value.is_number()){const auto y=value.get<double>()/scale.divisor;minimum=std::min(minimum,y);maximum=std::max(maximum,y);}
        }
        if(!std::isfinite(minimum)||!std::isfinite(maximum)){minimum=0;maximum=1;}
        const double padding=maximum>minimum?(maximum-minimum)*.1:std::max(std::abs(maximum)*.05,.001);
        ImPlot::SetupAxisLimits(ImAxis_Y1,minimum-padding,maximum+padding,ImPlotCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g");
        for (const auto& run : runs) {
            const bool modified = run.value("variant", "baseline") == "modified";
            const auto& series = getSeries(run); if (series.x.empty()) continue;
            reducedPoints += series.x.size();
            const auto label = run.value("variant", "baseline") + " " + std::to_string(run.value("index", 1));
            ImPlotSpec style; style.LineWeight = 1.5f;
            style.LineColor = modified ? ImVec4(0.35f, 0.85f, 0.53f, runs.size() > 2 ? .6f : 1) : ImVec4(0.26f, 0.64f, 0.98f, runs.size() > 2 ? .6f : 1);
            style.Marker = series.pointsOnly ? ImPlotMarker_Square : metric.at("key") == "gpu_time" ||
                (series.sourcePoints <= 64 && runs.size() <= 8) ? ImPlotMarker_Circle : ImPlotMarker_None;
            style.MarkerSize = 2;
            if (series.pointsOnly) ImPlot::PlotScatter(label.c_str(), series.x.data(), series.y.data(), int(series.x.size()), style);
            else ImPlot::PlotLine(label.c_str(), series.x.data(), series.y.data(), int(series.x.size()), style);
            const auto average = Statistic(metric, modified ? "modified" : "baseline", "mean");
            if (average.is_number()) {
                ImPlot::HideNextItem(ImPlot::IsLastItemHidden(), ImPlotCond_Always);
                const double xs[]{0, WindowLengthMs(run)}, ys[]{average.get<double>()/scale.divisor, average.get<double>()/scale.divisor};
                style.LineWeight = 1; style.Marker = ImPlotMarker_None; style.LineColor.w = .35f; style.Flags = ImPlotItemFlags_NoLegend;
                ImPlot::PlotLine((label + " average").c_str(), xs, ys, 2, style);
            }
        }
        if(metric.at("key")=="gpu_time"&&ImPlot::IsAxisHovered(ImAxis_X1)){
            ImGui::SetTooltip("Time from frame GPU start (milliseconds). Wheel zooms time on all plots.");
        }else if (ImPlot::IsPlotHovered()) {
            const auto mouse = ImPlot::GetPlotMousePos(); ImGui::BeginTooltip();
            ImGui::Text("%s | %.4f ms", title.c_str(), mouse.x);
            size_t shown = 0;
            for (const auto& run : runs) {
                const auto& series = getSeries(run); if (series.x.empty()) continue;
                if (++shown > 8) break;
                auto at = std::lower_bound(series.x.begin(), series.x.end(), mouse.x);
                size_t index = at == series.x.end() ? series.x.size() - 1 : size_t(at - series.x.begin());
                if (index && std::abs(series.x[index - 1] - mouse.x) < std::abs(series.x[index] - mouse.x)) --index;
                if (std::isfinite(series.y[index])) ImGui::Text("%s %d: %s %s", run.value("variant", "baseline").c_str(), run.value("index", 1), FormatPerformanceValue(series.y[index],{1,scale.unit}).c_str(), scale.unit.c_str());
            }
            ImGui::EndTooltip();
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar(); ImGui::PopID();
}

}

void Workspace::OpenPerformance() {
    if (!ImGui::FindWindowSettingsByID(ImHashStr("Perf"))) {
        if (const auto* legacy = ImGui::FindWindowSettingsByID(ImHashStr("GPU Performance"))) {
            const auto saved = *legacy; // Creating settings can relocate the settings storage.
            auto* current = ImGui::CreateNewWindowSettings("Perf");
            const auto id = current->ID; *current = saved; current->ID = id;
            current->WantApply = true; current->WantDelete = false;
        }
    }
    const bool alreadyOpen = perfPanelOpen_;
    perfPanelOpen_ = openPerf_ = true;
    if (alreadyOpen) return;
    if (perfFixture_) { perfFitPlots_ = true; }
    if (perfResult_.is_null()) perfSettingsRequest_ = 1;
    if (pendingDraw_ && selectedExecution_ >= 0) executionStatuses_[selectedExecution_] = {PanelState::Inactive, "Preview paused for GPU profiling"};
    pendingDraw_ = false; pendingFrame_ = 0; thumbnailQueue_.clear();
    if (capture_) {
        perfFrame_ = selectedExecution_ >= 0 ? int(capture_->executions[selectedExecution_].frame) :
            selectedEvent_ < capture_->events.size() ? int(capture_->events[selectedEvent_].frame) : std::max(1, frame_);
    }
    if (capture_ && !perfFixture_ && (!Busy() || autoReplayRunning_)) StartPerformance(true);
}

#ifdef IMGUI_TEMPLATE_TESTS
void Workspace::ConfigurePerformanceCaptureForTests(std::shared_ptr<Capture> capture, uint32_t frame, const std::string& serial) {
    capture_ = std::move(capture); frame_ = perfFrame_ = int(frame); selectedExecution_ = -1;
    const auto at = std::find_if(capture_->events.begin(), capture_->events.end(), [&](const Event& event) { return event.frame == frame; });
    selectedEvent_ = at == capture_->events.end() ? 0 : size_t(at - capture_->events.begin());
    replayMode_ = ReplayMode::Android; androidSerial_ = serial; perfSwapchain_ = 0; perfCollectCounters_ = false; perfRuns_ = 1;
    perfPeriodUs_ = 1000;
    activeOverrides_.clear(); perfFixture_ = false; perfResult_ = Json(); perfCapabilities_ = Json();
    previewPaused_ = true; pendingDraw_ = false; pendingFrame_ = 0; thumbnailQueue_.clear(); filterDirty_ = true; ++selectionGeneration_;
}
void Workspace::StartPerformanceJobFixture(uint32_t durationMs) {
    perfResult_ = Json(); const auto generation = selectionGeneration_;
    Start("Performance fixture", [this, generation, durationMs] {
        for (uint32_t i = 0; i < durationMs / 10 && !cancel_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        WorkspaceResult result; result.perfOperation = true; result.selectionGeneration = generation;
        result.perfData = {{"schema", "gfxrecon-perf/1"}, {"status", "complete"}, {"mode", "single-frame"}, {"policy", {{"frame", 1}}},
            {"runs_requested", 1}, {"metrics", Json::array()}, {"runs", Json::array()}};
        result.message = "Performance fixture finished"; return result;
    }, JobKind::Perf);
}
#endif

void Workspace::StartPerformance(bool discover) {
    if (!capture_) return;
    const auto capture = capture_; const auto tools = tools_; const auto replayMode = replayMode_;
    const auto serial = androidSerial_; const auto generation = selectionGeneration_;
    PerfSessionRequest request; request.capture = capture; request.tools = tools;
    request.memoryTranslation = memoryTranslation_;
    request.frame = static_cast<uint32_t>(perfFrame_); request.runs = static_cast<uint32_t>(std::max(1, perfRuns_));
    request.counterPeriodNs = static_cast<uint64_t>(std::max(1, perfPeriodUs_)) * 1000;
    request.counterProfile = perfCollectCounters_ ? "all" : "none";
    request.swapchainMode = SwapchainName(perfSwapchain_); request.screenOff = phoneScreenOff_;
    request.cacheDirectory = settingsDir_ / "replay-cache";
    request.output = sessionsDir_ / "perf" / std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    if (perfCompare_) request.overrides = activeOverrides_;
    { std::lock_guard<std::mutex> lock(perfProgressMutex_); perfProgress_ = discover ? "Checking performance capabilities" : "Loading captured state"; }
    if (!discover) { perfResult_ = Json(); perfPlotMetrics_.clear(); perfPlotsInitialized_ = false; }
    request.progress = [this](const std::string& progress) {
        { std::lock_guard<std::mutex> lock(perfProgressMutex_);
          if (perfProgress_ == progress) return;
          perfProgress_ = progress; }
        LOGI("perf") << progress;
    };
    Start(discover ? "Checking GPU performance capabilities" : "Measuring GPU performance", [this, request, capture, tools, replayMode, serial, generation, discover]() mutable {
        WorkspaceResult result; result.perfOperation = true; result.perfProbe = discover; result.selectionGeneration = generation;
        try {
            const auto adb = tools.find("adb");
            request.target = ResolveReplayTarget(replayMode, capture->android, serial, adb == tools.end() ? "" : adb->second, &cancel_);
            result.perfData = discover ? DiscoverPerfCapabilities(request, &cancel_) : RunPerfSession(request, &cancel_);
            result.failed = discover && !result.perfData.value("available", false);
            result.message = discover ? result.perfData.value("detail", "GPU capability discovery completed") : "GPU performance report: " + request.output.u8string();
        } catch (const std::exception& error) { result.failed = true; result.message = error.what(); }
        return result;
    }, JobKind::Perf, request.frame);
}

void Workspace::LoadPerformanceReport(const fs::path& path) {
    const auto generation = selectionGeneration_;
    { std::lock_guard<std::mutex> lock(perfProgressMutex_); perfProgress_ = "Opening saved Perf report"; }
    Start("Opening Perf report", [this, path, generation] {
        WorkspaceResult result; result.perfOperation = true; result.selectionGeneration = generation;
        try {
            std::ifstream input(path);
            auto loaded = Json::parse(input, [this](int, Json::parse_event_t, Json&) {
                if (cancel_) throw std::runtime_error("Perf report opening cancelled"); return true;
            });
            if (loaded.value("schema", "") != "gfxrecon-perf/1" || !loaded.contains("metrics") || !loaded.contains("runs"))
                throw std::runtime_error("Unsupported Perf report format");
            result.perfData = std::move(loaded); result.message = "Opened Perf report: " + path.u8string();
        } catch (const std::exception& error) { result.failed = true; result.message = error.what(); }
        return result;
    }, JobKind::Perf);
}

void Workspace::DrawPerformance() {
    if (!perfPanelOpen_) return;
    if (openPerf_) {
        const auto* window=ImGui::FindWindowByName("Perf");
        const auto* settings=ImGui::FindWindowSettingsByID(ImHashStr("Perf"));
        const auto dockId=window?window->DockId:settings?settings->DockId:0;
        auto* node=ImGui::DockBuilderGetNode(dockId);
        // Newly opened and legacy floating panels join the inspector tabs.
        // Keep an existing docked layout without resetting the other panels.
        if(!node||!ImGui::DockNodeGetRootNode(node)->IsDockSpace()){
            for(const auto* name:{"Pipeline","Image","Shader","Resources","Mesh"}){
                const auto* anchor=ImGui::FindWindowByName(name);
                auto* target=anchor?ImGui::DockBuilderGetNode(anchor->DockId):nullptr;
                if(target&&ImGui::DockNodeGetRootNode(target)->IsDockSpace()){
                    ImGui::SetNextWindowDockID(target->ID,ImGuiCond_Always);break;
                }
            }
        }
        ImGui::SetNextWindowFocus(); openPerf_ = false;
    }
    const auto viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(viewport->WorkSize.x * 0.94f, ImGui::GetFontSize() * 66), viewport->WorkSize.y * 0.90f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(680, 360), ImVec2(viewport->WorkSize.x * 0.97f, viewport->WorkSize.y * 0.94f));
    if (!ImGui::Begin("Perf", &perfPanelOpen_)) { ImGui::End(); return; }
    if (!capture_ && !perfResult_.is_object()) ImGui::TextDisabled("Open a capture or a saved report.");
    bool settingsChanged = false, capabilitiesChanged = false;
    if (perfSettingsRequest_) { ImGui::SetNextItemOpen(perfSettingsRequest_ > 0, ImGuiCond_Always); perfSettingsRequest_ = 0; }
    if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BeginDisabled(Busy());
    const auto sameLineFor = [](float width, const char* label) {
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        const float needed = width + ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemInnerSpacing.x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + needed <= right) ImGui::SameLine();
    };
    ImGui::SetNextItemWidth(70); capabilitiesChanged |= ImGui::InputInt("Frame", &perfFrame_, 0);
    sameLineFor(ImGui::GetFrameHeight(), "Counters"); capabilitiesChanged |= ImGui::Checkbox("Counters", &perfCollectCounters_);
    ImGui::SetItemTooltip("Collect all provider counters supported together; hardware limits and missing samples remain explicit in the report.");
    sameLineFor(ImGui::GetFrameHeight(), "Shader Override"); ImGui::Checkbox("Shader Override", &perfCompare_);
    ImGui::SetItemTooltip("Compare applied shader overrides against the original shaders.\n%zu applied modules; apply editor changes before starting.", activeOverrides_.size());
    sameLineFor(60, "Runs"); ImGui::SetNextItemWidth(60); settingsChanged |= ImGui::InputInt("Runs", &perfRuns_, 0);
    sameLineFor(110, "Swapchain");
    ImGui::SetNextItemWidth(110); capabilitiesChanged |= ImGui::Combo("Swapchain", &perfSwapchain_, "Offscreen\0Virtual\0Captured\0");
    if (perfCollectCounters_) {
        sameLineFor(100, "Counter us");
        ImGui::SetNextItemWidth(100); capabilitiesChanged |= ImGui::InputInt("Counter us", &perfPeriodUs_, 0);
        ImGui::SetItemTooltip("Requested interval; device support and actual sample gaps are reported.");
    }
    ImGui::EndDisabled();
    }
    if (settingsChanged || capabilitiesChanged) {
        if (capabilitiesChanged) perfCapabilities_ = Json();
        if (!Busy()) panelStatuses_["Perf"] = {PanelState::Inactive, capabilitiesChanged ?
            "Settings changed; ready to measure. Counters are discovered during replay." : "Run count changed; ready to measure."};
    }
    std::string invalid;
    if (capture_) {
        if (perfFrame_ < 1 || uint64_t(perfFrame_) > capture_->frames) invalid = "Performance frame is outside the capture.";
        else if (perfRuns_ < 1 || perfRuns_ > 50) invalid = "Performance runs must be 1..50.";
        else if (perfCollectCounters_ && (perfPeriodUs_ < 1 || perfPeriodUs_ > 1000000)) invalid = "Performance counter interval must be 1..1000000 us.";

    }
    if (invalid != perfValidationError_) {
        const auto previous = perfValidationError_; perfValidationError_ = invalid;
        if (!invalid.empty()) {
            panelStatuses_["Perf"] = {PanelState::Failed, invalid}; LOGE("perf") << invalid;
        } else if (panelStatuses_["Perf"].detail == previous)
            panelStatuses_["Perf"] = {PanelState::Inactive, "Ready to probe GPU performance capabilities."};
    }
    const bool parametersValid = capture_ && invalid.empty();
    ImGui::BeginDisabled(Busy() || !parametersValid);
    if (ImGui::Button("Probe")) StartPerformance(true);
    ImGui::EndDisabled(); ImGui::SameLine();
    const bool running = Busy() && perfJob_;
    ImGui::BeginDisabled(!running && (Busy() || !parametersValid));
    if (ImGui::Button(running ? "Cancel" : "Start")) {
        if (running) CancelJob(); else StartPerformance(false);
    }
    ImGui::EndDisabled();
    if (!running && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (Busy()) ImGui::SetTooltip("Wait for or cancel the active job.");
        else if (!capture_) ImGui::SetTooltip("Open a capture to measure.");
        else if (!invalid.empty()) ImGui::SetTooltip("%s", invalid.c_str());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(Busy());
    if (ImGui::Button("Open..."))
        if (const auto path = tpl::OpenFileDialog("Perf report", "json")) LoadPerformanceReport(fs::u8path(*path));
    ImGui::SetItemTooltip("Open a saved result.json. New runs are displayed automatically.");
    ImGui::EndDisabled(); ImGui::SameLine();
    DeviceInfoLabel();
    if (ImGui::IsItemHovered()) {
        BeginPerfTooltip();
        const auto& target = Object(perfResult_, "target");
        ImGui::TextWrapped("Device: %s", target.value("label", androidSerial_.empty() ? "Automatic" : androidSerial_).c_str());
        for (const auto& run : Array(perfResult_, "runs")) {
            const auto& device = Object(Object(run, "gpu"), "device");
            ImGui::TextWrapped("GPU: %s", device.value("device_name", "unavailable").c_str());
            ImGui::TextWrapped("Driver: %s (%s)", device.value("driver_name", Json{}).is_string() ?
                device.at("driver_name").get_ref<const std::string&>().c_str() : "unavailable", Number(device.value("driver_version", Json{})).c_str());
            break;
        }
        ImGui::TextWrapped("GPU timing: device timestamps. Counters: device-wide frame-window samples; approximate alignment, with few samples in short frames.");
        const Json* counterStatus = &Object(perfCapabilities_, "counters");
        if (!Array(perfResult_, "runs").empty()) counterStatus = &Object(Array(perfResult_, "runs").front(), "counters");
        if ((counterStatus->value("status", "") == "unavailable" || counterStatus->value("status", "") == "sparse") &&
            counterStatus->contains("reason") && counterStatus->at("reason").is_string())
            ImGui::TextWrapped("%s", CompactText(counterStatus->at("reason").get_ref<const std::string&>()).c_str());
        EndPerfTooltip();
    }
    ImGui::Separator();
    if (perfResult_.is_object()) try {
        const auto& metrics = perfResult_.at("metrics");
        const bool resetSelection=!perfPlotsInitialized_;
        if(resetSelection)perfSelectionAnchor_.clear();
        if (!perfPlotsInitialized_) {
            perfPlotMetrics_.clear();
            for (const auto* key : {"counter_26", "counter_132", "counter_143", "counter_145"})
                if (std::any_of(metrics.begin(), metrics.end(), [&](const Json& row) { return row.at("key") == key; })) perfPlotMetrics_.insert(key);
            if (perfPlotMetrics_.empty()) for (const auto& metric : metrics)
                if (metric.at("key") != "gpu_time" && perfPlotMetrics_.size() < 4) perfPlotMetrics_.insert(metric.at("key").get<std::string>());
            if(std::any_of(metrics.begin(),metrics.end(),[](const Json& row){return row.at("key")=="gpu_time";}))perfPlotMetrics_.insert("gpu_time");
            perfPlotsInitialized_ = true;
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize()*12);
        if(ImGui::InputTextWithHint("##perf-metric-filter","Filter...",perfMetricFilter_.InputBuf,IM_ARRAYSIZE(perfMetricFilter_.InputBuf)))perfMetricFilter_.Build();
        ImGui::SetItemTooltip("Filter metrics and graphs by name or unit.\nClick toggles; Shift selects a range; right-click selects all/none.");
        const auto matches=[&](const Json& metric){
            const auto text=metric.at("name").get<std::string>()+" "+metric.at("unit").get<std::string>()+" "+MetricScale(metric).unit;
            return perfMetricFilter_.PassFilter(text.c_str());
        };
        std::vector<const Json*> rows;
        for (const auto& metric : metrics) if(matches(metric))rows.push_back(&metric);
        const auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        const bool paired=std::any_of(Array(perfResult_,"runs").begin(),Array(perfResult_,"runs").end(),[](const Json& run){return run.value("variant","baseline")=="modified";});
        if (ImGui::BeginTable("perf-metrics", 5, flags, ImVec2(0, std::min(200.0f, 44.0f + float(std::max<size_t>(1,rows.size())) * ImGui::GetTextLineHeightWithSpacing())))) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_DefaultSort, 2.7f);
            ImGui::TableSetupColumn("Unit", 0, 1.2f);
            ImGui::TableSetupColumn("Mean", 0, 1); ImGui::TableSetupColumn("Min", 0, 1); ImGui::TableSetupColumn("Max", 0, 1);
            ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            const char* headers[]{"Metric","Unit","Mean","Min","Max"};
            for(int column=0;column<5;++column){
                ImGui::TableSetColumnIndex(column);ImGui::TableHeader(headers[column]);
                if(column>=2&&paired)ImGui::SetItemTooltip("Original / shader override values. Hover a metric for sample statistics and coverage.");
            }
            if (const auto* sort = ImGui::TableGetSortSpecs(); sort && sort->SpecsCount) {
                const auto column = sort->Specs[0].ColumnIndex; const bool descending = sort->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                std::stable_sort(rows.begin(), rows.end(), [&](const Json* a, const Json* b) {
                    int comparison = 0;
                    if (column == 0 || column == 1) comparison = a->at(column == 0 ? "name" : "unit").get<std::string>().compare(b->at(column == 0 ? "name" : "unit").get<std::string>());
                    else { const char* field=column==3?"min":column==4?"max":"mean";const auto before = SortNumber(*a, "baseline", field), after = SortNumber(*b, "baseline", field); comparison = before < after ? -1 : before > after ? 1 : 0; }
                    return descending ? comparison > 0 : comparison < 0;
                });
            }
            ImGuiListClipper clipper; clipper.Begin(int(rows.size()));
            while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& metric = *rows[row]; const auto key = metric.at("key").get<std::string>();
                const auto scale=MetricScale(metric);
                ImGui::PushID(key.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn();
                const bool selected=perfPlotMetrics_.count(key)!=0;
                if(ImGui::Selectable((metric.at("name").get<std::string>()+"###metric-row").c_str(),selected,ImGuiSelectableFlags_SpanAllColumns)){
                    if(ImGui::GetIO().KeyShift){
                        const auto anchor=std::find_if(rows.begin(),rows.end(),[&](const Json* item){return item->at("key")==perfSelectionAnchor_;});
                        const int first=anchor==rows.end()?row:int(anchor-rows.begin());
                        for(int index=std::min(first,row);index<=std::max(first,row);++index)perfPlotMetrics_.insert(rows[index]->at("key").get<std::string>());
                        if(anchor==rows.end())perfSelectionAnchor_=key;
                    }else{
                        if(selected)perfPlotMetrics_.erase(key);else perfPlotMetrics_.insert(key);
                        perfSelectionAnchor_=key;
                    }
                }
                if(ImGui::IsItemHovered()){
                    BeginPerfTooltip();ImGui::TextUnformatted(metric.at("name").get<std::string>().c_str());
                    const auto description=metric.value("description",std::string{});if(!description.empty())ImGui::TextWrapped("%s",CompactText(description).c_str());
                    ImGui::TextUnformatted(metric.contains("counter_id")?"Device-wide samples within the GPU window; approximate attribution.":"Valid GPU timestamp durations.");
                    for(const auto* variant:{"baseline","modified"})if(metric.contains(variant)&&metric.at(variant).is_object()){
                        ImGui::Text("%s: N %s | P95 %s | SD %s %s",std::string(variant)=="baseline"?"Original":"Shader override",
                            Number(Statistic(metric,variant,"count")).c_str(),ScaledNumber(Statistic(metric,variant,"p95"),scale).c_str(),
                            ScaledNumber(Statistic(metric,variant,"stddev"),scale).c_str(),scale.unit.c_str());
                    }
                    if(metric.contains("counter_id"))ImGui::Text("Coverage: %s",Coverage(perfResult_,metric).c_str());
                    if(metric.value("delta_percent",Json{}).is_number())ImGui::Text("Change: %s%%",Number(metric.at("delta_percent")).c_str());
                    for(const auto& reason:Array(metric,"quality"))if(reason.is_string())ImGui::TextWrapped("%s",CompactText(reason.get_ref<const std::string&>()).c_str());
                    EndPerfTooltip();
                }
                ImGui::TableNextColumn(); ImGui::TextUnformatted(scale.unit.c_str());
                ImGui::SetItemTooltip("%s", metric.at("unit").get<std::string>().c_str());
                for(const auto* field:{"mean","min","max"}){
                    ImGui::TableNextColumn();const auto original=ScaledNumber(Statistic(metric,"baseline",field),scale);
                    if(paired)ImGui::Text("%s / %s",original.c_str(),ScaledNumber(Statistic(metric,"modified",field),scale).c_str());
                    else ImGui::TextUnformatted(original.c_str());
                    if(paired)ImGui::SetItemTooltip("Original / shader override (%s)",scale.unit.c_str());
                }
                ImGui::PopID();
            }
            if(rows.empty()){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextDisabled("No matching metrics.");}
            if(ImGui::BeginPopupContextWindow("perf-table-menu")){
                if(ImGui::MenuItem("Select all")){for(const auto* metric:rows)perfPlotMetrics_.insert(metric->at("key").get<std::string>());perfSelectionAnchor_.clear();}
                if(ImGui::MenuItem("Deselect all")){for(const auto* metric:rows)perfPlotMetrics_.erase(metric->at("key").get<std::string>());perfSelectionAnchor_.clear();}
                ImGui::EndPopup();
            }
            ImGui::EndTable();
        }
        ImGui::BeginChild("perf-plots", ImVec2(0, std::max(120.0f, ImGui::GetContentRegionAvail().y)), ImGuiChildFlags_Borders);
        size_t points = 0;
        if (perfFitPlots_) { perfPlotMin_ = 0; perfPlotMax_ = .001; for (const auto& run : Array(perfResult_, "runs")) perfPlotMax_ = std::max(perfPlotMax_, WindowLengthMs(run)); }
        const auto plot = [&](const Json& metric) { PlotMetric(perfResult_, metric, perfFitPlots_, perfPlotMin_, perfPlotMax_, points, perfPlotCache_); };
        for (const auto& metric : metrics) if (metric.at("key")=="gpu_time"&&perfPlotMetrics_.count("gpu_time")&&matches(metric)) plot(metric);
        for (const auto& metric : metrics) if (metric.at("key")!="gpu_time"&&perfPlotMetrics_.count(metric.at("key").get<std::string>())&&matches(metric)) plot(metric);
        perfFitPlots_ = false;
        if (ImGui::BeginPopupContextWindow("perf-plot-menu")) {
            if (ImGui::MenuItem("Fit")) perfFitPlots_ = true;
            ImGui::EndPopup();
        }
        ImGui::EndChild();
    } catch (const std::exception& error) {
        const std::string message = std::string("Cannot display this performance result: ") + error.what();
        if (panelStatuses_["Perf"].detail != message) {
            panelStatuses_["Perf"] = {PanelState::Failed, message};
            LOGE("perf") << message;
        }
    }
    ImGui::End();
}
}
