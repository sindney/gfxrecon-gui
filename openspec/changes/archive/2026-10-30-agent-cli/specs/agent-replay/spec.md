## ADDED Requirements

### Requirement: Replay to frame
`gfxrecon-cli replay frame --capture C --frame N` SHALL replay on the resolved local or Android target and return the frame image path, replay log, target, timings, cache-hit status and the replay manifest, using the shared replay core including optimization, pipeline caches and result caches.

#### Scenario: Android capture with automatic target
- **WHEN** `replay frame` runs on an Android capture with one authorized phone connected and `--target auto`
- **THEN** the frame is replayed on that phone and the result records the device serial and the PNG frame image

### Requirement: Replay to draw
`gfxrecon-cli replay draw` SHALL accept a draw by block number, by dump draw directory, or by execution index, and SHALL replay the frame with resource readback at that draw, returning output attachments and, with `--full`, bound inputs and vertex/index data. Draw visualization modes and indirect subdraw selection SHALL be available as in the GUI.

#### Scenario: Draw from a dump path
- **WHEN** an agent runs `replay draw --draw D/frames/0002/passes/003-renderpass-b5120/draws/0017-b5188-vkCmdDrawIndexed`
- **THEN** the CLI resolves the capture and selector from the dump and returns that draw's output images

#### Scenario: Wireframe visualization
- **WHEN** `replay draw --block B --visualize wireframe` runs on a supported backend
- **THEN** the output shows the selected draw's wireframe and the result verifies the visualization provenance

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
`gfxrecon-cli perf` SHALL run a configurable number of fresh replays of a frame range on a target and report state-load time, measured frame time and FPS per run with min, median and max, together with cache reuse and screen state. With overrides, it SHALL also measure the baseline and report deltas.

#### Scenario: Override performance comparison
- **WHEN** `perf --capture C --frames 2 --runs 5 --override M=X.spv` runs
- **THEN** the result contains 5 baseline and 5 modified measurements with median frame-time delta, and results caches were not used

### Requirement: Image comparison
`gfxrecon-cli compare A B` SHALL report extent equality, per-channel maximum and mean absolute difference, differing pixel count, PSNR, and optionally write a difference image.

#### Scenario: Identical replays
- **WHEN** two replays of the same frame without overrides are compared
- **THEN** the differing pixel count and maximum difference are reported, and a mismatched extent is reported as an error rather than a difference

### Requirement: Replay provenance
Every replay result SHALL include the replay manifest path with capture hash, backend hash, target and device identity, overrides with hashes, request options and artifact hashes, as produced by the shared replay core.

#### Scenario: Reproduce an agent's result
- **WHEN** a user inspects a replay result directory produced by the CLI
- **THEN** its manifest identifies the exact capture, backend, device and override files used, and the GUI can open the same capture and replay the same selection
