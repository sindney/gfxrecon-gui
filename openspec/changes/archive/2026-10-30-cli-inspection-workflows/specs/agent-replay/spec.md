## MODIFIED Requirements

### Requirement: Replay to draw
`gfxrecon-cli replay draw` SHALL accept a draw by block number, by dump draw directory, or by global zero-based execution index, and SHALL replay the frame with resource readback at that draw, returning output attachments and, with `--full`, bound inputs and vertex/index data. Modes `original`, `solid`, `wireframe`, `depth`, `outline` and `uv` and supported indirect subdraw selection SHALL be available as in the GUI. Outline SHALL preserve original interior color and draw an orange boundary. UV SHALL use the effective fragment interface. Unsupported capabilities SHALL fail explicitly.

#### Scenario: Draw from a dump path
- **WHEN** an agent runs `replay draw --draw D/frames/0002/passes/003-renderpass-b5120/draws/0017-b5188-vkCmdDrawIndexed`
- **THEN** the CLI resolves the capture and selector from the dump and returns that draw's output images

#### Scenario: Wireframe visualization
- **WHEN** `replay draw --block B --visualize wireframe` runs on a supported backend
- **THEN** the output shows the selected draw's wireframe and the result verifies the visualization provenance

#### Scenario: Outline inspection
- **WHEN** a supported draw is replayed with `--visualize outline`
- **THEN** its output shows an orange boundary with original interior color and records auxiliary source manifests

#### Scenario: Unsupported UV interface
- **WHEN** UV mode is requested for a fragment interface without a compatible UV input
- **THEN** the command returns `visualization_unsupported` before launching the diagnostic replay
