#include "analysis/capture.h"

#include <fstream>
#include <stdexcept>

namespace gfx::tests {
namespace {
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void Save(const std::filesystem::path& path, const std::vector<Json>& records) {
    std::ofstream out(path, std::ios::binary);
    for (const auto& record : records) out << record.dump() << '\n';
}
Json Call(uint64_t index, const std::string& name, Json args) {
    return {{"index", index}, {"function", {{"name", name}, {"args", args}}}};
}
}
void RunAnalysisTests(const std::filesystem::path& root) {
    const auto file = root / "execution-fixture.jsonl";
    // Runtime trims contain unindexed state markers and original application
    // frame numbers. They must still navigate as capture-relative frames 1..N.
    const uint64_t firstGameFrame = 5000000000ull;
    Save(file, {
        {{"state", {{"marker_type", "BeginMarker"}, {"frame_number", firstGameFrame}}}},
        Call(1, "vkCreateBuffer", {{"pBuffer", 10}}),
        {{"state", {{"marker_type", "EndMarker"}, {"frame_number", firstGameFrame}}}},
        Call(2, "vkQueuePresentKHR", Json::object()),
        {{"frame", {{"marker_type", "EndMarker"}, {"frame_number", firstGameFrame}}}},
        Call(3, "vkQueuePresentKHR", Json::object()),
        {{"frame", {{"marker_type", "EndMarker"}, {"frame_number", firstGameFrame + 1}}}},
        Call(4, "vkQueuePresentKHR", Json::object()),
        {{"frame", {{"marker_type", "EndMarker"}, {"frame_number", firstGameFrame + 2}}}}
    });
    const auto delayed = ReadCapture(file);
    Require(delayed->frames == 3 && delayed->events.size() == 4, "Late capture lost relative frame numbering or snapshot calls");
    Require(delayed->events.back().frame == 3, "Late frame numbers leaked into navigation");
    Require(delayed->header.at("inspector_application_frames").at("3") == firstGameFrame + 2,
            "Late capture lost its 64-bit original frame mapping");
    Require(delayed->header.at("inspector_snapshot_application_frame") == firstGameFrame, "State snapshot origin was lost");
    Save(file, {
        Call(1, "vkAllocateCommandBuffers", {{"pAllocateInfo", {{"commandPool", 100}}}, {"pCommandBuffers", {10}}}),
        Call(2, "vkAllocateCommandBuffers", {{"pAllocateInfo", {{"commandPool", 200}}}, {"pCommandBuffers", {20}}}),
        Call(3, "vkBeginCommandBuffer", {{"commandBuffer", 10}}),
        Call(4, "vkCmdDispatch", {{"commandBuffer", 10}}),
        Call(5, "vkBeginCommandBuffer", {{"commandBuffer", 20}}),
        Call(6, "vkCmdDispatch", {{"commandBuffer", 20}}),
        Call(7, "vkResetCommandPool", {{"commandPool", 100}}),
        Call(8, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {20}}}}}}),
        Call(9, "vkBeginCommandBuffer", {{"commandBuffer", 10}}),
        Call(10, "vkCmdDispatch", {{"commandBuffer", 10}}),
        Call(11, "vkQueueSubmit2", {{"pSubmits", {{{"pCommandBufferInfos", {{{"commandBuffer", 10}}}}}}}}),
        Call(12, "vkFreeCommandBuffers", {{"commandPool", 200}, {"pCommandBuffers", {20}}}),
        Call(13, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {20}}}}}})
    });
    auto capture = ReadCapture(file);
    Require(capture->executions.size() == 2, "Pool reset must preserve other pools' recorded commands");
    Require(capture->events[capture->executions[0].event].block == 6, "Wrong command survived pool reset");
    Require(capture->executions[1].generation == 2, "Recording generations must survive pool reset");
    Require(!capture->limitations.empty(), "Submitting freed command buffers must be diagnosed");

    // Indices and expected selector come from upstream vulkan_dump_resources.md.
    std::vector<Json> secondary{
        Call(736, "vkBeginCommandBuffer", {{"commandBuffer", 226}}),
        Call(754, "vkBeginCommandBuffer", {{"commandBuffer", 230}}),
        Call(761, "vkCmdDrawIndexed", {{"commandBuffer", 230}, {"indexCount", 3}}),
        Call(762, "vkEndCommandBuffer", {{"commandBuffer", 230}}),
        Call(3948, "vkCmdBeginRenderPass", {{"commandBuffer", 226}}),
        Call(3949, "vkCmdExecuteCommands", {{"commandBuffer", 226}, {"pCommandBuffers", {230}}}),
        Call(3950, "vkCmdEndRenderPass", {{"commandBuffer", 226}}),
        Call(3951, "vkEndCommandBuffer", {{"commandBuffer", 226}}),
        Call(3952, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {226}}}}}})
    };
    Save(file, secondary); capture = ReadCapture(file);
    Require(capture->executions.size() == 1 && !capture->executions[0].primary, "Secondary draw occurrence is missing");
    const auto expected = Json::parse(R"({"BeginCommandBuffer":[754,736],"Draw":[[761],[]],"RenderPass":[[[]],[[3948,3950]]],"ExecuteCommands":[[[]],[[3949,754]]],"QueueSubmit":[3952,3952]})");
    Require(capture->DumpRequest(0) == expected, "Secondary dump selector differs from upstream's documented selector");

    secondary.push_back(Call(3953, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {226}}}}}}));
    Save(file, secondary); capture = ReadCapture(file);
    Require(capture->executions.size() == 2, "Repeated secondary submission lost an occurrence");
    Require(capture->DumpRequest(1).at("QueueSubmit") == Json::array({3953, 3953}), "Secondary selection lost its submission");

    secondary.back() = Call(3953, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {226,226}}}}}});
    Save(file, secondary); capture = ReadCapture(file);
    bool rejected = false; try { capture->DumpRequest(1); } catch (...) { rejected = true; }
    Require(rejected, "Ambiguous identical CB references must not select an arbitrary occurrence");
    Require(capture->executions[1].invocations[0].ordinal != capture->executions[2].invocations[0].ordinal, "Execution identities lost array positions");

    secondary.resize(8);
    secondary.push_back(Call(3952, "vkBeginCommandBuffer", {{"commandBuffer", 230}}));
    secondary.push_back(Call(3953, "vkCmdDraw", {{"commandBuffer", 230}}));
    secondary.push_back(Call(3954, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {226}}}}}}));
    Save(file, secondary); capture = ReadCapture(file);
    Require(capture->executions.empty() && !capture->limitations.empty(), "A re-recorded secondary must not silently replace the original invocation");

    const uint64_t buffer = 9007199254740993ull;
    auto write = [&](uint64_t block, uint64_t offset) {
        const Json descriptor{{"dstSet", 90}, {"dstBinding", 3}, {"descriptorCount", 1},
            {"descriptorType", "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER"},
            {"pBufferInfo", Json::array({Json{{"buffer", buffer}, {"offset", offset}, {"range", 16}}})}};
        return Call(block, "vkUpdateDescriptorSets", {{"pDescriptorWrites", Json::array({descriptor})}});
    };
    Save(file, {
        Call(1, "vkCreateBuffer", {{"pBuffer", buffer}, {"pCreateInfo", {{"size", 64}}}}),
        Call(2, "vkCreateShaderModule", {{"pShaderModule", 50}, {"pCreateInfo", {{"codeSize", 16}}}}),
        Call(3, "vkCreateGraphicsPipelines", Json::parse(R"({"pPipelines":[70],"pCreateInfos":[{"pStages":[{"module":50,"stage":"VK_SHADER_STAGE_FRAGMENT_BIT","pName":"entry_a","pSpecializationInfo":{"dataSize":4}}]}]})")),
        Call(4, "vkDestroyShaderModule", {{"shaderModule", 50}}),
        Call(5, "vkCreateDescriptorSetLayout", Json::parse(R"({"pSetLayout":80,"pCreateInfo":{"flags":"0x0","pBindings":[{"binding":3,"descriptorCount":2}]}})")),
        Call(6, "vkAllocateDescriptorSets", {{"pDescriptorSets", {90}}, {"pAllocateInfo", {{"descriptorPool", 88}, {"pSetLayouts", {80}}}}}),
        write(7, 4),
        Call(8, "vkBeginCommandBuffer", {{"commandBuffer", 10}}),
        Call(9, "vkCmdSetViewport", {{"commandBuffer", 10}, {"firstViewport", 0}, {"pViewports", {{{"width", 100}}, {{"width", 200}}}}}),
        Call(10, "vkCmdSetViewport", {{"commandBuffer", 10}, {"firstViewport", 1}, {"pViewports", {{{"width", 300}}}}}),
        Call(11, "vkCmdBeginDebugUtilsLabelEXT", {{"commandBuffer", 10}, {"pLabelInfo", {{"pLabelName", "Opaque"}}}}),
        Call(12, "vkCmdBindPipeline", {{"commandBuffer", 10}, {"pipeline", 70}, {"pipelineBindPoint", 0}}),
        Call(13, "vkCmdBindVertexBuffers", {{"commandBuffer", 10}, {"firstBinding", 2}, {"pBuffers", {buffer}}, {"pOffsets", {12}}}),
        Call(14, "vkCmdBindIndexBuffer", {{"commandBuffer", 10}, {"buffer", buffer}, {"offset", 16}, {"indexType", "VK_INDEX_TYPE_UINT16"}}),
        Call(15, "vkCmdBindDescriptorSets", {{"commandBuffer", 10}, {"pipelineBindPoint", 0}, {"firstSet", 2}, {"pDescriptorSets", {90}}, {"pDynamicOffsets", {256}}, {"layout", 100}}),
        Call(16, "vkCmdPushConstants", {{"commandBuffer", 10}, {"layout", 100}, {"offset", 0}, {"size", 16}, {"pValues", "[Binary data]"}}),
        Call(17, "vkCmdDrawIndirect", {{"commandBuffer", 10}, {"buffer", buffer}}),
        Call(18, "vkCmdEndDebugUtilsLabelEXT", {{"commandBuffer", 10}}),
        write(19, 8),
        Call(20, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {10}}}}}}),
        write(21, 12),
        Call(22, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {10}}}}}}),
        Call(23, "vkResetCommandBuffer", {{"commandBuffer", 10}}),
        Call(24, "vkBeginCommandBuffer", {{"commandBuffer", 10}}),
        Call(25, "vkCmdDraw", {{"commandBuffer", 10}}),
        Call(26, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {10}}}}}}),
        Call(27, "vkSetDebugUtilsObjectNameEXT", {{"pNameInfo", {{"objectHandle", buffer}, {"pObjectName", "vertex-data"}}}})
    });
    capture = ReadCapture(file);
    Require(capture->executions.size() == 3, "Submission occurrences missing from state fixture");
    const auto& draw = capture->events.at(capture->executions[0].event);
    Require(draw.label == "Opaque", "Ending a debug label changed the earlier draw snapshot");
    Require(draw.state->at("vertexBuffers").at("2").at("buffer").get<uint64_t>() == buffer, "Vertex bindings lost 64-bit object identity");
    Require(draw.state->at("indexBuffer").at("offset") == 16, "Index binding snapshot is wrong");
    Require(draw.state->at("viewports").at("0").at("width") == 100 && draw.state->at("viewports").at("1").at("width") == 300,
            "Partial dynamic array updates discarded unaffected viewports");
    Require(capture->executions[0].descriptorValues.at("2").at("bindings").at("3").at("0").at("value").at("offset") == 8,
            "Descriptors must resolve at submission, after command recording");
    Require(capture->executions[1].descriptorValues.at("2").at("bindings").at("3").at("0").at("value").at("offset") == 12,
            "Later descriptor updates must affect only later submissions");
    const auto& reset = capture->events.at(capture->executions[2].event);
    Require(reset.pipeline == 0 && reset.state->at("vertexBuffers").empty(), "CB reset leaked pipeline/buffer state");
    Require(capture->objects.at(50).created == 2 && capture->objects.at(50).destroyed == 4 && capture->pipelines.at(70)[0].module == 50,
            "Destroyed shader metadata must remain linked to existing pipelines");
    Require(capture->pipelines.at(70)[0].entry == "entry_a" && capture->pipelines.at(70)[0].specialization.at("dataSize") == 4,
            "Captured entry/specialization metadata was dropped");
    Require(capture->executions[0].notes.size() >= 2, "Indirect counts and omitted push bytes must be explicitly diagnosed");
    Require(capture->SearchText(12).find("vertex-data") != std::string::npos && capture->SearchText(16).find("Opaque") != std::string::npos,
            "Event search must include object names, block identities, and labels");

    Save(file, {
        Call(1, "vkCreateDescriptorSetLayout", Json::parse(R"({"pSetLayout":80,"pCreateInfo":{"flags":"0x2","pBindings":[{"binding":0,"descriptorCount":1}]}})")),
        Call(2, "vkAllocateDescriptorSets", {{"pDescriptorSets", {90}}, {"pAllocateInfo", {{"pSetLayouts", {80}}}}}),
        Call(3, "vkBeginCommandBuffer", {{"commandBuffer", 10}}),
        Call(4, "vkCmdBindDescriptorSets", {{"commandBuffer", 10}, {"pipelineBindPoint", 1}, {"pDescriptorSets", {90}}}),
        Call(5, "vkCmdDispatch", {{"commandBuffer", 10}}),
        Call(6, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {10}}}}}})
    });
    capture = ReadCapture(file);
    Require(capture->executions[0].descriptorValues.at("0").at("exactGpuValueUnavailable").get<bool>(), "Update-after-bind must not be shown as exact GPU contents");
    Capture editing;
    editing.pipelines[1] = {{10, "VK_SHADER_STAGE_VERTEX_BIT", "main"}};
    editing.pipelineInfo[1] = Json::object();
    Require(editing.ModuleEditLimitation(10).empty(), "Ordinary module path was rejected");
    Require(!editing.ModuleEditLimitation(0).empty(), "Inline/shader-object identity zero was accepted");
    for (const auto* type : {"VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR", "VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT", "VK_STRUCTURE_TYPE_PIPELINE_BINARY_INFO_KHR", "VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO"}) {
        editing.pipelineInfo[1] = {{"pNext", {{"sType", type}}}};
        Require(!editing.ModuleEditLimitation(10).empty(), "Unvalidated pipeline path was enabled for editing");
    }
    editing.pipelineInfo[1] = Json::object(); editing.events.push_back(Event{}); editing.events.back().name = "vkCreatePipelineBinariesKHR";
    Require(!editing.ModuleEditLimitation(10).empty(), "Captured pipeline binaries bypassed the replacement safeguard");
    Save(file, {
        Call(1, "vkBeginCommandBuffer", {{"commandBuffer", 10}}),
        Call(2, "vkCmdBindPipeline", {{"commandBuffer", 10}, {"pipelineBindPoint", 0}, {"pipeline", 70}}),
        Call(3, "vkCmdBindShadersEXT", {{"commandBuffer", 10}}),
        Call(4, "vkCmdDraw", {{"commandBuffer", 10}}),
        Call(5, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {10}}}}}})
    });
    capture = ReadCapture(file);
    Require(capture->events.at(capture->executions[0].event).pipeline == 0, "Unsupported shader-object draws reused the previous ordinary pipeline");
    Require(!capture->executions[0].notes.empty(), "Unsupported shader-object state lacks a diagnostic");
    std::filesystem::remove(file);
}
} // namespace gfx::tests
