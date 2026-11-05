#include "analysis/relations.h"
#include "core/artifacts.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace gfx::tests {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr uint64_t ModuleA = std::numeric_limits<uint64_t>::max();
constexpr uint64_t ModuleB = 42;
constexpr uint64_t PipelineA = uint64_t(std::numeric_limits<int64_t>::max()) + 17;
constexpr uint64_t PipelineB = 43;
constexpr uint32_t PerFrame = 1000;
double Milliseconds(Clock::time_point before, Clock::time_point after) {
    return std::chrono::duration<double, std::milli>(after - before).count();
}
void Require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(std::string("Relation benchmark: ") + reason);
}
Json Memory() {
    Json result{{"peak_scope", "process_lifetime_including_fixture_generation_and_build"}};
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        result["status"] = "available";
        result["working_set_bytes"] = uint64_t(memory.WorkingSetSize);
        result["peak_working_set_bytes"] = uint64_t(memory.PeakWorkingSetSize);
        result["private_commit_bytes"] = uint64_t(memory.PrivateUsage);
    } else result["status"] = "unavailable";
#elif defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        result["status"] = "available";
        result["peak_working_set_bytes"] = uint64_t(usage.ru_maxrss); // macOS reports bytes.
    } else result["status"] = "unavailable";
#else
    result["status"] = "unsupported_on_this_platform";
#endif
    return result;
}
Json Distribution(std::vector<double> values) {
    if (values.empty()) return nullptr;
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    const auto median = values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
    return {{"minimum", values.front()}, {"median", median}, {"maximum", values.back()}};
}
Capture Fixture(const fs::path& work, size_t count) {
    Capture capture; capture.source = work / "generated.gfxr"; capture.jsonl = work / "generated.jsonl";
    capture.frames = uint32_t((count + PerFrame - 1) / PerFrame);
    {
        std::ofstream source(capture.source, std::ios::binary);
        source << "Synthetic relation benchmark: " << count << " submitted executions; not a replayable capture.\n";
        Require(bool(source), "Cannot write synthetic source");
    }
    capture.fingerprint = Sha256File(capture.source);
    {
        std::ofstream decoded(capture.jsonl, std::ios::binary);
        auto event = [&](uint64_t block, const char* name, uint64_t pipeline, Json args) {
            Event e; e.block = block; e.name = name; e.pipeline = pipeline; e.commandBuffer = 100;
            e.draw = pipeline != 0; e.offset = uint64_t(decoded.tellp());
            decoded << Json{{"index", block}, {"function", {{"name", name}, {"args", args}}}}.dump() << '\n';
            capture.events.push_back(std::move(e));
        };
        event(1, "vkCmdBeginRenderPass", 0, Json::object());
        event(2, "vkCmdDraw", PipelineA, {{"vertexCount", 3}, {"instanceCount", 1}, {"firstVertex", 0}, {"firstInstance", 0}});
        event(3, "vkCmdDrawIndexed", PipelineB, {{"indexCount", 6}, {"instanceCount", 2}, {"firstIndex", 0}, {"vertexOffset", 0}, {"firstInstance", 0}});
        Require(bool(decoded), "Cannot write decoded source");
    }
    capture.pipelineInfo[PipelineA] = {{"layout", 90}, {"pRasterizationState", {{"cullMode", "VK_CULL_MODE_NONE"}}}};
    capture.pipelineInfo[PipelineB] = capture.pipelineInfo[PipelineA];
    // One module referenced by two stages exercises execution cardinality through reverse joins.
    capture.pipelines[PipelineA] = {{ModuleA, "VK_SHADER_STAGE_VERTEX_BIT", "main", nullptr}, {ModuleA, "VK_SHADER_STAGE_FRAGMENT_BIT", "main", nullptr}};
    capture.pipelines[PipelineB] = {{ModuleB, "VK_SHADER_STAGE_VERTEX_BIT", "main", nullptr}, {ModuleB, "VK_SHADER_STAGE_FRAGMENT_BIT", "main", nullptr}};
    capture.objects[ModuleA] = {"ShaderModule", "benchmark unsigned module", 0, 0, Json::object()};
    capture.objects[ModuleB] = {"ShaderModule", "benchmark second module", 0, 0, Json::object()};
    capture.executions.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Execution e; e.event = i % 2 ? 2 : 1; e.frame = uint32_t(i / PerFrame + 1);
        e.submit = 1000 + e.frame; e.begin = 1; e.generation = e.frame; e.renderScope = {1, 4};
        capture.executions.push_back(std::move(e));
    }
    return capture;
}
template<class Check>
Json Measure(const RelationIndexRef& index, const RelationQuery& query, unsigned repetitions, Check check) {
    Json samples = Json::array(); std::vector<double> queryTimes, serializationTimes;
    for (unsigned i = 0; i < repetitions; ++i) {
        const auto begin = Clock::now();
        const auto result = QueryRelations(index, query);
        const auto queried = Clock::now();
        const auto encoded = result.ToJson().dump();
        const auto serialized = Clock::now();
        check(result);
        const auto queryMs = Milliseconds(begin, queried), serializationMs = Milliseconds(queried, serialized);
        queryTimes.push_back(queryMs); serializationTimes.push_back(serializationMs);
        samples.push_back({{"query_ms", queryMs}, {"serialization_ms", serializationMs},
            {"rows", result.rows.size()}, {"output_bytes", encoded.size()}, {"has_more", result.hasMore}});
    }
    return {{"repetitions", repetitions}, {"query_ms", Distribution(queryTimes)},
        {"serialization_ms", Distribution(serializationTimes)}, {"samples", samples}};
}
}

// Optional benchmark: invoke explicitly from the core-test runner, never normal startup.
Json RelationBenchmark(const fs::path& root, size_t executionCount = 100000) {
    Require(executionCount >= 2 && executionCount <= 1000000, "Execution count must be 2..1000000");
    const auto work = root / ("relation-benchmark-" + std::to_string(Clock::now().time_since_epoch().count()));
    fs::create_directories(work);
    Json report{{"schema", "gfxrecon-relation-benchmark/1"}, {"executions", executionCount},
        {"artifact_directory", work.u8string()}, {"memory_before", Memory()}};
    RelationIndexRef built;
    const auto generated = Clock::now();
    {
        const auto capture = Fixture(work, executionCount);
        const auto begin = Clock::now(); report["fixture_generation_ms"] = Milliseconds(generated, begin);
        built = BuildRelationSnapshot(capture, work / "relations.sqlite");
        report["build_ms"] = Milliseconds(begin, Clock::now());
        report["decoded_source_bytes"] = fs::file_size(capture.jsonl);
        Require(built.metadata.at("counts").at("executions") == executionCount, "Build deduplicated repeated submissions");
        Require(fs::remove(capture.source) && fs::remove(capture.jsonl), "Cannot remove synthetic sources before snapshot queries");
    } // Capture and all model vectors are destroyed before timing explicit-index queries.
    report["memory_after_model_release"] = Memory();
    report["database_bytes"] = fs::file_size(built.path); report["snapshot"] = built.snapshot;
    const auto opened = Clock::now(); const auto index = OpenRelationIndex(built.path);
    report["snapshot_open_ms"] = Milliseconds(opened, Clock::now());
    report["query_environment"] = {{"source", "explicit_immutable_snapshot"}, {"capture_model_destroyed", true},
        {"original_and_decoded_files_removed", !fs::exists(work / "generated.gfxr") && !fs::exists(work / "generated.jsonl")},
        {"index_bin_created", false}, {"converter_shader_tools_or_device_configured", false},
        {"cache_note", "New database generation; operating system file cache is not evicted. First query follows build in the same process."}};
    const uint64_t expectedA = (executionCount + 1) / 2, expectedB = executionCount / 2;
    RelationQuery shader; shader.filters.shaders = {ModuleA}; shader.columns = {"execution", "block", "pipeline_id"}; shader.limit = 100;
    auto checkPage = [&](const RelationQueryResult& result) {
        Require(result.rows.size() == std::min<uint64_t>(100, expectedA), "Shader page has wrong cardinality");
        Require(!result.total && result.hasMore == (expectedA > 100), "Unrequested total or incorrect page boundary");
        for (size_t i = 0; i < result.rows.size(); ++i)
            Require(result.rows[i].at("execution") == 2 * i && result.rows[i].at("pipeline_id") == PipelineA && result.rows[i].at("block") == 2,
                "Reverse shader edges multiplied or changed submitted executions");
        Require(result.coverage.at("candidate_executions") == executionCount && result.coverage.at("unresolved_candidate_executions") == 0,
            "Persisted global coverage differs from generated population");
    };
    report["first_shader_page"] = Measure(index, shader, 1, checkPage);
    report["hot_shader_page"] = Measure(index, shader, 20, checkPage);
    // Explicit total is measured separately so its scan cost cannot hide in default page timings.
    auto counted = shader; counted.includeCount = true;
    report["shader_page_with_count"] = Measure(index, counted, 3, [&](const RelationQueryResult& result) {
        Require(result.total && *result.total == expectedA, "Explicit shader total counts stage edges rather than executions");
    });
    RelationQuery grouped; grouped.groupBy = {"frame", "pass"}; grouped.limit = 1000;
    report["grouped_frame_pass"] = Measure(index, grouped, 5, [&](const RelationQueryResult& result) {
        Require(!result.hasMore && result.rows.size() == (executionCount + PerFrame - 1) / PerFrame, "Frame/pass group count changed");
        uint64_t total = 0;
        for (const auto& row : result.rows) {
            const auto frame = row.at("frame").get<uint64_t>();
            const auto expected = std::min<uint64_t>(PerFrame, executionCount - (frame - 1) * PerFrame);
            Require(row.at("pass") == 0 && row.at("execution_count") == expected && row.at("draw_count") == expected && row.at("dispatch_count") == 0,
                "Frame/pass aggregation disagrees with submitted population");
            total += row.at("execution_count").get<uint64_t>();
        }
        Require(total == executionCount, "Pass groups are not additive over submitted executions");
    });
    grouped.groupBy = {"pipeline_id"};
    report["grouped_pipeline"] = Measure(index, grouped, 5, [&](const RelationQueryResult& result) {
        Require(result.rows.size() == 2 && !result.hasMore, "Pipeline grouping changed");
        for (const auto& row : result.rows) {
            const auto expected = row.at("pipeline_id") == PipelineA ? expectedA : expectedB;
            Require(row.at("execution_count") == expected && row.at("draw_count") == expected, "Pipeline grouping lost repetitions");
        }
    });
    grouped.groupBy = {"shader_id"};
    report["grouped_shader"] = Measure(index, grouped, 5, [&](const RelationQueryResult& result) {
        Require(result.rows.size() == 2 && !result.hasMore && result.coverage.at("shader_groups_overlap") == true, "Shader grouping identity missing");
        for (const auto& row : result.rows) {
            const auto expected = row.at("shader_id") == ModuleA ? expectedA : expectedB;
            Require(row.at("execution_count") == expected, "Shader grouping counted duplicate stages");
        }
    });
    report["memory_after_queries"] = Memory();
    report["assertions"] = "passed: submitted cardinality, unsigned IDs, bounded pages, explicit counts, grouped parity, source-independent snapshot queries";
    report["scope"] = "Synthetic static relationship storage/query benchmark; no replay, device, readback, shader extraction, dump tree, or capture-model reload";
    AtomicJson(work / "benchmark.json", report);
    return report;
}
} // namespace gfx::tests
