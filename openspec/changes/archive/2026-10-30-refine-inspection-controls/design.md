## Context

Undo/Redo currently call only TextEditor history. Revert text restores the document but does not remove active replay overrides. The image viewer owns an existing footer. Settings uses a dockable Begin window; Tools and devices is already modal.

## Goals / Non-Goals

**Goals:** Apply all requested UI refinements, preserve capture and unrelated overrides, and make outline pixels derive from actual selected-draw coverage.

**Non-Goals:** Add a UV selector without an established need, change text Undo into replay history, or recompile reconstructed GLSL to approximate an original captured binary.

## Decisions

- Remove the UV detection line completely; automatic selection and unavailable-mode diagnostics remain. Pass image identity/format/mip/slice into ImageViewer's existing footer, with full details in its tooltip.
- Replace the visible mode with Orange outline. Keep the internal silhouette request key, bump its protocol, and render its coverage in green while Solid remains orange. Differencing those two renders identifies coverage even when original scene pixels are orange. Composite a two-source-pixel inner boundary over the original selected-draw color, retaining all interior/context pixels. Include original/coverage hashes in replay cache identity and compose before manifest/cache publication.
- Reset restores the exact captured shader by removing this module's override, rather than compiling a reconstruction. Run a fresh replay with remaining overrides and publish the restored editor state only on success. Clear candidate, comparisons and text history; preserve edits and prior results on failure. Standalone/offline documents reset text without a replay. Locked-device retry retains reset intent.
- Promote Undo/Redo to the shader toolbar; use editor CanUndo/CanRedo, read-only and busy states to disable them. Tooltips identify text-only scope and actual Ctrl/Cmd+Z/Y shortcuts.
- Settings becomes an always-submitted popup modal, opened by a one-shot request, centered on appearance, non-dockable, and closed by its title close button, Close or Escape.

## Risks / Trade-offs

- Cross-pass nondeterminism can affect coverage differencing → use isolated diagnostic colors with the same target, overrides and subdraw; existing unsupported state restrictions remain. Test unchanged scene/interior pixels and thin boundaries.
- Reset failure or stale completion could lose edits → retain front-end state until a generation-matched successful replay and verify failure retention.
- Footer text can exceed narrow docks → clip to one status row and expose full details in a tooltip.
- New outline protocol requires updated host/Android tools → rebuild and verify both before publishing; old backends fail explicitly.
