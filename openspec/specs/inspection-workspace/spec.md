# inspection-workspace Specification

## Purpose
TBD - created by archiving change gfxreconstruct-inspector. Update Purpose after archive.
## Requirements
### Requirement: Image document workspace
Image SHALL provide a large central canvas with tabs for opened images and a right-hand scrollable input/output thumbnail sidebar modeled on the supplied RenderDoc reference. Selecting an asset SHALL open or focus its tab and retain binding details. Each new draw SHALL initially show its first readable color attachment, falling back to other attachments only when color is unavailable. Default docking SHALL give document views a wide shared area.

#### Scenario: Inspect and revisit a texture
- **WHEN** an input texture is clicked after a draw has loaded
- **THEN** its image tab is selected and the canvas/details identify that binding
- **AND** the output tab remains available without another GPU replay

### Requirement: Pipeline stage flow
Pipeline SHALL show a connected stage flow with unavailable stages visibly disabled. Selecting a stage SHALL display named shader, resource, uniform/storage-buffer or fixed-function sections appropriate to that stage, rather than a flattened source-data category list. Image and buffer rows SHALL navigate to their inspectors with binding/range identity preserved.

#### Scenario: Follow a fragment resource
- **WHEN** Fragment is selected and its texture or buffer link is clicked
- **THEN** the appropriate image or buffer inspector opens the fetched resource and details

### Requirement: Scoped refresh feedback
Status dots SHALL appear only on Frames, frame tiles and selected draw events. Refreshing Image, Pipeline, Shader, Mesh and Resources content SHALL show a dimmed input-blocking overlay until success, failure or cancellation. Event navigation SHALL remain available to supersede running draw queries. Console SHALL remain readable and interactive.

#### Scenario: Replace a pending selection
- **WHEN** a draw inspection is running and a newer draw is selected
- **THEN** inspector content remains blocked until current work terminates, obsolete results cannot become current, and the latest draw's result removes the overlay

### Requirement: Clean ImGui acceptance diagnostics
GUI and portable acceptance SHALL fail when ImGui reports any recovered internal error, a conflicting widget ID, or an error in its debug log, even when functional assertions pass. Diagnostic logging and conflict highlighting SHALL remain enabled. Inspector overlays SHALL not modify the parent layout extent or advertise an unsubmitted cursor item.

#### Scenario: Functional success with a layout error
- **WHEN** functional GUI tests succeed but an ImGui internal error or ID conflict is observed
- **THEN** the verification process exits with failure and records diagnostic counts

### Requirement: Automatic synchronized draw inspection
A draw click SHALL immediately select its bound pipeline and schedule shader, mesh, texture and render-target readback without per-element query clicks. One readback SHALL supply the asset and geometry views; decoded shader documents SHALL be cached. A newer selection SHALL cancel/drop obsolete automatic work, and generation checks SHALL prevent stale publication. Edited shader documents SHALL not be discarded by background inspection.

#### Scenario: Select a different draw during fetching
- **WHEN** a new draw is clicked while an automatic query is running
- **THEN** the old query is cancelled or discarded and pending work converges on the newest selection
- **AND** a right-aligned dot on the selected event indicates queued/running, success, failure or inactivity

### Requirement: Linked bound-image lists
Resources SHALL expose a vertical list of selected-draw textures and render targets. Pipeline SHALL expose image tables under shader stages and framebuffer outputs for its submitted occurrence. Selecting an asset SHALL focus Image and show its details without another GPU query. Binding/aspect identity SHALL be preserved for shared images. The mesh canvas tooltip SHALL contain controls/selection identity; implementation provenance belongs in source comments.

#### Scenario: Navigate from a bound texture
- **WHEN** the user clicks a fetched texture slot in Resources or Pipeline
- **THEN** Image receives focus and displays that slot's image and binding details without starting another GPU readback

### Requirement: Image display orientation
Image SHALL provide clockwise rotation and horizontal/vertical flips without changing source bytes. Fit, scroll zoom and pixel readout SHALL follow display orientation, and pixel coordinates SHALL identify the underlying source texel.

#### Scenario: Rotate and inspect an asymmetric image
- **WHEN** the image is rotated by 90 degrees and flipped
- **THEN** display dimensions/UVs and source-coordinate readout agree, and original image/capture bytes remain unchanged

### Requirement: Save completed captures at the default or a chosen destination
Completed scheduled and on-demand captures SHALL offer Save at the default local path and Save As with native folder/filename selection initially populated from that path or a valid saved destination. The default session copy SHALL remain available. Existing raw captures SHALL also support File/palette save actions. Only finalized outputs SHALL be offered. Reopening the capture dialog SHALL retain completed outputs after the game/session ends until a new capture session is explicitly chosen.

#### Scenario: Keep default capture
- **WHEN** the user chooses Save on a completed capture
- **THEN** its existing default path is retained, remembered and displayed as Saved without modifying capture bytes or stopping the game

#### Scenario: Choose a folder or name
- **WHEN** Save As succeeds
- **THEN** a verified copy is atomically published at the chosen .gfxr path and its saved location is remembered
- **AND** the current document uses the new name without resetting event/shader inspection state

#### Scenario: Cancel, collision or write failure
- **WHEN** the dialog is cancelled, replacement is not confirmed, copying is cancelled or saving fails
- **THEN** the original and previous destination remain intact, incomplete output is not published and the document retains its previous location
- **AND** a completed publication is reported as saved even if a late cancellation arrives afterwards

### Requirement: Full-width shader editing and one public distribution
Pipeline references SHALL appear in a resizable area below the shader editor and remain clickable for pipeline navigation. The public Windows executable SHALL be gfxrecon-gui.exe in one canonical distribution. The companion console executable gfxrecon-cli.exe SHALL ship in the same distribution directory, sharing its bundled tools, configuration, sessions and caches. Retiring review distributions SHALL preserve runtime settings, sessions and authored files with verified backups before replacement.

#### Scenario: Narrow shader dock
- **WHEN** a shader with pipeline references is opened in a narrow dock
- **THEN** the editor occupies the full content width and references remain accessible beneath it

#### Scenario: CLI in the distribution
- **WHEN** the canonical distribution is packaged
- **THEN** `gfxrecon-cli.exe` is present beside `gfxrecon-gui.exe`, is included in the release inventory hashes, and resolves the same bundled tools as the GUI

### Requirement: Template-based native workspace
The application SHALL provide a Windows C++/Dear ImGui workspace based on the supplied ImGui template, retaining docking, DPI scaling, native capture-file selection, themes, console logging, command palette, and layout reset.

#### Scenario: Open the application for the first time
- **WHEN** the user starts the application without saved settings
- **THEN** the application uses Programmer, Events on the left, Image/Pipeline/Shader/Mesh/Resources as wide document tabs, and Frames/Console sharing a bottom dock; File and the command palette provide open/capture/cancel actions, and the title identifies the open trace

#### Scenario: Restore workspace preferences
- **WHEN** the user restarts after changing layout, tool paths, and theme
- **THEN** the application restores those preferences and offers layout reset

#### Scenario: Upgrade the original stacked layout
- **WHEN** an installation without the current UI-defaults version starts
- **THEN** the application applies Programmer and the arranged docking layout once, then preserves subsequent user choices

#### Scenario: Reset the docking layout
- **WHEN** the user chooses Reset Layout
- **THEN** the preferred frame/event/pipeline/inspector/diagnostic regions are restored, including the intentional inspector and diagnostic tab groups

### Requirement: Procedural capture popup
Application launch and capture SHALL use an on-demand, non-dockable popup with Target, Setup, Options, Recording, and Open stages. There SHALL be no permanently displayed capture/setup dock. Tool configuration and replay-device selection SHALL remain available on demand. During recording the popup SHALL show real operation phases, allow cancellation, wait for cleanup, and offer opening the completed result.

#### Scenario: Launch and capture a workload
- **WHEN** the user chooses Capture application
- **THEN** the popup guides target selection, prerequisite setup, application/frame options, recording, and opening the result

#### Scenario: Cancel recording
- **WHEN** the user cancels from the Recording stage
- **THEN** the popup waits for owned capture activity to stop and device settings to be restored, returns to Options, and does not open a partial capture

#### Scenario: Inspect a capture normally
- **WHEN** no capture operation or settings dialog was requested
- **THEN** no capture/setup panel occupies the docked inspection workspace

### Requirement: Consistent inspection selection
The application SHALL share capture, frame, execution occurrence, shader stage, and resource selection between panels, and SHALL identify pending, unavailable, and failed data explicitly.

#### Scenario: Select another submitted draw
- **WHEN** the user selects an executed draw in a different frame
- **THEN** pipeline, stage, resource, and argument views resolve against that occurrence and do not show old selection data as current

#### Scenario: Frame thumbnails are unavailable
- **WHEN** a capture is opened without successful screenshot replay
- **THEN** the frame selector remains usable and labels missing thumbnails without displaying fabricated images or GPU timings

### Requirement: Nonblocking and cancellable operations
The application SHALL run capture processing, device communication, shader processing, and replay outside the GUI thread. It SHALL expose progress, cancellation, terminal status, and useful error output, and SHALL discard results for superseded selections or revisions.

#### Scenario: Navigate while importing
- **WHEN** a capture containing at least 100,000 calls is being indexed
- **THEN** the GUI continues to process user input and acknowledges cancellation within one second while required cleanup completes asynchronously

#### Scenario: An older replay finishes late
- **WHEN** a replay result arrives after the user changed capture or override revision
- **THEN** the result is retained only under its original identity and does not replace the current view

### Requirement: Versioned tool capability reporting
The application SHALL discover configured tools and report versions and supported operations. Missing or incompatible tools SHALL disable only dependent actions with a diagnostic. The process adapter SHALL preserve argument boundaries and Unicode paths without interpreting user input as a host shell command.

#### Scenario: Replay tools are absent
- **WHEN** a capture is indexed but no compatible replay executable or APK is configured
- **THEN** offline event inspection remains available and replay controls identify the missing dependency

#### Scenario: Capture path contains spaces and shell characters
- **WHEN** a tool operates on a valid path containing spaces, Unicode, or shell metacharacters
- **THEN** the intended path is passed as one argument and no additional host-shell operation executes

### Requirement: Compact structured inspection
Pipeline SHALL use a stage flow and stage-specific resource/state tables rather than JSON blobs. Event toggles SHALL live in a dropdown beside search, with totals below the tree. Stage buttons SHALL switch the pipeline stage; View shader SHALL activate Shader. The shader editor SHALL fill its dock vertically, omit redundant representation/search labels, place provenance/status in tooltips or a footer, and list referencing pipelines below the editor with links that select/focus Pipeline without discarding the document.
Vertical mouse-wheel scrolling over the stage flow SHALL scroll overflowing stages horizontally, without requiring Shift or arrow buttons.

#### Scenario: Navigate from a shader to a pipeline
- **WHEN** a user selects a referencing pipeline below the shader editor
- **THEN** Pipeline is focused with that pipeline selected and the shader document is retained

### Requirement: Application branding
The Windows executable and window SHALL use the supplied G icon re-encoded at native icon sizes with rounded transparent corners. Supported Windows versions SHALL use rounded window corners.

#### Scenario: Launch the branded application
- **WHEN** the Windows GUI starts
- **THEN** the taskbar/title icon uses the supplied G and supported Windows versions apply rounded corners

### Requirement: Title status indicators and console diagnostics
Replay state SHALL be indicated by a spaced title dot on Frames, a badge within each frame tile and a right-aligned dot on the selected draw event. Other inspectors SHALL use dimmed interaction-blocking refresh overlays. Shader and Console SHALL have no title dots. Colors SHALL be green for success, red for failure, yellow for running, and grey for inactive, cancelled or superseded work. Tooltips SHALL explain the state and direct full details to Console. Detailed status, diagnostics and capture phases SHALL be recorded in Console. There SHALL be no dedicated Jobs panel. File, the command palette, and frame context menus SHALL retain operation cancellation. Panel names/IDs SHALL remain stable for saved layout and automation.

#### Scenario: A shader edit fails while replay remains usable
- **WHEN** candidate compilation fails
- **THEN** the Shader overlay is dismissed and Console contains the compiler diagnostics, while replay status dots retain the corresponding frame/event state

#### Scenario: Cancel a running import
- **WHEN** the user cancels from File or the command palette
- **THEN** refresh overlays are dismissed and frame/event indicators become inactive after cleanup, with cancellation logged

### Requirement: Frames footer activation
Frames SHALL be the active footer tab at application startup and when resetting the default layout. Existing docking positions SHALL be preserved at startup. A later explicit Console selection SHALL remain respected during that session.

#### Scenario: Start with a layout last saved on Console
- **WHEN** the application restores the user's docking layout
- **THEN** Frames becomes the active footer document without moving or resetting other docks

### Requirement: Stable image refresh documents
Automatic draw refresh SHALL retain the previous image tabs, canvas and sidebar under refresh feedback until a new result is successfully published. Failed or cancelled work SHALL retain the last successful images with selection-unavailable feedback and SHALL NOT present them as the new selection's data.

#### Scenario: Select the next draw
- **WHEN** COLOR 0 is open and the next draw is selected
- **THEN** COLOR 0 remains visible under the refresh overlay until the completed result replaces it

### Requirement: Stable compute image sidebar
Image Inputs and Outputs SHALL respect explicit user selection even when no readable attachment tabs exist. Unsupported compute image inputs SHALL display an explanation without switching to Outputs or requesting another replay.

#### Scenario: Select compute inputs
- **WHEN** a compute dispatch is selected and the user clicks Inputs
- **THEN** Inputs remains selected and displays an unsupported explanation

### Requirement: Editor navigation ownership
The focused shader editor SHALL consume caret and selection navigation keys so that arrows, Home, End and page navigation do not scroll or navigate the parent dock/window.

#### Scenario: Move the caret in a long shader
- **WHEN** the user presses Right while editing a vec4 expression
- **THEN** the caret moves and the surrounding dock/window position and scroll remain unchanged

### Requirement: Bounded image controls and primary toolbar order
Image zoom and display-range sliders SHALL retain a fixed preferred width as the dock grows, shrinking only when necessary for a narrow dock. Shader More SHALL immediately follow Edit, before Undo and Redo.

#### Scenario: Wide image dock
- **WHEN** Image has excess horizontal space
- **THEN** its zoom and display-range sliders remain compact rather than filling the row

#### Scenario: Find shader actions
- **WHEN** Shader is opened
- **THEN** the primary toolbar begins with Edit, More, Undo and Redo

### Requirement: Compact image information
Image identity, format, mip and slice SHALL appear in the bottom image status area alongside dimensions, zoom and color interpretation. Automatic UV detection SHALL NOT add a persistent message above the canvas. Full image details SHALL remain available through a tooltip.

#### Scenario: Inspect UV density
- **WHEN** UV density is selected
- **THEN** no UV-location status row consumes canvas height and image metadata remains in the footer

### Requirement: Settings dialog
Settings SHALL open as a centered, non-dockable modal dialog and retain theme/logging controls. Closing with Close, title close or Escape SHALL return to the current workspace; reopening SHALL work through File and the command palette.

#### Scenario: Open Settings from the palette
- **WHEN** Settings is dispatched through the command palette
- **THEN** a modal dialog appears without entering a dock and Close returns focus to the workspace
