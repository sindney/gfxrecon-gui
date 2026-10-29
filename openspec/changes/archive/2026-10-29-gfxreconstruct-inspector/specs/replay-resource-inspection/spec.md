## ADDED Requirements

### Requirement: Explicit image provenance and on-demand descriptor inspection
The image view SHALL distinguish frame presentation, selected-draw attachments and bound textures. Pending/stale readback SHALL not display an older selection as current. Image choices SHALL retain output aspect/location or input shader stage/set/binding/array identity. Draw selection SHALL automatically fetch its descriptor images and attachments without automatically dumping every visible frame.

#### Scenario: Depth-only draw
- **WHEN** the selected pipeline disables color writes
- **THEN** the viewer explains the unchanged color output and offers depth/stencil readback for that draw

#### Scenario: Select a bound texture
- **WHEN** the user opens Bound textures for a selected execution
- **THEN** the viewer displays the execution's automatically fetched decoded images with slot metadata

### Requirement: Fresh Vulkan validation replay
Validation mode SHALL request VK_LAYER_KHRONOS_validation on original capture calls, avoid optimized captures and result/pipeline cache shortcuts, verify that the layer was enabled and retain structured VUID/severity messages plus full logs. Missing layers SHALL be reported as unavailable, never as a clean validation pass. Diagnostics SHALL be labeled replay-wide.

#### Scenario: Validation error during replay
- **WHEN** Khronos validation emits an error
- **THEN** its VUID/message is retained and visible even if rendering completes, without claiming the capture is validation-clean

### Requirement: Cached upstream optimization for replay
The application SHALL use a compatible configured gfxrecon-optimize tool to prepare a derived replay copy by default. Inspection, shader extraction and selection identities SHALL remain based on the original immutable capture. A content-verified manifest SHALL bind original bytes, optimizer bytes and derived bytes. Preparation SHALL be cached; no-change and optimizer failure SHALL use the original. The frame context menu SHALL allow original replay. Replay failures SHALL preserve diagnostics and the last working view.

#### Scenario: Game with unused Vulkan pipelines
- **WHEN** the optimizer removes unused initialization or pipeline creation calls
- **THEN** replay uses the verified derived file with original draw/module identities and records both source and derived hashes
- **AND** tested frame/draw pixels and selectors remain equivalent to original replay

#### Scenario: Changed source or optimizer
- **WHEN** original bytes, optimizer bytes or derived bytes differ from the cached manifest
- **THEN** the application rebuilds the derived copy instead of accepting stale preparation

#### Scenario: Original replay selected
- **WHEN** the user disables Optimize replay copy in the frame context menu
- **THEN** subsequent requests replay original capture bytes and retain the preference

### Requirement: Responsive inspection of large game captures
The application SHALL automatically render only the initially selected frame, and SHALL render additional frames only when requested. Draw selection SHALL immediately update CPU pipeline state and schedule a debounced selected-output replay. Newer selections SHALL cancel obsolete automatic requests and stale results SHALL not replace the current selection. Shader navigation SHALL take priority over automatic replay.

#### Scenario: Open a multi-frame game capture
- **WHEN** a large capture is opened and the first preview completes
- **THEN** other filmstrip tiles remain idle and no unsolicited replay queue occupies the worker

#### Scenario: Rapid draw selection
- **WHEN** the user selects several draws while replay is running
- **THEN** inspection converges on the latest draw without replaying each intermediate selection or freezing CPU navigation
- **AND** the image identifies its selected draw or explicitly indicates a previous selection

### Requirement: Verified reuse of replay work
The application SHALL reuse content-verified Android uploads, target-generated pipeline caches, and completed preview artifacts when capture, backend, target/driver identity, override set and request match. It SHALL retain fresh replay for shader comparison/revert and SHALL reject corrupt or mismatched artifacts. Automatic draw inspection SHALL gather outputs, descriptors and mesh buffers in one readback for the selected execution; frame presentation SHALL not trigger inspection of other frames.

#### Scenario: Revisit a selected output
- **WHEN** an unchanged frame or draw has verified completed artifacts
- **THEN** they are reused without restarting replay and provenance records the cache hit

#### Scenario: Cache corruption or changed shader
- **WHEN** cached bytes are damaged or shader/capture/backend/target identity changes
- **THEN** the cache is rejected and correct output is regenerated without publishing the invalid result

### Requirement: Selectable frame filmstrip
Frames SHALL be represented as a horizontally scrollable strip with a tile for every capture-relative frame, generated screenshot previews, explicit pending/unavailable tiles, and selection highlighting. The first/default frame and newly selected frames SHALL render asynchronously. Visible thumbnails SHALL populate in the background. Render, dump, and revert SHALL be offered from each tile's context menu instead of permanent buttons or a frame slider. Offline records SHALL remain usable after replay failure.
Replay target selection (Automatic, Local Windows, Android device) SHALL also live in tile/background context menus. The filmstrip SHALL omit the frame count, target dropdown and inline operation-status row. Frames SHALL share the default bottom dock with Console.
Pending/rendering/unavailable text SHALL wrap within and be clipped to the tile's padded inner rectangle, with frame numbers remaining outside the image tile. Per-frame status badges SHALL stay inside the tile.

#### Scenario: Open and browse an existing capture
- **WHEN** a replay-compatible trace is opened and a different frame tile is selected
- **THEN** the initial and selected frames render automatically, the event selection follows the frame, and actual replay thumbnails populate the strip

### Requirement: Compact image display controls
The viewer SHALL provide mouse-wheel zoom anchored to the pointer, Fit and scale on one line, channel toggles, and an sRGB display toggle with an explicit explanation of converted screenshot semantics. Pixel readout SHALL retain the original screenshot bytes regardless of display transforms.

#### Scenario: Inspect a rendered screenshot
- **WHEN** the user scrolls over the image or toggles sRGB
- **THEN** the display zooms around the pointer or changes its display transfer while preserving the screenshot byte readout

### Requirement: Explicit compatible replay target
The application SHALL support replay jobs on a selected Android device and a compatible Windows Vulkan GPU using versioned tool adapters. It SHALL record target/options, expose progress and cancellation, and preserve offline inspection when replay is unavailable or fails. Rendering a selected frame SHALL NOT imply constant-time seek or arbitrary draw stepping.

#### Scenario: Replay an Android capture on its original phone
- **WHEN** the selected device has a compatible replay APK and access to the capture
- **THEN** the application launches replay on that serial and collects status, logs, and requested frame artifacts

#### Scenario: Desktop GPU lacks a capture requirement
- **WHEN** replay encounters an unsupported device feature or extension
- **THEN** the application reports the incompatibility with available logs while keeping the indexed capture usable

### Requirement: Capture-aware replay target selection
The application SHALL expose Automatic, Local Windows, and Android device replay choices. Automatic SHALL detect Android surfaces or Android device extensions in capture records and use an authorized Android device for those captures, without silently trying desktop replay. The same resolved target SHALL be used for baseline, shader overrides, resource dumps, and revert. User target/device preferences SHALL persist and successful images SHALL identify their actual replay target.

#### Scenario: Render the supplied Android triangle capture
- **WHEN** `c_frames_1_through_3.gfxr` is opened with Automatic selected and one authorized phone with the replay APK is connected
- **THEN** Render frame runs replay on that phone, retrieves the complete screenshot, and displays it in the GUI with its device serial

#### Scenario: An Android device is unavailable
- **WHEN** Automatic selects Android replay but no authorized device is available, the selected serial is disconnected, or multiple devices require selection
- **THEN** the GUI explains the required connection/selection and retains offline inspection without launching local replay

#### Scenario: Explicit desktop replay cannot support an Android capture
- **WHEN** Local Windows replay fails with `VK_ERROR_EXTENSION_NOT_PRESENT`
- **THEN** the diagnostic identifies the compatibility failure and directs the user to Automatic or Android replay instead of presenting a generic tool exit alone

### Requirement: Exact resource dump selection
The application SHALL generate supported upstream dump requests from the selected command recording and execution context, including submission, render-pass or dynamic-rendering boundaries, subpass, and secondary invocation paths as applicable. Unsupported or ambiguous selections SHALL fail explicitly without substituting another event.

#### Scenario: Dump a supported submitted draw
- **WHEN** the user requests resources for a resolved draw occurrence
- **THEN** replay receives its required command-buffer, render-scope, draw, submission, and invocation indices and resulting artifacts are linked to that occurrence

#### Scenario: A recorded draw was never submitted
- **WHEN** the user requests execution resources for an unsubmitted recording
- **THEN** the application explains that execution resources are unavailable and retains raw call inspection

### Requirement: Image and buffer inspection
The application SHALL display available frame screenshots and selected-event image/buffer artifacts with format, dimensions/subresource or byte-range metadata, target, event, and revision provenance. It SHALL provide image pan/zoom, channel selection, supported decoded pixel readout, buffer hex/scalar views, and a vertex/index table when input layout is resolved. Unsupported representations SHALL remain explicitly unavailable or exportable as raw data.

#### Scenario: Inspect a render target and bound vertex buffer
- **WHEN** a supported replay dump contains an image and vertex buffer with resolved input layout
- **THEN** the user can inspect the image and its decoded pixels and view vertex bytes and interpreted input fields for the selected event

#### Scenario: Display image is converted from a packed format
- **WHEN** upstream supplies a converted display image rather than exact original texel storage
- **THEN** the viewer identifies that conversion and does not label decoded display values as original packed texel values

### Requirement: Artifact transport integrity
The application SHALL support CLI/device-file artifact collection for the tested bundle. If an experimental remote transport is enabled, it SHALL check protocol compatibility, bound received frame/file sizes, confine files to the session directory, and use loopback or selected-device forwarding. Partial output SHALL NOT be presented as a successful result.

#### Scenario: Configured bundle lacks remote protocol support
- **WHEN** its standard replay and file-output capabilities are available
- **THEN** supported replay inspection remains usable through CLI and file retrieval

#### Scenario: Replay sends an invalid output path or incomplete payload
- **WHEN** an artifact escapes the session directory or violates declared/allowed transfer bounds
- **THEN** collection rejects it and reports the failed job without publishing a completed artifact manifest

### Requirement: Useful image metadata and display range
Image tooltips SHALL contain concise binding/aspect, format, dimensions and mip/layer information, excluding raw API JSON. The viewer SHALL offer clockwise/counterclockwise rotation and a draggable black/white display range with numeric endpoints, Auto and Reset. Range adjustment SHALL affect display only; underlying readback and reported source values SHALL remain unchanged. Depth SHALL preserve native D16/D24/D32 samples when available, falling back to decoded BMP data for older artifacts. Depth/stencil data SHALL not receive color gamma conversion.

#### Scenario: Inspect a low-contrast depth output
- **WHEN** the user narrows or automatically fits its display range
- **THEN** the decoded values are remapped between black and white without changing underlying image data

### Requirement: Indirect draw readback and truthful failure diagnosis
Indirect execution inspection SHALL return the selected draw's readable resources without substituting a frame screenshot. Unsupported mesh readback SHALL not prevent image/shader inspection. Replay process death SHALL retain relevant diagnostics and SHALL not be reported as a timeout unless the deadline actually expired.

#### Scenario: Replayer exits while dumping an indirect draw
- **WHEN** the replayer process exits before its completion marker
- **THEN** the job fails promptly with retained replay/exit diagnostics, preserving the last good view

### Requirement: Selected-draw GPU visualization
The Image panel SHALL offer original rendering, solid geometry coverage, wireframe and geometry depth pass/fail modes for supported graphics draws. Diagnostic pipelines SHALL execute on the replay GPU with captured geometry stages, arguments and bindings. They SHALL restore the original pipeline, preserve captures and shader edits, and identify geometry-only semantics. Wireframe SHALL require supported non-solid rasterization. Unsupported pipeline/device paths SHALL report failure rather than presenting original pixels as successful visualization.

#### Scenario: Inspect an occluded selected draw
- **WHEN** the user selects solid/wireframe or depth pass/fail
- **THEN** the selected event replays automatically with orange geometry coverage or green passing/red failing depth comparison against pre-draw depth, retaining existing color context
- **AND** the UI explains that original fragment discard, stencil and shader depth writes are not emulated

### Requirement: Indirect batch and subdraw selection
For fixed-count indirect draws, the viewer SHALL offer All and individual subdraws with GPU argument counts. Individual inspection SHALL preserve earlier subdraws as context, use the selected original argument record and omit later subdraws. Isolation that changes DrawIndex-dependent shader behavior SHALL be refused explicitly. GPU-counted calls SHALL remain All-only until individual semantics are implemented.

#### Scenario: Change an indirect selection during replay
- **WHEN** a newer mode/subdraw/event is selected
- **THEN** pending work is superseded and stale results cannot replace the newer selection
- **AND** artifact identities and backend confirmation distinguish the captured pipeline, mode and subdraw

### Requirement: Replay recovery after device unlock
Android replay SHALL use the replay completion log rather than blocking on an Android activity's launch/visibility acknowledgment. A stalled screen-on replay SHALL recheck for a locked phone and provide the existing unlock/retry workflow. Retry SHALL preserve capture, selection and request options, finish or report a bounded failure, and clear running indicators. Replay completion and artifact retrieval phases SHALL be logged separately.

#### Scenario: Retry after unlocking a secure phone
- **WHEN** the user unlocks the phone and selects Retry replay
- **THEN** a fresh replay launches, its completion and artifact retrieval are observed, and the selected frame/draw leaves the running state
