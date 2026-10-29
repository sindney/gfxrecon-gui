#include "analysis/capture.h"
#include "analysis/state.h"

#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <algorithm>

namespace gfx {
namespace {
bool ReadLine(std::istream& input, std::string& line) {
    line.clear();
    for (;;) {
        char chunk[4096]; input.getline(chunk, sizeof(chunk));
        const auto read = input.gcount();
        if (!read && input.eof()) return !line.empty();
        const bool delimiter = !input.fail() && !input.eof();
        const auto count = static_cast<size_t>(read - (delimiter ? 1 : 0));
        if (line.size() + count > 64 * 1024 * 1024) throw std::runtime_error("Capture record exceeds 64 MiB limit");
        line.append(chunk, count);
        if (delimiter || input.eof()) return true;
        if (input.bad()) throw std::runtime_error("I/O failure reading capture record");
        input.clear(input.rdstate() & ~std::ios::failbit);
    }
}
uint64_t Id(const Json& object, const char* key) {
    if (!object.contains(key)) return 0;
    const auto& v = object[key];
    if (v.is_number_unsigned()) return v.get<uint64_t>();
    if (v.is_number_integer() && v.get<int64_t>() >= 0) return v.get<uint64_t>();
    return 0;
}
std::string Stage(const Json& s) {
    if (s.is_string()) return s.get<std::string>();
    if (s.is_number()) {
        switch (s.get<uint32_t>()) {
        case 1: return "VK_SHADER_STAGE_VERTEX_BIT";
        case 16: return "VK_SHADER_STAGE_FRAGMENT_BIT";
        case 32: return "VK_SHADER_STAGE_COMPUTE_BIT";
        }
    }
    return s.dump();
}
} // namespace
Json Execution::DumpRequest() const {
    if (!valid) throw std::runtime_error(unavailable);
    if (!begin || !submit) throw std::runtime_error("Execution has no resolved command-buffer recording/submission");
    Json result{{"BeginCommandBuffer", Json::array()}, {"QueueSubmit", Json::array()}};
    if (invocations.empty()) {
        if (!primary) throw std::runtime_error("Secondary invocation context is unavailable");
        result["BeginCommandBuffer"].push_back(begin); result["QueueSubmit"].push_back(submit);
    } else for (auto it = invocations.rbegin(); it != invocations.rend(); ++it) {
        if (!it->begin) throw std::runtime_error("A command-buffer recording in this invocation is unresolved");
        result["BeginCommandBuffer"].push_back(it->begin); result["QueueSubmit"].push_back(submit);
    }
    return result;
}
Json Capture::ReadEvent(size_t i) const {
    const auto& e = events.at(i);
    std::ifstream input(jsonl, std::ios::binary);
    input.seekg(static_cast<std::streamoff>(e.offset));
    std::string line;
    if (!ReadLine(input, line)) throw std::runtime_error("Capture record is no longer readable");
    return Json::parse(line);
}
std::string Capture::SearchText(size_t index) const {
    const auto& event = events.at(index);
    std::string text = std::to_string(event.block) + " " + event.name + " " + event.label;
    const auto pipeline = objects.find(event.pipeline);
    if (pipeline != objects.end()) text += " " + pipeline->second.name;
    for (const auto id : event.objects) {
        text += " " + std::to_string(id);
        const auto object = objects.find(id);
        if (object != objects.end()) text += " " + object->second.name;
    }
    return text;
}
std::string Capture::ModuleEditLimitation(uint64_t module) const {
    if (!module) return "Inline and shader-object replacement identities are not validated; an ordinary captured shader module is required.";
    for (const auto& event : events) if (event.name.find("PipelineBinar") != std::string::npos)
        return "Captured pipeline binaries may bypass shader replacement. Editing is disabled for this capture until binary invalidation is validated.";
    bool referenced = false;
    for (const auto& [pipeline, stages] : pipelines) {
        if (std::none_of(stages.begin(), stages.end(), [&](const auto& stage) { return stage.module == module; })) continue;
        referenced = true;
        const auto info = pipelineInfo.find(pipeline);
        if (info == pipelineInfo.end()) return "Referencing pipeline creation information is unavailable; replacement scope cannot be verified.";
        const auto text = info->second.dump();
        for (const auto token : {"PIPELINE_LIBRARY_CREATE_INFO", "GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO", "PIPELINE_BINARY_INFO", "SHADER_MODULE_CREATE_INFO"})
            if (text.find(token) != std::string::npos) return "A referencing pipeline uses an unvalidated library, inline module, or binary path. Editing is disabled for this module.";
    }
    return referenced ? "" : "No verified ordinary pipeline reference exists for this module.";
}
Json Capture::DumpRequest(size_t index) const {
    const auto& execution = executions.at(index);
    auto request = execution.DumpRequest();
    const auto& event = events.at(execution.event);
    // Upstream selectors identify calls and submissions, but not two identical
    // CB references inside a single submit/execute array. Never pick a neighbor.
    for (size_t other = 0; other < executions.size(); ++other) {
        if (other == index || executions[other].event != execution.event || executions[other].submit != execution.submit) continue;
        const auto& contexts = executions[other].invocations;
        if (contexts.size() != execution.invocations.size()) continue;
        bool same = true;
        for (size_t level = 0; level < contexts.size(); ++level)
            same &= contexts[level].begin == execution.invocations[level].begin && contexts[level].execute == execution.invocations[level].execute;
        if (same) throw std::runtime_error("Repeated identical command-buffer references in one submission cannot be uniquely selected by this replay backend");
    }
    const bool dispatch = event.name.rfind("vkCmdDispatch", 0) == 0;
    const char* operation = dispatch ? "Dispatch" : "Draw";
    request[operation] = Json::array(); request["RenderPass"] = Json::array();
    const auto contexts = execution.invocations.empty() ? std::vector<Invocation>{{event.commandBuffer, execution.begin, execution.generation, 0, 0, execution.renderScope}} : execution.invocations;
    bool scope = false;
    for (size_t n = contexts.size(); n > 0; --n) {
        const auto level = n - 1;
        request[operation].push_back(level == contexts.size() - 1 ? Json::array({event.block}) : Json::array());
        request["RenderPass"].push_back(Json::array({contexts[level].renderScope}));
        scope |= contexts[level].renderScope.size() >= 2;
        if (contexts.size() > 1) {
            if (!request.contains("ExecuteCommands")) request["ExecuteCommands"] = Json::array();
            request["ExecuteCommands"].push_back(level + 1 < contexts.size() ?
                Json::array({Json::array({contexts[level + 1].execute, contexts[level + 1].begin})}) : Json::array({Json::array()}));
        }
    }
    if (!dispatch && !scope) throw std::runtime_error("Draw has no complete resolved render scope");
    if (dispatch) request.erase("RenderPass");
    return request;
}

std::shared_ptr<Capture> ReadCapture(const std::filesystem::path& file, const std::atomic_bool* cancel) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open converted capture: " + file.u8string());
    auto capture = std::make_shared<Capture>();
    capture->jsonl = file;
    capture->source = file;
    StateTracker state(*capture);
    std::unordered_map<uint64_t, uint64_t> boundGraphics, boundCompute;
    struct Recording {
        uint64_t begin = 0, generation = 0;
        size_t currentScope = static_cast<size_t>(-1);
        std::vector<std::vector<uint64_t>> scopes;
        struct Command { size_t event; size_t scope; std::vector<uint64_t> secondary; std::vector<uint64_t> versions; };
        std::vector<Command> commands;
    };
    std::unordered_map<uint64_t, Recording> recordings;
    std::unordered_map<uint64_t, uint64_t> commandPools, versions;
    uint32_t frame = 1, markerFrame = 1;
    size_t lineNumber = 0;
    std::string line;
    for (;;) {
        if (cancel && cancel->load()) throw std::runtime_error("Import cancelled");
        const auto offset = input.tellg();
        if (!ReadLine(input, line)) break;
        ++lineNumber;
        if (line.empty() || line == "\r") continue;
        if (line.size() > 64 * 1024 * 1024) throw std::runtime_error("Capture record exceeds 64 MiB limit");
        Json record;
        try { record = Json::parse(line); }
        catch (const Json::exception& e) {
            throw std::runtime_error("Invalid JSONL at line " + std::to_string(lineNumber) + ": " + e.what());
        }
        if (record.contains("header")) { capture->header = record["header"]; continue; }
        if (record.contains("state") && record["state"].is_object()) {
            const auto marker = record["state"].value("marker_type", std::string{});
            if (marker != "BeginMarker" && marker != "EndMarker") throw std::runtime_error("Unknown state snapshot marker");
            capture->header["inspector_snapshot_application_frame"] = Id(record["state"], "frame_number");
            continue;
        }
        if (record.contains("frame")) {
            const auto& marker = record["frame"];
            if (marker.value("marker_type", std::string{}) == "EndMarker") {
                const auto number = Id(marker, "frame_number");
                if (markerFrame == UINT32_MAX) throw std::runtime_error("Capture-relative frame number exceeds supported range");
                capture->header["inspector_application_frames"][std::to_string(markerFrame)] = number;
                frame = ++markerFrame;
            }
            continue;
        }
        if (!record.contains("index")) throw std::runtime_error("Missing block index at line " + std::to_string(lineNumber));
        const char* kind = record.contains("function") ? "function" : record.contains("meta") ? "meta" : nullptr;
        Event event;
        event.block = Id(record, "index");
        event.offset = static_cast<uint64_t>(offset);
        event.frame = frame;
        if (kind) {
            const auto& call = record[kind];
            event.name = call.value("name", std::string(kind));
            if (event.name == "vkCreateAndroidSurfaceKHR") capture->android = true;
            event.thread = Id(call, "thread");
            const auto args = call.value("args", Json::object());
            if (event.name == "vkCreateDevice" && args.contains("pCreateInfo") && args["pCreateInfo"].is_object()) {
                const auto extensions = args["pCreateInfo"].value("ppEnabledExtensionNames", Json::array());
                if (extensions.is_array()) for (const auto& extension : extensions)
                    if (extension.is_string() && extension.get<std::string>().rfind("VK_ANDROID_", 0) == 0) capture->android = true;
            }
            event.commandBuffer = Id(args, "commandBuffer");
            if (event.name == "vkAllocateCommandBuffers" && args.value("pCommandBuffers", Json{}).is_array()) {
                const auto pool = Id(args.value("pAllocateInfo", Json::object()), "commandPool");
                for (const auto& cb : args["pCommandBuffers"]) if (cb.is_number_unsigned() || cb.is_number_integer()) commandPools[cb.get<uint64_t>()] = pool;
            }
            if (event.name == "vkFreeCommandBuffers" && args.value("pCommandBuffers", Json{}).is_array()) {
                for (const auto& cb : args["pCommandBuffers"]) if (cb.is_number_unsigned() || cb.is_number_integer()) {
                    const auto id = cb.get<uint64_t>(); recordings.erase(id); commandPools.erase(id); boundGraphics.erase(id); boundCompute.erase(id);
                }
            }
            if (event.name == "vkBeginCommandBuffer" || event.name == "vkResetCommandBuffer") {
                boundGraphics.erase(event.commandBuffer);
                boundCompute.erase(event.commandBuffer);
                auto& recording = recordings[event.commandBuffer];
                const auto generation = ++versions[event.commandBuffer];
                recording = {}; recording.generation = generation;
                if (event.name == "vkBeginCommandBuffer") recording.begin = event.block;
            }
            if (event.name == "vkResetCommandPool" || event.name == "vkDestroyCommandPool") {
                const auto pool = Id(args, "commandPool");
                for (const auto& [cb, owningPool] : commandPools) if (owningPool == pool) {
                    recordings.erase(cb); boundGraphics.erase(cb); boundCompute.erase(cb);
                }
            }
            auto& recording = recordings[event.commandBuffer];
            const bool renderBegin = event.name == "vkCmdBeginRenderPass" || event.name == "vkCmdBeginRenderPass2" ||
                event.name == "vkCmdBeginRenderPass2KHR" || event.name == "vkCmdBeginRendering" || event.name == "vkCmdBeginRenderingKHR";
            const bool renderEnd = event.name == "vkCmdEndRenderPass" || event.name == "vkCmdEndRenderPass2" ||
                event.name == "vkCmdEndRenderPass2KHR" || event.name == "vkCmdEndRendering" || event.name == "vkCmdEndRenderingKHR";
            if (renderBegin) {
                recording.currentScope = recording.scopes.size();
                recording.scopes.push_back({event.block});
            } else if (recording.currentScope != static_cast<size_t>(-1) &&
                (renderEnd || event.name.rfind("vkCmdNextSubpass", 0) == 0)) {
                recording.scopes[recording.currentScope].push_back(event.block);
                if (renderEnd) recording.currentScope = static_cast<size_t>(-1);
            }
            if (event.name == "vkCmdBindPipeline") {
                const auto point = args.value("pipelineBindPoint", Json{});
                const bool compute = (point.is_string() && point.get<std::string>().find("COMPUTE") != std::string::npos) || point == 1;
                (compute ? boundCompute : boundGraphics)[event.commandBuffer] = Id(args, "pipeline");
            }
            if (event.name == "vkCmdBindShadersEXT") { boundGraphics.erase(event.commandBuffer); boundCompute.erase(event.commandBuffer); }
            const bool draw = event.name.rfind("vkCmdDraw", 0) == 0;
            const bool dispatch = event.name.rfind("vkCmdDispatch", 0) == 0;
            event.draw = draw || dispatch;
            const auto result = call.value("return", Json{});
            const bool successful = !(result.is_string() && result.get<std::string>().rfind("VK_ERROR", 0) == 0) &&
                !(result.is_number_integer() && result.get<int64_t>() < 0);
            state.Observe(event, args, successful);
            if (event.draw) {
                const auto& bindings = dispatch ? boundCompute : boundGraphics;
                const auto it = bindings.find(event.commandBuffer);
                if (it != bindings.end()) event.pipeline = it->second;
                recording.commands.push_back({capture->events.size(), recording.currentScope, {}});
            }
            if (event.name == "vkCmdExecuteCommands" && args.contains("pCommandBuffers") && args["pCommandBuffers"].is_array()) {
                Recording::Command command{capture->events.size(), recording.currentScope, args["pCommandBuffers"].get<std::vector<uint64_t>>(), {}};
                for (const auto cb : command.secondary) command.versions.push_back(versions[cb]);
                recording.commands.push_back(std::move(command));
            }
            if (event.name == "vkQueueSubmit" || event.name == "vkQueueSubmit2" || event.name == "vkQueueSubmit2KHR") {
                std::function<void(uint64_t, std::vector<Invocation>, uint64_t, uint32_t)> execute;
                execute = [&](uint64_t cb, std::vector<Invocation> path, uint64_t call, uint32_t ordinal) {
                    const auto found = recordings.find(cb);
                    if (found == recordings.end() || !found->second.begin) {
                        capture->limitations.push_back("Submission " + std::to_string(event.block) + " references an unavailable/reset command buffer " + std::to_string(cb)); return;
                    }
                    if (path.size() >= 32 || std::any_of(path.begin(), path.end(), [&](const auto& context) { return context.commandBuffer == cb; }))
                        throw std::runtime_error("Invalid recursive secondary command-buffer invocation");
                    const bool primary = path.empty();
                    const auto& gen = found->second;
                    for (const auto& cmd : gen.commands) {
                        auto contexts = path;
                        Invocation context{cb, gen.begin, gen.generation, call, ordinal, {}};
                        if (cmd.scope < gen.scopes.size()) context.renderScope = gen.scopes[cmd.scope];
                        const auto& recorded = capture->events[cmd.event];
                        if (recorded.state) context.renderTargets = recorded.state->value("renderTargets", Json{});
                        contexts.push_back(std::move(context));
                        if (!cmd.secondary.empty()) {
                            for (size_t child = 0; child < cmd.secondary.size(); ++child) {
                                const auto secondary = cmd.secondary[child];
                                if (child >= cmd.versions.size() || versions[secondary] != cmd.versions[child]) {
                                    capture->limitations.push_back("Secondary recording changed after vkCmdExecuteCommands at block " + std::to_string(capture->events[cmd.event].block)); continue;
                                }
                                execute(secondary, contexts, capture->events[cmd.event].block, static_cast<uint32_t>(child));
                            }
                        } else {
                            Execution execution;
                            execution.event = cmd.event; execution.submit = event.block; execution.begin = gen.begin;
                            execution.generation = gen.generation; execution.frame = frame;
                            execution.invocations = std::move(contexts); execution.primary = primary;
                            for (const auto& invocation : execution.invocations) execution.invocationPath.push_back(invocation.commandBuffer);
                            if (cmd.scope < gen.scopes.size()) execution.renderScope = gen.scopes[cmd.scope];
                            state.Resolve(execution, recorded);
                            capture->executions.push_back(std::move(execution));
                        }
                    }
                };
                for (const auto& submit : args.value("pSubmits", Json::array())) {
                    const auto buffers = submit.value("pCommandBuffers", Json::array());
                    uint32_t ordinal = 0;
                    if (buffers.is_array()) for (const auto& cb : buffers) if (cb.is_number()) execute(cb.get<uint64_t>(), {}, 0, ordinal++);
                    for (const auto& info : submit.value("pCommandBufferInfos", Json::array())) execute(Id(info, "commandBuffer"), {}, 0, ordinal++);
                }
            }
            if (event.name == "vkCreateGraphicsPipelines" || event.name == "vkCreateComputePipelines") {
                const auto infos = args.value("pCreateInfos", Json::array());
                const auto handles = args.value("pPipelines", Json::array());
                if (infos.is_array() && handles.is_array()) {
                    for (size_t i = 0; i < std::min(infos.size(), handles.size()); ++i) {
                        if (!handles[i].is_number_unsigned() && !handles[i].is_number_integer()) continue;
                        auto stages = infos[i].value("pStages", Json::array());
                        if (infos[i].contains("stage")) stages = Json::array({infos[i]["stage"]});
                        for (const auto& stage : stages) {
                            capture->pipelines[handles[i].get<uint64_t>()].push_back({Id(stage, "module"),
                                Stage(stage.value("stage", Json{})), stage.value("pName", std::string("main")), stage.value("pSpecializationInfo", Json{})});
                        }
                    }
                }
            }
            if (event.name == "vkQueuePresentKHR") ++frame;
        } else {
            event.name = record.contains("annotation") ? "Annotation" : "Capture metadata";
        }
        if (!capture->events.empty() && event.block <= capture->events.back().block)
            throw std::runtime_error("Non-increasing block index at line " + std::to_string(lineNumber));
        capture->events.push_back(std::move(event));
    }
    if (input.bad()) throw std::runtime_error("I/O failure reading capture");
    if (capture->events.empty()) throw std::runtime_error("Capture has no indexed events");
    capture->frames = capture->events.back().frame;
    capture->limitations.push_back("Offline state describes recorded bindings and CPU descriptor values at submission. Cross-queue writes, aliasing, and device-address resource contents require replay validation.");
    if (std::any_of(capture->events.begin(), capture->events.end(), [](const auto& event) { return event.name == "vkCmdBindShadersEXT" || event.name == "vkCreateShadersEXT"; }))
        capture->limitations.push_back("Shader-object state and replacement identities are unavailable; raw calls remain inspectable.");
    return capture;
}
} // namespace gfx
