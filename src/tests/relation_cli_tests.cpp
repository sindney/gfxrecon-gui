#include "analysis/relations.h"
#include "analysis/export.h"
#include "cli/cli.h"

#include <fstream>
#include <sstream>

namespace gfx::tests {
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(std::string("Relation CLI: ") + message); }
Json Call(uint64_t index, const char* name, Json args) { return {{"index", index}, {"function", {{"name", name}, {"args", args}}}}; }
std::string Read(const std::filesystem::path& file) { std::ifstream in(file, std::ios::binary); std::ostringstream text; text << in.rdbuf(); return text.str(); }
}
void RelationCliTests(const std::filesystem::path& root) {
    using namespace gfx::cli;
    RegisterAll();
    const auto directory = root / "relation-cli"; std::filesystem::create_directories(directory);
    const auto captureFile = directory / "capture.jsonl";
    const uint64_t pipeline = UINT64_MAX - 1, shader = UINT64_MAX;
    const Json graphicsInfo{{"pStages", Json::array({Json{{"stage", "VK_SHADER_STAGE_VERTEX_BIT"}, {"module", shader}, {"pName", "main"}}})},
                            {"pRasterizationState", {{"cullMode", "VK_CULL_MODE_BACK_BIT"}}}};
    const std::vector<Json> records{
        Call(1, "vkCreateShaderModule", {{"pShaderModule", shader}, {"pCreateInfo", {{"codeSize", 0}}}}),
        Call(2, "vkCreateGraphicsPipelines", {{"pPipelines", {pipeline}}, {"pCreateInfos", Json::array({graphicsInfo})}}),
        Call(3, "vkBeginCommandBuffer", {{"commandBuffer", 10}}),
        Call(4, "vkCmdBindPipeline", {{"commandBuffer", 10}, {"pipelineBindPoint", "VK_PIPELINE_BIND_POINT_GRAPHICS"}, {"pipeline", pipeline}}),
        Call(5, "vkCmdDraw", {{"commandBuffer", 10}, {"vertexCount", 3}, {"instanceCount", 1}}),
        Call(6, "vkCmdDrawIndirect", {{"commandBuffer", 10}, {"drawCount", 16}, {"buffer", 20}, {"offset", 0}, {"stride", 16}}),
        Call(7, "vkEndCommandBuffer", {{"commandBuffer", 10}}),
        Call(8, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {10}}}}}}),
        Call(9, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {10}}}}}}),
        Call(10, "vkQueuePresentKHR", Json::object())};
    { std::ofstream out(captureFile, std::ios::binary); for (const auto& record : records) out << record.dump() << '\n'; }
    const auto capture = ReadCapture(captureFile); Require(capture->executions.size() == 4, "repeated submitted fixture");
    const auto settings = (directory / "settings").u8string();
    auto invoke = [&](std::vector<std::string> args, int expected = kOk, bool cancelled = false) {
        args.insert(args.end(), {"--settings-dir", settings, "--quiet"});
        std::ostringstream out; std::atomic_bool cancel{cancelled};
        const auto exit = Main(args, out, cancel);
        const auto document = Json::parse(out.str());
        if (exit != expected) throw std::runtime_error("Relation CLI unexpected exit: " + document.dump());
        Require(document.at("ok") == (expected == kOk), "single JSON envelope");
        return document;
    };
    for (const auto& command : {"executions", "draws", "pipelines", "shaders", "passes", "stats", "fields"}) {
        std::ostringstream help; std::atomic_bool cancel{false};
        Require(Main({"query", command, "--help"}, help, cancel) == kOk && help.str().find("Usage:") != std::string::npos, "registered command help");
    }
    const auto fields = invoke({"query", "fields", "--entity", "executions"});
    Require(fields.at("ok") == true, "catalog does not require a source");
    Require(!std::filesystem::exists(directory / "settings" / "sessions" / "cache"), "catalog has no capture side effects");
    invoke({"query", "draws"}, kUsage);
    invoke({"query", "draws", "--index", "absent", "--capture", "absent"}, kUsage);
    invoke({"query", "draws", "--capture", "absent", "--columns", "bogus_field"}, kUsage);
    invoke({"query", "draws", "--capture", "absent", "--pass", "0"}, kUsage);
    invoke({"query", "draws", "--index", "absent", "--frame", "4294967296"}, kUsage);
    invoke({"query", "draws", "--index", "absent", "--shader", "-1"}, kUsage);
    invoke({"query", "draws", "--index", "absent", "--kind", "dispatch"}, kUsage);
    invoke({"query", "draws", "--index", "absent", "--limit", "1001"}, kUsage);
    invoke({"query", "draws", "--index", "absent", "--all"}, kUsage);
    invoke({"query", "draws", "--index", "absent", "--export-format", "jsonl"}, kUsage);
    invoke({"draw", "--capture", "absent", "--execution", "0", "--block", "5"}, kUsage);
    invoke({"draw", "--capture", "absent", "--execution", "0", "--occurrence", "0"}, kUsage);
    invoke({"draw", "--capture", "absent", "--execution", "0", "--columns", "bogus_field"}, kUsage);
    invoke({"pipeline", "1", "--capture", "absent", "--section", "bogus_section"}, kUsage);
    const auto first = invoke({"draw", "--capture", captureFile.u8string(), "--execution", "0"}).at("result");
    Require(first == ExportDraw(*capture, DumpIndex::Build(*capture), 0), "execution zero preserves full dump-equivalent document");
    const auto repeated = invoke({"draw", "--capture", captureFile.u8string(), "--block", "5", "--occurrence", "1"}).at("result");
    Require(repeated == ExportDraw(*capture, DumpIndex::Build(*capture), 2), "legacy block occurrence preserves full document");
    Require(invoke({"pipeline", std::to_string(pipeline), "--capture", captureFile.u8string()}).at("result") == ExportPipeline(*capture, pipeline), "legacy pipeline equality");
    Require(invoke({"object", std::to_string(shader), "--capture", captureFile.u8string()}).at("result") == ExportObject(*capture, shader), "legacy object equality");
    const auto projected = invoke({"draw", "--capture", captureFile.u8string(), "--execution", "0", "--columns", "execution,pipeline_id", "--section", "rasterization"}).at("result");
    Require(projected.at("schema") == kRelationQuerySchema && projected.at("rows").size() == 1 && projected.at("rows")[0].at("execution") == 0, "opt-in draw projection");
    const auto pipelineProjected = invoke({"pipeline", std::to_string(pipeline), "--capture", captureFile.u8string(), "--columns", "pipeline_id"}).at("result");
    Require(pipelineProjected.at("schema") == kRelationQuerySchema && pipelineProjected.at("rows")[0].at("pipeline_id") == pipeline, "opt-in pipeline projection uint64");
    const std::string complexName = "pipe,\"quoted\"\nsecond line";
    capture->objects[pipeline].name = complexName;
    const auto snapshot = directory / "snapshot.sqlite"; BuildRelationSnapshot(*capture, snapshot);
    const auto source = snapshot.u8string();
    const auto page = invoke({"query", "draws", "--index", source, "--columns", "execution,index_count,pipeline_id", "--limit", "1", "--count"}).at("result");
    Require(page.at("rows").size() == 1 && page.at("page").at("has_more") == true && page.at("page").at("total") == 4, "bounded list and explicit count");
    const auto cursor = page.at("page").at("next_cursor").get<std::string>();
    invoke({"query", "draws", "--index", source, "--columns", "execution", "--cursor", cursor}, kUsage);
    const auto jsonl = directory / "rows.jsonl";
    const auto receipt = invoke({"query", "draws", "--index", source, "--columns", "execution,index_count,pipeline_id", "--limit", "1", "--export", jsonl.u8string(), "--all"}).at("result");
    Require(!receipt.contains("rows") && receipt.at("export").at("rows") == 4, "streaming export compact receipt");
    { std::ifstream in(jsonl); std::string line; uint64_t ordinal = 0; while (std::getline(in, line)) { const auto row = Json::parse(line); Require(row.at("execution") == ordinal++ && row.at("pipeline_id") == pipeline && row.at("index_count").is_null(), "JSONL order, uint64 and null"); } Require(ordinal == 4, "all pages exported"); }
    const auto csv = directory / "rows.csv";
    invoke({"query", "draws", "--index", source, "--columns", "execution,index_count,pipeline_id", "--limit", "1", "--export", csv.u8string(), "--export-format", "csv"});
    const auto csvBytes = Read(csv);
    Require(csvBytes.find("\"execution\",\"index_count\",\"pipeline_id\"\r\n") == 0 && csvBytes.find(",,\"18446744073709551614\"") != std::string::npos, "CSV field order, null and exact unsigned encoding");
    const auto complexCsv = directory / "complex.csv";
    invoke({"query", "pipelines", "--index", source, "--columns", "pipeline_id,name", "--section", "rasterization", "--export", complexCsv.u8string(), "--export-format", "csv"});
    const auto complexBytes = Read(complexCsv);
    Require(complexBytes.find("\"pipe,\"\"quoted\"\"\nsecond line\"") != std::string::npos && complexBytes.find("\"{\"\"") != std::string::npos, "CSV quotes, embedded newline and structured compact JSON");
    invoke({"query", "draws", "--index", source, "--export", snapshot.u8string()}, kUsage);
    OpenRelationIndex(snapshot); // Rejected export did not replace the database.
    const auto previous = Read(jsonl);
    invoke({"query", "draws", "--index", source, "--export", jsonl.u8string(), "--all"}, kCancelled, true);
    Require(Read(jsonl) == previous, "cancelled export preserves existing destination");
    // A standalone snapshot still works after its decoded source disappears.
    std::filesystem::rename(captureFile, directory / "capture.offline");
    const auto offline = invoke({"query", "draws", "--index", source, "--columns", "execution", "--count"}).at("result");
    Require(offline.at("rows").size() == 4, "offline explicit-index path");
    Require(ErrorJson(std::make_exception_ptr(RelationError(RelationErrorKind::Data, "corrupt_index", "bad"))).second.at("code") == "corrupt_index", "typed relation error code retained");
    Require(ErrorJson(std::make_exception_ptr(RelationError(RelationErrorKind::MissingTool, "missing_tool", "no converter", {{"tool", "convert"}}))).first == kMissingTool, "relation missing converter uses exit4");
    std::filesystem::remove_all(directory);
}
} // namespace gfx::tests
