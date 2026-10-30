#include "analysis/dump_dynamic.h"
#include "analysis/resources.h"
#include "core/image_io.h"
#include "core/image_decode.h"
#include "platform/process.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace gfx {
namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;

// Identifier shared by the merged request and the artifact metadata: (queueSubmit, begin) is a
// context row; (block) identifies the draw within it.
struct RowKey { uint64_t submit = 0, begin = 0; bool operator<(const RowKey& o) const { return std::tie(submit, begin) < std::tie(o.submit, o.begin); } };
struct ArtifactKey { uint64_t submit = 0, block = 0; bool operator<(const ArtifactKey& o) const { return std::tie(submit, block) < std::tie(o.submit, o.block); } };

RowKey ContextRow(const Execution& e) {
    RowKey row{e.submit, 0};
    // Primary begin block of the outermost invocation; secondaries chain to it.
    row.begin = e.invocations.empty() ? e.begin : e.invocations.front().begin;
    return row;
}
// The block the backend reports in drawIndex/dispatchIndex for this execution.
uint64_t DrawBlock(const Capture& capture, const Execution& e) { return capture.events[e.event].block; }
bool IsDispatch(const Capture& capture, const Execution& e) { return capture.events[e.event].name.rfind("vkCmdDispatch", 0) == 0; }

Json SelectorFor(const Capture& capture, size_t execution, std::string& reason) {
    try { return capture.DumpRequest(execution); }
    catch (const std::exception& error) { reason = error.what(); return Json(); }
}

fs::path DrawOutputsDir(const fs::path& out, const std::string& drawPath) { return out / fs::u8path(drawPath) / "outputs"; }
fs::path DrawInputsDir(const fs::path& out, const std::string& drawPath) { return out / fs::u8path(drawPath) / "inputs"; }
fs::path PassOutputsDir(const fs::path& out, const std::string& passDir) { return out / fs::u8path(passDir) / "outputs"; }

std::string TimestampUtc() { const auto t = std::time(nullptr); char b[32]; std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t)); return b; }

Json StatusJson(const std::string& status, const std::string& reason = {}) {
    Json j{{"status", status}}; if (!reason.empty()) j["reason"] = reason; return j;
}

// The selectors to replay for each tier. Returns ordered per-frame execution lists.
struct TierPlan {
    std::vector<size_t> frameImages;             // frames needing frame.png (the last frame replay captures the frame image)
    std::map<uint32_t, std::vector<size_t>> drawExecutions; // frame -> executions to dump outputs for
    bool full = false;
    std::map<uint32_t, std::vector<size_t>> passEnds;       // frame -> last execution per pass (pass outputs)
};
TierPlan PlanTier(const Capture& capture, const DumpIndex& index, const DynamicDumpOptions& options) {
    TierPlan plan;
    plan.full = options.tier == "full";
    const bool passTier = options.tier == "pass";
    const bool drawsTier = options.tier == "draws";
    const bool frameTier = options.tier == "frame";
    for (const auto& frame : index.frames) {
        const auto f = frame.frame;
        if (options.frames.count(f) == 0) continue;   // --frames restricts both static and dynamic work
        if (frameTier) plan.frameImages.push_back(f);
        if (passTier) {
            plan.frameImages.push_back(f);
            for (const auto& pass : frame.passes) if (!pass.executions.empty()) plan.passEnds[f].push_back(pass.executions.back());
        }
        if (drawsTier || plan.full) {
            for (const auto& pass : frame.passes) for (const auto e : pass.executions) {
                if (!options.filtered.empty() && std::find(options.filtered.begin(), options.filtered.end(), e) == options.filtered.end()) continue;
                plan.drawExecutions[f].push_back(e);
            }
            // Pass-tier post-process passes get full detail too.
            if (passTier) {
                for (size_t p = 0; p < frame.passes.size(); ++p) {
                    const auto& pass = frame.passes[p];
                    const auto post = options.postprocessPass.find({f, static_cast<uint32_t>(p)});
                    if (post != options.postprocessPass.end() && post->second) for (const auto e : pass.executions) plan.drawExecutions[f].push_back(e);
                }
            }
        }
    }
    return plan;
}

// Split executions into batches of at most `batchDraws`, keeping each (frame, row) contiguous where
// possible so one context's draws stay together.
std::vector<std::vector<size_t>> Batches(const std::vector<size_t>& executions, size_t batchDraws) {
    std::vector<std::vector<size_t>> batches;
    if (executions.empty()) return batches;
    std::vector<size_t> current;
    std::map<RowKey, size_t> rowCount; // per-row draw count of the current batch
    for (const auto e : executions) {
        current.push_back(e);
        if (current.size() >= batchDraws) { batches.push_back(current); current.clear(); rowCount.clear(); }
    }
    if (!current.empty()) batches.push_back(current);
    return batches;
}

// Move/copy one replay artifact into the dump tree. Images are converted to PNG; ASTC raw blocks are
// decoded when possible. Returns the slot JSON for draw/pass JSON.
Json PlaceArtifact(const fs::path& out, const fs::path& relative, const ResourceArtifact& artifact,
                   const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel, uint64_t& bytesWritten) {
    Json slot = artifact.metadata;
    slot["status"] = "ok"; slot["role"] = artifact.role;
    const auto ext = artifact.file.extension().u8string();
    const auto dest = out / relative;
    fs::create_directories(dest.parent_path());
    if (ext == ".bmp") {
        auto png = dest; png.replace_extension(".png");
        try { ConvertToPng(artifact.file, png); slot["file"] = fs::relative(png, out).generic_u8string(); slot["format_decoded"] = "png"; }
        catch (const std::exception& e) {
            fs::copy_file(artifact.file, dest, fs::copy_options::overwrite_existing);
            slot["file"] = fs::relative(dest, out).generic_u8string(); slot["png_status"] = StatusJson("unavailable", e.what());
        }
        bytesWritten += fs::file_size(slot["file"].get<std::string>().empty() ? dest : out / fs::u8path(slot["file"].get<std::string>()));
        return slot;
    }
    if (ext == ".png") {
        fs::copy_file(artifact.file, dest, fs::copy_options::overwrite_existing);
        slot["file"] = fs::relative(dest, out).generic_u8string(); bytesWritten += fs::file_size(dest); return slot;
    }
    // Raw binary (buffers, compressed images, depth).
    fs::copy_file(artifact.file, dest, fs::copy_options::overwrite_existing);
    bytesWritten += fs::file_size(dest);
    slot["file"] = fs::relative(dest, out).generic_u8string();
    if (IsAstcArtifact(artifact.file, artifact.metadata)) {
        auto png = dest; png.replace_extension(".bin.png");
        const auto decoded = DecodeCompressedImage(dest, artifact.metadata, png, tools, cancel);
        if (decoded.value("status", "") == "ok") { slot["png"] = fs::relative(png, out).generic_u8string(); slot["decoded_by"] = decoded.value("decoded_by", ""); slot["decode_mode"] = decoded.value("mode", ""); }
        else slot["png_status"] = decoded;
    } else slot["png_status"] = StatusJson("unavailable", "raw binary; not an image");
    return slot;
}

// Provenance for a resumed batch: identical content requires identical inputs.
Json BatchProvenance(const DynamicDumpOptions& options, const std::string& captureSha256, size_t batchIndex, const std::vector<size_t>& executions,
                     const ReplayTarget& target) {
    Json exec = Json::array(); for (const auto e : executions) exec.push_back(e);
    return {{"capture_sha256", captureSha256}, {"tier", options.tier}, {"full", options.full}, {"batch_draws", options.batchDraws},
        {"batch", batchIndex}, {"executions", exec}, {"target", target.Label()}};
}

fs::path BatchFile(const fs::path& out, size_t n) {
    char name[32]; std::snprintf(name, sizeof(name), "%04u.json", static_cast<unsigned>(n));
    return out / "dynamic" / "batches" / name;
}
} // namespace

Json BuildBatchDumpRequest(const Capture& capture, const std::vector<size_t>& executions, Json& rejected) {
    rejected = Json::array();
    // Merge per (submit, begin) row. Upstream takes BeginCommandBuffer/QueueSubmit as parallel lists,
    // with Draw/Dispatch/RenderPass/ExecuteCommands rows aligned to the same BeginCommandBuffer index.
    struct Row { std::set<uint64_t> draws, dispatches; std::set<std::vector<uint64_t>> passes; std::map<uint64_t, std::set<uint64_t>> execute; };
    std::map<RowKey, Row> rows;
    bool anyDraw = false, anyDispatch = false, anyExecute = false;
    for (const auto e : executions) {
        std::string reason; const auto request = SelectorFor(capture, e, reason);
        if (request.is_null()) { rejected.push_back({{"execution", e}, {"reason", reason}}); continue; }
        const auto& bcb = request.at("BeginCommandBuffer"), qs = request.at("QueueSubmit");
        // A single-execution request has one row; merge its content into the matching batch row.
        for (size_t i = 0; i < bcb.size(); ++i) {
            auto& row = rows[RowKey{qs[i].get<uint64_t>(), bcb[i].get<uint64_t>()}];
            if (request.contains("Draw")) { anyDraw = true; for (const auto& d : request["Draw"][i]) row.draws.insert(d.get<uint64_t>()); }
            if (request.contains("Dispatch")) { anyDispatch = true; for (const auto& d : request["Dispatch"][i]) row.dispatches.insert(d.get<uint64_t>()); }
            if (request.contains("RenderPass")) for (const auto& range : request["RenderPass"][i]) row.passes.insert(range.get<std::vector<uint64_t>>());
            if (request.contains("ExecuteCommands")) for (const auto& entry : request["ExecuteCommands"][i]) {
                if (entry.empty()) continue; anyExecute = true;
                for (size_t k = 1; k < entry.size(); ++k) row.execute[entry[0].get<uint64_t>()].insert(entry[k].get<uint64_t>());
            }
        }
    }
    Json merged{{"BeginCommandBuffer", Json::array()}, {"QueueSubmit", Json::array()}};
    if (anyDraw) { merged["Draw"] = Json::array(); merged["RenderPass"] = Json::array(); }
    if (anyDispatch) merged["Dispatch"] = Json::array();
    if (anyExecute) merged["ExecuteCommands"] = Json::array();
    for (const auto& [key, row] : rows) {
        merged["QueueSubmit"].push_back(key.submit); merged["BeginCommandBuffer"].push_back(key.begin);
        if (anyDraw) { merged["Draw"].push_back(row.draws); merged["RenderPass"].push_back(row.passes); }
        if (anyDispatch) merged["Dispatch"].push_back(row.dispatches);
        if (anyExecute) {
            Json entries = Json::array();
            for (const auto& [call, secondaries] : row.execute) { Json entry{call}; for (const auto s : secondaries) entry.push_back(s); entries.push_back(entry); }
            merged["ExecuteCommands"].push_back(entries);
        }
    }
    return merged;
}

DynamicDumpResult RunDynamicDump(const Capture& capture, const DumpIndex& index, const fs::path& out,
                                 const DynamicDumpOptions& options, const ReplayTarget& target,
                                 const std::map<std::string, std::string>& tools, const fs::path& replayCache,
                                 const std::string& captureSha256, std::chrono::seconds timeout,
                                 const std::atomic_bool* cancel, const std::function<void(const std::string&)>& progress) {
    DynamicDumpResult result;
    if (options.tier == "none") return result;
    const auto plan = PlanTier(capture, index, options);
    fs::create_directories(out / "dynamic" / "batches");
    uint64_t budgetUsed = 0;

    // Frame images (tiers frame/pass): one replay per frame. We dump nothing extra; --screenshots
    // captures the frame. Reuse the replay's own BMP->PNG.
    std::vector<size_t> frameList;
    for (const auto f : plan.frameImages) if (std::find(frameList.begin(), frameList.end(), f) == frameList.end()) frameList.push_back(f);
    size_t batchIndex = 0;
    for (const auto frame : frameList) {
        const auto batchFile = BatchFile(out, batchIndex);
        const auto provenance = Json{{"kind", "frame"}, {"frame", frame}, {"capture_sha256", captureSha256}, {"tier", options.tier}, {"target", target.Label()}};
        if (fs::is_regular_file(batchFile)) {
            try { std::ifstream in(batchFile); const auto done = Json::parse(in); if (done.value("provenance", Json()) == provenance && done.value("status", "") == "ok") {
                result.batches.push_back(done); ++batchIndex; continue; } } catch (...) {}
        }
        ReplayRequest request;
        request.capture = capture.source; request.expectedCaptureHash = captureSha256; request.cacheDirectory = replayCache;
        request.frame = frame; request.prefix = "baseline"; request.outputsOnly = false; request.timeout = timeout; request.optimize = true;
        const auto dir = out / "dynamic" / ("frame-" + std::to_string(frame));
        fs::remove_all(dir); request.output = dir;
        if (progress) progress("Replaying frame " + std::to_string(frame));
        Json record{{"kind", "frame"}, {"frame", frame}, {"provenance", provenance}, {"batch_index", batchIndex}};
        const auto started = std::chrono::steady_clock::now();
        try {
            const auto r = ReplayCapture(request, target, tools, cancel);
            record["status"] = "ok"; record["timings"] = r.timings; ++result.replays;
            if (!r.image.empty()) {
                const auto dest = out / "frames" / ([&] { char b[8]; std::snprintf(b, sizeof(b), "%04u", frame); return std::string(b); }()) / "frame.png";
                fs::create_directories(dest.parent_path());
                std::string err;
                try { ConvertToPng(r.image, dest); result.bytesWritten += fs::file_size(dest); record["frame_png"] = fs::relative(dest, out).generic_u8string(); }
                catch (const std::exception& e) { err = e.what(); }
                if (!err.empty()) { fs::copy_file(r.image, fs::path(dest).replace_extension(".bmp"), fs::copy_options::overwrite_existing); record["frame_bmp"] = true; record["png_error"] = err; }
            }
        } catch (const std::exception& e) { record["status"] = "failed"; record["error"] = e.what(); if (result.error.empty()) result.error = e.what(); }
        record["wall_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        AtomicJson(batchFile, record); result.batches.push_back(record); ++batchIndex;
        budgetUsed += result.bytesWritten;
    }

    // Draw batches: merged selectors, outputs for pass/draws tiers, full for full tier. A batch must
    // stay within one frame: --quit-after-frame stops the replay at that frame, so a batch spanning
    // frames would only dump its first frame's draws.
    std::vector<std::vector<size_t>> batches;
    {
        std::map<uint32_t, std::vector<size_t>> byFrame;
        auto add = [&](uint32_t frame, size_t e, bool dedup) {
            auto& list = byFrame[frame];
            if (dedup && std::find(list.begin(), list.end(), e) != list.end()) return;
            list.push_back(e);
        };
        for (const auto& [frame, list] : plan.drawExecutions) for (const auto e : list) add(frame, e, true);
        for (const auto& [frame, list] : plan.passEnds) for (const auto e : list) add(frame, e, true);
        for (const auto& [frame, list] : byFrame) {
            auto sub = Batches(list, std::max<size_t>(options.batchDraws, 1));
            for (auto& b : sub) batches.push_back(std::move(b));
        }
    }
    for (const auto& batch : batches) {
        if (cancel && cancel->load()) break;
        const auto batchFile = BatchFile(out, batchIndex);
        const auto provenance = BatchProvenance(options, captureSha256, batchIndex, batch, target);
        if (fs::is_regular_file(batchFile)) {
            try { std::ifstream in(batchFile); const auto done = Json::parse(in); if (done.value("provenance", Json()) == provenance && done.value("status", "") == "ok") {
                result.batches.push_back(done); for (const auto& d : done.value("draws", Json::array())) result.bytesWritten += d.value("bytes", uint64_t(0)); ++batchIndex; continue; } } catch (...) {}
        }
        Json rejected; const auto dump = BuildBatchDumpRequest(capture, batch, rejected);
        ReplayRequest request;
        request.capture = capture.source; request.expectedCaptureHash = captureSha256; request.cacheDirectory = replayCache;
        request.frame = capture.executions[batch.front()].frame; request.prefix = "baseline"; request.outputsOnly = !options.full;
        request.dump = dump; request.timeout = timeout; request.optimize = true; request.drawInspection = options.full; request.descriptorImages = options.full;
        const auto dir = out / "dynamic" / ("batch-" + std::to_string(batchIndex));
        fs::remove_all(dir); request.output = dir;
        if (progress) progress("Replay batch " + std::to_string(batchIndex) + " (" + std::to_string(batch.size()) + " draws)");
        Json record{{"kind", "draws"}, {"batch_index", batchIndex}, {"draw_count", batch.size()}, {"provenance", provenance}, {"rejected", rejected}};
        const auto started = std::chrono::steady_clock::now();
        // Split-and-retry on LOW_MEMORY/storage failure.
        auto attempt = [&](size_t size) -> std::pair<bool, ReplayResult> {
            std::vector<size_t> sub(batch.begin(), batch.begin() + std::min(size, batch.size()));
            Json rej; request.dump = BuildBatchDumpRequest(capture, sub, rej);
            fs::remove_all(dir);
            try { return {true, ReplayCapture(request, target, tools, cancel)}; }
            catch (const std::exception& e) {
                if (cancel && cancel->load()) throw;
                return {false, ReplayResult{{}, std::string(e.what())}};
            }
        };
        auto [ok, replayResult] = attempt(batch.size());
        if (!ok) {
            const auto text = replayResult.log;
            const bool memory = text.find("LOW_MEMORY") != std::string::npos || text.find("out of memory") != std::string::npos || text.find("storage") != std::string::npos;
            if (memory && batch.size() > 1) {
                if (progress) progress("Batch too large (memory/storage); splitting and retrying");
                // Split into two halves and record as sub-batches.
                Json halves = Json::array(); bool allOk = true;
                for (size_t half = 0; half < 2; ++half) {
                    std::vector<size_t> sub(batch.begin() + half * batch.size() / 2, batch.begin() + (half + 1) * batch.size() / 2);
                    fs::remove_all(dir);
                    Json rej; request.dump = BuildBatchDumpRequest(capture, sub, rej);
                    try { ReplayCapture(request, target, tools, cancel); halves.push_back({{"half", half}, {"status", "ok"}}); }
                    catch (const std::exception& e) { halves.push_back({{"half", half}, {"status", "failed"}, {"error", e.what()}}); allOk = false; }
                }
                record["split"] = halves; record["status"] = allOk ? "ok" : "failed"; ++result.replays;
                if (!allOk && result.error.empty()) result.error = "a split half failed";
            } else { record["status"] = "failed"; record["error"] = replayResult.log; if (result.error.empty()) result.error = replayResult.log; }
        } else record["status"] = "ok";
        record["timings"] = replayResult.timings;
        record["wall_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (ok) {
            ++result.replays;
            // Map artifacts back to executions via (queueSubmitIndex, drawIndex|dispatchIndex).
            std::map<ArtifactKey, std::vector<size_t>> owners;
            for (const auto e : batch) owners[{capture.executions[e].submit, DrawBlock(capture, capture.executions[e])}].push_back(e);
            // Which executions need per-draw slots: pass ends get pass-level output only; tier draws get
            // outputs (and inputs when full). A pass end that is also a tier draw counts once, as a draw.
            std::set<size_t> isPassEnd;
            for (const auto& [frame, list] : plan.passEnds) for (const auto e : list) isPassEnd.insert(e);
            std::set<size_t> tierDraw;
            for (const auto& [frame, list] : plan.drawExecutions) for (const auto e : list) tierDraw.insert(e);
            std::map<size_t, Json> drawSlots; Json draws = Json::array();
            for (const auto& artifact : replayResult.resources) {
                const auto& m = artifact.metadata;
                const uint64_t submit = m.value("queueSubmitIndex", uint64_t(0));
                const uint64_t block = m.value("drawIndex", m.value("dispatchIndex", uint64_t(0)));
                const auto found = owners.find({submit, block});
                if (found == owners.end()) { continue; } // not part of this batch's selection
                if (found->second.size() > 1) {
                    for (const auto e : found->second) {
                        const auto& loc = index.draws[e];
                        drawSlots[e] = Json::object();
                        drawSlots[e]["status"] = "unavailable"; drawSlots[e]["reason"] = "artifact metadata is ambiguous (shared submit/block)";
                    }
                    continue;
                }
                const auto e = found->second.front();
                const auto& loc = index.draws[e];
                auto& slots = drawSlots[e];
                const bool isOutput = m.value("category", "") == "output" || m.value("category", "").empty();
                const bool isDraw = tierDraw.count(e) != 0;
                // Pass ends write pass-level outputs; draw-tier executions write per-draw outputs/inputs.
                fs::path destDir;
                if (isDraw) destDir = isOutput ? DrawOutputsDir(out, loc.path) : DrawInputsDir(out, loc.path);
                else {
                    const auto& pass = index.frames[loc.frame - 1].passes[loc.pass];
                    destDir = PassOutputsDir(out, (fs::path(FrameDirectory(loc.frame)) / "passes" / pass.Directory()).generic_u8string());
                }
                auto slot = PlaceArtifact(out, fs::relative(destDir, out) / artifact.file.filename(), artifact, tools, cancel, result.bytesWritten);
                slots[isOutput ? "outputs" : "inputs"].push_back(slot);
                if (!isDraw) slot["pass_output"] = true;
            }
            // Emit slots. Pass ends are not counted as draws; they get pass outputs. Tier draws that got
            // nothing are unavailable. Draws that were only included as batch context are left as skipped.
            for (const auto e : batch) {
                auto& slots = drawSlots[e];
                const auto& loc = index.draws[e];
                const bool isDraw = tierDraw.count(e) != 0, isEnd = isPassEnd.count(e) != 0;
                if (slots.is_null()) {
                    if (isDraw) { slots = {{"status", "unavailable"}, {"reason", "no artifacts returned for this draw"}}; ++result.drawsUnavailable; }
                    else if (isEnd) slots = {{"status", "unavailable"}, {"reason", "no pass-output artifacts returned"}}; // pass ends: not counted as draws
                    else continue; // batch context only
                } else if (isDraw) ++result.drawsOk;
                Json d{{"execution", e}, {"path", loc.path}, {"slots", slots}, {"bytes", 0}, {"role", isDraw ? "draw" : "pass_output"}};
                draws.push_back(d);
                // Merge the dynamic slots into the draw's draw.json.
                const auto drawFile = out / fs::u8path(loc.path) / "draw.json";
                if (fs::is_regular_file(drawFile)) {
                    try { std::ifstream in(drawFile); auto doc = Json::parse(in); in.close();
                        doc["dynamic"] = slots;
                        std::ofstream o(drawFile, std::ios::binary | std::ios::trunc); o << doc.dump(2) << "\n";
                    } catch (...) {}
                }
                // Pass ends also write pass.json's outputs.
                if (isEnd && !isDraw) {
                    const auto& pass = index.frames[loc.frame - 1].passes[loc.pass];
                    const auto passFile = out / FrameDirectory(loc.frame) / "passes" / pass.Directory() / "pass.json";
                    if (fs::is_regular_file(passFile)) {
                        try { std::ifstream in(passFile); auto doc = Json::parse(in); in.close();
                            doc["outputs"] = slots;
                            std::ofstream o(passFile, std::ios::binary | std::ios::trunc); o << doc.dump(2) << "\n";
                        } catch (...) {}
                    }
                }
            }
            record["draws"] = draws;
            record["artifacts"] = replayResult.resources.size();
        }
        AtomicJson(batchFile, record); result.batches.push_back(record); ++batchIndex;
        // Budget: stop scheduling further batches once exceeded; remaining draws are "skipped".
        if (options.maxBytes && result.bytesWritten >= options.maxBytes) {
            if (progress) progress("Byte budget reached; remaining dynamic batches skipped");
            break;
        }
    }
    return result;
}
} // namespace gfx
