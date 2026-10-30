## Context

GfxReconGui is a C++17/Dear ImGui Vulkan capture inspector. Its non-UI logic is already separated by directory and none of it includes ImGui:

- `analysis/` — JSONL streaming reader, `StateTracker`, versioned binary index (`kIndexSchema = 6`), content-addressed capture cache (`OpenCachedCapture`), `Capture::DumpRequest(execution)` producing upstream `--dump-resources` selectors, resource collection, mesh decode.
- `replay/` — `ReplayCapture(ReplayRequest, ReplayTarget, tools)` for local `gfxrecon-replay` and the Android replay APK: optimization, pipeline caches, content-verified result caches, shader overrides (`--replace-shaders`), validation (`ParseValidationLog`), draw visualization, lock/screen-off handling, provenance manifests.
- `capture/` — scheduled Android/Windows capture with device journals and `RestoreAndroidCapture`; `LiveCaptureSession` (thread-owned arm → trigger → retrieve state machine).
- `shaders/`, `platform/` (process, adb, Android display), `core/` (hashing, atomic JSON, logging).

Two pieces live in `gui/workspace.cpp` but are front-end-neutral: `Workspace::ImportCapture` (convert + extract + index cache) and tool discovery in `Workspace::Init` (bundled `tools/`, `local-tools.json`, `gfxrecon_tools.json`). The CMake file lists these sources three times (GUI, UI tests, core tests).

Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.

Stakeholders: an LLM agent or script driving the CLI with file tools, plus the existing GUI user who must see no regression.

## Goals / Non-Goals

**Goals:**
- One C++ core shared by GUI and CLI: same tool resolution, capture cache, replay cache, device handling and provenance.
- A CLI contract an agent can rely on: JSON stdout, stable exit codes, self-describing schema, no interactive prompts.
- Agent-driven capture of a running game in steps that each return promptly.
- A deterministic directory tree for a frame/frames with static data everywhere and opt-in dynamic replay artifacts, generated with a bounded number of replays.
- Round trip: inspect → edit shader → compile/check → replay frame or draw with override → compare → validate → measure.

**Non-Goals:**
- A persistent, resident replayer process with incremental commands (would require deep upstream replay changes; see Open Questions).
- Editing capture contents other than shader module replacement (buffer/descriptor/state edits, capture rewriting).
- Invocation-level shader debugging (already ruled out in `shader-debugging-feasibility`).
- GPU hardware counters or vendor profiler integration; performance is replay timing only.
- Non-Vulkan APIs, macOS/Linux CLI packaging (the core stays portable, but only Windows is verified).
Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.

## Decisions

### D1. C++ CLI linked against an extracted `gfxrecon_core` library (not Python)
Create `gfxrecon_core` (STATIC) from `analysis/`, `replay/`, `capture/`, `shaders/`, `core/{log,artifacts}`, `platform/{process,adb,android_display}`, and new `core/tools.*` + `analysis/import.*`. Link it from `gfxrecon_gui`, `gfxrecon_gui_tests`, `gfxrecon_core_tests` and the new `gfxrecon_cli` console target (`/SUBSYSTEM:CONSOLE`, output `gfxrecon-cli.exe`). `core/settings.cpp` (`ExeDirectory`) moves into core too.

- *Why*: the logic is already UI-free; a Python reimplementation would duplicate replay caching, provenance, adb lock handling and diverge immediately. A Python wrapper over the CLI remains possible for users.
- *Alternative*: `GfxReconGui.exe --cli` mode. Rejected: the GUI is `/SUBSYSTEM:WINDOWS`, initializes SDL/OpenGL in `main`, and attaching consoles is fragile for piping.
- Move `Workspace::ImportCapture` → `gfx::ImportCapture(file, cacheRoot, tools, cancel)` and the defaults/merge logic in `Workspace::Init` → `gfx::ResolveTools(settingsDir, exeDir)`. `Workspace` calls them; behavior unchanged.

### D2. Output and process contract
- Subcommand grammar: `gfxrecon-cli <group> [<verb>] [args] [--options]`. Global options: `--settings-dir` (default: executable directory, matching the GUI), `--tools FILE`, `--serial`, `--target auto|local|android`, `--timeout SEC`, `--quiet`.
- stdout: exactly one JSON document `{ "ok": bool, "command": ..., "result": {...} | "error": {"code","message","detail"} }`. stderr: human progress lines (the existing `progress` callbacks) and log.
- Exit codes: `0` ok; `1` operation failed; `2` usage error; `3` human action required (locked/unauthorized device, conflicting device restore); `4` missing tool/capability; `5` timeout/cancelled (Ctrl+C sets the shared `cancel` atomic).
- `describe` prints commands, options, exit codes and the dump schema version, generated from the same table that drives argument parsing so it cannot drift.
- Argument parsing: small in-house table-driven parser (no new dependency).

### D3. Concurrency and device ownership
A per-serial host lock (`<settings>/locks/<serial>.lock`, OS exclusive file lock held for the replay/capture duration) is taken by both GUI and CLI around device-mutating operations; contention returns exit `1` with `device_busy` immediately unless `--wait-device SEC`. Caches are already content-addressed with atomic manifests; concurrent writers of the same key are tolerated by the existing write-then-rename + verify-on-read logic. Each CLI invocation creates its own session directory under `<settings>/sessions/` unless `--out` is given.

### D4. Agent capture as stateless steps over a journal
`LiveCaptureSession` keeps state in a thread; a CLI process exits between steps. The Android flow is already property-driven, so split the session body into free functions reused by the GUI thread:
`ArmAndroidCapture(adb, options) -> journal`, `TriggerAndroidCapture(adb, journal, frames, timeout) -> files` (set `capture_android_trigger=true`, wait for the layer log's "Finished recording", reset trigger, retrieve with hash verification), `EndAndroidCapture(adb, journal, closeGame)` (restore).
- `capture arm --package P [--frames N] --out DIR` sets journaled properties, launches the game, returns once the layer reports armed.
- `capture trigger --session DIR [--frames N]` records and retrieves, returning file paths, sizes, SHA-256, frame count; repeatable.
- `capture status`, `capture end [--close-game]`, `capture restore --session DIR` (existing recovery).
- `capture android --package P --frames 1-3` and `capture windows --exe ... --frames ...` wrap the existing scheduled `CaptureAndroid`/`CaptureWindows`.
- Windows on-demand needs the private event owned by a live process, so only `capture windows run --trigger-after SEC|--trigger-file PATH` (a blocking single command) is offered.
- Lock screen: arm/trigger return exit `3` with `{"human_action":"unlock_device","serial":...}`; the CLI never enters credentials.
Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.

### D5. Dump tree layout (schema `gfxrecon-dump/1`)
```
<out>/
  manifest.json            # schema, capture sha256, tools/backend hashes, target, tiers, per-node status counts, complete flag
  README.md                # generated guide to this tree for agents
  capture.json             # header, device/driver, extensions, limitations, frame list
  index.json               # flat list: every pass/draw with path, frame, block, kind, pipeline, status
  objects/objects.jsonl    # one object per line: id, type, name, created/destroyed blocks, createInfo
  pipelines/<id>.json      # create info, stages (module, entry, specialization), layouts, render pass
  shaders/<module>/        # module.spv, module.glsl, module.spvasm, reflection.json, interface.json, users.json
  frames/<NNNN>/
    frame.json             # submits, command buffers, pass list, draw counts
    events.jsonl           # all API calls of the frame (block, name, args) — grep-friendly
    frame.png              # tier >= frame
    passes/<PPP>-<kind>-b<block>/          # kind: renderpass | rendering | compute | transfer
      pass.json            # begin/end blocks, attachments, load/store ops, extent, draw list
      outputs/             # tier >= pass: attachment images after the pass's last draw
      draws/<DDDD>-b<block>-<vkCmdName>/
        draw.json          # call args, pipeline id, bound shaders (paths to shaders/), descriptors, vertex/index bindings,
                           # dynamic state, render targets, push constants, indirect params, execution/invocation path,
                           # replay selector (DumpRequest), status per artifact
        outputs/           # tier >= draws: color/depth after the draw (PNG) + metadata.json
        inputs/            # tier full: sampled images/textures, storage images (PNG), buffers (.bin + layout json)
        mesh/              # tier full: vertex/index buffers and decoded attributes json
```
- Each `pass.json` carries a heuristic `classification` (`scene`, `shadow`, `postprocess`, `ui`, `compute`, `transfer`, `unknown`) plus the evidence used. Examples: a single non-indexed 3- or 4-vertex draw with no vertex buffers into a full-extent target counts as full-screen; depth-only attachments count as shadow; the last passes writing the swapchain-sized target count as post-process/UI. This tells agents where post-processing is, and lets the dump target it if the D6 gate forces pass-level dumping. It is labeled a heuristic and never used to drop data.
- Passes are derived from `Execution::renderScope` (render pass / dynamic rendering begin–end blocks) grouped per submit and command buffer; dispatches and transfers outside any render scope form `compute`/`transfer` pseudo-passes split at render-scope boundaries. Ordering is submission order, so directory sort order equals GPU order.
- Draw directories use the occurrence ordinal within the frame plus the source block; secondary command buffer executions keep the `invocationPath` in `draw.json`.
- Shared data (pipelines, shaders, objects) is written once and referenced by relative path, keeping per-draw JSON small and the tree ~O(draws) KB.
- `--frames 1,3-4|all`, `--draws` filters (block list, pipeline, pass), `--overwrite` vs default resume.
- Every artifact slot in a node carries `{"status":"ok"|"unavailable"|"failed"|"skipped","reason":...}`; nothing is silently omitted.

### D6. Dynamic tiers with batched replays
Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.
1. all draws, outputs only (color/depth after each draw);
2. all draws, full (descriptors, vertex/index buffers, depth);
3. pass level: the last draw/dispatch of each pass, outputs only;
4. per replay: wall time split into state load, execution and readback, artifact pull time, bytes on device and host, and any LOW_MEMORY or storage failures, across batch sizes (e.g. 16/64/256).

Outcome decides the tier set:
- **Within budget**: keep tiers as below, with `draws` usable for a whole frame.
Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.

The measured numbers and the chosen strategy are recorded in `docs/CLI.md`. The `structured-capture-dump` spec and tasks group 6 are revised before implementation if the over-budget path is chosen.

Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.

`--dynamic none|frame|pass|draws|full` (no replay without `--dynamic`; bare `--dynamic` = `pass`).
- `frame`: one screenshot replay per frame.
- `pass`: one batched dump replay selecting only the last draw/dispatch of each pass with outputs only.
- `draws`: outputs for every selected draw; `full`: plus descriptors, vertex/index buffers, depth.
- **Batching**: upstream dump selectors accept many `Draw`/`Dispatch` indices per `BeginCommandBuffer`/`QueueSubmit`; the core gains `BuildBatchDumpRequest(capture, executions)` merging selectors and `ReplayRequest::dump` already accepts the merged JSON. Batches are chunked by `--batch-draws N` (default 64) and a conservative estimated readback size, because full dumps of hundreds of draws can exhaust phone storage/memory (observed LOW_MEMORY exit). Each batch is one replay, so the sample's ~1,650 executions need ~26 replays at warm cost rather than 1,650.
- Repeated identical CB references that upstream cannot disambiguate (existing `DumpRequest` rejection) are marked `unavailable` with that reason.
- Artifacts returned by the backend are mapped back to draws using `drawIndex`/`beginCommandBufferIndex`/`queueSubmitIndex` metadata already parsed by `CollectResources`.
- Images: request PNG from the backend (`ImageFormat: png`, upstream-supported); if a backend returns BMP, convert host-side via `stb_image_write` (already vendored at `thirdparty/gfxreconstruct/external/stb`). Raw `.bin` is kept for non-displayable formats with format metadata.
- **Resume**: each batch writes `dynamic/batches/<n>.json` on completion; rerunning `dump` with the same tier skips completed batches whose provenance matches; the replay result cache already makes reruns cheap.

### D7. Replay and modification API
- `replay frame --capture C --frame N [--out DIR] [--override MOD=FILE]... [--validate] [--target ...]` → frame image, log, validation report, manifest.
- `replay draw --capture C --block B | --draw <dump-path> [--outputs|--full] [--visualize solid|wireframe|depth] [--subdraw K]` → `ReplayCapture` with `DumpRequest`. Replay "to a draw" means the frame is replayed with resource readback at that draw (upstream dump-resources); truncating command buffers at the draw is not offered.
- Overrides accept `.spv`, or GLSL/`.spvasm` compiled first with the configured compiler/assembler using the module's captured stage and entry; `CheckShaderInterface` must pass unless `--allow-interface-change`. Replacement affects all captured uses of the module (existing behavior) and is reported.
- `shader show MOD [--format glsl|spvasm|reflection|all]`, `shader export MOD --out DIR`, `shader compile FILE --stage S [--entry E] -o OUT.spv`, `shader check MOD CANDIDATE.spv`.
- `compare A.png B.png [--out diff.png]` → per-channel max/mean absolute difference, differing pixel count, PSNR, diff image. `replay ... --baseline` runs baseline and modified in one command and emits the comparison.
- `--validate` maps to `ReplayRequest::validation` (fresh, unoptimized, uncached) and emits `validation.json` with per-VUID counts; with `--draw` scope the report notes validation is replay-wide.
- `perf --capture C [--frames range] [--runs N] [--override ...]`: runs N fresh replays with upstream `--measurement-frame-range`/`--measurement-file` (and `--quit-after-measurement-range`), reporting load seconds, measured frame time, FPS, min/median/max across runs, plus pipeline/upload cache reuse and screen-off state; baseline-vs-override deltas when overrides are given. Results caches are bypassed; pipeline caches are allowed and recorded.
- Android measurement flags must be passed through the APK argument string; added alongside existing argument assembly in `RunAndroid` (via a new `ReplayRequest::measurement` field).

### D9. ASTC decode via bundled `astcenc`
The sample capture's textures are almost all ASTC (4x4 to 12x12, UNORM and SRGB; more than 12,000 format references). When the backend cannot blit a compressed image to RGBA8, or when raw images are requested, the dump gets `.bin` block data that agents can't view.
- **Acquisition**: the two shipped executables from the `astcenc-5.7.0-windows-x64.zip` release (archive SHA-256 `<sha256>`) are committed under `thirdparty/astcenc/` (~2.2 MB). `cmake/astcenc.cmake` verifies each binary's pinned SHA-256 and copies them into the build output; no network access at build time. `GFXRECON_ASTCENC_DIR` can point at another extracted release.
- **Shipping**: the executables are about 1.1 MB each (3.4 MB for all three variants). The distribution ships `tools/astcenc/astcenc-avx2.exe` and `astcenc-sse2.exe`, plus Apache-2.0 license text in `licenses/astc-encoder.txt` and an entry in `dependencies.json`. At runtime the core picks AVX2 when the CPU supports it, otherwise SSE2. `astcenc` becomes a resolved tool (`tools["astcenc"]`, overridable in `local-tools.json`) and is reported by `doctor`.
- **Decode path**: core `DecodeCompressedImage(artifact)` wraps raw ASTC blocks in the 16-byte `.astc` header (magic, block dimensions, image extent), runs `astcenc -dl` (UNORM) or `-ds` (SRGB) to PNG through `RunProcess`, and records `decoded_by: astcenc 5.7.0` in the image metadata. Raw `.bin` is kept. The same function serves the static dump, dynamic tiers and `replay draw`, and the GUI image viewer can call it to display raw ASTC artifacts.
- **Static texture contents**: initial ASTC texture uploads in the capture (`vkCmdCopyBufferToImage` from captured buffer data) can be decoded without replay, but this is optional and only done when the source bytes are unambiguous. Otherwise texture images come from replay.
- **Encoding** (`astcenc -cl/-cs`) is kept available as a tool for a future texture-replacement mod path. Texture overrides are not part of this change.
- *Alternative*: compile astc-encoder from source as a library. Rejected for now: it adds an ISA-specific build, while the release binary has a verified hash and is small.

### D8. Static query commands share dump serializers
Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.

## Risks / Trade-offs

- [ASTC-encoded textures are only readable as raw blocks] → D9 bundles `astcenc` and decodes host-side; formats without a decoder remain raw with `unavailable` PNG status.
Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.
- [Full dumps are huge on 1,600+ draws (images × attachments × textures)] → tiered defaults (`none`), `--draws` filters, per-batch size estimates, `--max-bytes` budget with `skipped` status when exceeded, PNG compression.
- [Batched readback may exhaust device memory/storage] → conservative default batch size, automatic halving and retry on LOW_MEMORY/storage failure, artifacts pulled and deleted from device per batch.
- [Mapping batch artifacts back to draws may be ambiguous] → rely on upstream per-command metadata; nodes without unambiguous provenance are marked `unavailable`, never guessed.
- [Refactoring `LiveCaptureSession` into steps could regress GUI live capture] → GUI keeps its thread but calls the extracted step functions; existing live-capture and UI tests must pass unchanged; Android on-demand acceptance (archived follow-up 14.5) is exercised by the CLI acceptance task.
- [CLI and GUI contend for one phone] → per-serial lock (D3); GUI shows "device busy (CLI)".
- [Agents misread partially available data as authoritative] → explicit per-artifact status, generated README in each dump, `limitations` copied from the capture.
- [Warm replay still costs seconds per call] → result/pipeline/upload caches shared with the GUI; perf and validation intentionally bypass result caches and say so.
- [Each `replay` re-loads capture state; no true interactive session] → accepted for this change; tracked in Open Questions.

## Migration Plan

Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.
1. Introduce `gfxrecon_core` and move `ImportCapture`/`ResolveTools` with no behavior change; full native + UI suites must pass before CLI work lands.
2. Add `gfxrecon_cli` incrementally (static queries → dump static → replay → dynamic dump → capture → perf).
3. Packaging adds `gfxrecon-cli.exe` beside `GfxReconGui.exe`; settings/caches are shared, so no data migration. Rollback is removing the executable; the GUI is unaffected.

## Open Questions

- Resident replay server: could a patched replayer pause at frame end and accept new dump/override requests without reloading state (the 80 s cold cost)? Worth a follow-up feasibility spike once the CLI exists.
- Should per-pass outputs also capture "before pass" state (`DumpBeforeCommand`) for load-op debugging, or only after?
- Is a mod beyond shaders (e.g. forcing pipeline state, disabling a draw) wanted next? The visualization hook in the patched backend is a plausible base.
