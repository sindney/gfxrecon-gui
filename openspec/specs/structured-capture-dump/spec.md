## Purpose
A deterministic filesystem representation of one or more frames: static data per capture/frame/pass/draw plus optional tiered dynamic replay artifacts, with manifests, indexes and resumption.

## Requirements

### Requirement: Practical dynamic dump duration
The default dynamic tier SHALL be `pass`. A frame of a bundled triangle or cube sample SHALL complete pass-tier dumping within five minutes. The `draws` tier SHALL remain explicit and document its potentially high replay/storage cost. The `full` tier SHALL require a `--draws` filter; use `replay draw --full` for individual draws.

#### Scenario: Default dynamic dump time
- **WHEN** `dump --dynamic` without a tier value runs on one frame of the reference capture with warm caches
- **THEN** it uses the `pass` tier, completes in at most 5 minutes, and the manifest records elapsed time, replay count and bytes written

#### Scenario: Whole-frame draw outputs opt-in
- **WHEN** an agent runs `dump --dynamic draws` without a draw filter
- **THEN** capture and inspection use the selected target and preserve original input data

#### Scenario: Full tier without filter
- **WHEN** `dump --dynamic full` runs without `--draws`
- **THEN** the command fails with a usage error directing the agent to add a `--draws` filter or use `replay draw --full`

### Requirement: Pass classification
Each `pass.json` SHALL include a heuristic classification (`scene`, `shadow`, `postprocess`, `ui`, `compute`, `transfer` or `unknown`) with the evidence used, labeled as a heuristic.

#### Scenario: Full-screen post-process pass
- **WHEN** a pass after the main scene passes contains a single non-indexed full-screen draw without vertex buffers that samples a previous pass's output
- **THEN** it is classified `postprocess` with that evidence recorded

### Requirement: Deterministic dump tree
`gfxrecon-cli dump --capture C --out D` SHALL write a versioned directory tree containing `manifest.json`, `README.md`, `capture.json`, `index.json`, `objects/`, `pipelines/`, `shaders/` and `frames/<frame>/passes/<pass>/draws/<draw>/`. Frame, pass and draw directory names SHALL sort in GPU submission order, and the same capture, tools and options SHALL produce identical static content.

#### Scenario: Static dump of the sample capture
- **WHEN** `dump` runs on the six-frame sample capture with default options
- **THEN** each frame directory contains `frame.json`, `events.jsonl` and pass directories, every submitted draw/dispatch execution appears exactly once as a draw directory, and `index.json` lists each with its path

#### Scenario: Reproducible static output
- **WHEN** `dump` is run twice into empty directories with the same inputs
- **THEN** all static files are byte-identical apart from timestamps confined to `manifest.json`

### Requirement: Pass grouping
Draws SHALL be grouped into passes by render pass or dynamic rendering scope per command buffer invocation. Dispatches and transfer commands outside render scopes SHALL form `compute` or `transfer` passes split at render-scope boundaries. Each `pass.json` SHALL record begin/end blocks, attachments with formats and load/store operations, extent and its ordered draws.

#### Scenario: Render pass with subpasses
- **WHEN** a render pass contains `vkCmdNextSubpass`
- **THEN** its draws remain in one pass directory and each draw records its subpass index

#### Scenario: Secondary command buffers
- **WHEN** draws are recorded in a secondary command buffer executed inside a render pass
- **THEN** they appear in that pass, and `draw.json` records the invocation path

### Requirement: Static per-draw data
Each `draw.json` SHALL contain the call and its arguments, frame and block, bound pipeline ID with a relative link to `pipelines/<id>.json`, bound shader stages with entry, specialization and relative links to `shaders/<module>/`, descriptor bindings with resource IDs, vertex and index bindings, dynamic state, render targets, push-constant ranges, indirect parameters when known, and the replay selector. Shared objects SHALL be written once and referenced by path.

#### Scenario: Agent follows a shader link
- **WHEN** an agent reads a draw's fragment stage entry
- **THEN** it contains a relative path to `shaders/<module>/` holding `module.spv`, reconstructed GLSL, assembly when a disassembler is configured, and reflection JSON

### Requirement: Optional dynamic tiers
`--dynamic none|frame|pass|draws|full` SHALL control replay-derived artifacts: none; final frame image per frame; the frame image plus attachment outputs after each pass and full inputs/outputs for passes classified `postprocess`; color/depth outputs after each selected draw; and additionally bound input images, buffers and vertex/index data per selected draw. Without `--dynamic` no replay runs; `--dynamic` given without a value SHALL select `pass`. Images SHALL be written as PNG with metadata (format, extent, mip, layer, binding, role); non-displayable data SHALL be written raw with format metadata.

#### Scenario: Pass outputs
- **WHEN** `dump --dynamic pass --frames 2` runs against a reachable target
- **THEN** each pass directory of frame 2 has an `outputs/` directory with its attachment images after the pass's last draw

#### Scenario: Full draw inspection with filter
- **WHEN** `dump --dynamic full --draws pipeline=P` runs
- **THEN** only draws using pipeline P receive `inputs/`, `outputs/` and `mesh/` artifacts, and all other draws keep static data with dynamic slots marked `skipped`

### Requirement: Bounded batched replays
Dynamic tiers SHALL obtain draw and pass artifacts through batched resource-dump replays, each selecting many draws, rather than one replay per draw. Batch size SHALL be bounded by a configurable draw count and estimated readback size, and a batch failing for memory or storage reasons SHALL be split and retried.

#### Scenario: Batched draw outputs
- **WHEN** `dump --dynamic draws` runs on a frame with 400 submitted draws and the default batch size of 64
- **THEN** at most 7 dump replays are launched for that frame and the manifest records each batch's replay manifest

#### Scenario: Device memory exhaustion
- **WHEN** a batch fails because Android terminated the replayer for low memory
- **THEN** the batch is split and retried, and only draws that still fail at batch size 1 are marked `failed` with the diagnostic

### Requirement: Compressed texture decoding
Raw ASTC image artifacts SHALL be decoded to PNG with the bundled, hash-pinned `astcenc`, using the sRGB or linear decode mode that matches the Vulkan format. Raw block data SHALL be retained and the decoder identity recorded in metadata. Compressed formats without an available decoder SHALL keep raw data with PNG status `unavailable` and the reason.

#### Scenario: ASTC texture input
- **WHEN** a dumped draw input is a `VK_FORMAT_ASTC_10x8_SRGB_BLOCK` image returned as raw blocks
- **THEN** the draw's `inputs/` contains a PNG decoded in sRGB mode and the raw `.bin`, and the metadata names `astcenc 5.7.0`

#### Scenario: Decoder missing
- **WHEN** `astcenc` is not configured or fails
- **THEN** the raw `.bin` is kept, the PNG slot is `unavailable` with the diagnostic, and `doctor` reports the missing tool

### Requirement: Explicit artifact status
Every dynamic artifact slot in frame, pass and draw JSON SHALL carry a status of `ok`, `unavailable`, `failed` or `skipped` with a reason. Data that cannot be produced SHALL be labeled, never omitted silently or substituted from another draw.

#### Scenario: Ambiguous selector
- **WHEN** a draw cannot be uniquely selected by the replay backend
- **THEN** its dynamic slots are `unavailable` with the backend selection reason and no neighboring draw's images are attached

### Requirement: Resumable dumps with provenance
The dump manifest SHALL record the capture SHA-256, tool and backend hashes, target and device identity, tier and filters, and per-batch completion. Re-running `dump` into an existing directory SHALL skip completed batches whose provenance matches and SHALL refuse mismatched provenance unless `--overwrite` is given.

#### Scenario: Interrupted dynamic dump
- **WHEN** a `--dynamic draws` dump is cancelled after 3 of 7 batches and rerun with the same options
- **THEN** only the remaining 4 batches are replayed and the manifest becomes complete

### Requirement: Frame selection and size budget
`dump` SHALL accept `--frames` (list, ranges or `all`), draw filters by block, pass or pipeline, and `--max-bytes`. When the budget would be exceeded, remaining dynamic artifacts SHALL be marked `skipped` with the budget reason.

#### Scenario: Budget reached
- **WHEN** `dump --dynamic full --max-bytes 2GB` would exceed 2 GB
- **THEN** the dump stops scheduling batches before exceeding the budget, completes static data, and reports skipped draw counts in the manifest

### Requirement: Generated agent guide
Each dump SHALL include a `README.md` describing the tree layout, file meanings, status values, the replay selector, and example CLI commands to replay or modify a draw found in the tree.

#### Scenario: Agent orientation
- **WHEN** an agent opens a fresh dump's `README.md`
- **THEN** it finds the schema version, directory layout and a working `replay draw` command line for a draw in that dump
