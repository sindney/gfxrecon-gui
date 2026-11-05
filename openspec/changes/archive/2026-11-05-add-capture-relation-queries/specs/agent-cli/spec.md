## MODIFIED Requirements

### Requirement: Static capture queries
The CLI SHALL provide `info`, `frames`, `events`, `draw`, `pipeline` and `object` queries over an indexed capture. Without explicit projection options, query output for a draw, pipeline or object SHALL be identical to the corresponding JSON written by the structured dump. Static draw inspection SHALL additionally accept an execution selector as an alternative to block/occurrence selection; conflicting selector forms SHALL fail. Explicit column/section projection on draw/pipeline queries SHALL opt into the shared versioned query response and SHALL NOT change defaults or trigger replay/readback.

#### Scenario: Query a draw by block
- **WHEN** an agent runs `gfxrecon-cli draw --capture C.gfxr --block B` for a submitted draw
- **THEN** the result equals the `draw.json` that `dump` writes for that draw, including bound pipeline, shaders, descriptors, render targets and the replay selector

#### Scenario: Filtered event listing
- **WHEN** an agent runs `events --frame 2 --draws-only --limit 50`
- **THEN** at most 50 draw/dispatch events of frame 2 are returned in submission order with block numbers

#### Scenario: Query execution zero without projection
- **WHEN** an agent requests static draw execution 0 without projection
- **THEN** the CLI returns its original full draw document and accepts zero as a valid execution selector

#### Scenario: Explicit projected draw state
- **WHEN** an agent requests a draw's rasterization section
- **THEN** the response uses gfxrecon-query/1 with the requested state/evidence fields while existing unprojected calls remain unchanged
