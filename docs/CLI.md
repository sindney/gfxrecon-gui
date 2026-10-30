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

## Capture

```powershell
.\gfxrecon-cli.exe capture windows run --exe tools/gfxrecon-gui-live.exe --cwd samples/test-res --trigger-after 2 --frames 3 --out captured
```

Android sample (install the bundled debug sample APK first):

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
