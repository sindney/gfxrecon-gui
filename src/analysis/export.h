#pragma once
// Deterministic JSON serializers shared by `gfxrecon-cli` queries and the structured dump
// (schema gfxrecon-dump/1). The same function produces a query result and the file the dump writes,
// so `draw --block B` output equals that draw's draw.json byte-for-byte (after dump(2)).
// Link convention: every path inside exported JSON is relative to the DUMP ROOT (e.g. "pipelines/12.json",
// "shaders/28", "frames/0001/passes/000-renderpass-b141/draws/0000-b143-vkCmdDraw"); no "../" is ever emitted.
#include "analysis/capture.h"
#include <istream>

namespace gfx {
// One pass: a render pass / dynamic rendering scope of one command-buffer invocation, or a
// compute pseudo-pass of consecutive executions outside render scopes. (Transfer commands are not
// executions, so `transfer` pseudo-passes are never produced by GroupPasses; the kind stays reserved.)
struct Pass {
    uint32_t frame = 1;
    uint32_t ordinal = 0;               // 0-based order within the frame (submission order)
    std::string kind;                   // renderpass | rendering | compute | transfer
    uint64_t submit = 0, begin = 0;     // queue submit block, recording begin block of the owning (root) invocation
    std::vector<uint64_t> scope;        // render scope blocks (begin, [next subpass...], end); empty for pseudo-passes
    std::vector<size_t> executions;     // indexes into Capture::executions, submission order
    uint32_t firstOrdinal = 0;          // ordinal-in-frame of executions[0]
    std::string Directory() const;      // "<PPP>-<kind>-b<firstBlock>" sortable within the frame
    uint64_t FirstBlock() const { return firstBlock; }
    uint64_t firstBlock = 0;
};
struct FramePasses {
    uint32_t frame = 1;
    std::vector<Pass> passes;
    std::vector<std::pair<uint32_t, uint32_t>> swapchains; // swapchain image extents seen in the capture
};
// Groups all submitted executions of every frame (index 0 = frame 1) into passes in submission order.
std::vector<FramePasses> GroupPasses(const Capture& capture);
// Heuristic pass classification {"label": scene|shadow|postprocess|ui|compute|transfer|unknown,
// "heuristic": true, "evidence": [..]}. `frame` gives neighbours for ordering evidence.
Json ClassifyPass(const Capture& capture, const FramePasses& frame, size_t pass);

// Relative locations used across the dump tree.
std::string FrameDirectory(uint32_t frame);                         // "frames/0002"
std::string DrawDirectory(const Capture& capture, size_t execution, uint32_t ordinalInFrame); // "<DDDD>-b<block>-<vkCmd>"
std::string PipelinePath(uint64_t pipeline);                        // "pipelines/<id>.json"
std::string ShaderPath(uint64_t module);                            // "shaders/<module>"

// Where an execution lives in the tree.
struct DrawLocation { uint32_t frame = 0; size_t pass = 0; uint32_t ordinal = 0; std::string path; size_t execution = 0; uint32_t occurrence = 0; };
// Pass grouping plus execution -> location lookup. Build once per capture.
struct DumpIndex {
    std::vector<FramePasses> frames;
    std::vector<DrawLocation> draws; // by execution index
    static DumpIndex Build(const Capture& capture);
};
DrawLocation LocateDraw(const Capture& capture, const std::vector<FramePasses>& frames, size_t execution);

// Serializers (see link convention above).
Json ExportCapture(const Capture& capture, const std::filesystem::path& source, const std::string& sourceSha256);
Json ExportFrame(const Capture& capture, const DumpIndex& index, const FramePasses& frame);
Json ExportPass(const Capture& capture, const DumpIndex& index, const FramePasses& frame, size_t pass);
Json ExportDraw(const Capture& capture, const DumpIndex& index, size_t execution);
Json ExportPipeline(const Capture& capture, uint64_t pipeline);
Json ExportObject(const Capture& capture, uint64_t id);
Json ExportEvent(const Capture& capture, size_t event, std::istream* stream = nullptr); // {block, name, frame, args, ...}; reuse `stream` (opened on capture.jsonl) when exporting many
// Draw-slot status object used for not-yet-produced dynamic artifacts.
Json StaticDumpSkipped();
} // namespace gfx
