## Why

All capture, indexing, replay, resource-dump, validation and shader-override capability currently lives behind the ImGui workspace, so agents and automation scripts cannot capture a game, read what a frame contains, or round-trip a modification to a real device. A command-line front end over the same core, plus a filesystem-based structured dump of a frame, lets an agent explore static and dynamic frame data with ordinary file tools and then interact with the capture through real replays.

## What Changes

- Extract the ImGui-free sources (`analysis/`, `replay/`, `capture/`, `core/`, non-windowing `platform/`, shader services) into a `gfxrecon_core` static library linked by the GUI, the tests and a new console executable `gfxrecon-cli.exe`. Capture import and tool discovery move out of `Workspace` into the core so both front ends resolve tools, caches and sessions identically.
- Add `gfxrecon-cli` with a stable machine-readable contract: one JSON object on stdout per command, progress on stderr, documented exit codes (including a distinct "device locked / human action required" code), and a `describe` command that prints the command schema.
Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.
- Add `dump`: convert/index a capture and write a deterministic directory tree — capture, objects, pipelines and shaders (SPIR-V, GLSL, assembly, reflection), then `frames/<frame>/passes/<pass>/draws/<draw>/` with static JSON per level. Optional dynamic tiers replay the frame to add the final frame image, per-pass outputs, per-draw outputs, and full per-draw inputs/outputs/buffers as PNG plus metadata. Dynamic work is batched per command buffer, cached and resumable, and every node records its availability honestly.
- Add the dynamic replay API: `replay` to a frame or to a selected draw on local or Android targets, with shader overrides, Vulkan validation, repeated-run performance measurements, and `compare` for image differences; `shader show|compile|check` supports an edit → compile → interface-check → override → replay → compare loop without editing the original capture.
- Add static query commands (`info`, `events`, `draw`) that reuse the dump serializers for quick lookups without writing a tree.
- Ship `gfxrecon-cli.exe` beside `GfxReconGui.exe` in the single canonical distribution, sharing its tool configuration and caches, plus agent-facing documentation.

## Capabilities

### New Capabilities
- `agent-cli`: CLI executable, shared core with the GUI, output/exit-code contract, schema description, tool/device discovery and concurrent-use safety.
- `agent-capture`: Stateless, journaled agent-driven Android on-demand capture (arm/trigger/retrieve/end/restore) and scheduled Android/Windows capture from the CLI.
- `structured-capture-dump`: Filesystem representation of one or more frames with static data per capture/frame/pass/draw and optional tiered dynamic replay artifacts, manifests, indexes and resumption.
- `agent-replay`: Replay to frame or draw, shader overrides and shader edit services, validation replay, performance metrics, and image comparison from the CLI.

### Modified Capabilities
- `inspection-workspace`: The "one public distribution" requirement now includes the companion `gfxrecon-cli.exe` in the same canonical distribution, sharing configuration and caches.

## Impact

- **Build**: `CMakeLists.txt` gains a `gfxrecon_core` library and a `gfxrecon_cli` console target; `gfxrecon_gui`, `gfxrecon_gui_tests` and `gfxrecon_core_tests` link the library instead of listing sources.
- **Code**: `Workspace::ImportCapture` and tool defaults in `Workspace::Init` move to core; `LiveCaptureSession`'s Android flow is split into resumable arm/trigger/retrieve/restore steps reused by the GUI thread and CLI; `ReplayRequest`/`DumpOptions` gain multi-draw batch requests; new `analysis/export.*` serializers and `cli/` sources.
- **Dependencies**: `stb_image_write` (public domain/MIT, single header) for PNG output of host-decoded BMP replay artifacts; license added under `licenses/`. ARM `astcenc` 5.7.0 (Apache-2.0) release binaries, downloaded by CMake with a pinned SHA-256 and shipped under `tools/astcenc/`, decode ASTC textures to PNG.
- **Distribution**: `scripts/package_release.py` and `consolidate_dist.py` include `gfxrecon-cli.exe`; new `docs/CLI.md`.
- **Devices**: CLI and GUI may contend for one Android device; a host-side per-serial lock is introduced. Device settings remain journaled and restorable.
