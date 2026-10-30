#include "analysis/export.h"
#include "cli/cli.h"
#include "core/tools.h"
#include "platform/process.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <atomic>

namespace gfx::tests {
namespace {
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Json Call(uint64_t index, const std::string& name, Json args) { return {{"index", index}, {"function", {{"name", name}, {"args", args}}}}; }
void Save(const std::filesystem::path& path, const std::vector<Json>& records) {
    std::ofstream out(path, std::ios::binary);
    for (const auto& record : records) out << record.dump() << '\n';
}
std::string Slurp(const std::filesystem::path& path) { std::ifstream in(path, std::ios::binary); std::ostringstream s; s << in.rdbuf(); return s.str(); }
std::filesystem::path FindSample(const std::string& name) {
    auto dir = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i, dir = dir.parent_path()) {
        if (std::filesystem::is_regular_file(dir / "samples" / name)) return dir / "samples" / name;
        if (dir == dir.parent_path()) break;
    }
    return {};
}
size_t CountDraws(const std::vector<FramePasses>& frames) { size_t n = 0; for (const auto& f : frames) for (const auto& p : f.passes) n += p.executions.size(); return n; }
// Every execution appears exactly once across all passes, in submission order.
void CheckCoverage(const Capture& capture, const std::vector<FramePasses>& frames) {
    std::vector<size_t> all;
    for (const auto& f : frames) for (const auto& p : f.passes) for (const auto e : p.executions) all.push_back(e);
    Require(all.size() == capture.executions.size(), "Pass grouping dropped or duplicated executions");
    for (size_t i = 0; i < all.size(); ++i) Require(all[i] == i, "Pass grouping reordered executions");
    for (const auto& f : frames) for (size_t p = 0; p < f.passes.size(); ++p) {
        Require(f.passes[p].ordinal == p && !f.passes[p].executions.empty(), "Pass ordinals must be dense and passes non-empty");
        const auto label = ClassifyPass(capture, f, p);
        Require(label["heuristic"] == true && label["evidence"].is_array() && label["label"].is_string(), "Classification must be labeled heuristic with evidence");
    }
}
void SyntheticTests(const std::filesystem::path& root) {
    const auto file = root / "export-fixture.jsonl";
    std::vector<Json> r{
        // secondary recording
        Call(1, "vkBeginCommandBuffer", {{"commandBuffer", 20}}), Call(2, "vkCmdDraw", {{"commandBuffer", 20}, {"vertexCount", 3}}), Call(3, "vkEndCommandBuffer", {{"commandBuffer", 20}}),
        // primary: render pass with subpasses, secondary execute, then dynamic rendering, then dispatch outside scope
        Call(4, "vkBeginCommandBuffer", {{"commandBuffer", 10}}),
        Call(5, "vkCmdBeginRenderPass", {{"commandBuffer", 10}, {"pRenderPassBegin", {{"renderPass", 100}, {"framebuffer", 101}, {"renderArea", {{"extent", {{"width", 64}, {"height", 32}}}}}}}}),
        Call(6, "vkCmdDraw", {{"commandBuffer", 10}, {"vertexCount", 3}}),
        Call(7, "vkCmdNextSubpass", {{"commandBuffer", 10}}),
        Call(8, "vkCmdDraw", {{"commandBuffer", 10}, {"vertexCount", 6}}),
        Call(9, "vkCmdExecuteCommands", {{"commandBuffer", 10}, {"commandBufferCount", 1}, {"pCommandBuffers", {20}}}),
        Call(10, "vkCmdEndRenderPass", {{"commandBuffer", 10}}),
        Call(11, "vkCmdBeginRendering", {{"commandBuffer", 10}, {"pRenderingInfo", {{"renderArea", {{"extent", {{"width", 8}, {"height", 8}}}}}}}}),
        Call(12, "vkCmdDraw", {{"commandBuffer", 10}, {"vertexCount", 3}}),
        Call(13, "vkCmdEndRendering", {{"commandBuffer", 10}}),
        Call(14, "vkCmdDispatch", {{"commandBuffer", 10}}),
        Call(15, "vkCmdDispatch", {{"commandBuffer", 10}}),
        Call(16, "vkEndCommandBuffer", {{"commandBuffer", 10}}),
        Call(17, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {10}}}}}}),
        Call(18, "vkQueuePresentKHR", Json::object()),
        {{"frame", {{"marker_type", "EndMarker"}, {"frame_number", 1}}}},
        Call(19, "vkQueueSubmit", {{"pSubmits", {{{"pCommandBuffers", {10}}}}}}),
        Call(20, "vkQueuePresentKHR", Json::object()),
    };
    Save(file, r);
    const auto capture = ReadCapture(file);
    Require(capture->executions.size() == 12, "Fixture should submit 6 executions per frame");
    const auto frames = GroupPasses(*capture);
    Require(frames.size() == 2, "Fixture has two frames");
    CheckCoverage(*capture, frames);
    for (const auto& frame : frames) {
        Require(frame.passes.size() == 3, "Expected render pass, dynamic rendering and compute passes");
        const auto& pass = frame.passes[0];
        Require(pass.kind == "renderpass" && pass.executions.size() == 3 && pass.Directory().find("renderpass-b5") != std::string::npos, "Subpasses and secondary draws must stay in one render pass");
        Require(frame.passes[1].kind == "rendering" && frame.passes[1].executions.size() == 1, "Dynamic rendering pass missing");
        Require(frame.passes[2].kind == "compute" && frame.passes[2].executions.size() == 2, "Dispatches outside scopes form one compute pass");
        Require(frame.passes[0].Directory() < frame.passes[1].Directory() && frame.passes[1].Directory() < frame.passes[2].Directory(), "Pass names must sort in submission order");
    }
    const auto index = DumpIndex::Build(*capture);
    const auto& first = frames[0].passes[0];
    // draws: inline subpass 0, inline subpass 1, secondary-recorded draw (3 executions in pass 0)
    const auto d0 = ExportDraw(*capture, index, first.executions[0]), d1 = ExportDraw(*capture, index, first.executions[1]), d2 = ExportDraw(*capture, index, first.executions[2]);
    Require(d0["subpass"] == 0 && d1["subpass"] == 1, "Draws must record their subpass index");
    Require(d0["invocation_path"]["command_buffers"].size() == 1 && d2["invocation_path"]["command_buffers"].size() == 2, "Secondary draw must record its invocation path");
    Require(d2["invocation_path"]["command_buffers"][0] == 10 && d2["invocation_path"]["command_buffers"][1] == 20, "Invocation path must run root to leaf");
    for (const char* key : {"schema", "frame", "block", "execution", "occurrence", "call", "pipeline", "stages", "descriptors", "vertex_buffers", "index_buffer", "dynamic_state", "render_targets", "push_constants", "subpass", "invocation_path", "selector", "dynamic"})
        Require(d0.contains(key), "draw.json is missing a contract key");
    Require(d0["dynamic"]["outputs"]["status"] == "skipped" && d0["dynamic"]["inputs"]["status"] == "skipped" && d0["dynamic"]["mesh"]["status"] == "skipped", "Dynamic slots must be skipped in a static dump");
    Require(d0["occurrence"] == 0 && ExportDraw(*capture, index, frames[1].passes[0].executions[0])["occurrence"] == 1, "Occurrence must count executions of one block");
    Require(d0["selector"] == capture->DumpRequest(first.executions[0]), "Selector must equal Capture::DumpRequest");
    const auto pass = ExportPass(*capture, index, frames[0], 0);
    Require(pass["begin_block"] == 5 && pass["end_block"] == 10 && pass["subpass_count"] == 2 && pass["draws"].size() == 3, "pass.json block range or subpass count is wrong");
    Require(pass["classification"]["heuristic"] == true, "Classification must be labeled heuristic");
    Require(ExportDraw(*capture, index, 0) == ExportDraw(*capture, index, 0), "Export must be deterministic");
    std::filesystem::remove(file);
}
Json RunCli(const std::vector<std::string>& argv, int expectedExit = 0) {
    std::ostringstream out; std::atomic_bool cancel{false};
    const auto code = cli::Main(argv, out, cancel);
    Require(code == expectedExit, "CLI command exited with an unexpected code");
    return Json::parse(out.str());
}
void SampleTests(const std::filesystem::path& root) {
    const auto cube = FindSample("indexed-cube.gfxr"), indirect = FindSample("indirect-triangles.gfxr");
    if (cube.empty() || indirect.empty()) { std::cout << "export tests: samples/ not found, skipping sample checks\n"; return; }
    const auto repo = cube.parent_path().parent_path();
    const auto tools = ResolveTools(repo, std::filesystem::current_path());
    if (tools.count("convert") == 0 || FindExecutablePath(tools.at("convert")).empty()) { std::cout << "export tests: gfxrecon-convert unavailable, skipping sample checks\n"; return; }
    const auto settings = root / "export-settings";
    std::filesystem::create_directories(settings);
    { std::ofstream out(settings / "tools.json"); out << Json(tools).dump(); }
    const std::vector<std::string> common{"--settings-dir", settings.u8string(), "--tools", (settings / "tools.json").u8string()};
    auto cli = [&](std::vector<std::string> words, int expected = 0) { auto argv = common; argv.insert(argv.end(), words.begin(), words.end()); return RunCli(argv, expected); };
    bool sawIndirect = false;
    for (const auto& sample : {cube, indirect}) {
        const auto out = root / ("export-dump-" + sample.stem().u8string());
        const auto result = cli({"dump", "--capture", sample.u8string(), "--out", out.u8string()})["result"];
        Require(result["complete"] == true && result["manifest"]["status_counts"]["draws"].get<size_t>() > 0, "Sample dump is incomplete");
        const auto imported = ImportCapture(sample, settings / "sessions" / "cache", tools);
        const auto& capture = *imported.capture;
        const auto frames = GroupPasses(capture);
        CheckCoverage(capture, frames);
        Require(CountDraws(frames) == capture.executions.size() && !capture.executions.empty(), "Sample has no executions");
        // 3.4: `draw` query == dumped draw.json for every draw of the sample.
        const auto index = DumpIndex::Build(capture);
        for (size_t e = 0; e < capture.executions.size(); ++e) {
            const auto& event = capture.events[capture.executions[e].event];
            const auto query = cli({"draw", "--capture", sample.u8string(), "--block", std::to_string(event.block), "--occurrence", std::to_string(index.draws[e].occurrence)})["result"];
            const auto file = Json::parse(Slurp(out / index.draws[e].path / "draw.json"));
            Require(query == file, "draw query output differs from the dumped draw.json");
            Require(Slurp(out / index.draws[e].path / "draw.json") == query.dump(2) + "\n", "draw.json is not the 2-space dump of the query result");
            sawIndirect |= event.name.find("Indirect") != std::string::npos && query.contains("indirect");
        }
        for (const auto& step : {std::string("pipelines"), std::string("objects"), std::string("shaders")}) Require(std::filesystem::is_directory(out / step), "Dump tree is missing a shared directory");
        Require(std::filesystem::is_regular_file(out / "README.md") && std::filesystem::is_regular_file(out / "index.json") && std::filesystem::is_regular_file(out / "capture.json"), "Dump tree is missing top-level files");
        // Determinism: a second dump into an empty directory is byte identical outside manifest.json.
        const auto again = root / ("export-dump2-" + sample.stem().u8string());
        cli({"dump", "--capture", sample.u8string(), "--out", again.u8string()});
        size_t compared = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(out)) if (entry.is_regular_file() && entry.path().filename() != "manifest.json") {
            Require(Slurp(entry.path()) == Slurp(again / std::filesystem::relative(entry.path(), out)), "Static dump files are not deterministic");
            ++compared;
        }
        Require(compared > 5, "Determinism check compared nothing");
        // Resume / provenance.
        Require(cli({"dump", "--capture", sample.u8string(), "--out", out.u8string()})["result"]["reused"] == true, "Matching complete dump should be reused");
        const auto mismatch = cli({"dump", "--capture", sample.u8string(), "--out", out.u8string(), "--frames", "1"}, cli::kFailed);
        Require(mismatch["error"]["code"] == "provenance_mismatch", "Mismatched provenance must be refused");
        cli({"dump", "--capture", sample.u8string(), "--out", out.u8string(), "--frames", "1", "--overwrite"});
        Require(!std::filesystem::exists(out / "frames" / "0002") || capture.frames < 2, "--overwrite --frames 1 left other frames behind");
        Require(cli({"dump", "--capture", sample.u8string(), "--out", out.u8string(), "--dynamic", "full", "--overwrite"}, cli::kUsage)["error"]["message"].get<std::string>().find("--draws") != std::string::npos, "--dynamic full needs a --draws filter");
        std::filesystem::remove_all(out); std::filesystem::remove_all(again);
    }
    Require(sawIndirect, "indirect-triangles must expose indirect parameters in draw output");
    const auto info = cli({"info", "--capture", cube.u8string()})["result"];
    Require(info["frame_count"].get<int>() >= 1 && info["execution_count"].get<int>() > 0 && info.contains("limitations") && info.contains("android") && info["cache_hit"] == true, "info result is incomplete");
    const auto events = cli({"events", "--capture", cube.u8string(), "--frame", "1", "--draws-only", "--limit", "1"})["result"];
    Require(events["returned"] == 1 && events["events"][0]["draw"] == true && events["events"][0]["frame"] == 1, "events filters are wrong");
    Require(cli({"frames", "--capture", cube.u8string()})["result"]["frames"].size() == info["frame_count"].get<size_t>(), "frames result is incomplete");
    const auto pipeline = cli({"draw", "--capture", cube.u8string(), "--block", "113"})["result"]["pipeline"]["id"].get<uint64_t>();
    Require(cli({"pipeline", "--capture", cube.u8string(), std::to_string(pipeline)})["result"]["id"] == pipeline, "pipeline query failed");
    Require(cli({"object", "--capture", cube.u8string(), std::to_string(pipeline)})["result"]["type"] == "Pipeline", "object query failed");
    Require(cli({"draw", "--capture", cube.u8string(), "--block", "1"}, cli::kFailed)["error"]["code"] == "draw_not_found", "Unknown block must fail with draw_not_found");
    std::filesystem::remove_all(settings);
}
} // namespace
void RunExportTests(const std::filesystem::path& root) {
    SyntheticTests(root);
    SampleTests(root);
}
} // namespace gfx::tests
