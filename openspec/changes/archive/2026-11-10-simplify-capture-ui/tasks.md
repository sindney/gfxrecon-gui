## 1. Target and setup state

- [x] 1.1 Separate capture device selection from replay history and select the first ready connected phone on idle open/refresh.
- [x] 1.2 Add automatic launch preflight, focused repair guidance, and permanent capture environment controls in Settings.

## 2. Compact capture controls

- [x] 2.1 Replace the wizard with compact target/app selection, Options, concise status, and fixed disabled action buttons.
- [x] 2.2 Preserve live-session reopening, repeated/manual capture, scheduled capture, and completed-file actions with stable labels.

## 3. Validation and delivery

- [x] 3.1 Update GUI workflows and add regression coverage for device defaults, persistent actions, and setup failures; review screenshots.
- [x] 3.2 Verify real capture workflows, update user documentation, and deploy verified artifacts to dist while preserving settings/sessions.

## 4. Inline controls and session ending

- [x] 4.1 Show the few recording/launch options inline and remove the Options popup and bottom Close button.
- [x] 4.2 Replace Close app with a Yes/No End session question, preserving the app on No and treating the title-bar X as cancellation.
- [x] 4.3 Update UI/workflow tests and documentation, review screenshots, and refresh dist/ZIP while preserving user data.

## 5. Common GUI actions and immediate feedback

- [x] 5.1 Remove GUI Stop; finish manual recording through End session. Keep separate manual controls in the CLI/API.
- [x] 5.2 Publish accepted capture/end requests synchronously, disable actions immediately, and reject duplicate requests until completion or failure.
- [x] 5.3 Cover manual finalization and delayed worker requests with regression tests, update docs, verify workflows and refresh dist/ZIP.
