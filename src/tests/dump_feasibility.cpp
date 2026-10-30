// Throwaway spike for the agent-cli dynamic dump feasibility gate (tasks 0.1-0.3).
// Merges single-draw dump selectors into batches and times them through ReplayCapture.
#include "replay/replay.h"
#include "platform/adb.h"
#include "core/artifacts.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace gfx::tests {
namespace {
Json MergeDumpRequests(const std::vector<Json>& requests) {
    struct Row { std::set<uint64_t> draws, dispatches; std::set<std::vector<uint64_t>> passes; std::map<uint64_t, std::set<uint64_t>> execute; };
    std::map<std::pair<uint64_t, uint64_t>, Row> rows;
    bool draws = false, dispatches = false, execute = false;
    for (const auto& request : requests) {
        for (size_t i = 0; i < request.at("BeginCommandBuffer").size(); ++i) {
            auto& row = rows[{request["QueueSubmit"][i].get<uint64_t>(), request["BeginCommandBuffer"][i].get<uint64_t>()}];
            if (request.contains("Draw")) { draws = true; for (const auto& d : request["Draw"][i]) row.draws.insert(d.get<uint64_t>()); }
            if (request.contains("RenderPass")) for (const auto& range : request["RenderPass"][i]) row.passes.insert(range.get<std::vector<uint64_t>>());
            if (request.contains("Dispatch")) { dispatches = true; for (const auto& d : request["Dispatch"][i]) row.dispatches.insert(d.get<uint64_t>()); }
            if (request.contains("ExecuteCommands")) for (const auto& entry : request["ExecuteCommands"][i]) {
                if (entry.empty()) continue;
                execute = true;
                for (size_t k = 1; k < entry.size(); ++k) row.execute[entry[0].get<uint64_t>()].insert(entry[k].get<uint64_t>());
            }
        }
    }
    Json merged{{"BeginCommandBuffer", Json::array()}, {"QueueSubmit", Json::array()}};
    if (draws) { merged["Draw"] = Json::array(); merged["RenderPass"] = Json::array(); }
    if (dispatches) merged["Dispatch"] = Json::array();
    if (execute) merged["ExecuteCommands"] = Json::array();
    for (const auto& [key, row] : rows) {
        merged["QueueSubmit"].push_back(key.first); merged["BeginCommandBuffer"].push_back(key.second);
        if (draws) { merged["Draw"].push_back(row.draws); merged["RenderPass"].push_back(row.passes); }
        if (dispatches) merged["Dispatch"].push_back(row.dispatches);
        if (execute) {
            Json entries = Json::array();
            for (const auto& [call, secondaries] : row.execute) { Json entry{call}; for (const auto s : secondaries) entry.push_back(s); entries.push_back(entry); }
            merged["ExecuteCommands"].push_back(entries);
        }
    }
    return merged;
}
double LogSeconds(const std::string& log, const char* label) {
    std::smatch match; const std::regex pattern(std::string(label) + R"(:\s*([\d.]+) seconds)");
    return std::regex_search(log, match, pattern) ? std::stod(match[1]) : -1;
}
uint64_t ExecutionBegin(const Execution& e) { return e.invocations.empty() ? e.begin : e.invocations.front().begin; }
uint64_t ExecutionScope(const Execution& e) {
    const auto& scope = e.invocations.empty() ? e.renderScope : e.invocations.front().renderScope;
    return scope.empty() ? 0 : scope.front();
}
} // namespace

// plan: comma-separated mode:batch items; mode is frame | pass | outputs | full.
void DumpFeasibility(const std::filesystem::path& source, const std::filesystem::path& jsonl, const std::string& serial,
                     const std::filesystem::path& root, const std::filesystem::path& toolFile, uint32_t frame, const std::string& plan) {
    std::ifstream toolInput(toolFile); const auto tools = Json::parse(toolInput).get<std::map<std::string, std::string>>();
    const auto loadStarted = std::chrono::steady_clock::now();
    const auto capture = ReadCapture(jsonl);
    std::cout << "Indexed " << capture->events.size() << " events, " << capture->executions.size() << " executions in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - loadStarted).count() << " s\n";
    std::map<uint32_t, size_t> perFrame;
    for (const auto& e : capture->executions) ++perFrame[e.frame];
    if (!frame) { size_t best = 0; for (const auto& [f, n] : perFrame) if (n > best) { best = n; frame = f; } }
    // Selectable executions of the frame in submission order; pass ends are the last execution before the
    // (submit, primary recording, render scope) key changes.
    std::vector<size_t> selected, passEnds; Json unavailable = Json::array();
    std::map<size_t, Json> selectors;
    std::tuple<uint64_t, uint64_t, uint64_t> previous{};
    for (size_t i = 0; i < capture->executions.size(); ++i) {
        const auto& e = capture->executions[i];
        if (e.frame != frame) continue;
        const std::tuple<uint64_t, uint64_t, uint64_t> key{e.submit, ExecutionBegin(e), ExecutionScope(e)};
        if (!selected.empty() && key != previous) passEnds.push_back(selected.back());
        previous = key;
        try { selectors[i] = capture->DumpRequest(i); selected.push_back(i); }
        catch (const std::exception& error) { unavailable.push_back({{"block", capture->events[e.event].block}, {"reason", error.what()}}); }
    }
    if (!selected.empty()) passEnds.push_back(selected.back());
    Json summary{{"frame", frame}, {"frame_executions", perFrame[frame]}, {"selectable", selected.size()}, {"passes", passEnds.size()},
        {"unavailable", unavailable}, {"executions_per_frame", Json::object()}};
    for (const auto& [f, n] : perFrame) summary["executions_per_frame"][std::to_string(f)] = n;
    std::filesystem::create_directories(root);
    AtomicJson(root / "plan.json", summary);
    std::cout << summary.dump(2) << '\n' << std::flush;
    if (serial == "plan") return;

    Json report = Json::array();
    if (std::filesystem::is_regular_file(root / "result.json")) { std::ifstream in(root / "result.json"); report = Json::parse(in); }
    const AdbConnection adb{tools.at("adb"), serial};
    std::istringstream items(plan); std::string item;
    while (std::getline(items, item, ',')) {
        const auto colon = item.find(':');
        auto mode = item.substr(0, colon);
        // "png-" prefix: backend encodes dump images as PNG instead of BMP.
        const bool png = mode.rfind("png-", 0) == 0; if (png) mode = mode.substr(4);
        const size_t batch = colon == std::string::npos ? 1 : std::stoul(item.substr(colon + 1));
        if (std::any_of(report.begin(), report.end(), [&](const Json& r) {
                return r.value("label", "") == item && std::all_of(r["batches"].begin(), r["batches"].end(), [](const Json& b) { return b.value("ok", false); }); })) {
            std::cout << item << ": already measured\n"; continue;
        }
        const auto& chosen = mode == "pass" ? passEnds : selected;
        std::vector<std::vector<size_t>> batches;
        if (mode == "frame") batches.push_back({});
        else for (size_t at = 0; at < chosen.size(); at += batch)
            batches.emplace_back(chosen.begin() + at, chosen.begin() + std::min(chosen.size(), at + batch));
        Json run{{"label", item}, {"mode", mode}, {"batch_draws", batch}, {"replays", batches.size()}, {"draws", mode == "frame" ? 0 : chosen.size()}, {"batches", Json::array()}};
        const auto started = std::chrono::steady_clock::now();
        for (size_t n = 0; n < batches.size(); ++n) {
            ReplayRequest request; request.capture = source; request.cacheDirectory = root / "cache"; request.frame = frame;
            request.output = root / ((png ? "png-" : "") + mode + "-" + std::to_string(batch) + "-" + std::to_string(n)); request.reuseArtifacts = false; request.timeout = std::chrono::seconds(900);
            std::filesystem::remove_all(request.output);
            request.progress = [](const std::string& text) { std::cerr << "  " << text << '\n'; };
            if (mode != "frame") {
                std::vector<Json> parts; for (const auto i : batches[n]) parts.push_back(selectors.at(i));
                request.dump = MergeDumpRequests(parts); request.outputsOnly = true; request.drawInspection = mode == "full";
                if (png) request.imageFormat = "png";
            }
            Json entry{{"index", n}, {"draws", batches[n].size()}};
            const auto batchStarted = std::chrono::steady_clock::now();
            try {
                const auto result = ReplayCapture(request, {true, serial}, tools);
                entry["ok"] = true; entry["timing"] = result.timings;
                entry["load_seconds"] = LogSeconds(result.log, "Load time"); entry["total_seconds"] = LogSeconds(result.log, "Total time");
                uint64_t bytes = 0, images = 0;
                for (const auto& f : std::filesystem::recursive_directory_iterator(request.output)) if (f.is_regular_file()) {
                    bytes += f.file_size(); images += f.path().extension() == ".bmp" || f.path().extension() == ".png";
                }
                entry["host_bytes"] = bytes; entry["images"] = images; entry["artifacts"] = result.resources.size();
                // Map artifacts back to executions by (submit, command block).
                std::map<std::pair<uint64_t, uint64_t>, std::vector<size_t>> owners;
                for (const auto i : batches[n]) owners[{capture->executions[i].submit, capture->events[capture->executions[i].event].block}].push_back(i);
                std::set<size_t> covered; size_t unmapped = 0, ambiguous = 0; std::set<std::string> keys;
                for (const auto& artifact : result.resources) {
                    const auto& m = artifact.metadata;
                    for (auto it = m.begin(); it != m.end(); ++it) keys.insert(it.key());
                    const auto block = m.value("drawIndex", m.value("dispatchIndex", uint64_t(0)));
                    const auto found = owners.find({m.value("queueSubmitIndex", uint64_t(0)), block});
                    if (found == owners.end()) { ++unmapped; continue; }
                    if (found->second.size() > 1) ++ambiguous;
                    covered.insert(found->second.begin(), found->second.end());
                }
                entry["executions_with_artifacts"] = covered.size(); entry["unmapped_artifacts"] = unmapped; entry["ambiguous_artifacts"] = ambiguous;
                entry["metadata_keys"] = keys;
            } catch (const std::exception& error) { entry["ok"] = false; entry["error"] = error.what(); }
            entry["wall_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - batchStarted).count();
            // ReplayCapture leaves its remote session directory; measure then remove it.
            try {
                std::ifstream session(request.output / "device-session.json"); const auto remote = Json::parse(session).at("remote").get<std::string>();
                const auto du = adb.TryShell({"run-as", "com.lunarg.gfxreconstruct.replay", "du", "-sk", remote});
                entry["device_kib"] = du.Success() ? std::stoull(du.output) : 0;
                adb.TryShell({"run-as", "com.lunarg.gfxreconstruct.replay", "rm", "-rf", remote});
            } catch (...) {}
            std::cout << item << " batch " << n + 1 << "/" << batches.size() << ": " << entry.dump() << '\n' << std::flush;
            run["batches"].push_back(entry);
            if (mode == "full" || mode == "outputs") std::filesystem::remove_all(request.output); // keep host disk bounded
        }
        run["wall_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        report.push_back(run); AtomicJson(root / "result.json", report);
        std::cout << item << ": " << run["wall_seconds"] << " s over " << batches.size() << " replays\n" << std::flush;
    }
}
} // namespace gfx::tests
