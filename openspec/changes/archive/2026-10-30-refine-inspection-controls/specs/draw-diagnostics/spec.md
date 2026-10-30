## MODIFIED Requirements

### Requirement: Atomic selected-draw diagnostic query
A graphics-draw inspection SHALL query original color, solid coverage, wireframe, depth pass/fail, UV density checkerboard and orange outline in one cancellable background request. The completed bundle SHALL be cached by capture, execution, target, overrides and indirect record. Switching a fetched mode SHALL display its local result without replay. Unsupported modes SHALL retain a diagnostic without discarding available modes.

#### Scenario: Switch diagnostics after readback
- **WHEN** a draw query completes and the user selects wireframe, depth or original
- **THEN** the corresponding cached output appears without another replay job

#### Scenario: Supersede a bundle
- **WHEN** a different draw is selected while diagnostics are being fetched
- **THEN** obsolete partial results cannot replace the current selection

## REMOVED Requirements

### Requirement: Orange silhouette
**Reason**: Replaced by a thin boundary outline that preserves the original interior.
**Migration**: Use Orange outline, specified by the draw-outline capability.
