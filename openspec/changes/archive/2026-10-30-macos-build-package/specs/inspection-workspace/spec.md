## ADDED Requirements

### Requirement: Capture completion retains modal focus
A completed capture SHALL keep the capture dialog visible with its Open capture
action until the user chooses to inspect the result. Importing or automatically
rendering the completed capture SHALL NOT focus a background inspection panel
and dismiss the modal.

#### Scenario: Android scheduled capture completes
- **WHEN** the capture is retrieved, indexed and automatically previewed while
  the capture dialog is open
- **THEN** the completion stage and Open capture action remain available, and
  choosing Open capture returns to frame/draw inspection
