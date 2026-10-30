## Why

Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.

## What Changes

- Keep shader caret/navigation keys within the focused editor.
- Retain the previous image documents, sidebar and canvas under refresh feedback until the selected draw's results are ready, then publish them together.
- Fetch original color, solid coverage, wireframe, depth pass/fail, UV checkerboard and an orange silhouette in one selected-draw query; switch cached modes locally.
- Show a stable unsupported message for compute image inputs without forcing Outputs selection.
- Preview all drawable records of indirect draws by default, with a selector for an individual record.
- Validate on the supplied capture and desktop regression workloads.

## Capabilities

### New Capabilities

- `draw-diagnostics`: Batched diagnostic readback, UV checkerboard and orange draw silhouette.

### Modified Capabilities

- `inspection-workspace`: Stable image refresh, compute sidebar selection and editor key ownership.
- `mesh-inspection`: Indirect argument records and all/single record mesh preview.

## Impact

Changes affect the ImGui workspace, shader editor integration, mesh decoder/viewer, replay requests/cache and generated Vulkan replay instrumentation. Host and Android replay tools must implement the same updated diagnostic protocol; capture files remain unchanged.
