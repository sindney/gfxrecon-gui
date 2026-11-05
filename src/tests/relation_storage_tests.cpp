#include "analysis/relations_internal.h"
#include "analysis/export.h"
#include "analysis/index.h"
#include "core/artifacts.h"

#include <fstream>
#include <limits>
#include <stdexcept>

namespace gfx::tests {
namespace {
using namespace relation_detail;
namespace fs = std::filesystem;
void Require(bool condition, const std::string& reason) {
    if (!condition) throw std::runtime_error("Relation storage: " + reason);
}
template<class F> void Fails(F action, const std::string& code) {
    try { action(); }
    catch (const RelationError& error) {
        Require(error.code == code, "Expected " + code + ", received " + error.code + ": " + error.what()); return;
    }
    throw std::runtime_error("Relation storage accepted invalid input: " + code);
}
void Text(const fs::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc); stream << text;
    if (!stream) throw std::runtime_error("Cannot write relation fixture");
}
Capture Fixture(const fs::path& root) {
    Capture capture; capture.source = root / "capture.gfxr"; capture.jsonl = root / "capture.jsonl";
    capture.frames = 2; capture.limitations = {"Synthetic submitted-execution fixture"};
    Text(capture.source, "public relation fixture, not a Vulkan binary");
    capture.fingerprint = Sha256File(capture.source);
    constexpr uint64_t pipeline = uint64_t(std::numeric_limits<int64_t>::max()) + 17;
    constexpr uint64_t module = std::numeric_limits<uint64_t>::max();
    std::ofstream stream(capture.jsonl, std::ios::binary);
    auto event = [&](uint64_t block, const std::string& name, Json arguments, uint64_t bound = 0) {
        Event e; e.block = block; e.name = name; e.offset = static_cast<uint64_t>(stream.tellp());
        e.commandBuffer = 100; e.pipeline = bound; e.draw = name.rfind("vkCmdDraw", 0) == 0 || name.rfind("vkCmdDispatch", 0) == 0;
        Json state{{"dynamic", {{"vkCmdSetCullMode", {{"cullMode", "VK_CULL_MODE_NONE"}}}}},
            {"vertexBuffers", {{"0", {{"buffer", 71}, {"offset", 4096}}}}},
            {"descriptorSets", {{"graphics", {{"0", {{"set", 72}}}}}}}};
        e.state = std::make_shared<const Json>(std::move(state));
        stream << Json{{"index", block}, {"function", {{"name", name}, {"args", arguments}}}}.dump() << '\n';
        capture.events.push_back(std::move(e));
    };
    event(10, "vkCmdBeginRenderPass", Json::object());
    event(20, "vkCmdDraw", {{"vertexCount", module}, {"instanceCount", 0}, {"firstVertex", 0}, {"firstInstance", 0}}, pipeline);
    event(30, "vkCmdDrawIndexedIndirectCount", {{"buffer", 71}, {"maxDrawCount", 99}, {"drawCount", 40}, {"countBuffer", 73}}, pipeline);
    event(40, "vkCmdDrawIndexed", {{"indexCount", 0}, {"instanceCount", 2}}, 99);
    event(50, "vkCmdDispatch", {{"groupCountX", 8}, {"groupCountY", 4}, {"groupCountZ", 1}}, 101);
    event(std::numeric_limits<uint64_t>::max(), "vkCmdDraw", {{"vertexCount", 3}, {"instanceCount", 1}});
    stream.close();
    capture.pipelineInfo[pipeline] = {{"layout", 80}, {"pRasterizationState", {{"cullMode", "VK_CULL_MODE_BACK_BIT"}}}, {"pDynamicState", {{"pDynamicStates", {"VK_DYNAMIC_STATE_CULL_MODE"}}}}};
    capture.pipelines[pipeline] = {{module, "VK_SHADER_STAGE_VERTEX_BIT", "vertexMain", {{"map", 3}}}, {module, "VK_SHADER_STAGE_FRAGMENT_BIT", "fragmentMain", nullptr}};
    capture.pipelineInfo[101] = {{"stage", {{"stage", "VK_SHADER_STAGE_COMPUTE_BIT"}}}, {"layout", 80}};
    capture.pipelines[101] = {{0, "VK_SHADER_STAGE_COMPUTE_BIT", "main", nullptr}};
    for (uint64_t id : {201, 202, 203, 204, 205, 206}) {
        capture.pipelineInfo[id] = capture.pipelineInfo[pipeline]; capture.pipelines[id] = capture.pipelines[pipeline];
    }
    capture.pipelineInfo[201]["flags"] = 0x800;
    capture.pipelineInfo[202]["flags"] = "0x00000800";
    capture.pipelineInfo[203]["flags"] = "VK_PIPELINE_CREATE_LIBRARY_BIT_KHR";
    capture.pipelineInfo[204]["pNext"] = {{"sType", "VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR"}, {"flags", "0x00000800"}};
    capture.pipelineInfo[205]["pNext"] = {{"sType", "VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO"}, {"flags", "VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR"}};
    capture.pipelineInfo[206]["pNext"] = {{"sType", 1000470005}, {"flags", 0x800}};
    capture.objects[module] = {"ShaderModule", "unsigned-max-module", 5, 0, Json::object()};
    capture.objects[pipeline] = {"Pipeline", "unsigned-pipeline", 6, 0, capture.pipelineInfo[pipeline]};
    capture.objects[71] = {"Buffer", "vertices", 7, 0, Json::object()};
    auto execute = [&](size_t index, uint32_t frame, uint64_t submit, uint32_t slot) {
        Execution x; x.event = index; x.frame = frame; x.submit = submit; x.begin = 9; x.generation = 2;
        if (index != 4) x.renderScope = {10, 60};
        x.primary = slot == 0; x.invocationPath = {100, 200};
        x.invocations = {{100, 9, 2, 0, slot, x.renderScope, nullptr}, {200, 8, 4, 19, slot, {}, nullptr}};
        x.descriptorValues = {{"bindings", {{"0", {{"buffer", 71}, {"bytes", "THIS_DESCRIPTOR_PAYLOAD_MUST_NOT_BE_DUPLICATED"}}}}}};
        capture.executions.push_back(std::move(x));
    };
    execute(1, 1, 1000, 0); execute(1, 1, 1000, 1); execute(1, 2, 1001, 0);
    execute(2, 2, 1001, 0); execute(3, 2, 1001, 0); execute(4, 2, 1001, 0); execute(5, 2, 1001, 0);
    capture.executions.back().valid = false; capture.executions.back().unavailable = "Unresolved recording context";
    return capture;
}
void SetMeta(const fs::path& path, const std::string& key, const Json& value) {
    Database db(path, true); Statement update(db, "UPDATE meta SET value=?1 WHERE key=?2");
    update.Text(1, value.dump()); update.Text(2, key); update.Step();
}
}

void RelationStorageTests(const fs::path& root) {
    const auto work = root / "relation-storage"; fs::create_directories(work);
    for (uint64_t id : {uint64_t(0), uint64_t(1), uint64_t(std::numeric_limits<int64_t>::max()),
                       uint64_t(std::numeric_limits<int64_t>::max()) + 1, std::numeric_limits<uint64_t>::max()}) {
        const auto encoded = EncodeId(id); Require(DecodeId(encoded.data(), 8) == id, "Unsigned ID codec lost precision");
    }
    const auto smaller = EncodeId(uint64_t(std::numeric_limits<int64_t>::max()));
    const auto greater = EncodeId(uint64_t(std::numeric_limits<int64_t>::max()) + 1);
    Require(smaller < greater, "Big-endian BLOB ordering does not follow unsigned ordering");
    Fails([&] { DecodeId(smaller.data(), 7); }, "invalid_index");
    Fails([&] { DecodeId(nullptr, 8); }, "invalid_index");

    auto capture = Fixture(work); const auto expected = DumpIndex::Build(capture);
    const auto snapshot = BuildRelationSnapshot(capture, work / "snapshot.sqlite");
    Require(!snapshot.reused && snapshot.metadata.at("complete") == true, "Built snapshot not complete");
    Require(snapshot.metadata.at("parser_schema") == kIndexSchema && snapshot.metadata.at("grouping_version") == 1,
        "Snapshot omitted parser/grouping identity");
    Require(snapshot.metadata.at("source_sha256") == Sha256File(capture.source) && snapshot.metadata.at("decoded_sha256") == Sha256File(capture.jsonl),
        "Snapshot source identity differs from fixture");
    Require(snapshot.metadata.at("counts").at("executions") == 7, "Repeated invocations were deduplicated");
    const auto before = Sha256File(snapshot.path);
    Fails([&] { BuildRelationSnapshot(capture, snapshot.path); }, "index_target_exists");
    Require(Sha256File(snapshot.path) == before, "Existing target was changed by build refusal");
    {
        Database db(snapshot.path); ValidateDatabase(db);
        Statement rows(db, "SELECT execution,block,occurrence,frame,pass,pipeline_id,vertex_count,index_count,instance_count,relation_status,unavailable,dynamic_json,resources_json,source_json FROM executions ORDER BY execution");
        size_t n = 0;
        while (rows.Step()) {
            Require(rows.IdColumn(0) == n, "Execution identity changed");
            Require(rows.IdColumn(1) == capture.events[capture.executions[n].event].block, "Block ID changed");
            Require(rows.CountColumn(2) == expected.draws[n].occurrence, "Occurrence differs from DumpIndex");
            Require(rows.CountColumn(3) == expected.draws[n].frame && rows.CountColumn(4) == expected.draws[n].pass,
                "Frame/pass membership differs from DumpIndex");
            const auto source = Json::parse(rows.TextColumn(13));
            Require(source["dump_availability"] == "not_generated" && source["logical_dump_path"] == expected.draws[n].path + "/draw.json",
                "Logical dump evidence was falsely marked as generated");
            Require(source["invocations"][1]["slot"] == capture.executions[n].invocations[1].ordinal,
                "Secondary invocation slot was lost");
            Require(rows.TextColumn(12).find("THIS_DESCRIPTOR_PAYLOAD") == std::string::npos,
                "Compact resource refs copied descriptor payload");
            Require(Json::parse(rows.TextColumn(11))["calls"].contains("vkCmdSetCullMode"), "Recorded dynamic rasterization override missing");
            if (n < 3) {
                Require(rows.IdColumn(5) == uint64_t(std::numeric_limits<int64_t>::max()) + 17, "Pipeline ID crossed signed boundary incorrectly");
                Require(rows.IdColumn(6) == std::numeric_limits<uint64_t>::max(), "Recorded vertex count lost UINT64_MAX");
                Require(rows.IdColumn(8) == 0, "Valid zero instance count became null");
            }
            if (n == 3) {
                Require(rows.IdColumn(6).is_null() && rows.IdColumn(7).is_null() && rows.IdColumn(8).is_null(),
                    "Indirect maximum/drawCount was presented as measured geometry");
                Require(!Json::parse(rows.TextColumn(10)).empty(), "Missing indirect counts have no unavailable reason");
            }
            if (n == 4) Require(rows.TextColumn(9) == "unresolved" && rows.IdColumn(7) == 0, "Missing pipeline or valid zero index count mishandled");
            if (n == 5) Require(rows.TextColumn(9) == "partial", "Null shader module was incorrectly resolved");
            if (n == 6) Require(rows.IdColumn(5).is_null() && rows.TextColumn(9) == "unresolved", "Unbound pipeline was fabricated");
            ++n;
        }
        Require(n == 7, "Execution scan lost rows");
        Statement libraries(db, "SELECT relation_status FROM pipelines WHERE id=?1");
        for (uint64_t id : {201, 202, 203, 204, 205, 206}) {
            libraries.Id(1, id); Require(libraries.Step() && libraries.TextColumn(0) == "partial", "Flag-only library pipeline was incorrectly resolved"); libraries.Reset();
        }
        Statement edges(db, "SELECT module_id,stage,entry,specialization_json FROM pipeline_stages WHERE pipeline_id=?1 AND module_id IS NOT NULL ORDER BY ordinal");
        edges.Id(1, uint64_t(std::numeric_limits<int64_t>::max()) + 17);
        Require(edges.Step() && edges.IdColumn(0) == std::numeric_limits<uint64_t>::max() && edges.TextColumn(1) == "vertex" && edges.TextColumn(2) == "vertexMain" && Json::parse(edges.TextColumn(3))["map"] == 3,
            "Vertex stage identity/specialization changed");
        Require(edges.Step() && edges.TextColumn(1) == "fragment" && edges.TextColumn(2) == "fragmentMain" && !edges.Step(),
            "Duplicate-module stage edge was collapsed");
        Statement badCount(db, "SELECT -1"); Require(badCount.Step(), "Count guard fixture did not return a row");
        Fails([&] { badCount.CountColumn(0); }, "invalid_index");
        Statement badId(db, "SELECT 9007199254740993"); Require(badId.Step(), "ID guard fixture did not return a row");
        Fails([&] { badId.IdColumn(0); }, "invalid_index");
    }
    const auto knownPipeline = uint64_t(std::numeric_limits<int64_t>::max()) + 17;
    const auto recordedInfo = capture.pipelineInfo.at(knownPipeline);
    capture.pipelineInfo[knownPipeline] = nullptr;
    const auto partial = BuildRelationSnapshot(capture, work / "missing-pipeline-info.sqlite");
    {
        Database db(partial.path);
        Statement status(db, "SELECT relation_status FROM pipelines WHERE id=?1"); status.Id(1, knownPipeline);
        Require(status.Step() && status.TextColumn(0) == "partial", "Missing pipeline metadata was marked resolved");
    }
    capture.pipelineInfo[knownPipeline] = recordedInfo;
    const auto rebuilt = BuildRelationSnapshot(capture, work / "second.sqlite");
    Require(rebuilt.snapshot != snapshot.snapshot, "New generation reused a cursor snapshot identity");
    std::atomic_bool cancel{true};
    Fails([&] { BuildRelationSnapshot(capture, work / "cancel-before.sqlite", &cancel); }, "cancelled");
    Require(!fs::exists(work / "cancel-before.sqlite"), "Pre-cancelled build created a database");
    cancel = false;
    Fails([&] { BuildRelationSnapshot(capture, work / "cancel-during.sqlite", &cancel, [&](const std::string& message) {
        if (message.find("Materializing") != std::string::npos) cancel = true;
    }); }, "cancelled");
    Require(!fs::exists(work / "cancel-during.sqlite"), "Cancelled build left a publishable database");
    Fails([&] { BuildRelationSnapshot(capture, work / "changed.sqlite", nullptr, [&](const std::string& message) {
        if (message.find("Validating") != std::string::npos) Text(capture.source, "source changed during build");
    }); }, "source_changed");
    Require(!fs::exists(work / "changed.sqlite"), "Source-mutated build left a database");
    Text(capture.source, "public relation fixture, not a Vulkan binary");

    auto copy = [&](const std::string& name) { const auto path = work / (name + ".sqlite"); fs::copy_file(snapshot.path, path); return path; };
    const auto incomplete = copy("incomplete"); SetMeta(incomplete, "complete", false);
    Fails([&] { OpenRelationIndex(incomplete); }, "invalid_index");
    const auto foreign = work / "foreign.sqlite";
    { Database db(foreign, true); db.Exec("CREATE TABLE authored(value TEXT); INSERT INTO authored VALUES('preserve');"); }
    Fails([&] { OpenRelationIndex(foreign); }, "relation_index_unowned");
    const auto future = copy("future"); { Database db(future, true); db.Exec("PRAGMA user_version=999;"); }
    Fails([&] { OpenRelationIndex(future); }, "relation_index_version");
    const auto missing = copy("missing-table"); { Database db(missing, true); db.Exec("DROP TABLE executions;"); }
    Fails([&] { OpenRelationIndex(missing); }, "invalid_index");
    const auto malformed = copy("malformed-metadata");
    { Database db(malformed, true); db.Exec("UPDATE meta SET value='invalid JSON' WHERE key='counts';"); }
    Fails([&] { OpenRelationIndex(malformed); }, "invalid_index");
    const auto futureParser = copy("future-parser"); SetMeta(futureParser, "parser_schema", 999);
    Fails([&] { OpenRelationIndex(futureParser); }, "relation_index_version");
    Text(work / "not-sqlite", "an authored document");
    Fails([&] { OpenRelationIndex(work / "not-sqlite"); }, "relation_storage");
    fs::remove(capture.source); fs::remove(capture.jsonl);
    const auto offline = OpenRelationIndex(snapshot.path);
    Require(offline.snapshot == snapshot.snapshot && offline.metadata.at("counts").at("executions") == 7,
        "Offline snapshot required external source files");
    Require(Sha256File(snapshot.path) == before, "Read-only snapshot open mutated its contents");
    fs::remove_all(work);
}
} // namespace gfx::tests
