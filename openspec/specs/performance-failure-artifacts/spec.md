## Purpose

Retain honest current-session failure evidence for owned performance outputs while preserving original errors and prior or unrelated artifacts.

## Requirements

### Requirement: Owned session failures produce durable receipts

Once a fresh output is claimed, performance initialization, preflight, override preparation, collection and reporting failures SHALL produce a stage-aware failure receipt when writable. Artifact failure SHALL NOT replace the original error. Existing completed or unrelated output evidence SHALL remain untouched.

#### Scenario: Preflight rejects an unsupported operation
- **WHEN** preflight fails before counters or replay begin in a fresh owned output
- **THEN** failure.json records the stage/reason and completed runs without claiming a measurement occurred

#### Scenario: Existing result directory is reused
- **WHEN** a requested output already belongs to a prior performance run
- **THEN** the operation refuses reuse without overwriting its previous artifacts

### Requirement: CLI failure links refer to actual current evidence

CLI errors SHALL include failure_report only for an existing receipt from the current failed session, retaining established cancellation/device-busy semantics.

#### Scenario: Receipt cannot be written
- **WHEN** failure recording is unavailable or the output was never claimed
- **THEN** the CLI returns the original error without a nonexistent or stale failure-report link
