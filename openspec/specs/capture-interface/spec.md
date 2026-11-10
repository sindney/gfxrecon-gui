# capture-interface Specification

## Purpose
TBD - created by archiving change simplify-capture-ui. Update Purpose after archive.
## Requirements
### Requirement: Compact capture flow

The capture GUI SHALL show a compact device/app selection and launch/recording flow without a numbered wizard or routine setup instructions. Launch SHALL probe the current environment and continue automatically when prerequisites pass. Launch arguments, recording mode, and the Windows working directory SHALL be shown inline without an Options popup. Normal status SHALL be concise, with detailed state and action explanations available through tooltips and Console. The title-bar X SHALL hide controls without ending capture; a redundant bottom Close button SHALL not appear.

#### Scenario: Ready device and app
- **WHEN** the user selects an app on a ready connected device and clicks Launch
- **THEN** the environment is checked, the app launches armed, and Capture becomes available without advancing wizard pages

### Requirement: Capture actions remain visible

Launch, Capture, and End session SHALL remain visible in stable positions across idle, launching, armed, recording, stopping, retrieving, ended, and failed states. Unavailable actions SHALL be disabled and visually gray rather than removed. Tooltips SHALL explain their purpose or why they are unavailable. A separate Stop button SHALL NOT appear. End session SHALL finish and retrieve any active manual recording before ending the session. Separate manual start/stop controls SHALL remain available through CLI/API for advanced automation. End session SHALL ask whether to close the app: Yes ends the session and closes it; No ends the session and keeps it running. Closing that question with its X SHALL leave the session unchanged. End session SHALL remain available while the app still runs, including after a No answer, and SHALL become disabled once both session and app have ended. No separate Close app button SHALL appear. Scheduled capture SHALL retain its existing launch/close behavior, explained through its mode tooltip. Completed capture actions SHALL retain stable labels and preserve saved files.

#### Scenario: Record a burst
- **WHEN** Capture is clicked in an armed session
- **THEN** Capture becomes disabled as soon as the request is accepted, stays visible while recording/retrieving, and becomes available again after finalized retrieval. Duplicate pending capture requests SHALL be rejected. A failed capture SHALL leave Capture disabled until a new session is launched

#### Scenario: Pending end request
- **WHEN** the user confirms End session while capture work is still running
- **THEN** End session becomes disabled immediately until completion or failure, and duplicate end requests cannot change the accepted close-app choice

#### Scenario: End monitoring
- **WHEN** the user chooses No in the End session question
- **THEN** monitoring ends, the app keeps running, completed captures remain accessible, and End session can later be used with Yes to close the app

#### Scenario: End and close the app
- **WHEN** the user chooses Yes in the End session question
- **THEN** the session ends, the launched app closes, and End session stays visible but disabled after completion

#### Scenario: Dismiss controls or the end question
- **WHEN** the title-bar X is clicked on capture controls or the End session question
- **THEN** that dialog closes without stopping recording, ending the session, or closing the app

### Requirement: Setup guidance appears when needed

Missing prerequisites or a failed probe SHALL open focused setup guidance with appropriate repair actions and Retry. The routine capture panel SHALL not display all tool paths or installation settings. APK installation SHALL require an explicit user action. Capture environment controls SHALL also remain available in Settings > Tools and devices, including device probing, APK installation, and tool configuration. Cancelling setup guidance SHALL cancel any pending launch continuation.

#### Scenario: Missing replay APK
- **WHEN** probing finds that the selected phone lacks the compatible replay/layer APK
- **THEN** setup guidance offers installation and Retry without exposing unrelated launch options or starting capture

#### Scenario: Edit environment independently
- **WHEN** the user opens Settings > Tools and devices without opening capture
- **THEN** capture environment probing, layer/sample installation, and tool paths are available there

#### Scenario: Continue inspection after closing controls
- **WHEN** a read-only capture setup check is finishing and the user opens another document
- **THEN** the check yields to that explicit operation, the document opens, and any pending capture launch is cancelled
