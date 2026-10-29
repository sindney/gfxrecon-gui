## Context

Upstream `gfxrecon-optimize` now prepares a derived replay file, cached by original
and optimizer hashes with verification of derived bytes. The inspector/extractor
continue using original data; removed calls become annotations preserving block
indices for replay selectors. Optimization defaults on with a frame-context opt-out,
Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.
2,168 graphics pipelines to 68 with matching frame/draw hashes and original shader
replacement identity. The original source is hashed before and after preparation
and replay, and source/derived provenance is explicit in replay manifests.

Large-game replay profiling distinguishes cold state reconstruction from frame
execution. The filmstrip no longer fans out visible tiles into replay jobs.
Selected draw inspection uses a 160 ms latest-selection debounce and cancellation;
one readback supplies attachments, descriptors and mesh buffers, while shader
documents are decoded and cached by stage identity. Foreground work can preempt
automatic replay. Verified artifacts and content-addressed
device uploads avoid repeated work; replay-created caches are scoped by capture,
backend, device/build and shader revision. Generated SDK copies fix the loaded
pipeline-cache buffer lifetime, binary I/O and repeated linear cache-file scans.
These optimizations do not claim a resident RenderDoc-style arbitrary-event
seek backend: uncached requests still reconstruct replay state.

The on-demand revision uses a session actor separate from bounded jobs: launch
armed, trigger N frames or manual start/stop, retrieve finalized files, and rearm.
Stop/End leave the game running; Close is explicit. Windows uses a private named
event read by a generated copy of the pinned layer source and verified by a
protocol/hash manifest. Android uses journaled upstream runtime trigger properties.
Opening captures defers automatic replay while the live session is active.
Trim snapshots preserve 64-bit application frame mapping independently of GUI
relative frames. See `docs/CLI.md` for lifecycle and validation.
The shader reference list moves below the editor; one public distribution uses
`GfxReconGui.exe` and migration backs up prior settings/sessions before replacement.

The repository currently contains OpenSpec and agent skill configuration, with no application code or existing capability specs. The user supplied `LOCAL_PATH` and a Sokatoa screenshot as the GUI/workflow reference, and requested a proposal before implementation.

### Verified starting points (2026-10-26)

| Component | Observation | Design consequence |
| --- | --- | --- |
| ImGui template | Commit `09751a407a0799055f23e8e1960aff417343c7db`; C++17/CMake, docking ImGui, SDL3 or SFML 3 with OpenGL, native dialogs, console, settings, UI tests, Windows packaging | Import tracked sources and pinned dependencies; retain SDL3/OpenGL initially |
| Upstream GFXReconstruct | Inspected a shallow checkout of `dev`, commit `a20d3dbf83df4f8e02d52e42f19f0ea060708237` | Treat this as a research reference; select and record a tested dependency revision in milestone 0 |
| Capture processing | `gfxrecon-info`, `gfxrecon-convert --format jsonl`, `--include-binaries`, and `gfxrecon-extract` exist | Use upstream decoders through tool adapters before considering a custom decoder |
| Replay inspection | Desktop and Android replay support screenshots and draw/dispatch resource dumps with JSON metadata | Derive dump selections from capture execution context, then collect output artifacts |
| Shader substitution | `--replace-shaders <dir>` loads replacement binaries; ordinary modules use `sh<captured-handle-id>` | Shader editing can use upstream replay without patching the capture |
| Newer shader paths | Inspected replay also has shader-object and inline pipeline shader replacement paths with different identity details | Enable each path only after its extraction, mapping, and replay behavior passes a fixture |
| Remote protocol | Experimental length-prefixed settings/output channel; documents hello/settings/ready, file transfer, done | Optional transport for job configuration and artifacts; it is not an arbitrary-event or shader-step debugger API |
| Validation target | Authorized Android device or local Vulkan device | Selected at runtime |
| Existing software | BoatAttack reports `DEBUGGABLE`; no `com.lunarg` package found; older replay APKs are available under `G:\SDK` | Prefer upstream triangle test app for a known workload; do not assume installed apps use Vulkan or old APKs are compatible |
| Android build tools | PATH Java is JDK 11; installed SDK includes platform 34 and NDK 25.1; inspected upstream requests NDK 26.3 and newer Gradle/AGP | Audit and provision a compatible build toolchain locally without changing global Java configuration |

Preflight was read-only on the phone. No APK was installed, capture settings changed, or replay executed during proposal preparation.

## Goals / Non-Goals

**Goals:**

- A practical Windows host GUI for Vulkan capture and inspection, with first-class Android capture/replay over ADB.
- RenderDoc-style navigation: select a frame and executed draw/dispatch, inspect its API/state/resources/shaders, edit a shader, replay, compare, and revert.
- Useful offline analysis even when no compatible replay device exists.
- Responsive handling of multi-frame captures, explicit provenance and unsupported states, reproducible dependency/tool versions, and real-device acceptance evidence.
- An evidence-based decision on invocation-level shader debugging after a bounded prototype.

**Non-Goals:**

- Initial D3D12, OpenGL/ES, Metal, iOS, Linux/macOS host certification, vendor GPU counters, or complete Sokatoa/RenderDoc parity.
- Editing the running captured application, mutating the original `.gfxr`, instant in-process shader hot swapping, or guaranteed replay across unrelated GPUs.
- Initial pixel history, arbitrary post-tessellation/geometry/mesh-shader debugging, GPU assembly decompilation, or a complete SPIR-V interpreter.
- Advertising breakpoints, variables, or shader stepping before an actual debugging backend exists and is validated.

## Decisions

### 1. Reuse the template as tracked application sources

Import the template's tracked application/build/test/packaging files and dependency pins into this repository, retaining notices and recording its origin commit. Do not copy its `.git`, agent configuration, OpenSpec history, build trees, generated settings, or `dist`. Keep the original template repository unchanged. Adapt target names to `gfxrecon_gui` and `gfxrecon_gui_tests`.

Keep the template's SDL3/OpenGL renderer and C++17 baseline. A Vulkan capture backend does not require the GUI itself to render with Vulkan: decoded replay images can be uploaded as OpenGL textures on the GUI thread. Retain the OS abstraction so SFML and other hosts can be revisited without exposing backend details in product panels.

Alternative considered: build a new Vulkan ImGui renderer or embed the template via an absolute path. Both add initial integration work; the latter also prevents a portable checkout.

### 2. Isolate upstream tools behind asynchronous adapters

Use an application model independent of ImGui and a native process runner with argument arrays, UTF-8/Windows Unicode path handling, captured output, cancellation, timeout, process ownership, and exit status. On Windows use hidden child processes and job objects for owned process trees. Do not build shell command strings from filenames, application arguments, or package names. ADB's remote-shell arguments need their own tested quoting/validation layer even when the local process uses an argument array.

The main services are:

| Module | Responsibility |
| --- | --- |
| `core` / `platform` | Settings, logging, process jobs, cancellation, paths, artifact storage |
| `backend` | Tool discovery, version/capability probes, normalized info/convert/extract/replay adapters |
| `capture` | Windows launch, Android discovery/setup/capture/restore, capture import |
| `analysis` | Persistent index, object lifetimes, command recordings and execution references, state reconstruction |
| `replay` | Target selection, screenshot/dump requests, transport, result revision and collection |
| `shaders` | Binary identity, SPIRV-Cross reflection/GLSL, SPIRV-Tools assembly/validation, compilation and overrides |
| `gui` | Workspace, events, pipeline, shader editor, images/buffers, capture controls, jobs and console |

All long operations run off the GUI thread. Results carry capture identity, selection generation, replay target, and override revision; outdated results cannot overwrite the current view. Cancellation completes cleanup before a job reaches its terminal state. Independent offline analysis remains available after device loss.

Use a pinned JSON parser for upstream documents. Support a tested tool bundle and user-configured tool paths; capability probes decide which controls are available. Core offline navigation does not require every shader/replay tool. Unknown schemas produce a version-specific diagnostic, with raw records available when safe to show.

Alternative considered: link all GFXReconstruct replay internals into the UI. Its generated API surface and internal changes would tightly couple application lifetime, crashes, and Vulkan driver initialization to the GUI. A future binary-index helper can reuse upstream decoder libraries behind this same service boundary if JSON conversion becomes a measured bottleneck.

### 3. Ship a docked workspace centered on shared selection

Launch/capture is a procedural non-dockable popup, with Target, Setup, Options,
Recording, and Open stages. It remains open while recording/retrieving/restoring,
shows real operation phases, supports cancellation with cleanup, and offers the
completed result for inspection. No permanent capture panel is docked. Event
navigation uses the full left column; tool/device configuration and recovery are
available through an on-demand dialog. Defaults version 3 migrates the old
capture-dock layout once while preserving the user's selected theme.

The default workspace follows the supplied RenderDoc image/pipeline references: Events at the left, wide Image/Pipeline/Shader/Mesh/Resources document tabs in the main area, and Frames/Console in a bottom dock. Image contains open-image tabs and a right-hand scrollable input/output thumbnail sidebar. A new draw initially opens its first readable color attachment; image slots retain binding/aspect identity. Pipeline uses a connected VTX/VS/TCS/TES/GS/Rasterizer/FS/FB/CS flow with disabled unbound stages, stage-specific resource/buffer tables and links into image/buffer/shader inspectors. Fixed-function state belongs to Rasterizer/FB/input sections rather than unrelated top-level category tabs. File and the command palette expose open/capture; the native title identifies the trace. Status dots belong only to Frames, frame tiles and selected draw events. Dimmed overlays disable refreshing inspector content, while event navigation and Console remain available. Source documents retain edits and shader pipeline references remain below the editor.

One selection model identifies capture, frame, execution occurrence, stage, and resource. Panel links update it consistently. Source views provide line numbers, search, syntax coloring, and copy/export; the shader editor tracks unsaved changes. Preserve layout, tool paths, recent captures, and preferred target. Default empty, unavailable, pending, failed, and completed states must be distinguishable.

Show frame thumbnails only when generated from replay artifacts. Do not invent timing bars, original shader source, resource values, or GPU duration when absent from the capture/backend.

### 4. Keep recording order distinct from GPU execution occurrences

Convert captures to a session-owned JSONL file and build a versioned compact index containing byte offsets and lightweight summaries. Page detailed arguments on demand and virtualize large event lists. Fingerprint capture contents and include converter/schema versions in the cache key; reuse only matching completed caches. Publish caches atomically so interrupted imports cannot appear complete.

Index 64-bit block indices, captured object IDs and lifetimes, CPU thread IDs, debug names/labels, frame boundaries, command-buffer recording generations, render-pass/dynamic-rendering scopes, and queue submissions. A recorded draw is not a unique executed event: command buffers can be recorded earlier, resubmitted, reset, or invoked as secondary buffers. Present a submitted execution tree alongside raw API recording order. An execution identity includes submission, primary and secondary invocation path, recording generation, and recorded block index. Preserve both capture-relative frame numbers and original frame metadata when available; replay frame arguments use upstream's 1-based numbering.

For supported core graphics/compute paths, reconstruct bound pipelines, stages, entry points, specialization constants, descriptor sets and update versions, vertex/index bindings, push constants, render targets, and supported dynamic state for the selected occurrence. Keep created shader binaries after module destruction if a pipeline still references them. Resolve library/extension paths only where tested. Mark unresolved dynamic descriptors, unsupported extension state, unsubmitted recordings, and ambiguous resource values explicitly.

API-derived state describes captured state; replay dumps describe observed resource contents at a particular execution. Descriptor update-after-bind, cross-queue synchronization, aliasing, and indirect execution can prevent exact offline resolution. Never substitute a current/global object value for an unavailable event-specific value.

Alternative considered: show only a flat JSON call list. It is useful for raw arguments but cannot reliably select a frame's executed draw or construct valid resource-dump requests.

### 5. Treat capture setup as a recoverable session

For Windows, launch a selected Vulkan executable with a child-specific environment enabling the capture layer, capture destination, and frame/trim settings. Preserve inherited environment values where applicable and leave system/user environment unchanged. Detect complete non-empty capture files before import; a cancelled/truncated output is labeled incomplete. Initial capture attaches by launching the application; arbitrary existing-process injection is outside scope.

For Android, discover devices with `adb devices -l`, select by serial for every command, inspect ABI/API/authorization/lock state, enumerate candidate applications, and verify debuggability and Vulkan suitability. Use upstream's replay APK as the capture-layer provider and replay application. Build/install the upstream test launcher for controlled acceptance workloads. Use ABI-matched libraries and validate the chosen writable capture location on the actual Android version.

Before changing GPU debug settings or `debug.gfxrecon.*` properties, persist their previous values, presence/absence, target package, and owned paths in a recovery journal. Configure only the selected app, launch capture, monitor bounded frame/range completion, retrieve the finalized file, and restore previous settings on success, failure, or cancel. On reconnect/startup offer journaled restoration; do not overwrite values a different session has since changed without surfacing the conflict. Delete only session-owned temporary artifacts. Do not clear app data or uninstall existing apps to work around signing conflicts.

If a test encounters installation failure, device lock, authorization prompt, storage prompt, or another required human interaction, stop dependent device actions, preserve diagnostics/journal state, and ask the user for specific help. Read-only/offline tasks can continue. No credential bypass, repeated installation retries, rooting, or APK repackaging. Replay may perform one wake/dismiss only for a positively confirmed nonsecure keyguard; secure/unknown locks prompt for manual unlock. Optional screen-off replay uses supported physical display-power controls while preserving logical rendering, with owned-state restoration and reconnect recovery.

### 6. Replay produces versioned artifacts through a supported transport

The GUI exposes Automatic, Local Windows, and Android device targets. Automatic
uses recorded Android surface calls/device extensions to identify Android
captures and routes them to one authorized phone or the selected serial.
Missing/ambiguous devices stop replay with a connection/selection diagnostic,
preserving offline inspection. Baseline, overrides, dumps, and revert share the
selected target; displayed results identify the actual device. ADB uses
app-private staging through `run-as`, streamed stdin/stdout files for binary
transfer, bounded completion polling, and owned-activity cleanup on cancel or
failure. Binary transfer is separate from bounded stderr/log capture.

First support replay jobs via upstream CLI/Android NativeActivity arguments and device artifact retrieval. Play/replay, stop, render selected frame, and inspect selected event are jobs. A frame selection can restart replay from the capture beginning; it must not imply constant-time seek or arbitrary draw stepping. Upstream playback window/touch controls can remain separate from the GUI. Frame looping is optional behind a capability check because upstream marks it experimental.

Build resource-dump requests using the exact upstream nesting for `BeginCommandBuffer`, draw/dispatch, render-pass/subpass boundaries, `QueueSubmit`, and secondary `ExecuteCommands`. Identify repeated submission/invocation occurrences. Reject an unresolved or unsupported selection instead of dumping a neighboring event. Support render passes and dynamic rendering in the validated core subset. Replay on the original phone is the preferred Android baseline; local Windows replay is enabled when its device capabilities and capture portability allow it. Replay failure leaves offline inspection intact.

Load screenshot, dump JSON, PNG/BMP, and raw buffer artifacts into a session manifest. The image viewer supports pan/zoom, RGBA channels, and pixel readout where decoded data permits; the buffer viewer supports hex and explicitly selected scalar/structured interpretations. Display format, dimensions, mip/layer/aspect, provenance, and unsupported formats. Never interpret a tone-mapped/converted screenshot as exact original HDR/packed pixel data. Provide a basic vertex/index table where the captured input layout is resolved; the approved mesh milestone adds a local SDL/OpenGL solid/wireframe viewer and supported post-VS evaluation from replay-readback vertex/index/uniform data. Local evaluation is explicitly distinguished from device-native vertex-output instrumentation, which the pinned replay bundle does not expose. Unsupported resources/stages remain diagnosed.

The newer remote channel can replace file polling/pulling after a protocol fixture passes. Negotiate its documented version, enforce receiver size limits and session-relative file paths, and bind to loopback/ADB forwarding. Retain CLI transfer fallback for older bundles. The current channel configures a replay run and streams its outputs; it supplies no documented play/pause/seek/step command service.

### 7. Separate shader readability from compilation and override identity

Use pinned [BalazsJako/ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) for read-only shader representations and editable documents. Supply custom hand-written `LanguageDefinition::mTokenize` callbacks for GLSL and SPIR-V, leaving regex token lists empty. GLSL tokens cover keywords/types, built-ins, identifiers, numeric literals, operators, strings, preprocessor directives, and line/block comments; SPIR-V tokens cover opcodes, `%` IDs, enum operands, strings, numbers, punctuation, and semicolon comments. Configure the editor's comment scanner for each language and test multiline edits/continuations. Keep tokenization linear in scanned bytes, avoid regex and temporary substring allocation in the hot path, and retain upstream's incremental colorization. Verify compatibility with the template's pinned ImGui version with narrowly documented build-copy compatibility changes if necessary. Benchmark cold tokenization and editing a 10,000-line shader, and keep selection/scrolling responsive while incremental coloring catches up.

Use `gfxrecon-extract` and/or converter binary references to obtain exact captured SPIR-V. Track creation block, object lifetime, module or inline-stage identity, binary hash, entry point, stage, specialization data, and referencing pipelines. Normal shader modules map to upstream `sh<id>` filenames. Inline pipeline shaders and `VK_EXT_shader_object` require separately tested mapping rules; unsupported paths remain readable when extraction succeeds, with replay editing disabled and explained.

Use pinned SPIRV-Cross core/GLSL/reflection functionality for reconstructed Vulkan GLSL and interface metadata. Use SPIRV-Tools for disassembly, assembly, and validation. Use a pinned `glslangValidator` tool for Vulkan GLSL-to-SPIR-V compilation with explicit stage, entry point, Vulkan/SPIR-V target environment, and diagnostics. Compiler toolpaths remain configurable. HLSL output can be added later; HLSL editing is outside initial acceptance.

Generated GLSL cannot restore stripped names, comments, includes, macros, or original source structure. Display captured source/debug metadata separately when present. Preserve raw SPIR-V on cross-compilation failure. Extensions that cannot be represented or round-tripped do not prevent assembly inspection/editing.

Compile/assemble into a candidate revision, validate against target environment, and compare reflected interface requirements with the original and bound pipeline layout: descriptor sets/bindings/types/counts, push constant ranges, stage inputs/outputs, entry point, capabilities, and execution modes. Preserve specialization constants and flag unresolved runtime-sized requirements. Reject unsupported interface changes and report the reason; reflection is a preflight, with pipeline creation/replay validation remaining authoritative.

Store edits and binaries outside the capture in immutable revision directories. An Apply operation snapshots the active override set and starts fresh replay using `--replace-shaders`. Module replacement affects every use of that captured module, potentially across several pipelines and entry points; show the affected uses before applying. Start with validated vertex/fragment/compute module paths. Do not silently claim pipeline-local replacement semantics. Omit incompatible cached pipeline data and gate pipeline-binary/library paths until tested.

Publish a modified result only after replacement is confirmed and replay/artifact collection succeeds. Retain the last working revision when compilation or replay fails. Baseline and modified artifacts must use the same target, selected occurrence, and replay options. Offer side-by-side and absolute image difference, plus Revert to baseline. Restore the original capture unchanged; reversion requires only clearing active overrides and replaying the baseline. Restart latency is visible as job progress rather than represented as live hot reload.

### 8. Investigate shader debugging without blocking the edit loop

Time-box the initial debugging investigation to two engineering days after shader edit/replay works. Define one test case: a triangle fragment invocation at a chosen pixel with a simple local arithmetic value; a compute invocation is an alternative if fragment selection makes the experiment disproportionately expensive.

Assess available source/nonsemantic debug metadata, input/resource reconstruction, feasible SPIR-V instrumentation versus interpretation, invocation selection, required replay hooks, and a trace/stepping UI contract. Implement a minimal experimental proof only if it fits the bound, checking its reported value against a known test shader output. Explicitly cover missing source, derivatives/subgroups, memory/atomics, unsupported instructions, and the difference between generated-source lines and original-source lines.

Deliver a report with tested revision/device, method, evidence, limits, and a go/no-go decision. If the prototype is viable, prepare a separate OpenSpec change for production debugging. If not, document what backend work is required. The current release must not expose functioning breakpoint/step/watch controls without a validated backend. Compiler diagnostics, SPIR-V validation, resource inspection, and replay comparison remain available.

## Risks / Trade-offs

- [Android 17 installation/driver/storage compatibility and older local APKs] → Build or obtain a tested ABI-compatible bundle early, verify actual on-device capture/replay, and ask for user help on device blockers.
- [New upstream build dependencies differ from installed JDK/NDK] → Pin a working toolchain in local configuration, record it, and keep compiler/SDK setup outside global settings.
- [JSONL expansion and long initial indexing/replay] → Stream conversion, persist a compact offset index, bound queues/caches, show cancellation/progress, and measure before adopting an upstream binary helper.
- [Capture commands differ from submitted execution order] → Model recording generations and submission occurrences; test resubmission/secondary command buffers and never approximate ambiguous dump selections.
- [Driver and feature portability] → Prefer original-device replay, probe capabilities, preserve logs, and keep offline analysis independent.
- [Reconstructed GLSL or overrides change interfaces/semantics] → Preserve original binary, compile/validate, check reflection, show affected uses, and verify actual replay output before committing a revision.
- [Pipeline caches, binaries, shader objects, or libraries bypass edited code] → Test module replacement first, disable conflicting cache paths, and gate each newer path on evidence.
- [Resource readback perturbs execution or timings] → Label dump artifacts as replay observations; do not present them as original GPU performance measurements.
- [Debugging requires a substantial execution backend] → Bound the investigation and make production stepping a separate evidence-driven change.

## Migration Plan

This is an initial application, so there is no user-data migration. Keep dependency and fixture manifests versioned; store capture sessions, cache schemas, settings, and override revisions with explicit versions. Cache upgrades rebuild derived artifacts while preserving captures and authored edits.

Delivery gates:

1. **M0: feasibility/toolchain** — prove upstream triangle capture, conversion, extraction, baseline replay, resource dump, and replacement on Windows/Android as applicable; record versions and blockers.
2. **M1: usable capture workspace** — import template, configure tools, open a capture, launch local capture, capture/retrieve from the selected Android device, and recover settings.
3. **M2: inspection/replay** — event execution tree, core selected-event state, frame thumbnails, image/buffer inspection, and explicit unsupported cases.
4. **M3: shader inspection** — linked shader identities, assembly, generated GLSL, reflection, and source provenance.
5. **M4: shader fixes** — edit/compile/validate/apply, same-target replay comparison, failed-edit retention, and revert; required for completing this change.
6. **M5: evidence and packaging** — debugging feasibility report, automated regression coverage, real-device acceptance record, and a portable Windows distribution with licenses.

Rollback is removal of the portable application and derived caches/overrides after restoring any journaled device settings. Original captures and application data remain intact. Do not archive the change until required implementation tasks and acceptance evidence are complete.

## Validation Strategy

- Reuse template CTest/ImGui Test Engine for open/import, frame/event/stage/resource linking, editor apply/revert, job failures/cancel, and layout behavior.
- Use small deterministic JSONL/binary fixtures for index/state correctness: reset/re-record, repeated submits, secondary invocations, module destruction, descriptor updates, dynamic rendering, multiple queues, malformed/truncated input, and 64-bit IDs. Fixture expectations derive from known workloads/API behavior rather than copying the reducer implementation.
- Capture the upstream triangle test application and applicable debug-utils/shader-object tests. Use extension-dependent tests only on devices supporting them, recording skips. Add a small known fragment-color modification to prove replacement, output change, and revert; run a compute variation for supported compute editing.
- Keep hardware-required tests opt-in and deterministic tests runnable without a phone. Record tool revisions, device/API/GPU/driver, test-app build, commands, selected event, source capture hash, baseline/modified artifact hashes, and results under test output rather than checking large captures into git.
- Real-phone acceptance: install compatible replay and test APKs, capture a bounded frame range, retrieve/open, inspect a submitted draw and shader, replay/dump on that phone, apply a validated shader edit, observe the expected image change, revert, and verify prior device settings were restored. Stop for user assistance if installation, lock state, authorization, or another device interaction blocks progress.
- Measure responsiveness on an indexed fixture containing at least 100,000 calls: GUI input remains usable during indexing/replay, cancellation is acknowledged within one second, and no full JSON document is materialized in GUI memory. Record machine and measured latency/memory; do not substitute simulated records for final real-capture acceptance.

## Open Questions

These are implementation experiments with stated defaults, not reasons to delay the proposal:

- Which exact GFXReconstruct revision/bundle passes Android 17 capture and replacement? Resolve in M0; retain versioned CLI fallback if the experimental remote transport is unavailable.
- Does BoatAttack use Vulkan on this device? Use the upstream triangle launcher for acceptance regardless; inspect BoatAttack only as an additional real workload.
- Which inline/library/shader-object paths work correctly in the chosen upstream revision? Enable only tested paths; ordinary module-based vertex/fragment/compute editing is the required baseline.
- Can invocation tracing or interpretation be made useful within the debugging investigation budget? Decide in M5 with actual prototype evidence or a concrete backend gap report.

## References

Selected-draw diagnostic modes clone the captured graphics pipeline with a constant
fragment stage and modified raster/depth/blend state. Generated-source hooks keep
the upstream checkout pinned. Depth visualization uses complementary comparisons
against pre-draw/batch depth, suppressing depth writes; it deliberately describes
geometry coverage rather than original fragment/stencil execution. Fixed-count
indirect isolation executes the prefix unchanged and offsets the original GPU
argument buffer to one record. DrawIndex-dependent shaders are refused because
offsetting resets DrawIndex. Every result confirms mode/pipeline/subdraw before
publication, and cache/cancellation generations include that selection.

- [Supplied ImGui template](LOCAL_PATH and its tracked source/build/test files at the recorded commit.
- [GFXReconstruct repository](https://github.com/LunarG/gfxreconstruct), [desktop Vulkan usage](https://github.com/LunarG/gfxreconstruct/blob/dev/USAGE_desktop_Vulkan.md), and [Android usage](https://github.com/LunarG/gfxreconstruct/blob/dev/USAGE_android.md).
- [Resource dump selection and output schema](https://github.com/LunarG/gfxreconstruct/blob/dev/vulkan_dump_resources.md), [JSON conversion](https://github.com/LunarG/gfxreconstruct/blob/dev/tools/convert/README.md), and [upstream test applications](https://github.com/LunarG/gfxreconstruct/blob/dev/TESTING_test_apps.md).
- [Replay shader option implementation](https://github.com/LunarG/gfxreconstruct/blob/dev/tools/replay/replay_vulkan_feature.cpp), [replacement implementation](https://github.com/LunarG/gfxreconstruct/blob/dev/framework/decode/vulkan_replay_consumer_base.cpp), and [experimental remote protocol](https://github.com/LunarG/gfxreconstruct/blob/dev/docs/remote_protocol.md), inspected locally at `a20d3dbf83df4f8e02d52e42f19f0ea060708237`.
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross), [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools), and [glslang](https://github.com/KhronosGroup/glslang).
- [LunarG's Sokatoa article](https://www.lunarg.com/how-samsung-built-its-new-android-gpu-profiler-on-lunargs-gfxreconstruct/) and [Sokatoa upstream](https://github.com/sarc-acl/sokatoa), used as workflow references; Sokatoa's implementation/features are not assumed to be present in vanilla GFXReconstruct.
