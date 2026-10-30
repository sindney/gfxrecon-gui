## ADDED Requirements

### Requirement: Indirect mesh records
Mesh SHALL decode replay-readback indirect and indexed-indirect argument records and preview all drawable records by default. A record selector SHALL allow inspecting one record locally. Geometry SHALL preserve per-record primitive boundaries, original vertex and instance identities, offsets and index types. Empty, invalid or oversized records SHALL be diagnosed with bounded resource use.

#### Scenario: Inspect an indirect batch
- **WHEN** a supported indexed-indirect draw with three nonempty argument records is inspected
- **THEN** All draws previews their combined geometry by default and selecting record 1 displays only its geometry without replay

#### Scenario: Empty indirect record
- **WHEN** a batch contains a record with zero vertices or instances
- **THEN** All draws skips that record and individual selection explains that it has no drawable geometry
