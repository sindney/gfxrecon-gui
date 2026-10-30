// Dynamic dump tiers for `dump --dynamic`: batched resource-dump replays that add replay-derived
// artifacts (frame image, per-pass outputs, per-draw outputs, full per-draw inputs) to a static dump tree.
#pragma once
#include "analysis/export.h"
#include "core/artifacts.h"
#include "replay/replay.h"
#include <set>

namespace gfx {
// Merges the per-execution selectors of `executions` (indexes into Capture::executions) into one
// batched --dump-resources request, grouped per (QueueSubmit, BeginCommandBuffer) row. This is the
// BuildBatchDumpRequest of task 6.1. Executions whose selector cannot be built are returned in `rejected`
// with the reason; they are never silently added to the batch.
Json BuildBatchDumpRequest(const Capture& capture, const std::vector<size_t>& executions, Json& rejected);

struct DynamicDumpOptions {
    std::string tier = "none";           // none|frame|pass|draws|full
    size_t batchDraws = 64;              // selector rows per replay (memory/storage bound on device)
    uint64_t maxBytes = 0;               // 0: no budget; beyond it dynamic slots are "skipped" (budget)
    std::vector<size_t> filtered;        // executions selected by --draws (a tier that needs filters)
    bool full = false;                   // tier == "full" (full inputs/vertex data)
    std::set<uint32_t> frames;           // frames to dump (from --frames); empty set is rejected by caller
    std::map<std::pair<uint32_t, uint32_t>, bool> postprocessPass; // frame,pass -> classified postprocess
};
struct DynamicDumpResult {
    uint64_t replays = 0, bytesWritten = 0, artifacts = 0;
    uint64_t drawsOk = 0, drawsFailed = 0, drawsUnavailable = 0, drawsSkipped = 0;
    Json batches = Json::array();        // per dynamic/batches/<n>.json
    std::string error;                   // empty when the dynamic tier completed
};
// Runs the tier's batched replays and writes dynamic artifacts into the dump tree at `out`.
// `tools`, `target`, `timeout` come from the caller's CLI context. Resumable: completed batches whose
// provenance (capture sha256, tier, options) matches are skipped.
DynamicDumpResult RunDynamicDump(const Capture& capture, const DumpIndex& index, const std::filesystem::path& out,
                                 const DynamicDumpOptions& options, const ReplayTarget& target,
                                 const std::map<std::string, std::string>& tools, const std::filesystem::path& replayCache,
                                 const std::string& captureSha256, std::chrono::seconds timeout,
                                 const std::atomic_bool* cancel, const std::function<void(const std::string&)>& progress);
} // namespace gfx
