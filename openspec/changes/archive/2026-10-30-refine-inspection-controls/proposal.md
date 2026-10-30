## Why

The previous diagnostic and editor controls expose implementation details, use a filled highlight where an outline is needed, and make restoration of an applied shader edit unclear. Settings should behave like a transient dialog rather than occupy workspace docking space.

## What Changes

- Remove the UV detection status line; retain automatic UV selection.
- Move image identity, format, mip and slice into the existing image footer.
- Replace the visible orange silhouette mode with an orange boundary outline, preserving original image pixels inside the draw.
- Add one-click Reset shader to restore captured text and the original binary, clear the current candidate/history, remove its override and replay automatically.
- Put text Undo and Redo on the primary shader toolbar, disable unavailable actions, and show shortcut tooltips.
- Present Settings as a centered non-dockable modal dialog.

## Capabilities

### New Capabilities

- `draw-outline`: Boundary highlighting based on actual replay coverage, including indirect record selection.

### Modified Capabilities

- `inspection-workspace`: Compact image footer and Settings dialog behavior.
- `shader-edit-replay`: Primary text history actions and explicit reset-to-captured replay.
- `draw-diagnostics`: Replace the filled silhouette requirement with the outline mode.

## Impact

Image rendering, diagnostic replay protocol/cache identity, shader toolbar and reset jobs, Settings, GUI/native tests and portable packaging. Existing uncommitted QoL work remains in place. No capture bytes or unrelated shader overrides are changed by reset.
