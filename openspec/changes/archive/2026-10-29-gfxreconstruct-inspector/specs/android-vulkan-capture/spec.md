## ADDED Requirements

### Requirement: Runtime Android capture triggers
The application SHALL support launch-armed N-frame and manual start/stop capture with the compatible layer's runtime trigger property. Mutations SHALL be journaled before mutation, scoped to the selected serial/app, conflict-checked and restored on disarm/error. Stop capture SHALL keep the app running. Restoring a previously enabled trigger SHALL require the launched app to exit first.

#### Scenario: Delayed game capture
- **WHEN** the unlocked debuggable game is armed and the user triggers capture later
- **THEN** finalized output is retrieved without force-stopping the game and another capture may be requested

#### Scenario: Locked device
- **WHEN** preflight detects a locked phone
- **THEN** dependent testing stops and asks for user help without automated unlock attempts

### Requirement: Explicit Android device and target selection
The application SHALL enumerate ADB device serials, connection/authorization status, ABI, Android version, and candidate packages. Every device operation SHALL address the selected serial. Capture preflight SHALL check target debuggability, compatible layer deployment, and a usable capture output path.

#### Scenario: Multiple phones are connected
- **WHEN** the user selects one authorized device and a capture target
- **THEN** preflight and subsequent deployment/capture/replay commands address only that serial

#### Scenario: Target cannot load the capture layer
- **WHEN** a selected target is non-debuggable or incompatible with the available layer
- **THEN** the application explains the failing prerequisite and does not attempt capture or repackage the application

### Requirement: Bounded Android capture and retrieval
The application SHALL support deploying compatible replay/layer and test APKs, configuring an application-specific Vulkan capture with frame-range options, launching it, and retrieving finalized `.gfxr` output. It SHALL record the actual output path and tool/device identities.

#### Scenario: Capture and open a known Vulkan test app
- **WHEN** the upstream triangle launcher and compatible capture layer are installed on the selected device and the user starts a bounded capture
- **THEN** a finalized capture is retrieved and made available to the workspace with the target/device and capture configuration recorded

#### Scenario: The device disconnects during transfer
- **WHEN** ADB connectivity is lost while retrieving a capture
- **THEN** the job reports the interruption and does not present a partial local file as a completed capture

### Requirement: Restore device capture configuration
The application SHALL journal prior presence and values of every changed GPU debug setting and capture property before mutation. It SHALL restore session-owned changes after success, failure, or cancellation and preserve recovery information across application restart or device loss. It SHALL limit cleanup to owned temporary artifacts and SHALL preserve application data.

#### Scenario: Existing GPU debug configuration is present
- **WHEN** a capture session ends after temporarily changing existing GPU debug settings
- **THEN** the previous values and absent/present states are restored and restoration status is recorded

#### Scenario: The application restarts after device loss
- **WHEN** a device reconnects with an unfinished recovery journal
- **THEN** the application exposes restoration for the journaled device and reports any intervening configuration conflict before overwriting it

### Requirement: Stop for human device blockers
Device testing SHALL stop dependent actions and request specific user assistance when APK installation fails, authorization is pending, the phone is locked, or another required device interaction cannot be completed. It SHALL retain diagnostics and restoration state and SHALL NOT bypass credentials, clear app data, root the phone, or uninstall conflicting applications. Replay MAY wake and dismiss a positively confirmed unsecured lock screen once, as explicitly requested by the user; secure or unknown locks require manual assistance.

#### Scenario: APK installation fails
- **WHEN** installation returns an error or requires an on-device approval
- **THEN** dependent device testing stops and the user receives the package, error, and action needed to continue without repeated installation attempts

#### Scenario: Phone becomes locked
- **WHEN** a test requires an interactive device and the phone is locked
- **THEN** the test waits for user assistance while preserving its session state

### Requirement: Replay lock readiness and notification
Replay SHALL detect a locked device and present its serial and unlock guidance in the GUI, with retry for the same current replay request or continued offline inspection. It MAY attempt one wake and keyguard dismissal only when the delegate positively reports an unsecured lock. It SHALL recheck the result and SHALL not enter credentials or change lock settings. Secure/unknown states or dismissal failure SHALL require manual unlock. Cancelled or stale requests SHALL not prompt for an obsolete selection.

#### Scenario: Unsecured phone is asleep
- **WHEN** replay detects a known nonsecure keyguard
- **THEN** it wakes and dismisses that lock once, verifies unlocked state and continues replay

#### Scenario: Secure or unknown lock
- **WHEN** replay detects a secure lock or cannot positively establish lock security
- **THEN** it sends no dismissal input and asks the user to unlock the named device

### Requirement: Screen-off replay
An optional replay setting SHALL power down the physical display while keeping Android logically awake and rendering. It SHALL use verified available device capabilities, report unsupported devices, journal owned changes and restore them after success, failure or cancellation, retaining reconnect recovery when restoration fails. It SHALL preserve existing off states and user intervention and SHALL not modify credentials or original captures.

#### Scenario: Inspect a game with the panel off
- **WHEN** the user enables screen-off replay on a compatible unlocked phone
- **THEN** frame/draw readback completes while the panel is off, pixels agree with normal replay and owned display state is restored afterward
