## MODIFIED Requirements

### Requirement: Settings dialog
Settings SHALL initially open centered as a non-dockable modal with stable viewport-bounded geometry. Users SHALL be able to resize and move it, and geometry SHALL be retained. Tab changes and section expansion SHALL not auto-size the dialog. Scrollable content SHALL leave the tab bar and Close button visible. General SHALL retain theme/logging and global replay preferences; Tools and devices SHALL contain tool paths, version checks and recovery controls instead of a separate tools modal/View action. Closing with Close, title close or Escape SHALL return to the workspace; File and the command palette SHALL reopen it.

#### Scenario: Open Settings from the palette
- **WHEN** Settings is dispatched through the command palette
- **THEN** a modal dialog appears without entering a dock and Close returns focus to the workspace

#### Scenario: Resize and scroll long settings
- **WHEN** a user resizes Settings, expands tool configuration and switches tabs
- **THEN** the chosen size remains stable, overflowing content scrolls, tabs and Close remain visible, and reopening retains the chosen size

### Requirement: Clean ImGui acceptance diagnostics
GUI and portable acceptance SHALL fail when ImGui reports any recovered internal error, a conflicting widget ID, or an error in its debug log, or a frame with an OpenGL error, even when functional assertions pass. Diagnostic logging and conflict highlighting SHALL remain enabled. Inspector overlays SHALL not modify the parent layout extent or advertise an unsubmitted cursor item.

#### Scenario: Functional success with a layout error
- **WHEN** functional GUI tests succeed but an ImGui internal error or ID conflict is observed
- **THEN** the verification process exits with failure and records diagnostic counts

## ADDED Requirements

### Requirement: Events owns shared replay-device selection
Events SHALL offer adjacent icon-only clock and phone buttons immediately after its filter arrow. The phone tooltip SHALL identify the selected replay target and opening it SHALL offer Automatic, local replay and authorized Android devices with refresh and unavailable-state feedback. Selection SHALL be persisted, shared by normal and Perf replay, disabled during conflicting work and invalidate incompatible derived views.

#### Scenario: Select an Android phone
- **WHEN** the user selects an authorized serial through the Events phone dialog
- **THEN** the selected target is saved, its identity appears in the tooltip, and later frame/draw/Perf requests use that target

### Requirement: Retire image textures after draw submission
Replacing or releasing an image during UI construction SHALL retain texture IDs referenced by queued ImGui draw commands until submission is complete.

#### Scenario: Replace a preview in the same UI frame
- **WHEN** new image or mesh content replaces a texture already referenced by queued UI draws
- **THEN** the queued commands remain valid and GUI acceptance reports no OpenGL errors
