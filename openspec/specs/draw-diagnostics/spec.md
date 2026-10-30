# draw-diagnostics Specification

## Purpose
TBD - created by archiving change draw-inspection-qol. Update Purpose after archive.
## Requirements
### Requirement: Atomic selected-draw diagnostic query
A graphics-draw inspection SHALL use a cancellable background query with a persisted Light or Full preparation policy. Light SHALL be the default and fetch only the requested view and missing prerequisites. Full SHALL prepare original color, solid coverage, wireframe, depth pass/fail, UV density checkerboard and orange outline. Completed views SHALL be cached by capture, execution, target, overrides and indirect record. Switching a fetched mode SHALL display its local result without replay. Unsupported modes SHALL retain a diagnostic without discarding available modes. Shader and resource inspection SHALL remain available under either policy.

#### Scenario: Switch diagnostics after readback
- **WHEN** a fetched view is selected again
- **THEN** its cached output appears without another replay job

#### Scenario: Supersede a bundle
- **WHEN** a different draw is selected while diagnostics are being fetched
- **THEN** obsolete partial results cannot replace the current selection

#### Scenario: Light initial selection
- **WHEN** a draw is selected in Original mode with default preferences
- **THEN** one original readback runs and unrelated diagnostic modes are not prepared

#### Scenario: Light outline selection
- **WHEN** outline is requested after original readback
- **THEN** only missing solid coverage and outline passes run, retaining the original readback and earlier cached views

#### Scenario: Latest uncached mode
- **WHEN** a user changes the desired mode while another mode is loading
- **THEN** the latest desired mode is fetched after the current query when it is still missing

### Requirement: UV checkerboard
UV density SHALL show a checkerboard derived from a supported captured interpolated UV varying, with a consistent number of cells per UV unit. Missing compatible UV input SHALL show an explicit unavailable explanation.

#### Scenario: Inspect textured geometry
- **WHEN** the selected draw has a compatible UV varying
- **THEN** checker cell size changes with its UV mapping and the chosen varying location is identified

### Requirement: Draw replay preference
Settings SHALL offer Light (on demand) and Full (all views), persist the selection across restarts, and default to Light when the preference is absent. Full SHALL preserve eager preparation of all six views.

#### Scenario: Choose Full
- **WHEN** Full is selected and a new draw inspection starts
- **THEN** all supported views are prepared and subsequent mode changes use the local cache

#### Scenario: Restart with Light
- **WHEN** Light is saved and the application restarts
- **THEN** Light remains selected

