## Why

GFXReconstruct already captures and replays Vulkan workloads, but investigating a rendering defect currently requires several command-line tools and manual correlation of API calls, pipeline state, resources, and shaders. A native inspection application based on `LOCAL_PATH` will bring those operations into a RenderDoc-style workflow and let developers test shader fixes through replay on their Android device or a compatible desktop GPU.

## What Changes

- Build a Windows-first C++/Dear ImGui application from the supplied template, preserving docking, DPI handling, themes, logging, native dialogs, and ImGui Test Engine automation.
- Provide local Windows Vulkan launch-and-capture and ADB Android device/application selection, capture configuration, capture retrieval, and recovery. Pause device testing and ask the user for help when installation, authorization, lock-screen, or other device interaction prevents progress.
- Open `.gfxr` files into a multi-frame workspace with a searchable event hierarchy, API arguments, selected-event pipeline state, resource metadata, and frame thumbnails when replay succeeds.
- Run upstream replay as a separate job on the original Android device or a compatible Windows GPU; collect frame images and event-specific image/buffer dumps for inspection. Report unsupported replay requirements explicitly.
- Extract shaders, reflect their interfaces, show SPIR-V assembly and SPIRV-Cross-generated Vulkan GLSL, and show captured source/debug metadata when present. Generated GLSL is identified as reconstructed code.
- Support shader editing through GLSL compilation or SPIR-V assembly, validation, compatible replay overrides, baseline/modified comparison, and revert. Applying an edit restarts replay; it does not modify the live application or original capture.
- Use BalazsJako/ImGuiColorTextEdit for shader source views and editing, with custom callback-based GLSL and SPIR-V tokenizers rather than its slow regex language definitions.
- Investigate invocation-level shader debugging as a bounded feasibility milestone, with a documented implementation or follow-up decision based on a real prototype. Breakpoints and variable stepping are not assumed to exist in upstream replay.
- Validate with upstream Vulkan test applications, deterministic fixtures, template UI automation, and the connected Android phone.

## Capabilities

### New Capabilities

- `inspection-workspace`: Docked capture workspace, shared selection, asynchronous jobs, persistent settings, and tool capability diagnostics.
- `desktop-vulkan-capture`: Launch a Windows Vulkan application with capture settings and import its completed capture.
- `android-vulkan-capture`: Discover devices and debuggable applications, configure and retrieve captures, restore device settings, and request help on device blockers.
- `capture-analysis`: Index captures and resolve frames, recorded calls, submitted command executions, object lifetimes, and event-specific Vulkan state.
- `replay-resource-inspection`: Execute compatible local/device replay and inspect frame screenshots, event resources, buffer contents, and baseline/modified images.
- `shader-inspection`: Extract shader binaries, reflect interfaces, display assembly and generated GLSL, and preserve source provenance.
- `shader-edit-replay`: Compile/assemble and validate edits, manage compatible shader overrides, replay revisions, compare results, and revert.
- `shader-debugging-feasibility`: Assess and prototype a defined shader debugging path and record an evidence-based scope decision.

### Modified Capabilities

None. This repository has no existing application specifications.

## Impact

- Introduces the application, process/ADB adapters, capture index, replay orchestration, resource viewers, shader services, tests, and portable Windows packaging.
- Adds pinned template dependencies, ImGuiColorTextEdit, GFXReconstruct tools/layer/replay APK, SPIRV-Cross, SPIRV-Tools, a GLSL compiler, and a JSON parser, with dependency/license records and configurable tool locations.
- Vulkan on Windows and Android is the initial supported scope. D3D12, OpenGL/ES capture, full GPU profiling/counters, full RenderDoc feature parity, and guaranteed source-level shader debugging are follow-up work.
- Android capture changes GPU debug settings and capture properties temporarily. Sessions record and restore previous values, including recovery after interrupted jobs. No APK repackaging, device rooting, or app data clearing is part of this change.
- Delivery is staged: template/toolchain and capture, offline inspection and replay resources, shader viewing, shader edit/replay, then the debugging feasibility decision. Shader editing remains part of this change's completion criteria.
