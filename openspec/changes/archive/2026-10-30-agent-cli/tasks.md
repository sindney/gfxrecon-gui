## 0. Dynamic dump feasibility gate (do first)

- [x] 0.1 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.
- [x] 0.2 Measure all-draws outputs-only, all-draws full, and pass-level (last draw of each pass) dumps at batch sizes 16/64/256: wall time split into state load, execution/readback and artifact pull, replay count, bytes on device/host, LOW_MEMORY or storage failures; cold and warm
- [x] 0.3 Extrapolate per-frame and six-frame totals and compare with the 5-minute-per-frame budget; also check whether artifacts map unambiguously back to draws
- [x] 0.4 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.

## 1. Shared core library

- [x] 1.1 Add `gfxrecon_core` STATIC target in `CMakeLists.txt` from `analysis/`, `replay/`, `capture/`, `shaders/`, `core/{log,artifacts,settings}`, `platform/{process,adb,android_display}`; link it from `gfxrecon_gui`, `gfxrecon_gui_tests`, `gfxrecon_core_tests` and remove duplicated source lists
- [x] 1.2 Move `Workspace::ImportCapture` to `analysis/import.{h,cpp}` as `gfx::ImportCapture(file, cacheRoot, tools, cancel)`; keep `Workspace` calling it
- [x] 1.3 Move tool defaults and `local-tools.json`/`gfxrecon_tools.json` merging from `Workspace::Init` to `core/tools.{h,cpp}` as `gfx::ResolveTools(settingsDir, exeDir)`; move tool availability probing alongside
- [x] 1.4 Add per-serial host device lock (`platform/device_lock.*`) and take it in GUI replay/capture/display paths; show "device busy" in the GUI on contention
- [x] 1.5 Build and run native + UI suites; confirm no expectation changes and zero ImGui errors

## 1A. ASTC tooling

- [x] 1A.1 Vendor `astcenc-avx2.exe`/`astcenc-sse2.exe` 5.7.0 (from the release zip, archive SHA-256 `<sha256>`) in `thirdparty/astcenc/`; `cmake/astcenc.cmake` verifies per-binary SHA-256 and copies them beside the built executables under `tools/astcenc/`
- [x] 1A.2 Add `licenses/astc-encoder.txt` (Apache-2.0) and a `dependencies.json` entry. Include the binaries in `package_release.py` and release inventory hashes
- [x] 1A.3 Resolve `tools["astcenc"]` in `ResolveTools` with AVX2/SSE2 selection and report it in `doctor`
- [x] 1A.4 Implement core `DecodeCompressedImage`: `.astc` header wrapping, `-dl`/`-ds` selection by format, PNG output, metadata. Verify `astcenc` decode flags and header layout against the 5.7.0 binary. Test round-trip on an encoded fixture for 4x4, 10x8 and 12x12 in UNORM and SRGB
- [x] 1A.5 GUI image viewer opens raw ASTC artifacts through DecodeCompressedImage (unit round-trip passed; the sample's ASTC textures are blitted to PNG by the replay backend, so no raw .bin exists to drive the GUI check — host decode is the fallback for formats the backend cannot blit)

## 2. CLI skeleton and contract

- [x] 2.1 Add `gfxrecon_cli` console target (`gfxrecon-cli.exe`) with `src/cli/main.cpp` and a table-driven command/option parser
- [x] 2.2 Implement the JSON stdout envelope, stderr progress sink, exit-code mapping (incl. `AndroidUnlockRequired` → 3, missing tool → 4, cancel/timeout → 5) and Ctrl+C → shared cancel token
- [x] 2.3 Implement global options (`--settings-dir`, `--tools`, `--serial`, `--target`, `--timeout`, `--wait-device`) and default settings dir = executable directory
- [x] 2.4 Implement `describe` generated from the command table and `doctor` (tools, versions, devices, replay APK, validation layer)
- [x] 2.5 Add native tests for parser, envelope and exit codes (`src/tests/cli_tests.cpp`)

## 3. Static export and queries

- [x] 3.1 Add `analysis/export.{h,cpp}` serializers: capture, frame, object, pipeline, shader-module references, draw (call args, pipeline, stages, descriptors, vertex/index, dynamic state, targets, push constants, indirect params, invocation path, `DumpRequest` selector)
- [x] 3.2 Implement pass grouping from render scopes per submit/command buffer, with compute/transfer pseudo-passes and subpass indices; unit-test on `samples/indexed-cube.gfxr`, secondary and indirect samples
- [x] 3.2Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.
- [x] 3.3 Implement `info`, `frames`, `events` (frame/filter/draws-only/limit), `draw`, `pipeline`, `object` commands using the serializers
- [x] 3.4 Test that `draw --block B` output equals the dumped `draw.json` for the same draw

## 4. Static dump tree

- [x] 4.1 Implement `dump` static writer: `manifest.json`, `capture.json`, `index.json`, `objects/objects.jsonl`, `pipelines/<id>.json`, `frames/<NNNN>/{frame.json,events.jsonl}`, pass and draw directories with sortable names
- [x] 4.2 Write `shaders/<module>/` (spv, reconstructed GLSL via SPIRV-Cross, assembly via disassembler, reflection/interface JSON, users) reusing existing shader services; mark unavailable representations
- [x] 4.3 Generate per-dump `README.md` with schema version, layout, status meanings and a working example command for a real draw in the dump
- [x] 4.4 Implement `--frames`, `--draws` (block/pass/pipeline) filters and resume/`--overwrite` provenance checks
- [x] 4.5 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.

## 5. Replay commands

- [x] 5.1 Implement `replay frame` over `ReplayCapture` with target resolution, device lock and PNG frame output (host BMP→PNG via `stb_image_write`; add license under `licenses/`)
- [x] 5.2 Implement `replay draw` by `--block`, `--draw <dump dir>` or `--execution`, with `--outputs|--full`, `--visualize`, `--subdraw`
- [x] 5.3 Implement `shader show|export|compile|check`; `--override MOD=FILE` accepting `.spv`, GLSL or `.spvasm` with compile + `CheckShaderInterface` and `--allow-interface-change`
- [x] 5.4 Implement `--baseline` (baseline + modified in one command) and `compare A B [--out diff.png]` metrics (extent check, per-channel max/mean, differing pixels, PSNR)
- [x] 5.5 Wire `--validate` to `ReplayRequest::validation`; emit `validation.json` with per-VUID counts; map unavailable layer to exit 4
- [x] 5.6 Tests: desktop triangle frame/draw replay, override confirmation failure, interface rejection, compile error path, compare on identical/different images

## 6. Dynamic dump tiers (scope set by group 0)

- [x] 6.0 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.
- [x] 6.1 Add `BuildBatchDumpRequest(capture, executions)` merging selectors per begin/submit; unit-test merged selectors against single-draw `DumpRequest`s
- [x] 6.2 Map batch artifacts back to draws via `drawIndex`/`dispatchIndex`/`beginCommandBufferIndex`/`queueSubmitIndex` metadata; mark ambiguous mappings `unavailable`
- [x] 6.3 Implement tiers `frame`, `pass` (last draw of each pass, outputs only), `draws`, `full`; request PNG from backend, fall back to host conversion; write per-artifact metadata and statuses
- [x] 6.4 Implement batch sizing (`--batch-draws`, readback estimate), split-and-retry on LOW_MEMORY/storage failure, per-batch device cleanup, `--max-bytes` budget with `skipped` status
- [x] 6.5 Persist `dynamic/batches/<n>.json` and resume completed batches on rerun with matching provenance
- [x] 6.6 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.

## 7. Performance metrics

- [x] 7.1 Add `ReplayRequest::measurement` (frame range, runs) and pass `--measurement-frame-range`, `--measurement-file`, `--quit-after-measurement-range` in `RunLocal` and the Android argument string; retrieve the measurement file
- [x] 7.2 Implement `perf` with N fresh runs, load/frame time/FPS per run, min/median/max, cache and screen-off reporting, and baseline-vs-override deltas
- [x] 7.3 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.

## 8. Agent capture

- [x] 8.1 Extract Android live-capture body into `ArmAndroidCapture`, `TriggerAndroidCapture`, `EndAndroidCapture` (journal-backed, stateless between calls); make `LiveCaptureSession` call them
- [x] 8.2 Implement `capture arm|trigger|status|end|restore` with session directory persistence, locked-device exit 3, and index verification of each retrieved capture
- [x] 8.3 Implement `capture android` (scheduled range) and `capture windows run` (`--trigger-after` / `--trigger-file`) over existing functions
- [x] 8.4 Re-run live-capture native tests and GUI on-demand capture UI tests
- [x] 8.5 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.

## 9. Distribution and documentation

- [x] 9.1 Add `gfxrecon-cli.exe` to install rules, `scripts/package_release.py`, `consolidate_dist.py` and release inventory hashing
- [x] 9.2 Validate capture, inspection and replay using bundled triangle/cube samples; retain machine-specific evidence locally.
- [x] 9.3 Add README section linking the CLI docs (packaged-CLI smoke test in `tests/test_package.py` is done: `test_packaged_cli_smoke`, part of 8 passing packaging tests)
- [x] 9.4 End-to-end agent workflow on the sample capture: static dump → pick a draw from the tree → export and edit its fragment shader → replay draw with `--baseline` → compare → `--validate` → `perf`; record evidence
