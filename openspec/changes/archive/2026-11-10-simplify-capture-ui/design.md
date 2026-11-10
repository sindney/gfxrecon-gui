## Context

Capture currently uses a five-step modal wizard, shares `androidSerial_` with replay preferences, and conditionally removes action buttons. Setup controls exist mainly inside the wizard. The underlying live-session service already supports launch once, repeated triggers, stop, disarm, and explicit app closure.

## Goals / Non-Goals

**Goals:** Make the normal flow device/app selection, Launch, then Capture at any time. Keep action placement stable, default capture to a currently connected phone, and show prerequisite fixes only when necessary. Preserve persistent access to environment configuration.

**Non-Goals:** Changing capture protocols, automatically installing APKs without a user action, replacing replay-device preferences, or automatically handling secure device locks.

## Decisions

1. Use one compact capture panel with Device and App/Executable rows, a short status, and fixed Launch/Capture/End session controls. Unavailable actions remain visible and disabled with explanatory tooltips. Remove numbered steps and repeated instructions. Use only the title-bar X to hide controls.
2. Introduce a capture-specific serial that is not persisted. Opening an idle panel refreshes discovery and selects the first ready phone, ignoring replay history. Empty discovery retries quietly every two seconds while controls are open, covering ADB startup and newly connected phones without a mandatory Refresh click. Explicit device choices remain while the panel is open; refresh falls back when a choice disappears. Freeze the target while a session or its launched app still owns it. Replay preferences remain independent.
3. Automatically probe the selected environment before launching. A successful probe continues the requested launch without another button press; a failed probe opens a focused Capture setup modal with only the relevant remediation and Retry. Installation remains explicit. Full logs remain in Console; the main panel uses concise labels and status tooltips.
4. Show recording mode and launch arguments directly in the capture dialog, plus the Windows working directory when applicable. Retain a small frame-count control for bounded recording. Scheduled mode uses Launch with appropriate enabled states and tooltips. No Options popup is needed for these few fields.
5. Add capture environment controls to Settings > Tools and devices: capture device selection, probe, APK installation, and access to tool paths. The main panel's Settings action opens this existing settings dialog. Setup failures can be repaired there and retried from capture.
6. Keep the backend session and saving services. Publish accepted capture/end requests synchronously under the state mutex before returning to the UI. Reject duplicate requests while pending and clear the pending state when the worker acknowledges capture or finishes/fails ending. Keep the GUI focused on Launch, Capture and End session; End session finalizes a manual clip. Expose separate manual start/stop in the C++ API and Android CLI, plus stop-file control for Windows CLI automation. Reopening controls preserves the running session and completed captures. End session opens a Yes/No question: Yes ends capture and closes the app; No ends capture and leaves the app running. Closing the question with its X does neither. End session remains available if an ended session still has a running app, allowing a later explicit close without a separate Close app button. Stable Save controls become disabled after saving instead of changing their identity.
7. Update existing GUI workflows for the compact layout and add state/selection/setup tests and screenshot review. Validate real Windows launch and repeated/manual capture through the existing package workflow, plus connected-device discovery/probe where available. Refresh dist while preserving user settings and sessions.

## Risks / Trade-offs

- Discovery and probe are asynchronous: queue them explicitly and invalidate readiness on target/tool/app changes; never launch from a stale probe or after cancelling its repair dialog.
- Capture settings and replay share the global job slot: keep probe/launch actions disabled during conflicting work and avoid starting queued work from a worker thread. Read-only setup/discovery checks yield to explicit document and inspection operations, cancelling any pending launch continuation.
- Nested setup/settings modals: close the capture popup when opening Settings, preserve session state, and keep the capture entry available from File.
- Fewer visible setup details: use focused remediation, tooltips, Console evidence, and permanent environment controls in Settings.
