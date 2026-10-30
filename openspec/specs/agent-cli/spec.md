## Purpose
A console front end over the shared core for agents and automation: a stable JSON/exit-code contract, self-describing schema, shared tool/device discovery, static capture queries and concurrent-use safety.

## Requirements

### Requirement: Shared core between GUI and CLI
The capture import, tool resolution, capture analysis, replay, capture and shader services SHALL be built once as a UI-independent core library linked by both `GfxReconGui.exe` and `gfxrecon-cli.exe`. Both front ends SHALL resolve tools from the same sources in the same precedence and SHALL use the same capture index cache, replay result cache and session directory root.

#### Scenario: Capture opened in the GUI is reused by the CLI
- **WHEN** a capture has already been indexed by the GUI and the CLI runs `info` on the same file with the same tools
- **THEN** the CLI loads the cached index without re-running conversion and reports `cache_hit: true`

#### Scenario: GUI behavior unchanged after extraction
- **WHEN** the core library replaces the per-target source lists
- **THEN** the existing native and UI test suites pass without modification of their expectations

### Requirement: Machine-readable output contract
Every CLI command SHALL write exactly one JSON document to stdout containing `ok`, `command`, and either `result` or `error` (`code`, `message`, optional `detail`). Progress and logs SHALL go to stderr only. The CLI SHALL never prompt interactively.

#### Scenario: Successful command output
- **WHEN** an agent runs `gfxrecon-cli info --capture C.gfxr` and parses stdout as JSON
- **THEN** parsing succeeds, `ok` is true and `result` contains the frame count, event count and execution count

#### Scenario: Failure output
- **WHEN** a command fails because the capture file does not exist
- **THEN** stdout is a JSON document with `ok: false` and an `error.code`, and the process exits nonzero

### Requirement: Stable exit codes
The CLI SHALL use documented exit codes: 0 success, 1 operation failure, 2 usage error, 3 human action required, 4 missing tool or capability, 5 timeout or cancellation.

#### Scenario: Locked phone
- **WHEN** an Android replay or capture command finds the selected phone locked with a secure or unknown lock
- **THEN** the CLI exits with code 3 and the error identifies `unlock_device` and the device serial

#### Scenario: Missing replay tool
- **WHEN** a local replay is requested and no replay executable is configured
- **THEN** the CLI exits with code 4 and names the missing tool

#### Scenario: Cancellation
- **WHEN** the user interrupts a running replay with Ctrl+C
- **THEN** the replay is cancelled through the shared cancel token, device settings and display state are restored as in the GUI, and the CLI exits with code 5

### Requirement: Self-describing command schema
`gfxrecon-cli describe` SHALL print all commands, arguments, options, exit codes and the dump schema version, generated from the same definitions used for argument parsing.

#### Scenario: Agent discovers commands
- **WHEN** an agent runs `gfxrecon-cli describe`
- **THEN** the output lists every command accepted by the parser with its options and no command absent from the parser

### Requirement: Static capture queries
The CLI SHALL provide `info`, `frames`, `events`, `draw`, `pipeline` and `object` queries over an indexed capture. Query output for a draw, pipeline or object SHALL be identical to the corresponding JSON written by the structured dump.

#### Scenario: Query a draw by block
- **WHEN** an agent runs `gfxrecon-cli draw --capture C.gfxr --block B` for a submitted draw
- **THEN** the result equals the `draw.json` that `dump` writes for that draw, including bound pipeline, shaders, descriptors, render targets and the replay selector

#### Scenario: Filtered event listing
- **WHEN** an agent runs `events --frame 2 --draws-only --limit 50`
- **THEN** at most 50 draw/dispatch events of frame 2 are returned in submission order with block numbers

### Requirement: Device ownership across processes
Device-mutating operations (replay, capture, display control) from the GUI and the CLI SHALL hold an exclusive per-serial host lock. A contending operation SHALL fail immediately with `device_busy` unless a wait duration is given.

#### Scenario: GUI replay in progress
- **WHEN** the GUI is replaying on device S and the CLI requests a replay on S without `--wait-device`
- **THEN** the CLI exits with code 1 and error code `device_busy` without touching the device

### Requirement: Tool and device discovery
The CLI SHALL report configured tools with availability and versions, and connected Android devices with state, using the same discovery as the GUI's Tools and devices view.

#### Scenario: Doctor command
- **WHEN** an agent runs `gfxrecon-cli doctor`
- **THEN** the result lists each tool path with availability, connected devices with state, whether the replay APK is installed per device, and whether validation layers are available
