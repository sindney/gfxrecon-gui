# CLI guide

## Discover

```powershell
.\gfxrecon-cli.exe --help
.\gfxrecon-cli.exe replay --help
.\gfxrecon-cli.exe replay draw --help
.\gfxrecon-cli.exe describe > commands.json
.\gfxrecon-cli.exe doctor
```

No arguments, `help`, `--help` and `-h` show text help. `describe` exposes commands
and options as JSON. Normal commands write one JSON document to stdout; progress
goes to stderr. Read `ok` and `result`, or `error.code` and `error.message`.

| Exit | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Operation failed |
| 2 | Invalid command/options |
| 3 | Device action required |
| 4 | Required tool/capability missing |
| 5 | Cancelled or timed out |

Common options: `--settings-dir DIR`, `--tools FILE`, `--target auto|local|android`,
`--serial SERIAL`, `--timeout SEC`, `--wait-device SEC`, `--quiet`. Settings default
to the executable directory, shared with the GUI.

## 1. Decode

```powershell
.\gfxrecon-cli.exe info --capture samples/indexed-cube.gfxr
.\gfxrecon-cli.exe dump --capture samples/indexed-cube.gfxr --out decoded
```

The default dump is static: no replay. Start with:

| Path | Contents |
| --- | --- |
| `manifest.json` | Capture/tool identity, options, completion |
| `index.json` | Submitted draws in frame/pass/execution order |
| `frames/0001/passes/.../pass.json` | Pass boundaries, attachments, classification |
| `.../draws/.../draw.json` | Arguments, state, descriptors, shader IDs |
| `pipelines/`, `shaders/`, `objects/` | Pipeline state, SPIR-V/GLSL/reflection, object data |

A matching completed dump is reused. `--overwrite` replaces a mismatched dump.
Use fresh output directories for replay commands.

## 2. Search by replay

```powershell
.\gfxrecon-cli.exe frames --capture samples/indexed-cube.gfxr
.\gfxrecon-cli.exe events --capture samples/indexed-cube.gfxr --draws-only
.\gfxrecon-cli.exe replay draw --capture samples/indexed-cube.gfxr --execution 0 --outputs --out inspect/cube
```

Select by `--execution I`, `--block B`, or `--draw DIR` for a decoded draw
directory. IDs come from the dump/events output; execution indices are zero-based.
Results list output directories, artifacts and timings.

For larger captures, inspect pass-end outputs first:

```powershell
.\gfxrecon-cli.exe dump --capture capture.gfxr --out decoded-pass --frames 1 --dynamic pass
```

Within a relevant pass, replay the midpoint execution and inspect the same color
attachment. Search the half containing the first appearance of the feature, then
confirm adjacent draws. Keep target/subresource identity fixed: clears, attachment
changes and later overwrites can break a simple monotonic search.
`--visualize original|solid|wireframe|depth|outline|uv` matches the GUI inspect modes.

```powershell
.\gfxrecon-cli.exe replay draw --capture samples/indexed-cube.gfxr --execution 0 --visualize wireframe --out inspect/wire
.\gfxrecon-cli.exe replay draw --capture samples/indexed-cube.gfxr --execution 0 --visualize outline --out inspect/outline
```

`image_png` selects COLOR 0 when available. All attachments remain in `outputs`.
Choose a specific output with `--attachment color:1|depth|stencil` or `--image ID`;
add `--mip N --layer N` for a subresource. `selected_output` identifies the chosen
image. Missing selections return `output_unavailable` with available metadata.
Nonzero mips/layers enable all-subresource readback, which can increase memory use.
Outline automatically prepares original/solid source passes; `diagnostic_sources`
records their manifests and timings. UV uses a compatible interpolated UV varying
from the effective fragment shader and reports unavailable when none exists.
Combine any mode with `--full`, shader overrides or indirect `--subdraw K`.
Wireframe requires device support. Diagnostic geometry ignores fragment discard
and stencil, as in the GUI.
`operation_timings` reports total processing wall time, accumulated replay wall
time, remaining frontend time and replay call count (including outline sources
and baseline runs). It excludes process startup and final JSON serialization.
`inspection_seconds` remains the single-draw total; `timings` describes the final
backend replay. `--execution` is global across the capture, not frame-local.

Inspect several candidates without exporting the static dump again:

```powershell
.\gfxrecon-cli.exe replay draws --capture samples/indexed-cube.gfxr --executions 0-2 --full --out inspect/candidates
```

Indexes/ranges are comma-separated, ordered and unique (maximum 4096). Original
mode merges readbacks per frame in chunks of `--batch-draws N` (default 32, maximum
256). Modes, `--subdraw` and `--baseline` use isolated strategy with shared import
and override compilation. `draws` lists each execution's status and result;
`batches` records shared replay provenance. On partial failure, exit 1 and
`batch_incomplete` point to the saved `result.json`, including completed draws.

## 3. Probe

```powershell
.\gfxrecon-cli.exe replay draw --capture samples/indexed-cube.gfxr --execution 0 --full --out inspect/cube-full
```

`--full` adds descriptors, textures and vertex/index bytes. Read shader inputs and
uniform/storage accesses alongside the vertex layout. Compare readbacks across
frames to distinguish changing geometry from changing transforms; a shader name
alone is not proof of skinning or animation.

`geometry` reports `submitted_triangles`, `unique_vertices`, per-record counts and
referenced buffer availability. Triangles include instances and degenerates at
input assembly, before tessellation/geometry amplification. Unique vertices are
distinct adjusted IDs across active records, independent of instances. Missing
indices can leave triangle-list counts known while unique vertices remain null;
restart needs index bytes. Unresolved dynamic topology and unsupported paths
include a reason. These counts describe submitted geometry, not visible pixels.

For batches, `dump --dynamic draws --draws pass=1/000` collects a pass's outputs.
`--dynamic full` requires `--draws`, such as `block=B` or `pipeline=P`. These tiers
can be expensive; narrow the selection first.

## Edit and verify

Replace `MOD` with a module ID from the draw/pipeline output:

```powershell
.\gfxrecon-cli.exe shader export MOD --capture samples/indexed-cube.gfxr --out edit
# Edit edit/module.glsl.
.\gfxrecon-cli.exe shader check MOD edit/module.glsl --capture samples/indexed-cube.gfxr
.\gfxrecon-cli.exe replay frame --capture samples/indexed-cube.gfxr --frame 1 --override MOD=edit/module.glsl --baseline
.\gfxrecon-cli.exe compare before.png after.png --out diff.png
```

`shader check` compiles/validates against the captured stage, entry and interface.
`--baseline` compares on the same target/options. `--validate` runs fresh Vulkan
validation. `perf --help` explains repeatable timing. Omitting overrides uses the
captured shaders.

Each CLI invocation performs a real replay; rendered results are never restored
from another session. Completed session artifacts remain as evidence, not as a
cross-session result cache. `--fresh` also disables GUI process-local snapshot
reuse for that request. Validation and performance measurements always run
fresh. Compatible target-generated PSO caches can remain warm and are separate
from rendered output. File content is hashed for replay/artifact identity
validation; timestamps alone are not trusted to prove unchanged bytes.

`replay frame --swapchain virtual|captured|offscreen` selects the same backend
mode exposed by `perf` (default: `virtual`). Use a matching mode for image checks,
for example `replay frame --capture C --frame 3 --swapchain offscreen --baseline
--override MOD=edit/module.spv --out images/offscreen-pair`. Each run and its
manifest record requested/effective mode; non-default modes require backend
confirmation. Swapchain modes remain separated in cache identities.

Replay commands (`replay frame`, `replay draw`, `replay draws`), `perf`, and
dynamic dumps accept `--memory-translation default|none|remap|realign|rebind`.
This is an explicit memory portability policy; it never automatically retries
with another mode. `default` preserves existing behavior: local macOS resolves
to `rebind`, while Windows and Android resolve to `none`. Explicit `none` remains
`none` on every target. For a capture whose original memory/address allocation
cannot be replayed on a different GPU, select a policy deliberately, for example:

```powershell
.\gfxrecon-cli.exe replay frame --capture capture.gfxr --frame 1 --memory-translation remap --out images/remapped
```

`memory_translation` records requested and effective policy with source
`backend_arguments` and `backend_confirmed: false`: effective here denotes the
mode passed to the backend, not a native-reported observation. Resolved modes
enter pipeline cache identities and comparison checks; requested policy
is also retained in request provenance. Dumps record the dynamic replay policy
in resume provenance. Use matching policies for image and performance pairs.

## GPU frame measurements

`perf --gpu-timing isolated-frame` measures device timestamps around the first
through last GPU submission of each selected frame. It supports one active
command queue per frame and replays the original capture. Earlier GPU work is
drained before the frame; no idle waits are inserted between its submissions.
This changes overlap with adjacent frames. The resulting GPU elapsed interval
may include dependency waits and CPU submission gaps between its endpoints.
Waits that gate the start timestamp are excluded. It does not represent pure GPU
busy time, display latency, or the game's normal frame latency.

Use `--swapchain offscreen` to avoid creating real swapchains, surfaces and
windows. This removes display-backed acquisition/presentation from replay;
dependency waits and CPU gaps between the GPU timestamp endpoints can still
contribute to the elapsed interval. The default remains `virtual`, which can
acquire real swapchain images and copy replay images for presentation.
`--swapchain captured` is also available and uses captured image indices with
the real replay swapchain.

Every run records requested and backend-confirmed effective swapchain modes.
GPU reports retain this under `gpu.swapchain` and `gpu.provenance.swapchain`;
the request manifest/cache identity includes `swapchain_mode`. Different modes
use separate pipeline cache identities and cannot produce a compatible GPU
comparison. Explicit offscreen replay fails if an older backend cannot confirm
its effective implementation; rebuild/update the bundled replay tool or APK.

For a manual Android baseline/modified sample pair:

```powershell
.\gfxrecon-cli.exe perf --capture captures/capture-1.gfxr --frames 2 --gpu-timing isolated-frame --swapchain offscreen --runs 1 --override MOD=edit/module.spv --target android --serial DEVICE --out profiles/pair-1
```

Use a new output directory when manually collecting another pair. GPU mode
defaults to one run per variant. Existing `--runs` remains available; it keeps
baseline/modified alternating order. There is no automatic warm-up, frame loop,
sample rejection, or steady-state detection. A few short-capture samples cannot
by themselves establish a small performance improvement.

Each run includes `gpu_measurement_file`, `manifest`, and a `gpu` report with
individual frame samples, raw timestamp ticks, timestamp period/valid bits,
availability, completion strategy, actual replay GPU/driver, shader replacement
evidence, cache policy/observations, and optional before/after telemetry.
`gpu_frame_deltas` compares corresponding frames within each pair. Different
frames are not pooled as repeated measurements. Incompatible device, scope,
backend/capture identity, cache policy, or pipeline-cache load state suppresses
that GPU delta.

The existing `frame_time_ms`, `fps`, `load_seconds`, and `frame_time_delta` fields
retain their CPU replay-elapsed meanings, including instrumentation overhead
when GPU mode is enabled. GPU values are separate.

GPU requests require the matching bundled replay executable/APK. Unsupported
timestamp queues, ambiguous frame boundaries, unsupported multi-queue or submit
configurations, invalid query results, and timeouts fail explicitly. No CPU
timer substitutes for a GPU result. Check `error.detail.gpu_report` and
`error.detail.replay_log` for backend diagnostics; a preflight scope failure uses
`gpu_profiling_unsupported`.

Thermal and GPU-clock readings are optional host-side observations outside the
timed frame. Their source, units, access status, observation interval and sensor
freshness are reported. Cached temperatures are not treated as current readings;
missing or denied sensors do not invalidate GPU timestamps. These snapshots do
not describe continuous clock or temperature behavior during a frame.

Cache provenance distinguishes result-cache bypass, excluded captured pipeline
data, target-generated pipeline-cache use, and uncontrolled driver/hardware
caches. No pipeline-cache file loaded does not imply all caches were cold.

Hardware counters remain separate from timestamps. Sustained mode below collects
them through the OEM Perfetto provider. Controlled Adreno workloads verify a
qualitative full/half fragment counter response; exact normalization and
per-shader attribution remain limited. Mali support requires independent validation.

## Optional counters for a fresh frame replay

GPU timing uses a fresh replay process for each outer run. Optional
`--counters` also measures device timestamps and
requires one explicit `--frames N`. Counters are off by default.
`--counter-profile all` is equivalent to `--counters`; `precision` and `bandwidth`
remain compatibility aliases for `all` and no longer filter acquisition.
`--counter-period-us` is accepted only with an active counter profile.

```powershell
.\gfxrecon-cli.exe perf --capture captures/capture-1.gfxr --frames 3 --gpu-timing isolated-frame --runs 5 --out profiles/gpu-5
.\gfxrecon-cli.exe perf --capture captures/capture-1.gfxr --frames 3 --counters --counter-period-us 1000 --runs 3 --override MOD=edit/module.spv --out profiles/counter-pairs
.\gfxrecon-cli.exe perf --capture captures/capture-1.gfxr --frames 3 --counters --runs 3 --out profiles/counters-3
```

`--runs` is the number of fresh processes per variant (default 1 for GPU timing
and counter requests). With overrides, baseline and modified runs alternate.
Each process loads captured state and executes the selected frame once.
There is no automatic in-process repetition, warm-up, or frame pacing.
The older `--sustained`, `--fps`, `--duration-ms`, and `--warmup-ms` options have
been removed and are rejected as unknown options. Replace them with outer
`--runs N` and, when useful, `--counters`.
Legacy CPU replay-time measurements and manual GPU frame ranges keep their
existing behavior; CPU replay timing retains its default three runs.
Plain `perf` without either GPU option retains this legacy CPU wall-time mode.
Use `--gpu-timing isolated-frame --frames N --runs N` for GPU-only measurements;
explicit `--counter-profile none` also enables GPU timing while leaving counters off.

Android OEM Perfetto providers support optional counter sampling on Adreno and
Mali devices with an accessible `gpu.counters` producer. This was tested on a
Mali-G720-Immortalis MC12 (V2339A, Android 16); the GPU vendor alone does not
guarantee that a phone exposes counters.
Configure the bundled `gpu_counter_producer` helper and matching replay APK.
All advertised metrics are exposed without a named-metric allowlist, including
ALU, bandwidth and unfamiliar vendor counters. IDs and units are discovered at
runtime. Simultaneous-counter block capacities constrain collection only when
the provider advertises such a limit; metrics not sampled for this reason stay
in the report as unavailable. Missing or sparse counter data remains explicit and does
not invalidate successful GPU timestamps.

GPU duration uses device timestamps. Counters are system-wide sampled estimates
in this frame window; averaging and clock alignment can blur boundaries, and
short frames may have few samples. Bandwidth rates do not establish exact bytes
per frame. Full/half fragment rates do not by themselves prove a particular
shader used FP16 arithmetic.

Mali MONOTONIC/RAW timestamps are mapped to BOOTTIME using direct trace clock
snapshots. The report retains `counter_clock_bridges`, original timestamps in
`gpu-counters-decoded.json`, observed offset variation and extrapolation distance.
Snapshot acquisition latency and drift between snapshots remain unverified;
observed offset variation is not an uncertainty bound. Missing/inconsistent clock
evidence leaves counters unavailable. Discovery and helper failures retain
`counter-producer.log` when available, alongside the selected producer identity.

The report retains `runs[].gpu.samples`, each run's `gpu.counter_window`,
`runs[].counters`, summary `metrics`, paired `comparisons`, CSV exports, and raw
counter artifacts. Counter plots use milliseconds relative to each frame
window's start and keep distinct replay sessions separate. GPU and counter
comparison policies retain device/driver, cache, shader-override, swapchain,
memory-translation, and selected-definition provenance. Unsupported or
incompatible numerical comparisons remain suppressed.

## Capture

```powershell
.\gfxrecon-cli.exe capture windows run --exe tools/gfxrecon-gui-live.exe --cwd samples/test-res --trigger-after 2 --frames 3 --out captured
```

Before Android capture, install the compatible **replay APK**, which supplies
the capture layer. On Android 11+, install it with `--force-queryable` so the
target can see it; this usually avoids target manifest changes. From the extracted
Windows distribution:

```powershell
.\tools\adb.exe -s SERIAL install --force-queryable -g -t -r .\tools\android\replay-debug.apk
```

On Android 10, omit `--force-queryable`. The GUI's **Install replay APK** button
selects the flag automatically. The target must be debuggable, actively rendering
with Vulkan, and match the layer ABI (`arm64-v8a` for the bundled APKs). No capture
SDK or extra storage permissions are needed in the target: CLI capture uses its
private storage and `run-as`. See [Android setup](BUILD.md#android-capture-setup)
for layer visibility alternatives and troubleshooting.

Android sample (also install the bundled debug sample APK first):

```powershell
.\gfxrecon-cli.exe capture arm --package com.lunarg.gfxreconstruct.test.test_apps.test_launcher --args triangle --frames 3 --out capture-session
# Interact with the sample, then record.
.\gfxrecon-cli.exe capture trigger --session capture-session
.\gfxrecon-cli.exe capture end --session capture-session
```

`capture status` reports progress; `capture restore` recovers journaled settings
after interruption. Keep the phone authorized and unlocked. See command `--help`
for scheduling and device selection. Keep private captures and generated reports
outside Git, for example under `.sessions/`.
`capture arm` configures the layer before restarting the app; trigger recording
after reaching the desired scene and use `capture end` to restore settings.
An app already running before layer setup must be restarted to load the layer.
Android recording events are merged from the layer file and game-scoped logcat,
with duplicates and other sessions excluded. The session retains `layer.log`,
`layer-logcat.log`, and `capture-events.log`. Short trimmed captures can still
require a large initial resource snapshot. The CLI waits for a recording-finished
event before retrieval; a timeout does not make an existing partial file valid.

Advanced manual Android recording keeps the app and session armed between clips:

```powershell
.\gfxrecon-cli.exe capture arm --package com.example.game --frames 0 --out manual-session
.\gfxrecon-cli.exe capture start --session manual-session
# Interact with the app, then finish and retrieve the clip.
.\gfxrecon-cli.exe capture stop --session manual-session
# Repeat start/stop for more clips, then restore settings.
.\gfxrecon-cli.exe capture end --session manual-session
```

`start` returns when recording begins; calling it again while recording does not
start another clip. `stop` waits for finalization, retrieves and verifies the file,
and leaves the app running. Repeating `stop` without another recording returns no
new files. Stop before `end` to retrieve a manual clip; `end --close-game` closes
the app. The GUI combines finishing a manual clip and ending the session into
**End session**. The C++ capture API also retains separate `Capture`,
`StopCapture`, and `End` operations for automation.

Windows automation can use separate trigger and stop files:

```powershell
.\gfxrecon-cli.exe capture windows run --exe game.exe --cwd game-dir --trigger-file start.capture --frames 0 --stop-file stop.capture --out manual-windows
```

From another terminal, create `start.capture` when ready, then `stop.capture`
to finish. Use fresh signal paths or remove old signal files before running;
an existing file is acted on immediately. The command retrieves and verifies
the finished capture and closes its launched app. Bounded Windows captures still
use `--frames N` without `--stop-file`.

## Static relationship queries

```powershell
.\gfxrecon-cli.exe index build --capture C.gfxr
.\gfxrecon-cli.exe index info --index relations.sqlite
.\gfxrecon-cli.exe query fields --entity executions
.\gfxrecon-cli.exe query draws --capture C.gfxr --frame 1 --pass 2 --shader 1392 --stage vertex --columns execution,block,pipeline_id,index_count,instance_count --limit 50
.\gfxrecon-cli.exe query pipelines --index relations.sqlite --shader 1392
.\gfxrecon-cli.exe query stats --index relations.sqlite --kind draw --group-by frame,pass
.\gfxrecon-cli.exe draw --capture C.gfxr --execution 0 --section rasterization
.\gfxrecon-cli.exe pipeline 12 --capture C.gfxr --columns pipeline_id,name --section shaders
```

`index build` returns the published database path and snapshot identity. It reuses
compatible decoded metadata and indexes, without replay, a device, shader
extraction/disassembly or a dump tree. `--rebuild` publishes a new owned generation
while existing readers keep their original snapshot.

Every list/stat query requires exactly one of `--capture FILE` and `--index FILE`.
The capture form validates current source identity and ensures a matching index.
The index form reads an immutable snapshot directly; original capture, converter
and decoded evidence may be offline. Responses identify the as-indexed source and
report external evidence availability, rather than claiming the source is current.

`query executions`, `draws`, `pipelines`, `shaders` and `passes` share frame, pass,
pipeline, shader, stage and execution-kind filters. Different filters are AND;
repeated or comma-separated values within one filter are OR. Frames are one-based;
passes are zero-based within a frame, so `--pass` requires one explicit `--frame`.
Shader and stage must match the same captured pipeline-stage edge. These are
binding relationships, not proof that shader instructions ran or affected pixels.
Missing bindings remain unresolved coverage; unavailable indirect counts are null.

`query fields --entity executions|pipelines|shaders|passes` discovers canonical
column/section names, types, defaults and filterability without opening a source.
`--columns` projects rows; `--section` adds structured details. Rasterization detail
includes recorded static and dynamic components and discloses unresolved effective
state. Source/evidence metadata appears once in `gfxrecon-query/1` responses.

Pages default to 100 rows, maximum 1000. Follow `page.next_cursor` with the same
snapshot, filters, columns and sections to continue deterministic ordering. Changed
queries or stale snapshot cursors fail. `--count` explicitly requests total rows.
`query stats` counts submitted executions, splitting draw/dispatch counts; its
default grouping is `frame,pass`. Other groups are `pipeline_id` and `shader_id`.
Shader groups can overlap because one execution can bind several modules.

Existing unprojected `draw --block B [--occurrence K]`, `pipeline ID` and `object ID`
retain their full dump-equivalent documents. `draw --execution I` also returns that
full document unless projection is requested; zero is valid. Execution selection
conflicts with block/occurrence selection. Projection is static and never causes
resource readback or replay.

```powershell
.\gfxrecon-cli.exe query draws --index relations.sqlite --columns execution,block,pipeline_id --limit 50 --export draws.csv --export-format csv
.\gfxrecon-cli.exe query executions --index relations.sqlite --columns execution --section arguments --export executions.jsonl --export-format jsonl --all
```

Exports contain the selected page by default. Export-only `--all` follows pages
with bounded memory, starting at any supplied cursor. Stdout contains one compact
JSON receipt with identity, row count and file path, without duplicating the table.
JSONL preserves JSON types and exact unsigned IDs. CSV uses catalog/projection
field order, quotes non-null values and doubles embedded quotes; null is an empty
unquoted cell, while an empty string is `""`. Structured values use compact JSON
inside the quoted cell. Output is staged privately and published only on success;
cancellation/failure preserves an existing destination. Export destinations cannot
replace the index or recorded source/decoded evidence.

### Android artifact transfer integrity

Uploads wait for the Android receiver to consume the declared byte count and
verify SHA-256 before publishing the file. ADB's `exec-in` exit alone does not
establish completion. Each upload uses its own staging files, so a failed or
cancelled upload cannot replace a valid destination. Reusable capture-cache
entries are also checksum-verified before replay uses them.

When the bundled `artifact_transport` helper is available, Android replay sends
artifact bundles with LZ4. The header precedes the compressed payload and records
protocol, codec, uncompressed length, payload length and the XXH64 checksum of the
entire uncompressed bundle. The receiver verifies frame completion, both lengths,
the content checksum and artifact records before publishing files. Invalid data
fails the replay; it is never silently retried as successful raw data. Cancellation
removes private staging files. Raw transfer remains available when the optional
helper cannot be used (or its configured path is empty).

`backend_timing.artifact_transfer` in the replay manifest records `codec`,
`integrity`, `raw_bytes` and `wire_bytes`. Every request performs a real replay;
the timings describe that replay.
