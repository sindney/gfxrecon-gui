## ADDED Requirements

### Requirement: Atomic selected-draw diagnostic query
A graphics-draw inspection SHALL query original color, solid coverage, wireframe, depth pass/fail, UV density checkerboard and orange silhouette in one cancellable background request. The completed bundle SHALL be cached by capture, execution, target, overrides and indirect record. Switching a fetched mode SHALL display its local result without replay. Unsupported modes SHALL retain a diagnostic without discarding available modes.

#### Scenario: Switch diagnostics after readback
- **WHEN** a draw query completes and the user selects wireframe, depth or original
- **THEN** the corresponding cached output appears without another replay job

#### Scenario: Supersede a bundle
- **WHEN** a different draw is selected while diagnostics are being fetched
- **THEN** obsolete partial results cannot replace the current selection

### Requirement: UV checkerboard
UV density SHALL show a checkerboard derived from a supported captured interpolated UV varying, with a consistent number of cells per UV unit. Missing compatible UV input SHALL show an explicit unavailable explanation.

#### Scenario: Inspect textured geometry
- **WHEN** the selected draw has a compatible UV varying
- **THEN** checker cell size changes with its UV mapping and the chosen varying location is identified

### Requirement: Orange silhouette
An orange silhouette mode SHALL issue an additional diagnostic draw over the selected draw to highlight its geometry coverage, preserving viewport, scissor, culling and indirect selection while ignoring depth occlusion.

#### Scenario: Highlight a selected draw
- **WHEN** Silhouette is selected after the bundle is fetched
- **THEN** orange pixels identify the selected geometry on its original rendered context
