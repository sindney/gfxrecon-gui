## Context

Image documents are reset in SelectEvent before replay, and an empty document list repeatedly selects Outputs. Diagnostics currently encode one mode in each replay request and temporary Vulkan pipeline. Indirect argument bytes are already read back, but mesh decoding only accepts direct commands.

## Goals / Non-Goals

**Goals:** Fix all six reported interactions; retain real GPU-derived geometry and UVs; make mode switching immediate after one background inspection query; preserve selection cancellation and provenance.

**Non-Goals:** General post-VS instrumentation, new support for shader objects/extended dynamic state, and guessing UV coordinates where the captured stage interface has no suitable varying.

## Decisions

- Keep the last published image documents and resources during automatic selection refresh. Generation checks continue to reject obsolete results; refresh overlays identify and block stale content. Replace documents only on successful publication. Compute inputs get an explicit unsupported message, with no forced tab selection.
- Reserve focused editor navigation keys using ImGui key ownership and active navigation masks in the generated editor integration.
- Add a single cancellable diagnostic-bundle request. It fetches the full original draw once and performs output-only diagnostic replay passes under the same selection, target, overrides and subdraw identity. Publish the complete bundle atomically; cache artifacts per mode and keep switching in the GUI local. Reusing one replay process would require new render-target/depth snapshot and command-buffer cloning machinery; bounded sequential passes reuse the existing verified replay isolation at the cost of initial latency.
- Generate UV checkerboard fragment SPIR-V using a reflected floating-point fragment input (prefer UV/TEXCOORD names, otherwise a two-component input); use interpolated captured UVs and a 32-cell-per-unit checker with derivative antialiasing. Missing UVs produce a mode-specific unavailable message without failing original color or other modes.
- Add Silhouette as an explicit orange coverage mode, drawing the original selected draw before an extra orange diagnostic draw with depth disabled. Solid retains the existing coverage replacement semantics.
- Decode indirectParams records with the same bounded direct decoder, preserving each record's primitive boundaries, first index/vertex, base vertex and first instance. Retain records locally for All (default) or individual selection without replay. Post-VS evaluation runs per record so instance identity remains correct.

## Risks / Trade-offs

- Multiple diagnostic passes increase first-query latency and storage → use outputs-only passes, existing result caches, cancellation and one atomic publication; document the measured cost.
- UV semantic names can be stripped → choose only supported float varyings and expose the chosen location; explicitly report unavailable when no safe candidate exists.
- Wireframe/device or pipeline limitations → store individual errors and keep the other modes usable.
- Large indirect batches → preserve the existing total vertex limit and diagnose omitted/empty or invalid records; never connect primitives across records.

## Migration Plan

Version the bundle identity and rebuild host/Android replay tools. Existing captures and settings remain valid. Existing single-mode CLI requests remain supported.
