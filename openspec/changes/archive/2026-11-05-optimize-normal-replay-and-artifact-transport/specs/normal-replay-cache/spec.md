## ADDED Requirements

### Requirement: Reuse bounded validated file identities
The shared core SHALL memoize SHA-256 hashes in a bounded process-local cache keyed by canonical path and file identity, size, modification time and change time. A changed stamp SHALL require rehashing. Cancellation SHALL be honored even on a memoized hit, and changed files SHALL not publish successful preparation results.

#### Scenario: Same-size mutation with restored modification time
- **WHEN** file contents change without changing size and the modification time is restored
- **THEN** change-time or file-identity validation forces a new hash rather than reusing stale content identity

### Requirement: Retain completed draw inspections
The GUI SHALL retain completed compatible graphics-draw inspections in an LRU bounded to 16 selections and 128 MiB of resource-file working set. Snapshots SHALL preserve geometry, shaders and fetched display modes. Restoration SHALL validate resource identity and current capture/target/override/replay-policy/subdraw compatibility and bind the result to the current selection generation.

#### Scenario: Revisit a completed draw
- **WHEN** a retained draw is selected again with compatible settings
- **THEN** its data is restored locally without ADB or GPU replay and geometry remains consistent with that draw

#### Scenario: Reselect the currently displayed draw
- **WHEN** an idle successful draw remains displayed with compatible identity and requested view
- **THEN** selecting it again starts no job

#### Scenario: Changed or missing resource
- **WHEN** a snapshot resource no longer matches its validated identity
- **THEN** the snapshot is rejected and the normal replay path remains available

### Requirement: Restore historical Android results before device contact
For an explicit Android serial, ordinary replay SHALL look for a matching validated local result before querying the current device. Capture, backend, overrides and rendering options SHALL match. Historical device identity SHALL be retained and identified as cached_result with current_device_verified=false. Fresh, validation and measurement requests SHALL bypass this historical result reuse.

#### Scenario: Restore while the phone is unavailable
- **WHEN** a compatible result exists and an explicit serial is supplied while ADB is unavailable
- **THEN** ordinary replay returns verified historical output without contacting the device and without claiming current-device verification

#### Scenario: Fresh or corrupt result
- **WHEN** fresh replay is requested or cached artifacts fail validation
- **THEN** the cache does not substitute a successful historical result for the required replay
