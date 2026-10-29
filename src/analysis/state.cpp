#include "analysis/state.h"
#include <algorithm>

namespace gfx {
namespace {
uint64_t Id(const Json& object, const char* name) {
    const auto it = object.find(name);
    return it != object.end() && (it->is_number_unsigned() || it->is_number_integer()) ? it->get<uint64_t>() : 0;
}
bool Starts(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }
bool Compute(const Json& args) {
    const auto point = args.value("pipelineBindPoint", Json{});
    return point == 1 || (point.is_string() && point.get<std::string>().find("COMPUTE") != std::string::npos);
}
bool Flag(const Json& value, uint64_t mask, const char* token) {
    if (value.is_number()) return (value.get<uint64_t>() & mask) != 0;
    if (!value.is_string()) return false;
    const auto text = value.get<std::string>();
    if (text.find(token) != std::string::npos) return true;
    if (Starts(text, "0x")) { try { return (std::stoull(text, nullptr, 16) & mask) != 0; } catch (...) {} }
    return false;
}
Json EmptyState() {
    return {{"descriptorSets", {{"graphics", Json::object()}, {"compute", Json::object()}}},
        {"vertexBuffers", Json::object()}, {"dynamic", Json::object()}, {"labels", Json::array()}, {"subpass", 0}};
}
}
Json& StateTracker::Mutable(uint64_t cb) {
    auto& state = states_[cb];
    if (!state) state = std::make_shared<Json>(EmptyState());
    else if (!state.unique()) state = std::make_shared<Json>(*state);
    return *state;
}
void StateTracker::Observe(Event& event, const Json& args, bool successful) {
    const auto& name = event.name; const auto cb = event.commandBuffer;
    for (const auto* key : {"commandBuffer", "pipeline", "buffer", "image", "sampler", "shaderModule", "commandPool", "descriptorPool",
                           "srcBuffer", "dstBuffer", "srcImage", "dstImage", "pBuffer", "pImage", "pShaderModule", "pPipelineLayout", "pSetLayout", "pRenderPass", "pFramebuffer"}) {
        const auto id = Id(args, key); if (id) event.objects.push_back(id);
    }
    for (const auto* key : {"pBuffers", "pDescriptorSets", "pCommandBuffers", "pPipelines"})
        if (args.value(key, Json{}).is_array()) for (const auto& id : args[key])
            if (id.is_number_unsigned() || id.is_number_integer()) event.objects.push_back(id.get<uint64_t>());
    struct ObjectType { const char* type; const char* result; const char* input; };
    static const ObjectType types[] = {
        {"Buffer","pBuffer","buffer"}, {"Image","pImage","image"}, {"ImageView","pView","imageView"},
        {"BufferView","pView","bufferView"}, {"ShaderModule","pShaderModule","shaderModule"},
        {"Sampler","pSampler","sampler"}, {"RenderPass","pRenderPass","renderPass"},
        {"Framebuffer","pFramebuffer","framebuffer"}, {"PipelineLayout","pPipelineLayout","pipelineLayout"},
        {"DescriptorSetLayout","pSetLayout","descriptorSetLayout"}, {"DescriptorPool","pDescriptorPool","descriptorPool"},
        {"CommandPool","pCommandPool","commandPool"}, {"PipelineCache","pPipelineCache","pipelineCache"},
        {"Fence","pFence","fence"}, {"Semaphore","pSemaphore","semaphore"}};
    for (const auto& type : types) {
        if (name == std::string("vkCreate") + type.type && successful) {
            const auto id = Id(args, type.result);
            if (id) capture_.objects[id] = {type.type, "", event.block, 0, args.value("pCreateInfo", Json{})};
        }
        if (name == std::string("vkDestroy") + type.type) {
            const auto object = capture_.objects.find(Id(args, type.input));
            if (object != capture_.objects.end()) object->second.destroyed = event.block;
        }
    }
    if (name == "vkSetDebugUtilsObjectNameEXT") {
        const auto info = args.value("pNameInfo", Json::object()); const auto id = Id(info, "objectHandle");
        if (id) { capture_.objects[id].name = info.value("pObjectName", std::string{}); event.objects.push_back(id); }
    }
    if ((name == "vkCreateGraphicsPipelines" || name == "vkCreateComputePipelines") && successful) {
        const auto infos = args.value("pCreateInfos", Json::array()), handles = args.value("pPipelines", Json::array());
        if (infos.is_array() && handles.is_array()) for (size_t i = 0; i < std::min(infos.size(), handles.size()); ++i) {
            if (!handles[i].is_number()) continue; const auto id = handles[i].get<uint64_t>(); if (!id) continue;
            capture_.pipelineInfo[id] = infos[i]; capture_.objects[id] = {"Pipeline", "", event.block, 0, infos[i]};
        }
    }
    if (name == "vkDestroyPipeline") {
        const auto found = capture_.objects.find(Id(args, "pipeline")); if (found != capture_.objects.end()) found->second.destroyed = event.block;
    }
    if (name == "vkAllocateCommandBuffers" && successful && args.value("pCommandBuffers", Json{}).is_array()) {
        const auto info = args.value("pAllocateInfo", Json::object());
        for (const auto& item : args["pCommandBuffers"]) if (item.is_number()) {
            const auto id = item.get<uint64_t>(); pools_[id] = Id(info, "commandPool");
            capture_.objects[id] = {"CommandBuffer", "", event.block, 0, info};
        }
    }
    if (name == "vkBeginCommandBuffer" || name == "vkResetCommandBuffer") states_.erase(cb);
    if (name == "vkResetCommandPool" || name == "vkDestroyCommandPool") {
        for (const auto& [buffer, pool] : pools_) if (pool == Id(args, "commandPool")) states_.erase(buffer);
    }
    if (name == "vkFreeCommandBuffers" && args.value("pCommandBuffers", Json{}).is_array())
        for (const auto& item : args["pCommandBuffers"]) if (item.is_number()) {
            const auto id = item.get<uint64_t>(); states_.erase(id); pools_.erase(id); capture_.objects[id].destroyed = event.block;
        }
    if (name == "vkCreateDescriptorSetLayout" && successful) descriptorLayouts_[Id(args, "pSetLayout")] = args.value("pCreateInfo", Json::object());
    if (name == "vkAllocateDescriptorSets" && successful) {
        const auto info = args.value("pAllocateInfo", Json::object());
        const auto sets = args.value("pDescriptorSets", Json::array()), layouts = info.value("pSetLayouts", Json::array());
        if (sets.is_array() && layouts.is_array()) for (size_t i = 0; i < std::min(sets.size(), layouts.size()); ++i) {
            if (!sets[i].is_number()) continue;
            const auto id = sets[i].get<uint64_t>();
            descriptorSets_[id] = {{"layout", layouts[i]}, {"pool", info.value("descriptorPool", Json{})}, {"bindings", Json::object()}, {"version", descriptorVersion_}};
            capture_.objects[id] = {"DescriptorSet", "", event.block, 0, descriptorSets_[id]};
        }
    }
    if (name == "vkFreeDescriptorSets" && args.value("pDescriptorSets", Json{}).is_array())
        for (const auto& item : args["pDescriptorSets"]) if (item.is_number()) {
            const auto id = item.get<uint64_t>(); descriptorSets_.erase(id); capture_.objects[id].destroyed = event.block;
        }
    if (name == "vkResetDescriptorPool" || name == "vkDestroyDescriptorPool") {
        for (auto it = descriptorSets_.begin(); it != descriptorSets_.end();) {
            if (Id(it->second, "pool") == Id(args, "descriptorPool")) { capture_.objects[it->first].destroyed = event.block; it = descriptorSets_.erase(it); }
            else ++it;
        }
    }
    if (name == "vkUpdateDescriptorSets") {
        ++descriptorVersion_;
        for (const auto& write : args.value("pDescriptorWrites", Json::array())) {
            auto& set = descriptorSets_[Id(write, "dstSet")];
            set["version"] = descriptorVersion_;
            const auto count = Id(write, "descriptorCount"), first = Id(write, "dstArrayElement");
            const auto layout = descriptorLayouts_.find(Id(set, "layout"));
            if (layout != descriptorLayouts_.end()) {
                bool fits = false;
                for (const auto& binding : layout->second.value("pBindings", Json::array()))
                    if (Id(binding, "binding") == Id(write, "dstBinding")) fits = first + count <= Id(binding, "descriptorCount");
                if (!fits) { set["unresolvedWrite"] = write; continue; }
            }
            Json data;
            for (const auto* field : {"pBufferInfo", "pImageInfo", "pTexelBufferView"})
                if (write.value(field, Json{}).is_array()) { data = write[field]; break; }
            if (!data.is_array() || count > data.size() || count > 65536) { set["unresolvedWrite"] = write; continue; }
            for (uint64_t i = 0; i < count; ++i)
                set["bindings"][std::to_string(Id(write, "dstBinding"))][std::to_string(first + i)] =
                    {{"type", write.value("descriptorType", Json{})}, {"value", data[static_cast<size_t>(i)]}};
        }
        for (const auto& copy : args.value("pDescriptorCopies", Json::array())) {
            const auto count = Id(copy, "descriptorCount"); auto& destination = descriptorSets_[Id(copy, "dstSet")];
            destination["version"] = descriptorVersion_;
            const auto source = descriptorSets_.find(Id(copy, "srcSet"));
            if (source == descriptorSets_.end() || count > 65536) { destination["unresolvedCopy"] = copy; continue; }
            const auto snapshot = source->second.value("bindings", Json::object());
            for (uint64_t i = 0; i < count; ++i) {
                const auto binding = snapshot.find(std::to_string(Id(copy, "srcBinding")));
                if (binding == snapshot.end() || !binding->contains(std::to_string(Id(copy, "srcArrayElement") + i))) { destination["unresolvedCopy"] = copy; continue; }
                destination["bindings"][std::to_string(Id(copy, "dstBinding"))][std::to_string(Id(copy, "dstArrayElement") + i)] = binding->at(std::to_string(Id(copy, "srcArrayElement") + i));
            }
        }
    }
    if (Starts(name, "vkUpdateDescriptorSetWithTemplate")) descriptorSets_[Id(args, "descriptorSet")]["unresolvedTemplateUpdate"] = event.block;
    if (!cb || !Starts(name, "vkCmd")) return;
    if (name == "vkCmdBindShadersEXT") { auto& state = Mutable(cb); state["graphicsPipeline"] = 0; state["computePipeline"] = 0; state["unsupportedShaderObjects"] = args; }
    else if (name == "vkCmdBindPipeline") Mutable(cb)[Compute(args) ? "computePipeline" : "graphicsPipeline"] = args.value("pipeline", Json{});
    else if (name == "vkCmdBindDescriptorSets") {
        auto& state = Mutable(cb); const auto list = args.value("pDescriptorSets", Json::array());
        const auto first = Id(args, "firstSet");
        if (list.is_array()) for (size_t i = 0; i < list.size(); ++i)
            state["descriptorSets"][Compute(args) ? "compute" : "graphics"][std::to_string(first + i)] =
                {{"set", list[i]}, {"layout", args.value("layout", Json{})}, {"dynamicOffsets", args.value("pDynamicOffsets", Json{})}};
    } else if (Starts(name, "vkCmdBindVertexBuffers")) {
        auto& state = Mutable(cb); const auto buffers = args.value("pBuffers", Json::array()), offsets = args.value("pOffsets", Json::array());
        if (buffers.is_array() && offsets.is_array()) for (size_t i = 0; i < std::min(buffers.size(), offsets.size()); ++i) {
            auto& binding = state["vertexBuffers"][std::to_string(Id(args, "firstBinding") + i)];
            binding = {{"buffer", buffers[i]}, {"offset", offsets[i]}};
            for (auto key : {"pStrides", "pSizes"}) if (args.value(key, Json{}).is_array() && i < args[key].size()) binding[key] = args[key][i];
        }
    } else if (Starts(name, "vkCmdBindIndexBuffer")) Mutable(cb)["indexBuffer"] = args;
    else if (name == "vkCmdPushConstants") {
        auto& state = Mutable(cb);
        state["pushConstants"].push_back(args);
        if (args.value("pValues", Json{}) == "[Binary data]") state["pushConstantDataUnavailable"] = true;
    } else if (Starts(name, "vkCmdSet")) {
        auto values = args; values.erase("commandBuffer"); auto& state = Mutable(cb);
        state["dynamic"][name] = values;
        if (Starts(name, "vkCmdSetStencil")) {
            const auto face = args.value("faceMask", Json{});
            if (Flag(face, 1, "FRONT")) state["stencilFront"][name] = values;
            if (Flag(face, 2, "BACK")) state["stencilBack"][name] = values;
        }
        const auto key = Starts(name, "vkCmdSetViewport") ? "viewports" : Starts(name, "vkCmdSetScissor") ? "scissors" : nullptr;
        if (key) {
            const char* array = key == std::string("viewports") ? "pViewports" : "pScissors";
            if (name.find("WithCount") != std::string::npos) state[key] = Json::object();
            const auto first = Id(args, key == std::string("viewports") ? "firstViewport" : "firstScissor");
            const auto items = args.value(array, Json::array());
            if (items.is_array()) for (size_t i = 0; i < items.size(); ++i) state[key][std::to_string(first + i)] = items[i];
        }
    } else if (Starts(name, "vkCmdBeginRenderPass")) {
        auto& state = Mutable(cb); state["renderTargets"] = args.value("pRenderPassBegin", Json{}); state["subpass"] = 0;
    } else if (Starts(name, "vkCmdBeginRendering")) Mutable(cb)["renderTargets"] = args.value("pRenderingInfo", Json{});
    else if (Starts(name, "vkCmdNextSubpass")) { auto& state = Mutable(cb); state["subpass"] = state.value("subpass", 0) + 1; }
    else if (Starts(name, "vkCmdEndRenderPass") || Starts(name, "vkCmdEndRendering")) Mutable(cb)["renderTargets"] = Json();
    else if (name == "vkCmdBeginDebugUtilsLabelEXT" || name == "vkCmdDebugMarkerBeginEXT") {
        const auto info = args.value("pLabelInfo", args.value("pMarkerInfo", Json::object()));
        Mutable(cb)["labels"].push_back(info.value("pLabelName", info.value("pMarkerName", std::string{})));
    } else if (name == "vkCmdEndDebugUtilsLabelEXT" || name == "vkCmdDebugMarkerEndEXT") {
        auto& labels = Mutable(cb)["labels"]; if (!labels.empty()) labels.erase(labels.size() - 1);
    } else if (name == "vkCmdInsertDebugUtilsLabelEXT" || name == "vkCmdDebugMarkerInsertEXT") {
        const auto info = args.value("pLabelInfo", args.value("pMarkerInfo", Json::object()));
        event.label = info.value("pLabelName", info.value("pMarkerName", std::string{}));
    }
    if (event.draw || name == "vkCmdExecuteCommands") {
        Mutable(cb); event.state = states_.at(cb);
        for (const auto& label : event.state->at("labels")) { if (!event.label.empty()) event.label += " / "; event.label += label.get<std::string>(); }
    }
}
void StateTracker::Resolve(Execution& execution, const Event& event) {
    if (!event.state) return;
    const auto point = Starts(event.name, "vkCmdDispatch") ? "compute" : "graphics";
    const auto sets = event.state->at("descriptorSets").at(point);
    execution.descriptorValues = Json::object();
    for (auto it = sets.begin(); it != sets.end(); ++it) {
        const auto found = descriptorSets_.find(Id(it.value(), "set"));
        if (found == descriptorSets_.end()) { execution.descriptorValues[it.key()] = {{"unavailable", true}}; continue; }
        execution.descriptorValues[it.key()] = found->second;
        const auto layout = descriptorLayouts_.find(Id(found->second, "layout"));
        bool uncertain = layout == descriptorLayouts_.end() || found->second.contains("unresolvedWrite") ||
            found->second.contains("unresolvedCopy") || found->second.contains("unresolvedTemplateUpdate");
        if (layout != descriptorLayouts_.end()) {
            uncertain |= Flag(layout->second.value("flags", Json{}), 2, "UPDATE_AFTER_BIND");
            if (!layout->second.value("pNext", Json{}).is_null()) uncertain = true;
        }
        execution.descriptorValues[it.key()]["provenance"] = "CPU descriptor state at queue submission";
        execution.descriptorValues[it.key()]["exactGpuValueUnavailable"] = uncertain;
        if (uncertain) execution.notes.push_back("Descriptor contents require runtime validation: update-after-bind, extension/template update, or unresolved layout.");
    }
    execution.renderTargets = event.state->value("renderTargets", Json{});
    if (execution.renderTargets.is_null()) for (auto it = execution.invocations.rbegin(); it != execution.invocations.rend(); ++it)
        if (!it->renderTargets.is_null()) { execution.renderTargets = it->renderTargets; break; }
    if (event.name.find("Indirect") != std::string::npos) execution.notes.push_back("Indirect argument contents require replay readback; API arguments alone do not establish executed counts.");
    if (event.state->value("pushConstantDataUnavailable", false)) execution.notes.push_back("Push-constant byte payload was omitted by the converter; recorded ranges/stages remain available.");
    if (!capture_.pipelineInfo.count(event.pipeline)) execution.notes.push_back("Pipeline state is unresolved for this path; inspect recorded calls for shader objects, libraries, or unsupported binding commands.");
    if (event.state->contains("unsupportedShaderObjects")) execution.notes.push_back("Shader-object binding state is not resolved; no prior ordinary pipeline is used as a substitute.");
}
} // namespace gfx
