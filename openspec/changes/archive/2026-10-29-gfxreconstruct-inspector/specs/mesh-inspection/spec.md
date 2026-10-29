## ADDED Requirements

### Requirement: Replay-backed input geometry
The inspector SHALL provide a Mesh tab for a selected submitted direct or indexed draw, resolving vertex/index readbacks by execution, binding, stride, format, attribute offset, first vertex/index, base vertex, primitive restart, and selected instance. Actual resource bytes SHALL be collected by the configured Windows or Android replay target. Missing/unsupported data SHALL be diagnosed without inventing vertices.

#### Scenario: Inspect indexed input geometry
- **WHEN** replay produces a supported indexed draw's vertex/index buffers and layout
- **THEN** local-space preview and a vertex-ID/coordinate table show its resolved input geometry

### Requirement: Local post-vertex evaluation
Supported captured vertex modules SHALL be reconstructed to OpenGL and evaluated with original vertex/instance identities and available replay-readback uniform state. Transform feedback SHALL verify the emitted vertex count and preserve homogeneous positions for screen-space preview. The UI SHALL distinguish local evaluation from device-native post-VS readback, and identify precision or unsupported-resource limitations. Unverified storage/image/push-constant, specialization, later shader stages and advanced capability paths SHALL remain unavailable with an explanation.

#### Scenario: Evaluate a captured transform
- **WHEN** a supported vertex module uses a readback uniform matrix
- **THEN** post-VS homogeneous positions match independent expected fixture coordinates within its declared tolerance

#### Scenario: Geometry is generated in the vertex shader
- **WHEN** a draw has no vertex attributes but a supported procedural vertex module
- **THEN** post-VS screen-space preview is available while input geometry is explicitly unavailable

### Requirement: Interactive plain mesh rendering
The Mesh tab SHALL use a local SDL/OpenGL framebuffer for solid plain shading, wireframe and point modes, selectable input/local or post-VS screen space, position attribute, and instance. Local-space controls SHALL provide orbit, pan, zoom and Fit. OpenGL state/resources SHALL remain isolated from the GUI renderer, and captured/modified geometry SHALL remain tied to selection and revision.

#### Scenario: Switch rendering and coordinate space
- **WHEN** a user switches solid/wireframe and local/screen space on the indexed cube
- **THEN** the actual geometry renders in the requested mode and ordinary GUI/image rendering remains correct
