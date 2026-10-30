#include "analysis/export.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <set>
#include <stdexcept>

namespace gfx {
namespace {
constexpr const char* kSchema = "gfxrecon-dump/1";
uint64_t U(const Json& j, const char* key) {
    if (!j.is_object()) return 0;
    const auto it = j.find(key);
    return it != j.end() && (it->is_number_unsigned() || it->is_number_integer()) && it->get<int64_t>() >= 0 ? it->get<uint64_t>() : 0;
}
bool Starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
std::string Num(const char* format, unsigned a) { char buffer[32]; std::snprintf(buffer, sizeof(buffer), format, a); return buffer; }
size_t EventByBlock(const Capture& capture, uint64_t block) {
    const auto it = std::lower_bound(capture.events.begin(), capture.events.end(), block, [](const Event& e, uint64_t b) { return e.block < b; });
    return it != capture.events.end() && it->block == block ? static_cast<size_t>(it - capture.events.begin()) : static_cast<size_t>(-1);
}
bool IsDepthFormat(const std::string& f) { return Starts(f, "VK_FORMAT_D") || Starts(f, "VK_FORMAT_S8"); }
const Json* CreateInfo(const Capture& capture, uint64_t id) {
    const auto it = capture.objects.find(id); return it == capture.objects.end() ? nullptr : &it->second.createInfo;
}
uint64_t ViewImage(const Capture& capture, uint64_t view) { const auto* info = CreateInfo(capture, view); return info ? U(*info, "image") : 0; }
std::string ViewFormat(const Capture& capture, uint64_t view) {
    const auto* info = CreateInfo(capture, view);
    return info && info->contains("format") && (*info)["format"].is_string() ? (*info)["format"].get<std::string>() : std::string{};
}
Json ReadRecord(const Capture& capture, size_t event, std::istream* stream) {
    if (!stream) return capture.ReadEvent(event);
    stream->clear(); stream->seekg(static_cast<std::streamoff>(capture.events.at(event).offset));
    std::string line;
    if (!std::getline(*stream, line)) throw std::runtime_error("Capture record is no longer readable");
    return Json::parse(line);
}
// Render-target description of one execution: extent, attachments with formats and load/store ops.
Json Attachments(const Capture& capture, const Execution& execution) {
    const auto& rt = execution.renderTargets;
    Json result{{"attachments", Json::array()}};
    if (!rt.is_object() || rt.empty()) { result["unavailable"] = "no render targets are bound at this command"; return result; }
    auto extent = [&](const Json& area) {
        const auto e = area.is_object() && area.contains("extent") ? area["extent"] : Json();
        if (e.is_object()) result["extent"] = {{"width", U(e, "width")}, {"height", U(e, "height")}};
    };
    if (rt.contains("renderPass")) {
        result["api"] = "renderpass";
        result["render_pass"] = U(rt, "renderPass"); result["framebuffer"] = U(rt, "framebuffer");
        extent(rt.value("renderArea", Json()));
        const auto* pass = CreateInfo(capture, U(rt, "renderPass")); const auto* fb = CreateInfo(capture, U(rt, "framebuffer"));
        if (fb && !result.contains("extent")) result["extent"] = {{"width", U(*fb, "width")}, {"height", U(*fb, "height")}};
        if (!pass || !pass->contains("pAttachments") || !(*pass)["pAttachments"].is_array()) { result["unavailable"] = "render pass create info is not in the capture index"; return result; }
        const auto views = fb ? fb->value("pAttachments", Json::array()) : Json::array();
        size_t i = 0;
        for (const auto& a : (*pass)["pAttachments"]) {
            Json item{{"index", i}, {"format", a.value("format", Json())}, {"samples", a.value("samples", Json())}, {"loadOp", a.value("loadOp", Json())}, {"storeOp", a.value("storeOp", Json())},
                {"stencilLoadOp", a.value("stencilLoadOp", Json())}, {"stencilStoreOp", a.value("stencilStoreOp", Json())},
                {"initialLayout", a.value("initialLayout", Json())}, {"finalLayout", a.value("finalLayout", Json())}};
            const auto format = a.value("format", std::string{});
            item["role"] = IsDepthFormat(format) ? "depth" : "color";
            if (views.is_array() && i < views.size() && views[i].is_number()) { item["view"] = views[i]; item["image"] = ViewImage(capture, views[i].get<uint64_t>()); }
            result["attachments"].push_back(std::move(item)); ++i;
        }
    } else {
        result["api"] = "rendering";
        extent(rt);
        auto add = [&](const Json& a, const char* role, size_t index) {
            if (!a.is_object()) return;
            const auto view = U(a, "imageView");
            result["attachments"].push_back({{"index", index}, {"role", role}, {"view", view}, {"image", ViewImage(capture, view)}, {"format", ViewFormat(capture, view)},
                {"loadOp", a.value("loadOp", Json())}, {"storeOp", a.value("storeOp", Json())}, {"resolveImageView", a.value("resolveImageView", Json())}});
        };
        size_t index = 0;
        for (const auto& a : rt.value("pColorAttachments", Json::array())) add(a, "color", index++);
        add(rt.value("pDepthAttachment", Json()), "depth", index++);
        add(rt.value("pStencilAttachment", Json()), "stencil", index++);
    }
    return result;
}
void ImageViews(const Json& value, std::set<uint64_t>& out, unsigned depth = 0) {
    if (depth > 12) return;
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() == "imageView" && it.value().is_number_unsigned()) out.insert(it.value().get<uint64_t>()); else ImageViews(it.value(), out, depth + 1);
        }
    } else if (value.is_array()) for (const auto& v : value) ImageViews(v, out, depth + 1);
}
} // namespace

std::string Pass::Directory() const { return Num("%03u-", ordinal) + kind + "-b" + std::to_string(firstBlock); }
std::string FrameDirectory(uint32_t frame) { return Num("frames/%04u", frame); }
std::string PipelinePath(uint64_t pipeline) { return "pipelines/" + std::to_string(pipeline) + ".json"; }
std::string ShaderPath(uint64_t module) { return "shaders/" + std::to_string(module); }
std::string DrawDirectory(const Capture& capture, size_t execution, uint32_t ordinal) {
    const auto& event = capture.events[capture.executions[execution].event];
    return Num("%04u-b", ordinal) + std::to_string(event.block) + "-" + event.name;
}
Json StaticDumpSkipped() { return {{"status", "skipped"}, {"reason", "static dump (--dynamic none)"}}; }

std::vector<FramePasses> GroupPasses(const Capture& capture) {
    std::vector<FramePasses> frames(std::max<size_t>(capture.frames, 1));
    for (size_t i = 0; i < frames.size(); ++i) frames[i].frame = static_cast<uint32_t>(i + 1);
    {
        std::vector<std::pair<uint32_t, uint32_t>> swapchains;
        std::ifstream stream(capture.jsonl, std::ios::binary);
        for (size_t i = 0; i < capture.events.size(); ++i) if (capture.events[i].name == "vkCreateSwapchainKHR") {
            try {
                const auto info = ReadRecord(capture, i, &stream)["function"]["args"]["pCreateInfo"]["imageExtent"];
                swapchains.emplace_back(static_cast<uint32_t>(U(info, "width")), static_cast<uint32_t>(U(info, "height")));
            } catch (...) {}
        }
        std::sort(swapchains.begin(), swapchains.end()); swapchains.erase(std::unique(swapchains.begin(), swapchains.end()), swapchains.end());
        for (auto& f : frames) f.swapchains = swapchains;
    }
    struct Key {
        uint64_t submit = 0, begin = 0; uint32_t ordinal = 0; std::vector<uint64_t> scope;
        bool operator==(const Key& o) const { return submit == o.submit && begin == o.begin && ordinal == o.ordinal && scope == o.scope; }
    };
    Key previous; size_t previousFrame = 0; bool open = false;
    std::vector<uint32_t> counts(frames.size(), 0);
    for (size_t index = 0; index < capture.executions.size(); ++index) {
        const auto& execution = capture.executions[index];
        const size_t slot = std::min<size_t>(execution.frame ? execution.frame - 1 : 0, frames.size() - 1);
        Key key; key.submit = execution.submit;
        if (execution.invocations.empty()) { key.begin = execution.begin; key.scope = execution.renderScope; }
        else {
            key.begin = execution.invocations[0].begin; key.ordinal = execution.invocations[0].ordinal;
            for (const auto& invocation : execution.invocations) if (!invocation.renderScope.empty()) { key.scope = invocation.renderScope; break; }
        }
        if (!open || previousFrame != slot || !(key == previous)) {
            Pass pass; pass.frame = frames[slot].frame; pass.ordinal = static_cast<uint32_t>(frames[slot].passes.size());
            pass.submit = key.submit; pass.begin = key.begin; pass.scope = key.scope; pass.firstOrdinal = counts[slot];
            if (key.scope.empty()) { pass.kind = "compute"; pass.firstBlock = capture.events[execution.event].block; }
            else {
                const size_t begin = EventByBlock(capture, key.scope.front());
                pass.kind = begin != static_cast<size_t>(-1) && capture.events[begin].name.find("Rendering") != std::string::npos ? "rendering" : "renderpass";
                pass.firstBlock = key.scope.front();
            }
            frames[slot].passes.push_back(std::move(pass)); previous = key; previousFrame = slot; open = true;
        }
        frames[slot].passes.back().executions.push_back(index); ++counts[slot];
    }
    return frames;
}
DumpIndex DumpIndex::Build(const Capture& capture) {
    DumpIndex index; index.frames = GroupPasses(capture);
    index.draws.resize(capture.executions.size());
    std::map<uint64_t, uint32_t> occurrences;
    for (size_t i = 0; i < capture.executions.size(); ++i) index.draws[i].occurrence = occurrences[capture.events[capture.executions[i].event].block]++;
    for (const auto& frame : index.frames) for (size_t p = 0; p < frame.passes.size(); ++p) {
        const auto& pass = frame.passes[p];
        for (size_t n = 0; n < pass.executions.size(); ++n) {
            auto& draw = index.draws[pass.executions[n]];
            draw.frame = frame.frame; draw.pass = p; draw.execution = pass.executions[n]; draw.ordinal = pass.firstOrdinal + static_cast<uint32_t>(n);
            draw.path = FrameDirectory(frame.frame) + "/passes/" + pass.Directory() + "/draws/" + DrawDirectory(capture, pass.executions[n], draw.ordinal);
        }
    }
    return index;
}
DrawLocation LocateDraw(const Capture& capture, const std::vector<FramePasses>& frames, size_t execution) {
    for (const auto& frame : frames) for (size_t p = 0; p < frame.passes.size(); ++p) {
        const auto& pass = frame.passes[p];
        const auto it = std::find(pass.executions.begin(), pass.executions.end(), execution);
        if (it == pass.executions.end()) continue;
        DrawLocation location; location.frame = frame.frame; location.pass = p; location.execution = execution;
        location.ordinal = pass.firstOrdinal + static_cast<uint32_t>(it - pass.executions.begin());
        location.path = FrameDirectory(frame.frame) + "/passes/" + pass.Directory() + "/draws/" + DrawDirectory(capture, execution, location.ordinal);
        return location;
    }
    throw std::runtime_error("Execution is not part of any pass");
}
Json ClassifyPass(const Capture& capture, const FramePasses& frame, size_t passIndex) {
    const auto& pass = frame.passes.at(passIndex);
    Json evidence = Json::array(); std::string label = "unknown";
    auto note = [&](const std::string& text) { evidence.push_back(text); };
    if (pass.kind == "transfer") { label = "transfer"; note("transfer pseudo-pass outside render scopes"); }
    else if (pass.kind == "compute") { label = "compute"; note("no render scope; " + std::to_string(pass.executions.size()) + " dispatch execution(s) in this submission"); }
    else if (!pass.executions.empty()) {
        const auto& first = capture.executions[pass.executions.front()];
        const auto attachments = Attachments(capture, first);
        size_t colors = 0, depths = 0;
        for (const auto& a : attachments["attachments"]) { if (a.value("role", std::string{}) == "color") ++colors; else ++depths; }
        const bool hasExtent = attachments.contains("extent");
        const uint32_t w = hasExtent ? static_cast<uint32_t>(U(attachments["extent"], "width")) : 0, h = hasExtent ? static_cast<uint32_t>(U(attachments["extent"], "height")) : 0;
        bool swapchainSized = false;
        for (const auto& s : frame.swapchains) swapchainSized |= (s.first == w && s.second == h) || (s.first == h && s.second == w);
        if (hasExtent) note("extent " + std::to_string(w) + "x" + std::to_string(h) + (swapchainSized ? " matches a swapchain extent" : " does not match a swapchain extent"));
        note(std::to_string(colors) + " color and " + std::to_string(depths) + " depth/stencil attachment(s); " + std::to_string(pass.executions.size()) + " draw(s)");
        bool fullscreen = false;
        if (pass.executions.size() == 1) {
            const auto& event = capture.events[first.event];
            if (event.name == "vkCmdDraw" && (!event.state || event.state->value("vertexBuffers", Json::object()).empty())) {
                try {
                    const auto args = capture.ReadEvent(first.event)["function"]["args"];
                    const auto count = U(args, "vertexCount");
                    fullscreen = count == 3 || count == 4;
                    if (fullscreen) note("single non-indexed " + std::to_string(count) + "-vertex draw without vertex buffers (full-screen primitive)");
                } catch (...) {}
            }
        }
        bool samplesEarlier = false;
        if (passIndex > 0) {
            std::set<uint64_t> earlier;
            for (size_t p = 0; p < passIndex; ++p) if (!frame.passes[p].executions.empty() && frame.passes[p].kind != "compute")
                for (const auto& a : Attachments(capture, capture.executions[frame.passes[p].executions.front()])["attachments"]) if (U(a, "image")) earlier.insert(U(a, "image"));
            std::set<uint64_t> views;
            for (const auto e : pass.executions) ImageViews(capture.executions[e].descriptorValues, views);
            for (const auto view : views) if (earlier.count(ViewImage(capture, view))) { samplesEarlier = true; note("samples image " + std::to_string(ViewImage(capture, view)) + " written by an earlier pass"); break; }
        }
        bool sceneBefore = false;
        for (size_t p = 0; p < passIndex; ++p) sceneBefore |= frame.passes[p].executions.size() > 1 && frame.passes[p].kind != "compute";
        const bool lastPasses = passIndex + 2 >= frame.passes.size();
        if (colors == 0 && depths > 0) { label = "shadow"; note("depth-only attachments (no color attachment)"); }
        else if (fullscreen) { label = "postprocess"; note(samplesEarlier || sceneBefore ? "full-screen draw after earlier multi-draw passes" : "full-screen draw (no earlier scene pass in this frame)"); }
        else if (swapchainSized && lastPasses && depths == 0 && pass.executions.size() > 1 && colors > 0) { label = "ui"; note("among the last passes of the frame, draws into a swapchain-sized color target without depth"); }
        else if (swapchainSized && lastPasses && samplesEarlier) { label = "postprocess"; note("among the last passes of the frame and writes a swapchain-sized target from earlier pass output"); }
        else if (colors > 0) { label = "scene"; note(depths > 0 ? "color+depth attachments with multiple draws" : "color-only pass without full-screen or swapchain-last evidence"); }
    }
    return {{"label", label}, {"heuristic", true}, {"evidence", evidence}};
}

Json ExportCapture(const Capture& capture, const std::filesystem::path& source, const std::string& sha) {
    Json result{{"schema", kSchema}, {"source", {{"file_name", source.filename().u8string()}, {"sha256", sha}}}, {"header", capture.header}, {"android", capture.android},
        {"frame_count", capture.frames}, {"event_count", capture.events.size()}, {"execution_count", capture.executions.size()},
        {"limitations", capture.limitations}, {"fingerprint", capture.fingerprint}, {"frames", Json::array()}};
    std::ifstream stream(capture.jsonl, std::ios::binary);
    for (size_t i = 0; i < capture.events.size(); ++i) {
        const auto& name = capture.events[i].name;
        if (name == "vkCreateDevice" && !result.contains("device_extensions")) {
            try { result["device_extensions"] = ReadRecord(capture, i, &stream)["function"]["args"]["pCreateInfo"].value("ppEnabledExtensionNames", Json::array()); } catch (...) {}
        }
        if (name == "vkGetPhysicalDeviceProperties" && !result.contains("physical_device")) {
            try {
                const auto p = ReadRecord(capture, i, &stream)["function"]["args"]["pProperties"];
                result["physical_device"] = {{"deviceName", p.value("deviceName", Json())}, {"apiVersion", p.value("apiVersion", Json())}, {"driverVersion", p.value("driverVersion", Json())},
                    {"vendorID", p.value("vendorID", Json())}, {"deviceID", p.value("deviceID", Json())}};
            } catch (...) {}
        }
    }
    std::vector<uint64_t> first(capture.frames + 1, 0), last(capture.frames + 1, 0), events(capture.frames + 1, 0), draws(capture.frames + 1, 0);
    for (const auto& e : capture.events) { const auto f = std::min<size_t>(e.frame, capture.frames); if (!first[f]) first[f] = e.block; last[f] = e.block; ++events[f]; }
    for (const auto& x : capture.executions) ++draws[std::min<size_t>(x.frame, capture.frames)];
    const auto mapping = capture.header.value("inspector_application_frames", Json::object());
    for (uint32_t f = 1; f <= capture.frames; ++f) {
        Json item{{"frame", f}, {"first_block", first[f]}, {"last_block", last[f]}, {"events", events[f]}, {"executions", draws[f]}, {"path", FrameDirectory(f) + "/frame.json"}};
        if (mapping.contains(std::to_string(f))) item["application_frame"] = mapping[std::to_string(f)];
        result["frames"].push_back(item);
    }
    return result;
}
Json ExportFrame(const Capture& capture, const DumpIndex& index, const FramePasses& frame) {
    (void)index;
    Json result{{"schema", kSchema}, {"frame", frame.frame}, {"events_path", FrameDirectory(frame.frame) + "/events.jsonl"}, {"passes", Json::array()}, {"submits", Json::array()},
        {"frame_image", StaticDumpSkipped()}};
    std::map<uint64_t, std::pair<size_t, std::set<uint64_t>>> submits; size_t draws = 0;
    for (size_t p = 0; p < frame.passes.size(); ++p) {
        const auto& pass = frame.passes[p];
        result["passes"].push_back({{"ordinal", pass.ordinal}, {"kind", pass.kind}, {"directory", pass.Directory()}, {"path", FrameDirectory(frame.frame) + "/passes/" + pass.Directory() + "/pass.json"},
            {"draws", pass.executions.size()}, {"classification", ClassifyPass(capture, frame, p)["label"]}});
        draws += pass.executions.size();
        auto& s = submits[pass.submit]; s.first += pass.executions.size();
        for (const auto e : pass.executions) for (const auto& inv : capture.executions[e].invocations) s.second.insert(inv.commandBuffer);
    }
    for (const auto& [block, s] : submits) result["submits"].push_back({{"block", block}, {"executions", s.first}, {"command_buffers", Json(s.second)}});
    result["draw_count"] = draws;
    return result;
}
Json ExportPass(const Capture& capture, const DumpIndex& index, const FramePasses& frame, size_t passIndex) {
    const auto& pass = frame.passes.at(passIndex);
    Json result{{"schema", kSchema}, {"frame", frame.frame}, {"ordinal", pass.ordinal}, {"kind", pass.kind}, {"submit", pass.submit}, {"recording_begin", pass.begin},
        {"scope_blocks", pass.scope}, {"draws", Json::array()}, {"classification", ClassifyPass(capture, frame, passIndex)}, {"outputs", StaticDumpSkipped()}};
    if (!pass.scope.empty()) {
        result["begin_block"] = pass.scope.front();
        const size_t last = EventByBlock(capture, pass.scope.back());
        result["end_block"] = pass.scope.size() > 1 && last != static_cast<size_t>(-1) && Starts(capture.events[last].name, "vkCmdEnd") ? Json(pass.scope.back()) : Json();
    } else { result["begin_block"] = pass.firstBlock; result["end_block"] = nullptr; }
    if (!pass.executions.empty()) {
        auto a = Attachments(capture, capture.executions[pass.executions.front()]);
        result["attachments"] = a["attachments"]; if (a.contains("extent")) result["extent"] = a["extent"];
        for (const char* key : {"api", "render_pass", "framebuffer", "unavailable"}) if (a.contains(key)) result[key] = a[key];
    }
    uint32_t maxSubpass = 0;
    for (const auto e : pass.executions) {
        const auto& event = capture.events[capture.executions[e].event];
        const auto subpass = event.state ? static_cast<uint32_t>(event.state->value("subpass", 0)) : 0u;
        maxSubpass = std::max(maxSubpass, subpass);
        const auto& d = index.draws[e];
        result["draws"].push_back({{"ordinal", d.ordinal}, {"execution", e}, {"block", event.block}, {"call", event.name}, {"pipeline", event.pipeline}, {"subpass", subpass}, {"path", d.path + "/draw.json"}});
    }
    result["subpass_count"] = maxSubpass + 1;
    return result;
}
Json ExportDraw(const Capture& capture, const DumpIndex& index, size_t execution) {
    const auto& x = capture.executions.at(execution); const auto& event = capture.events.at(x.event); const auto& location = index.draws.at(execution);
    Json args = Json::object();
    try { args = capture.ReadEvent(x.event)["function"].value("args", Json::object()); } catch (...) {}
    const bool dispatch = Starts(event.name, "vkCmdDispatch");
    Json result{{"schema", kSchema}, {"frame", x.frame}, {"block", event.block}, {"execution", execution}, {"occurrence", location.occurrence},
        {"ordinal", location.ordinal}, {"pass", location.pass}, {"path", location.path}, {"call", {{"name", event.name}, {"args", args}}},
        {"submit", x.submit}, {"command_buffer", event.commandBuffer}, {"label", event.label}, {"valid", x.valid}};
    if (!x.valid) result["unavailable"] = x.unavailable;
    const auto info = capture.pipelineInfo.find(event.pipeline);
    result["pipeline"] = {{"id", event.pipeline}, {"path", info != capture.pipelineInfo.end() ? Json(PipelinePath(event.pipeline)) : Json()}, {"kind", dispatch ? "compute" : "graphics"}};
    if (info == capture.pipelineInfo.end()) result["pipeline"]["status"] = {{"status", "unavailable"}, {"reason", "no pipeline create info resolved for this draw (shader objects, libraries or unbound pipeline)"}};
    result["stages"] = Json::array();
    const auto stages = capture.pipelines.find(event.pipeline);
    if (stages != capture.pipelines.end()) for (const auto& s : stages->second)
        result["stages"].push_back({{"stage", s.stage}, {"module", s.module}, {"entry", s.entry}, {"specialization", s.specialization}, {"path", ShaderPath(s.module) + "/"}});
    const Json empty = Json::object();
    const Json& state = event.state ? *event.state : empty;
    result["subpass"] = state.value("subpass", 0);
    result["descriptors"] = {{"bound_sets", state.value("descriptorSets", Json::object()).value(dispatch ? "compute" : "graphics", Json::object())}, {"values", x.descriptorValues.is_null() ? Json::object() : x.descriptorValues}};
    result["vertex_buffers"] = state.value("vertexBuffers", Json::object());
    result["index_buffer"] = state.value("indexBuffer", Json());
    result["push_constants"] = {{"ranges", state.value("pushConstants", Json::array())}, {"data_unavailable", state.value("pushConstantDataUnavailable", false)}};
    result["dynamic_state"] = {{"calls", state.value("dynamic", Json::object())}, {"viewports", state.value("viewports", Json::object())}, {"scissors", state.value("scissors", Json::object())}};
    result["render_targets"] = Attachments(capture, x);
    if (event.name.find("Indirect") != std::string::npos) {
        Json indirect{{"buffer", U(args, "buffer")}, {"offset", U(args, "offset")}};
        for (const char* key : {"drawCount", "stride", "countBuffer", "countBufferOffset", "maxDrawCount"}) if (args.contains(key)) indirect[key] = args[key];
        indirect["contents"] = {{"status", "unavailable"}, {"reason", "indirect argument contents require replay readback"}};
        result["indirect"] = indirect;
    }
    Json path = Json::array();
    for (const auto& v : x.invocations) path.push_back({{"command_buffer", v.commandBuffer}, {"recording_begin", v.begin}, {"generation", v.generation}, {"execute_block", v.execute}, {"slot", v.ordinal}});
    result["invocation_path"] = {{"command_buffers", x.invocationPath}, {"invocations", path}, {"primary", x.primary}};
    try { result["selector"] = capture.DumpRequest(execution); } catch (const std::exception& e) { result["selector"] = {{"unavailable", e.what()}}; }
    result["notes"] = x.notes;
    result["dynamic"] = {{"outputs", StaticDumpSkipped()}, {"inputs", StaticDumpSkipped()}, {"mesh", StaticDumpSkipped()}};
    return result;
}
Json ExportPipeline(const Capture& capture, uint64_t id) {
    const auto info = capture.pipelineInfo.find(id);
    if (info == capture.pipelineInfo.end()) throw std::out_of_range("No pipeline " + std::to_string(id) + " in this capture");
    Json result{{"schema", kSchema}, {"id", id}, {"kind", info->second.contains("stage") ? "compute" : "graphics"}, {"create_info", info->second}, {"stages", Json::array()},
        {"layout", U(info->second, "layout")}, {"render_pass", U(info->second, "renderPass")}};
    const auto object = capture.objects.find(id);
    if (object != capture.objects.end()) { result["name"] = object->second.name; result["created"] = object->second.created; result["destroyed"] = object->second.destroyed; }
    const auto stages = capture.pipelines.find(id);
    if (stages != capture.pipelines.end()) for (const auto& s : stages->second)
        result["stages"].push_back({{"stage", s.stage}, {"module", s.module}, {"entry", s.entry}, {"specialization", s.specialization}, {"path", ShaderPath(s.module) + "/"}});
    return result;
}
Json ExportObject(const Capture& capture, uint64_t id) {
    const auto found = capture.objects.find(id);
    if (found == capture.objects.end()) throw std::out_of_range("No object " + std::to_string(id) + " in this capture");
    return {{"schema", kSchema}, {"id", id}, {"type", found->second.type}, {"name", found->second.name}, {"created", found->second.created},
        {"destroyed", found->second.destroyed}, {"create_info", found->second.createInfo}};
}
Json ExportEvent(const Capture& capture, size_t index, std::istream* stream) {
    const auto& event = capture.events.at(index);
    Json result{{"block", event.block}, {"name", event.name}, {"frame", event.frame}, {"thread", event.thread}};
    if (event.commandBuffer) result["command_buffer"] = event.commandBuffer;
    if (!event.label.empty()) result["label"] = event.label;
    if (event.draw) result["draw"] = true;
    const auto record = ReadRecord(capture, index, stream);
    for (const char* kind : {"function", "meta"}) if (record.contains(kind)) {
        result["args"] = record[kind].value("args", Json::object());
        if (record[kind].contains("return")) result["return"] = record[kind]["return"];
        return result;
    }
    result["record"] = record;
    return result;
}
} // namespace gfx
