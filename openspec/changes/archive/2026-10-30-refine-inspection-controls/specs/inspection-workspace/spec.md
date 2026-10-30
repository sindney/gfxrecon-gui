## ADDED Requirements

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
