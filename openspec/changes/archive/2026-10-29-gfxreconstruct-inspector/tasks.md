## 1. M0 — Verify upstream toolchain and replay contracts

- [x] 1.1 Record the template source/dependency commits and select a pinned GFXReconstruct revision or bundle; create dependency/license/version manifests and document local tool overrides.
- [x] 1.2 Audit Windows compiler/Vulkan and Android SDK/NDK/JDK requirements; configure compatible local build paths and produce host tools, arm64 replay/layer APK, and upstream triangle test launcher without changing global Java settings.
- [x] 1.3 Run a Windows upstream triangle capture, info/JSONL conversion, shader extraction, baseline replay, screenshot, and selected-draw resource dump; retain commands, versions, schemas, hashes, and expected outputs as integration evidence.
- [x] 1.4 Preflight the selected phone and deploy compatible replay/test APKs; verify same-device triangle capture/retrieval/replay/screenshot/dump, restore capture settings, and stop for user help on installation/authorization/lock/storage blockers.
- [x] 1.5 Prove ordinary module replacement with a known fragment-color change and revert; verify upstream filenames, affected module uses, cache options, and matching-target output. Document unsupported inline/library/shader-object paths without enabling them.

## 2. M1 — Import the template and establish the workspace

- [x] 2.1 Import tracked template source/build/test/packaging files and pinned dependencies, retaining licenses and origin; exclude template git/agent/OpenSpec/generated files and keep the source template unchanged.
- [x] 2.2 Rename application/test/distribution targets, add module directories, retain SDL3/OpenGL and C++17, and build/run the native GUI and inherited smoke suite.
- [x] 2.3 Implement the default events/pipeline/shader/resources/frame-strip/jobs docking layout, capture/open toolbar, menu and palette actions, layout reset, and persisted preferences.
- [x] 2.4 Introduce the shared capture/frame/execution/stage/resource selection model and explicit empty/loading/unavailable/error states; verify panel selection synchronization in ImGui tests.
- [x] 2.5 Add configured tool discovery, version/capability display, and partial-feature availability so missing replay/compiler tools leave offline inspection usable.
- [x] 2.6 Set Programmer as the default with a one-time settings migration; arrange inspection panels in distinct docks, anchor saved layouts beside the executable, fit replay images to their view, and verify the layout visually and through UI automation.
- [x] 2.7 Move launch/capture into a procedural, non-dockable popup; remove the permanent capture dock; test Target/Setup/Options/Recording/Open, cancellation/restoration, and on-demand tool/device settings.

## 3. M1 — Jobs, artifacts, and local capture

- [x] 3.1 Implement native process execution with argument boundaries, Unicode paths, captured output, timeouts, hidden Windows processes, and owned-process cancellation; test paths/arguments containing spaces and shell metacharacters.
- [x] 3.2 Implement background jobs, bounded queues, progress/cancellation, capture/selection/revision tokens, terminal errors, and stale-result rejection; verify UI responsiveness and cleanup behavior.
- [x] 3.3 Add session directories and versioned artifact manifests with capture fingerprints, atomic publication, ownership-limited cleanup, and preservation of authored edits.
- [x] 3.4 Implement Windows executable/argument/working-directory capture configuration, process-local capture layer/environment setup, and finalized output detection; validate with the upstream triangle workload.
- [x] 3.5 Wire capture jobs to open completed output, retain failed/incomplete output status, and verify cancellation does not report a successful import or change parent/system environment.

## 4. M1 — Android capture and recoverable device sessions

- [x] 4.1 Implement ADB discovery/serial selection, connection/authorization/lock/ABI/API status, package enumeration, target debuggability checks, and tested remote-shell argument handling.
- [x] 4.2 Implement compatible APK deployment and target/output-path preflight using the M0-validated bundle; retain exact errors and request user assistance on blocked device actions without retry/uninstall/data-clearing workarounds.
- [x] 4.3 Add a persistent journal of prior setting/property presence and values before mutation, with target/device identity and owned artifacts; implement normal, cancel, failure, reconnect, and startup restoration with conflict reporting.
- [x] 4.4 Implement application-specific GPU-layer/capture setup, bounded frame-range launch/monitoring, finalized-file retrieval, and automatic completed-capture opening.
- [x] 4.5 Verify capture success, cancellation, transfer interruption, recovery after restart, pre-existing debug settings, and multiple-device isolation using deterministic adapter tests plus an opt-in real-phone test.

## 5. M2 — Capture indexing and execution-aware navigation

- [x] 5.1 Implement normalized info/JSONL conversion adapters and streaming, versioned offset indexing with 64-bit identities, capture/tool/schema invalidation, bounded record loading, and interrupted-import handling.
- [x] 5.2 Implement searchable raw API views, paged arguments, object/debug names, thread/return metadata, frame boundaries, and explicit original versus capture-relative frame numbering.
- [x] 5.3 Track object lifetimes, command-buffer recording generations, reset/re-record, submissions, repeated execution, secondary invocation paths, and render-pass/dynamic-rendering scopes; verify against independent expected fixtures.
- [x] 5.4 Build the submitted frame/submission/render-scope/event hierarchy with links back to raw recording order and distinct identities for repeated executions.
- [x] 5.5 Resolve graphics/compute pipeline-stage state, shader/entry/specialization references, dynamic state, render targets, push constants, vertex/index buffers, and descriptor binding/update history for supported paths.
- [x] 5.6 Add explicit unresolved-state/provenance handling for update-after-bind, unsupported extensions/library paths, cross-queue ambiguity, indirect execution, and unsubmitted recordings; verify destroyed shader modules remain inspectable through pipelines.
- [x] 5.7 Measure indexing/navigation/cancel behavior on at least 100,000 calls and record latency/memory; verify detailed JSON records are loaded on demand and stale/incomplete caches are rejected.

## 6. M2 — Replay and resource inspection

- [x] 6.1 Implement compatible local Windows and selected-serial Android replay jobs, option/frame-number mapping, stop/cancel, logs, and replay-from-start behavior without claiming arbitrary event stepping or instant seek.
- [x] 6.2 Generate validated resource-dump JSON for selected draw/dispatch occurrences, including required recording/render/subpass/submission/secondary indices; test repeated submissions and reject unresolved selections.
- [x] 6.3 Collect screenshots, resource JSON, images, and buffers through CLI/device paths into atomically completed manifests keyed by target, selection, options, and override revision; handle partial output and disconnects.
- [x] 6.4 Implement GUI-thread texture uploads and frame thumbnails, image pan/zoom/channels/decoded pixel readout, resource metadata, raw buffer hex/scalar views, and resolved vertex/index tables with format/provenance diagnostics.
- [x] 6.5 Verify a selected draw's attachments and bound resources against the known upstream test output on Windows and the original phone; record unsupported formats/features and preserve offline navigation on replay failure.
- [x] 6.6 Probe the experimental remote protocol and record compatibility; either validate an optional loopback/ADB-forwarded transport with bounded payloads and confined paths or retain the tested CLI/file fallback with an explicit capability result.

## 7. M3 — Shader extraction and readable views

- [x] 7.0 Pin/integrate BalazsJako/ImGuiColorTextEdit and implement custom non-regex GLSL/SPIR-V tokenizer callbacks, comment/preprocessor handling, token correctness tests, multiline edit refresh, and a 10,000-line performance measurement.
- [x] 7.1 Pin and integrate SPIRV-Cross, SPIRV-Tools, and a Vulkan GLSL compiler, including build/package options, configurable toolpaths, notices, and version records.
- [x] 7.2 Implement exact binary extraction and module/lifetime/hash/stage/entry/specialization/pipeline mapping, distinguishing identical binaries with different replacement identities.
- [x] 7.3 Implement assembly, reconstructed Vulkan GLSL, reflection, and captured-source/debug metadata views with line numbers, syntax coloring, search, copy/export, and explicit provenance.
- [ ] 7.4 Verify known vertex/fragment/compute modules, destroyed/shared modules, stripped debug information, non-default entry/specialization metadata, extraction failure, and cross-compilation fallback without losing assembly/raw binaries.

## 8. M4 — Shader editing, replay comparison, and revert

- [x] 8.1 Add editable GLSL and SPIR-V assembly documents with dirty state, immutable candidate revisions, explicit compile/assemble settings, validation, and actionable source diagnostics.
- [x] 8.2 Implement interface/target compatibility checks for entry/stage, descriptors, push constants, stage I/O, capabilities/execution modes, and specialization requirements; reject unsupported interface changes without altering the working revision.
- [x] 8.3 Map supported module overrides to upstream filenames, expose affected shared-module uses, snapshot active override sets, apply tested cache/binary safeguards, and restart fresh replay for Apply.
- [x] 8.4 Confirm actual replacement and output collection before publishing success; retain the last working revision and logs on compile, validation, pipeline-creation, or replay failure.
- [x] 8.5 Add matching-target/options/selection baseline generation, side-by-side and absolute-difference views, mismatch diagnostics, Revert, and original capture hash verification.
- [ ] 8.6 Run known fragment-color and compute-output edits through the full compile/validate/replay/compare/revert loop with declared output tolerances; verify invalid edits, shared-module scope, target mismatch, and selection/revision races.
- [ ] 8.7 Exercise inline pipeline/library and shader-object paths supported by the chosen bundle; enable only paths with verified extraction/identity/replacement coverage and document all remaining editing limitations.

## 9. M5 — Shader debugging feasibility

- [x] 9.1 Within a two-engineering-day investigation, evaluate a known fragment/compute invocation, available debug/source metadata, resource/input reconstruction, and instrumentation/interpreter/backend-hook options.
- [x] 9.2 Run and verify a minimal invocation-value prototype if feasible within the bound; otherwise document a concrete backend gap and evidence. Address missing source mappings, derivatives/subgroups, memory/atomics, and unsupported instructions.
- [x] 9.3 Write the go/no-go report with versions/device/results/limits and a separate production-debugging proposal if warranted; verify the GUI exposes no operational stepping/breakpoint/watch controls without a validated backend.

## 10. M5 — Acceptance and portable release

- [x] 10.1 Complete deterministic regression coverage for malformed/truncated records, cache invalidation, recording/execution/state cases, artifact transport, device recovery, shader diagnostics, and failed revision retention; keep hardware tests opt-in.
- [x] 10.2 Extend ImGui Test Engine end-to-end coverage for open/import, synchronized event/stage/resource navigation, jobs/cancel/errors, shader Apply/compare/Revert, and persisted layout; run the relevant CTest suite.
- [x] 10.3 Perform the full live-phone acceptance loop: bounded triangle capture, retrieve/open, inspect selected submitted draw and shader, same-device screenshot/resource replay, known shader edit and image change, revert, and restoration of prior device settings. Record versions, device/GPU/driver, commands, hashes, results, and justified feature skips; stop for user help on device blockers.
- [x] 10.4 Update template-derived Windows build/package scripts for the application/tool bundle, required runtime DLLs, shader tools, optional Android artifacts, license notices, and version manifests; verify extracted portable startup and a representative capture workflow.
- [ ] 10.5 Document supported workflows, installation/tool configuration, same-device versus cross-device replay limits, reconstructed-source semantics, module-wide edit scope, recovery instructions, and debugging feasibility outcome; verify all capability requirements have acceptance evidence before marking the change complete.

## 11. Visual review UI revision

- [x] 11.1 Migrate to the requested top filmstrip, left Events, central Pipeline, right Image/Resources/Shader tabs, and shared bottom Jobs/Console layout; move capture under File, remove the toolbar, and put the trace filename in the native title.
- [x] 11.2 Replace frame sliders/buttons with real preview tiles, automatic default/selection replay, visible preview generation, selection highlighting, and frame context actions.
- [x] 11.3 Add pointer-anchored wheel zoom, sRGB display control, and inline Fit/scale while preserving raw screenshot readout.
- [x] 11.4 Move event toggles beside search and totals below the tree; replace pipeline JSON views with category tabs and structured tables.
- [x] 11.5 Focus Shader from stage buttons, fill the editor dock, simplify labels/status, and add clickable pipeline references in a resizable editor sidebar.
- [x] 11.6 Re-encode the supplied rounded G application icon and integrate native executable/window icons and rounded corners.
- [x] 11.7 Run native/ImGui and portable existing-trace checks, review screenshots, and hand over for visual inspection without using ADB or the occupied Android device.

## 12. Compact filmstrip and operation status

- [x] 12.1 Move target selection into tile/background context menus, remove the count/status row, and migrate Frames into the Console dock by default.
- [x] 12.2 Replace Jobs with spaced essential Shader/Console title dots and per-frame tile badges; wrap/clip pending text, log operation results and capture phases, and retain cancellation; test all four states and stable panel identities.
- [x] 12.3 Verify native/UI regression and live-phone capture/replay/edit/revert/recovery, validate the portable review build, and hand over for visual inspection.

## 13. Input fixes and mesh inspection

- [x] 13.1 Add wheel scrolling over pipeline categories and fix native SDL/IME editor activation/caret/composition handling using the marp-gui findings; verify native UTF-8 packets and composition keys.
- [x] 13.2 Resolve replay-backed indexed/direct input geometry, format/stride/offset/base-vertex/restart/instance handling, bounded/cancellable loading and coordinate tables with explicit unsupported cases.
- [x] 13.3 Add an isolated OpenGL solid/wireframe/points mesh renderer with local orbit/pan/zoom/Fit and supported captured vertex-shader transform-feedback evaluation for screen space.
- [x] 13.4 Validate procedural and indexed-cube positions/uniform transforms against independent expected values, verify rendered pixels and UI controls, exercise phone readback, and document device-native versus local evaluation limits.
- [x] 13.5 Bundle the indexed-cube trace, update OpenSpec/workflow documentation, run portable verification, and hand over for visual inspection.

## 14. Shader width, canonical release and on-demand game sessions

- [x] 14.1 Move shader pipeline references below the full-width editor and verify focus/selection and geometry in UI tests.
- [x] 14.2 Rename the public executable to GfxReconGui.exe while retaining internal CMake target identities.
- [x] 14.3 Implement armed Windows/Android sessions, delayed N-frame and manual capture, repeated retrieval, separate disarm/close-game actions, and Android restoration journals.
- [x] 14.4 Verify delayed bursts, manual stop and continued game lifetime on Windows through the actual layer/native GUI; regress trimmed snapshot/original-frame indexing.
- [ ] 14.5 Validate delayed N-frame/manual-stop Android sessions, game lifetime, retrieval and settings restoration on the unlocked phone.
- [x] 14.6 Verify one canonical portable distribution/sample, preserve prior review settings/session data, retire duplicate review folders, and hand over for inspection.

## 15. Large-capture replay and responsive draw inspection

- [x] 15.1 Remove filmstrip preview fan-out; render only the initially selected frame and explicitly clicked frames, leaving other tiles idle.
- [x] 15.2 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.
- [x] 15.3 Implement verified upload/output reuse and target-generated pipeline caches with capture/backend/device/override invalidation; fix cache lifetime, binary I/O and indexed reads in generated backend copies, with native reader tests and exact-binary capability gating.
- [x] 15.4 Add debounced latest-selection draw-output replay, cancel obsolete work, prioritize shader/inspection actions, cache completed outputs, and keep pipeline/event navigation responsive.
- [x] 15.5 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.
- [x] 15.6 Update workflow/backend limitations, preserve user data, publish the single portable release, and hand over measured results for inspection.
- [x] 15.7 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.

## 16. Upstream optimizer integration

- [x] 16.1 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.
- [x] 16.2 Bundle/discover gfxrecon-optimize and prepare hash-verified, cached replay copies by default while retaining original CPU inspection and source/derived provenance; support unchanged/error fallback and an original-replay context option.
- [x] 16.3 Validate automatic preparation/reuse/invalidation, original-file preservation, shader editing, mesh readback, cancellation and packaged workflows; publish one release and record same-device results.

## 17. Completed capture save destinations

- [x] 17.1 Add Save/Save As to all completed on-demand and scheduled outputs, with native default-directory/filename selection, persisted saved destinations and File/palette actions for existing captures.
- [x] 17.2 Implement background hash-verified copy and atomic publication, safe default-location saves, confirmed replacement, cancellation/error preservation and current-document filename updates without losing inspection state.
- [x] 17.3 Verify Unicode/renamed destinations, default-save/live-game continuity, cancelled dialogs, errors, collision protection and original-byte preservation; publish one portable release while retaining existing game/session data.

## 18. Selected-draw images, game meshes and Vulkan validation

- [x] 18.1 Separate end-of-frame, selected color/depth/stencil outputs and bound-texture views; preserve binding/stage/subresource identity, hide stale images and fetch texture inputs only when requested.
- [x] 18.2 Fix game vertex format decoding/default position selection and active shader interface reconstruction; preserve input geometry with explicit post-VS limitations, and validate the saved real-game readback.
- [x] 18.3 Bundle/configure Khronos validation layers and expose fresh original-capture validation mode, with no result/optimizer/pipeline cache bypass and structured enabled/VUID/error/warning diagnostics.
- [x] 18.4 Validate native/UI/Windows validation, preserve original captures/settings, update documented limits and publish one verified portable release.
- [x] 18.5 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.

## 19. Automatic draw inspection and image navigation

- [x] 19.1 Make draw selection query all shader stages, mesh inputs/post-VS and bound images in one cancellable readback; keep pipeline selection synchronized and protect edited documents.
- [x] 19.2 Add a right-aligned selected-event fetch dot, coalesce pending clicks, terminate obsolete automatic work and reject stale results.
- [x] 19.3 Add vertical image lists and a Pipeline Textures table with links into Image, removing per-element fetch requirements and shortening the mesh tooltip.
- [x] 19.4 Add display-only rotate/flip controls with correct transformed fit/zoom/pixel coordinates, and verify mapping on asymmetric images.
- [x] 19.5 Run native/UI/game-readback and portable regression, preserve existing captures/settings and publish one release for review.

## 20. RenderDoc image and pipeline workspace

- [x] 20.1 Reproduce the reference image workspace: wide canvas, selected-image document tabs, default color output, right-side input/output thumbnail lists and binding/details navigation.
- [x] 20.2 Replace flat pipeline categories with a clickable stage flow and stage-specific shader, texture, sampler, buffer and fixed-function sections with working resource links.
- [x] 20.3 Restrict dots to Frames, frame tiles and draw events; block refreshing inspector content with dimmed overlays while preserving event cancellation/navigation.
- [x] 20.4 Verify fresh real-game replay and selected-draw readback, native/UI stage and image navigation, refresh success/failure/cancellation, and review rendered screenshots.
- [x] 20.5 Publish the verified single portable release, preserve user data and update the specification and evidence.

- [x] 20.6 Eliminate ImGui layout/ID diagnostics and fail native GUI/portable verification on any recovered error or ID conflict.

## 21. Locked-phone replay and screen-off operation

- [x] 21.1 Detect keyguard/security state, notify in the GUI with replay retry/continue-inspecting actions, and attempt one wake/dismiss only for a confirmed unsecured lock screen.
- [x] 21.2 Add optional screen-off replay using supported Android display-power controls, preserve logical rendering, and restore owned display changes on success/failure/cancellation with reconnect recovery.
- [x] 21.3 Verify lock parsing, secure/unknown refusal, nonsecure dismissal, failure/cancel, notification/retry, screen-off restoration and real-game on/off pixel equivalence with clean ImGui diagnostics.
- [x] 21.4 Document capability limits, preserve captures/settings, publish one verified portable release and audit the full requested workflow.

- [x] 21.5 Remove the slow automation dialog-resize gesture and keep launch/tool dialog width stable while content updates.

## 22. Image inspection controls and indirect replay

- [x] 22.1 Replace raw image tooltips with compact format/dimensions/mip/aspect/binding details and directional rotation controls.
- [x] 22.2 Add a dual-ended display range with numeric endpoints, auto-range and reset for depth/color, retaining original pixel values and correct orientation.
- [x] 22.3 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.
- [x] 22.4 Verify native range math, GUI interaction/clean diagnostics, real indirect readback and portable workflows; preserve user data and publish one release.

## 23. Captured shader entry-point editing

- [x] 23.1 Compile reconstructed GLSL main() to the captured SPIR-V entry name, retaining validation and actionable compatibility checks.
- [x] 23.2 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.
- [x] 23.3 Preserve authored edits and captures, run clean native/GUI/portable regression and publish one release.

## 24. Selected-draw GPU visualization

- [x] 24.1 Implement selection-scoped GPU solid/wireframe and geometry depth-pass/fail pipelines with explicit device/state limitations, original capture preservation and exact cache identity.
- [x] 24.2 Isolate fixed-count indirect subdraws using their original GPU argument buffers, expose All/individual selection and retain correct readback provenance.
- [x] 24.3 Integrate visualization controls with automatic latest-selection replay and validate native/GUI modes, subdraw isolation, failure handling, Vulkan validation and clean diagnostics.
- [x] 24.4 Document supported semantics, preserve user data and publish one verified portable release for inspection.
- [x] 24.5 After user unlocks the securely locked phone, install the updated replay APK and verify real-game coverage/depth-occlusion modes and the three-record indirect batch on Android.

## 25. Replay recovery and Frames startup

- [x] 25.1 Remove the blocking Android activity-draw wait, detect a lock during a stalled replay, and verify unlock/retry with real replay completion.
- [x] 25.2 Make Frames the active footer tab at startup and layout reset while preserving docking positions and manual Console selection.
- [x] 25.3 Run native/GUI/device checks, preserve user data and publish one verified portable release.
