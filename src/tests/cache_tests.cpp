#include "analysis/cache.h"
#include "core/artifacts.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace gfx::tests {
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Text(const std::filesystem::path& path, const std::string& text) { std::ofstream out(path, std::ios::binary); out << text; }
std::string Fixture(int vertices) {
    return "{\"index\":1,\"function\":{\"name\":\"vkBeginCommandBuffer\",\"args\":{\"commandBuffer\":10}}}\n"
        "{\"index\":2,\"function\":{\"name\":\"vkCmdBeginRendering\",\"args\":{\"commandBuffer\":10}}}\n"
        "{\"index\":3,\"function\":{\"name\":\"vkCmdDraw\",\"args\":{\"commandBuffer\":10,\"vertexCount\":" + std::to_string(vertices) + "}}}\n"
        "{\"index\":4,\"function\":{\"name\":\"vkCmdEndRendering\",\"args\":{\"commandBuffer\":10}}}\n"
        "{\"index\":5,\"function\":{\"name\":\"vkQueueSubmit\",\"args\":{\"pSubmits\":[{\"pCommandBuffers\":[10]}]}}}\n";
}
}
void RunCacheTests(const std::filesystem::path& root) {
    const auto abc = root / "sha.txt"; Text(abc, "abc");
    Require(Sha256File(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 differs from the standard abc test vector");
    const auto source = root / "capture with spaces.jsonl", cache = root / "owned-cache";
    Text(source, Fixture(3));
    int builds = 0;
    auto builder = [&](const std::filesystem::path& stage) {
        ++builds; std::filesystem::copy_file(source, stage / "capture.jsonl"); return ReadCapture(stage / "capture.jsonl");
    };
    auto first = OpenCachedCapture(source, cache, "a", builder);
    Require(!first.hit && builds == 1, "Initial index was not built");
    auto second = OpenCachedCapture(source, cache, "a", builder);
    Require(second.hit && builds == 1, "Unchanged input reran conversion/indexing");
    Require(second.capture->DumpRequest(0) == first.capture->DumpRequest(0), "Persisted index changed execution selection");
    Require(second.capture->events[2].state != nullptr && second.capture->ReadEvent(2)["function"]["args"]["vertexCount"] == 3,
            "Cached state or paged API details were lost");
    { std::ofstream damage(second.directory / "index.bin", std::ios::binary | std::ios::app); damage << 'x'; }
    auto repaired = OpenCachedCapture(source, cache, "a", builder);
    Require(!repaired.hit && builds == 2, "Corrupt cached index was accepted");
    Text(source, Fixture(4)); // same byte length, different contents
    auto changed = OpenCachedCapture(source, cache, "a", builder);
    Require(!changed.hit && builds == 3 && changed.fingerprint != first.fingerprint, "Same-size input mutation reused stale identities");
    Require(changed.capture->ReadEvent(2)["function"]["args"]["vertexCount"] == 4, "Changed capture arguments are stale");
    auto differentTool = OpenCachedCapture(source, cache, "b", builder);
    Require(!differentTool.hit && builds == 4 && differentTool.directory != changed.directory, "Tool identity change did not invalidate the cache");
    auto marker = differentTool.directory / "cache.json";
    { std::ifstream in(marker); auto json = Json::parse(in); json["schema"] = kIndexSchema + 1; in.close(); AtomicJson(marker, json); }
    Require(!OpenCachedCapture(source, cache, "b", builder).hit && builds == 5, "Unsupported cache schema was accepted");
    bool failed = false;
    try { OpenCachedCapture(source, cache, "c", [&](const auto& stage) -> std::shared_ptr<Capture> { Text(stage / "capture.jsonl", "{partial"); throw std::runtime_error("injected interrupted conversion"); }); }
    catch (...) { failed = true; }
    Require(failed && !OpenCachedCapture(source, cache, "c", builder).hit, "Interrupted import published a completed cache");
    std::atomic_bool cancel{true}; failed = false;
    try { OpenCachedCapture(source, cache, "a", builder, &cancel); } catch (...) { failed = true; }
    Require(failed, "Cancelled cache operation continued");
    Require(std::filesystem::weakly_canonical(cache).parent_path() == std::filesystem::weakly_canonical(root), "Test cleanup escaped its temporary root");
    std::filesystem::remove_all(cache); std::filesystem::remove(source); std::filesystem::remove(abc);
}
void BenchmarkIndex(const std::filesystem::path& root) {
    const auto input = root / "100k.jsonl", index = root / "100k.index";
    {
        std::ofstream out(input, std::ios::binary);
        out << "{\"index\":1,\"function\":{\"name\":\"vkBeginCommandBuffer\",\"args\":{\"commandBuffer\":10}}}\n";
        for (int i = 2; i <= 100000; ++i)
            out << "{\"index\":" << i << ",\"function\":{\"name\":\"vkCmdSetViewport\",\"args\":{\"commandBuffer\":10,\"firstViewport\":0,\"pViewports\":[{\"x\":0,\"y\":0,\"width\":1024,\"height\":1024}]}}}\n";
    }
    const auto start = std::chrono::steady_clock::now();
    auto capture = ReadCapture(input);
    const auto parsed = std::chrono::steady_clock::now();
    WriteCaptureIndex(*capture, index); capture.reset();
    const auto written = std::chrono::steady_clock::now();
    capture = ReadCaptureIndex(input, index);
    const auto loaded = std::chrono::steady_clock::now();
    Require(capture->events.size() == 100000, "100k benchmark lost calls");
    Require(capture->ReadEvent(99999)["index"] == 100000, "100k benchmark did not page the final record");
    const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    Json report{{"calls", 100000}, {"jsonl_bytes", std::filesystem::file_size(input)}, {"index_bytes", std::filesystem::file_size(index)},
        {"parse_ms", ms(start, parsed)}, {"write_ms", ms(parsed, written)}, {"cached_load_ms", ms(written, loaded)}};
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) report["peak_working_set_bytes"] = memory.PeakWorkingSetSize;
#endif
    AtomicJson(root / "index-benchmark.json", report); std::cout << report.dump(2) << '\n';
}
} // namespace gfx::tests
