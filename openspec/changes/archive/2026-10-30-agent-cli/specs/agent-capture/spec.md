## ADDED Requirements

### Requirement: Stateless on-demand Android capture steps
The CLI SHALL expose on-demand Android capture as separate commands — `capture arm`, `capture trigger`, `capture status`, `capture end` — whose state is persisted in a session directory and device journal so that each command returns promptly and other tools can drive the game between commands.

#### Scenario: Arm, drive, trigger
- **WHEN** a supported sample application is captured and replayed
- **THEN** the game is launched with capture tracking armed, no frames are recorded until trigger, and trigger returns the retrieved `.gfxr` path, size, SHA-256 and frame count

#### Scenario: Repeated triggers
- **WHEN** `capture trigger` is run twice on the same armed session
- **THEN** two distinct verified capture files are returned and the game keeps running

#### Scenario: End restores settings
- **WHEN** `capture end --session S` is run, optionally with `--close-game`
- **THEN** every journaled property and setting is restored to its previous value, conflicts are reported, and the journal is marked restored

### Requirement: Capture shares GUI capture logic
The step commands SHALL use the same arm/trigger/retrieve/restore functions as the GUI live capture session, including journal schema, property set, retrieval hash verification and conflict rules.

#### Scenario: Recovery across front ends
- **WHEN** a CLI capture session is interrupted before `end`
- **THEN** both `capture restore --session S` and the GUI's recovery action can restore it from the same journal

### Requirement: Human blockers stop capture
Capture commands SHALL stop with exit code 3 and a `human_action` description when the device is locked, unauthorized, or a restore conflict needs attention. The CLI SHALL never enter credentials or change lock settings.

#### Scenario: Locked during trigger
- **WHEN** the phone is locked at `capture trigger`
- **THEN** no trigger property is set, the session stays armed, and the result reports `unlock_device` with the serial

### Requirement: Scheduled capture from the CLI
The CLI SHALL expose the existing scheduled Android frame-range capture and Windows executable capture, including a blocking Windows on-demand run with a timed or file-signalled trigger.

#### Scenario: Scheduled Android capture
- **WHEN** an agent runs `capture android --package P --frames 100-102 --out S`
- **THEN** frames 100–102 are captured, retrieved, verified and device settings are restored before the command returns

#### Scenario: Windows timed trigger
- **WHEN** an agent runs `capture windows run --exe G.exe --trigger-after 20 --frames 2 --out S`
- **THEN** the game launches with the capture layer, two frames are recorded after 20 seconds, and the capture path is returned

### Requirement: Capture verification
Every capture returned by a CLI capture command SHALL have been indexed successfully (convert + index) before being reported, and the result SHALL include frame count, draw/dispatch execution count and SHA-256.

#### Scenario: Truncated capture
- **WHEN** a retrieved capture cannot be converted or indexed
- **THEN** the command reports failure with the conversion diagnostic and retains the file for inspection
