## Purpose
Replay to a frame or a selected draw on local or Android targets, with shader overrides and shader-edit services, validation replay, performance measurement and image comparison.
## Requirements
### Requirement: Replay to frame
`gfxrecon-cli replay frame --capture C --frame N` SHALL replay on the resolved local or Android target and return the frame image path, replay log, target, timings, cache-hit status and the replay manifest, using the shared replay core including optimization, pipeline caches and result caches.

#### Scenario: Android capture with automatic target
- **WHEN** `replay frame` runs on an Android capture with one authorized phone connected and `--target auto`
- **THEN** the frame is replayed on that phone and the result records the device serial and the PNG frame image

### Requirement: Replay to draw
`gfxrecon-cli replay draw` SHALL accept a draw by block number, by dump draw directory, or by global zero-based execution index, and SHALL replay the frame with resource readback at that draw, returning output attachments and, with `--full`, bound inputs and vertex/index data. Modes `original`, `solid`, `wireframe`, `depth`, `outline` and `uv` and supported indirect subdraw selection SHALL be available as in the GUI. Outline SHALL preserve original interior color and draw an orange boundary. UV SHALL use the effective fragment interface. Unsupported capabilities SHALL fail explicitly.

#### Scenario: Draw from a dump path
- **WHEN** an agent runs `replay draw --draw D/frames/0002/passes/003-renderpass-b5120/draws/0017-b5188-vkCmdDrawIndexed`
- **THEN** the CLI resolves the capture and selector from the dump and returns that draw's output images

#### Scenario: Wireframe visualization
- **WHEN** `replay draw --block B --visualize wireframe` runs on a supported backend
- **THEN** the output shows the selected draw's wireframe and the result verifies the visualization provenance

#### Scenario: Outline inspection
- **WHEN** a supported draw is replayed with `--visualize outline`
- **THEN** its output shows an orange boundary with original interior color and records auxiliary source manifests

#### Scenario: Unsupported UV interface
- **WHEN** UV mode is requested for a fragment interface without a compatible UV input
- **THEN** the command returns `visualization_unsupported` before launching the diagnostic replay

### Requirement: Shader modification round trip
The CLI SHALL let an agent export a captured module, compile an edited GLSL or SPIR-V assembly file for that module's captured stage and entry, check its interface against the original, and replay a frame or draw with one or more module overrides. The original capture SHALL NOT be modified. Interface-changing candidates SHALL be rejected unless explicitly allowed, and replay SHALL fail if the backend does not confirm each replacement.

#### Scenario: Edit, compile and replay
- **WHEN** an agent runs `shader export M --out E`, edits `E/module.glsl`, then `replay draw --block B --override M=E/module.glsl --baseline`
- **THEN** the candidate is compiled and interface-checked, baseline and modified replays run, and the result contains both images, a comparison and confirmation that module M was replaced

#### Scenario: Interface mismatch
- **WHEN** a candidate changes the module's inputs, outputs or resource bindings
- **THEN** the command fails before replay with the interface differences in `error.detail`

#### Scenario: Compile error
- **WHEN** a candidate fails to compile
- **THEN** the command fails with compiler diagnostics and no replay is launched

### Requirement: Validation replay
`--validate` on replay commands SHALL run a fresh, unoptimized, uncached replay with the Khronos validation layer and write `validation.json` with status, error and warning counts and messages with VUIDs. Missing validation SHALL be reported as unavailable, never as a clean pass.

#### Scenario: Validate a modified shader
- **WHEN** `replay frame --override M=X.spv --validate` runs
- **THEN** the result includes the validation report path, counts per VUID, and notes that validation covers the whole replay

#### Scenario: Validation layer missing
- **WHEN** validation is requested and the layer cannot be enabled on the target
- **THEN** the command exits with code 4 and the report status is `unavailable`

### Requirement: Replay performance metrics

`gfxrecon-cli perf` SHALL preserve existing CPU replay-elapsed measurements of fresh frame-range replays, including state-load/replay time, FPS, per-run samples, summary statistics, cache reuse and screen state. CPU-derived fields SHALL NOT be represented as GPU profiling. Opt-in device GPU timing SHALL retain individual full-frame samples with explicit support, scope, completion and validity. GPU-only defaults SHALL not start a counter provider. `--counters` or `--counter-profile all` SHALL request optional all-counter sampling for one explicit frame; legacy CLI `precision` and `bandwidth` profile names SHALL be compatibility aliases for `all`, not acquisition filters. `--runs` SHALL repeat fresh replay processes, preserving alternating baseline/modified order when overrides are supplied. No option SHALL introduce in-process frame restoration, warm-up or pacing, silently discard early samples or claim steady state. Numeric GPU/counter deltas SHALL require the corresponding valid and compatible evidence and remain distinct from CPU replay deltas.

#### Scenario: Legacy CPU comparison
- **WHEN** `perf --capture C --frames 2 --runs 5 --override M=X.spv` runs without a GPU/counter option
- **THEN** it returns five baseline and five modified CPU replay measurements with replay-time deltas and no result-cache reuse

#### Scenario: Fresh GPU comparison pairs
- **WHEN** a supported frame is measured with GPU timing, three runs and a shader override
- **THEN** three independent baseline/modified process pairs retain individual valid GPU samples and compatible deltas, without an in-process replay loop

#### Scenario: All-counter compatibility alias
- **WHEN** an existing script uses `--counter-profile bandwidth` or `precision`
- **THEN** the request collects all supported provider counters under the current all-counter policy rather than retaining the old named-metric allowlist

#### Scenario: Incomparable samples
- **WHEN** variants differ in measured scope, device/driver, memory/swapchain or controlled cache policy, or required timing endpoints are invalid
- **THEN** the affected numeric comparison is suppressed with a reason instead of producing a claimed optimization result

### Requirement: Image comparison
`gfxrecon-cli compare A B` SHALL report extent equality, per-channel maximum and mean absolute difference, differing pixel count, PSNR, and optionally write a difference image.

#### Scenario: Identical replays
- **WHEN** two replays of the same frame without overrides are compared
- **THEN** the differing pixel count and maximum difference are reported, and a mismatched extent is reported as an error rather than a difference

### Requirement: Replay provenance

Every replay result SHALL include the replay manifest path with capture hash, backend hash, target and device identity, overrides with hashes, request options and artifact hashes, as produced by the shared replay core. GPU measurement provenance SHALL additionally identify the effective replay GPU, Vulkan API and driver properties, device OS/build, effective capture/optimization settings, instrumentation and completion mode, and controlled cache policy and observed pipeline-cache reuse/identity. Uncontrolled driver and hardware cache state SHALL be identified as unknown in GPU reports. For GPU measurement/comparison requests, every requested override SHALL have structured successful application evidence matching its effective bytes; replacement-file discovery alone SHALL NOT establish successful application. Whether a replacement executes within measured scope SHALL be distinguished from successful replacement creation. Ordinary and CPU-only replay SHALL retain their existing backend compatibility and replacement checks.

#### Scenario: Reproduce an agent's result
- **WHEN** a user inspects a replay result directory produced by the CLI
- **THEN** its manifest identifies the exact capture, backend, device and override files used, and the GUI can open the same capture and replay the same selection

#### Scenario: Actual replay driver differs from system probe
- **WHEN** Android selects a different driver for the replay process than a standalone system Vulkan probe
- **THEN** GPU measurement provenance identifies the actual replay driver and does not substitute the standalone probe's driver properties

#### Scenario: Requested replacement not successfully applied
- **WHEN** any shader replacement requested for a GPU comparison lacks successful backend application evidence matching its hash
- **THEN** the performance comparison fails and identifies the unconfirmed module rather than reporting an optimization result

#### Scenario: No explicit pipeline cache loaded
- **WHEN** the replay loads no target pipeline-cache file
- **THEN** provenance records that fact and does not imply that driver or hardware caches were cold

#### Scenario: Historical Android result cache hit
- **WHEN** ordinary replay restores a validated result without contacting the explicitly selected phone
- **THEN** recorded device identity is identified as historical through device_identity_source=cached_result and current_device_verified=false, while fresh/validation/measurement requests retain their live replay requirements
