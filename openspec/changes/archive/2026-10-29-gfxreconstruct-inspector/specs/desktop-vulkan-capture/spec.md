## ADDED Requirements

### Requirement: On-demand game capture sessions
The application SHALL offer launch-armed N-frame and manual start/stop modes alongside scheduled ranges. Arming SHALL track state without recording. Sessions SHALL accept repeated triggers and expose only finalized captures. Stop capture and End session SHALL keep the game running. Closing it SHALL be a separate explicit action. Windows controls SHALL be confined to the launched process and compatible layer.

#### Scenario: Delayed repeated bursts
- **WHEN** gameplay has advanced beyond startup and the user requests N frames twice
- **THEN** two finalized traces contain N capture-relative frames each with original application frame mapping
- **AND** the game continues and the session rearms after each burst

#### Scenario: Manual recording
- **WHEN** the user starts recording and later presses Stop capture
- **THEN** recording finalizes at a frame boundary without terminating the game
- **AND** opening the trace defers automatic replay while the session remains active

#### Scenario: Paused game or incomplete exit
- **WHEN** the game cannot reach a stop boundary or exits mid-recording
- **THEN** the UI reports that state and retains incomplete artifacts without publishing a successful capture

### Requirement: Launch configured Vulkan capture
The application SHALL launch a selected Windows Vulkan executable with arguments, working directory, output location, and supported frame-range/trim options using a compatible GFXReconstruct layer. Capture environment changes SHALL be confined to the launched process environment.

#### Scenario: Capture an upstream triangle workload
- **WHEN** the user launches the upstream Vulkan triangle test application with a bounded capture range
- **THEN** the application starts it with the required capture layer and produces an identifiable capture job and destination
- **AND** the user's system and parent environment remain unchanged

#### Scenario: Capture layer is incompatible or missing
- **WHEN** required layer files or executable architecture compatibility checks fail
- **THEN** capture does not start and the application reports the missing or incompatible component

### Requirement: Import completed capture output
The application SHALL distinguish completed non-empty capture files from active, failed, or interrupted output, and SHALL offer completed output for indexing without requiring the user to locate it manually.

#### Scenario: Capture completes
- **WHEN** the capture output is finalized successfully
- **THEN** the completed file can be opened directly in the inspection workspace with its capture settings recorded

#### Scenario: Capture is cancelled
- **WHEN** the user cancels a running capture
- **THEN** the application stops its owned capture activity, records the terminal state, and labels any incomplete output instead of importing it as a successful capture
