
## Purpose

Inspect fresh-frame GPU timestamps and optional reference counters in a compact docked Perf panel, with shared asynchronous replay, selectable filtered graphs and honest measurement details.

## Requirements

### Requirement: Perf opens in the inspector dock

Events SHALL provide a readable Perf action immediately right of its filter arrow, using a clock symbol or glyph-safe fallback with stable identity. View SHALL also expose Perf. Opening SHALL select a regular closable dockable Perf panel alongside the Pipeline/Image inspector tabs by default. Older floating Perf preferences SHALL join the inspector on reopening without resetting other panel layouts; existing valid docked custom placements SHALL remain usable. Automation SHALL exercise the normal docked layout rather than visibly detaching, resizing or repositioning Perf.

#### Scenario: Open from Events
- **WHEN** the user clicks Perf with a capture open
- **THEN** the inspector dock selects Perf and its frame derives from the selected execution/event/current frame without blocking the GUI thread

#### Scenario: Legacy floating preference
- **WHEN** a previous installation saved Perf as a floating window
- **THEN** opening Perf immediately joins the inspector tabs without a visible resize/move sequence

### Requirement: Shared asynchronous controls require no probe prerequisite

Settings SHALL expose selected frame, optional Counters, Shader Override comparison, runs, swapchain and counter interval when enabled. Shader Override SHALL be on the first settings row with its full comparison meaning in a tooltip. Start SHALL run the shared current-settings validation/measurement path when inputs are valid and no conflicting job is active; prior Probe success SHALL NOT be required after toggling counters or editing valid settings. Probe SHALL remain an optional explicit capability action. Start SHALL become Cancel during active work. No FPS, measured duration, warm-up or restoration controls SHALL appear.

#### Scenario: Enable counters after timing-only probe
- **WHEN** counters are toggled or their valid interval changes
- **THEN** Start remains available without another probe, and runtime discovery determines actual counter availability

#### Scenario: Invalid frame or interval
- **WHEN** a setting is outside its supported input range
- **THEN** Start is disabled with the input reason and becomes available again when the value is corrected

### Requirement: Jobs, results and Console diagnostics survive panel closing

Closing Perf SHALL preserve active work, cleanup and retained results. Reopening SHALL reveal the original result without replacing an active session. Active profiling SHALL retain exclusive ownership and suppress automatic preview contamination. Opening Perf MAY clear currently queued automatic preview requests to prioritize its work, but panel visibility alone SHALL NOT impose a persistent pause on newly requested previews once conflicting work is idle. No automatic resumption of the cleared requests is implied. Capture/selection generations SHALL prevent stale background results from attaching to unrelated selections. Progress, errors and artifact paths SHALL go to Console and the existing status dot. Valid completed results with warnings SHALL use a warning state rather than a failure state.

#### Scenario: Close during measurement
- **WHEN** Perf is closed during an active session
- **THEN** work and owned cleanup continue, Console receives completion, and reopening shows the retained result

### Requirement: Compact statistics preserve full evidence

The sortable table SHALL show Metric, Unit, Mean, Min and Max. A comparison SHALL show original/modified pairs with their meaning available on hover. Sample count, P95, standard deviation, coverage, delta where comparable, provider description and availability limitations SHALL remain accessible in compact tooltips and raw results, rather than persistent wide columns. Missing data SHALL not become zeros. New results SHALL display automatically and Open SHALL load saved reports.

#### Scenario: Timing-only result
- **WHEN** only baseline measurements exist
- **THEN** each statistic shows one value instead of a redundant modified-value placeholder column

### Requirement: Row selection controls graphs without modifiers

The table SHALL have no Plot checkbox column. Clicking a metric row SHALL toggle its graph without deselecting other rows. Shift-click SHALL add the range between the stable selection anchor and current row in the current filtered/sorted order; if that anchor is excluded by the filter, the clicked row SHALL start a new range. Any number of metrics, including GPU timing, SHALL be selectable. A table context menu SHALL offer Select all and Deselect all, applying to all matching rows including scrolled-out rows, while preserving selections excluded by the filter.

#### Scenario: Add a fifth graph
- **WHEN** four counter graphs are selected and another row is clicked
- **THEN** the fifth graph is added without a hardcoded plot-count gate or clearing the previous selection

#### Scenario: Toggle one selected row
- **WHEN** an already selected row is clicked without Ctrl
- **THEN** only that graph is hidden and other selections remain unchanged

### Requirement: Placeholder-only filtering affects display only

A fixed-width input above the table SHALL show a Filter placeholder without an external label. The query SHALL filter table rows and selected graphs by metric name or raw/display unit. Filtering SHALL preserve the selected metric identities, including hidden selections; clearing it SHALL restore matching selected graphs. Sorting SHALL preserve selected identities and range-anchor meaning. Display filtering SHALL NOT change counter acquisition, saved samples, units or report provenance.

#### Scenario: Filter ALU metrics after all-counter acquisition
- **WHEN** ALU text is entered after an all-counter measurement
- **THEN** only matching rows and selected graphs are displayed while the complete acquired counter result remains unchanged

#### Scenario: No matches
- **WHEN** the filter matches no metrics
- **THEN** the table shows its empty matching state, no nonmatching graph is drawn, and clearing the filter restores prior selections

### Requirement: Readable units are consistent across variants and graphs

Displayed statistics, graph axes, averages and sample tooltips SHALL use one appropriate scale per metric across original/modified data, such as KB/s, MB/s or GB/s for byte rates. SI byte scales SHALL use powers of 1000. GPU timing SHALL retain sufficient display precision. Provider units, original sample values and exported numeric evidence SHALL remain unchanged.

#### Scenario: Gigabyte-scale bandwidth
- **WHEN** a metric contains rates of a few billion bytes per second
- **THEN** its table and graph use readable GB/s values instead of scientific-notation B/s values

### Requirement: Shared time navigation uses fixed metric ranges

Selected graphs SHALL use pinned ImPlot with frame-relative milliseconds on a shared horizontal time axis. GPU timing SHALL appear first when selected and matching the filter. Repeated horizontal-axis titles SHALL be omitted; the GPU timing axis tooltip SHALL explain the time unit. Mouse-wheel zoom SHALL change only horizontal time and leave each vertical range fitted to that metric's full valid data. Right-click Fit SHALL refit the shared time view. Separate replay processes SHALL remain separate series.

#### Scenario: Horizontal wheel zoom
- **WHEN** the wheel is used over a graph
- **THEN** the shared time range changes while the metric's vertical limits remain unchanged

### Requirement: Plots preserve reference evidence and rendering limits

Plots SHALL preserve actual valid zeros, missing-data gaps, isolated observations and important extrema under bounded display reduction. They SHALL not invent continuous samples, fine pass/stage attribution or cross-process continuity. Legends SHALL list only actual baseline/modified run series; mean lines SHALL follow their corresponding series visibility without duplicate legend swatches. ImPlot context lifetime, pinned source/license packaging and vertex/index limits SHALL remain compatible with supported SDL/SFML renderers while raw samples remain available.

#### Scenario: Sparse frame window
- **WHEN** only one counter sample or a gap is present
- **THEN** the graph displays the actual observation/gap with its limitations instead of fabricating a detailed timeline

### Requirement: Compact passive device and metric tooltips

A passive phone symbol SHALL expose device/GPU/driver identity and concise timing/counter scope without a button-style info control. Tooltips SHALL have a readable bounded width, compact paragraphs and the relevant provider/availability details. Provider wording and unknown hardware normalization SHALL not be replaced by stronger unsupported claims.

#### Scenario: Metric unavailable from a changed descriptor
- **WHEN** the user hovers its row
- **THEN** the tooltip exposes the mismatch reason and reference-data limits while unrelated valid rows remain usable
