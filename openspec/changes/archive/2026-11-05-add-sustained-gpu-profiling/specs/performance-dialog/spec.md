## ADDED Requirements

### Requirement: Events toolbar performance action

The Events panel SHALL place a readable Perf action immediately right of the filter arrow at the top of the panel. The action SHALL use a clock symbol with readable text or a glyph-safe fallback, stable widget identity and tooltip. Search-field sizing SHALL reserve space for both buttons. Activating Perf SHALL open a resizable dockable panel and initiate non-destructive capability discovery when the target is available.

#### Scenario: Open performance panel
- **WHEN** a user clicks Perf with a capture open
- **THEN** the panel opens with an explicit frame selection derived from the selected execution/event/current frame, target and capability status, without blocking the GUI thread

### Requirement: Shared asynchronous profiling workflow

The panel SHALL use the shared session core and expose mode/frame, target, overrides, swapchain policy, warm-up, duration, FPS and counter profile/period. It SHALL distinguish capture loading from warm-up and measurement, provide progress/cancellation and disable unsupported required modes. Automatic preview work SHALL NOT contaminate an active profiling session. Selection changes SHALL NOT attach stale results to a different capture.

#### Scenario: Required counters unavailable
- **WHEN** a sustained precision profile lacks required counters
- **THEN** the panel identifies missing metrics and does not silently run timing-only, while an explicitly chosen separate supported single-frame mode remains available

#### Scenario: Capture changes during profiling
- **WHEN** the user changes capture/selection before a background session completes
- **THEN** the old artifacts are retained with their original identity and are not applied as results for the new selection

### Requirement: Metrics table with averages and validity

The panel SHALL show a sortable metrics table including exact metric name, unit, baseline/modified summaries, variability/delta where comparable, valid sample count, coverage and scope/status. Averages SHALL use the shared calculation policies and SHALL NOT hide invalid or missing required data. Metadata and raw-artifact paths SHALL remain accessible.

#### Scenario: Many available counters
- **WHEN** a session returns numerous timing/counter metrics
- **THEN** the user can sort the table and inspect means/spread and validity without conflating different units or scopes

### Requirement: ImPlot time-series visualization

The GUI SHALL use pinned ImPlot for GPU-duration and counter plots with a shared elapsed-time axis, unit-appropriate separate tracks/axes, mean references, phase boundaries and actual sample tooltips. Full/half instruction-rate lines SHALL retain their true units. Gaps and inadequate single-frame sampling SHALL remain visible; plotting SHALL NOT manufacture fine-grained vertex/fragment/pass attribution or smooth across lost data. Display reduction SHALL preserve important extrema while raw data remains unchanged.

#### Scenario: Sustained data varies over time
- **WHEN** a session contains varying frame timings and sampled counters
- **THEN** the panel shows their time-series lines and summary averages together, with warm-up/measurement/restoration scope visible

#### Scenario: Zoom into a short frame with too few samples
- **WHEN** a selected frame contains insufficient counter samples for detailed interpretation
- **THEN** the plot states that limitation rather than drawing a fabricated high-resolution counter profile

### Requirement: Compatible plotting integration and lifetime

ImPlot SHALL be pinned to a tested compatible revision, with its license included in packaging. Its context SHALL live within the owning ImGui/backend lifetime. Plot rendering SHALL respect vertex/index limits on both supported renderer configurations, using consistent index configuration or validated budgets. Large data and panel cancellation/reopening SHALL preserve responsiveness and resource correctness.

#### Scenario: SFML backend with dense plots
- **WHEN** a user displays a large counter dataset under the SFML renderer
- **THEN** the configured indexing/render budget prevents draw-index overflow and the raw samples remain available

### Requirement: Dockable panel and Console diagnostics

GPU Performance SHALL be a regular closable, dockable window, available from Events Perf and View, without modal input blocking. Closing the panel SHALL preserve active work and retained results. Merely keeping the panel open SHALL NOT pause otherwise idle previews; active performance jobs SHALL retain exclusive profiling ownership through cleanup.

Progress, unsupported capability reasons, input errors, failures and artifact paths SHALL be logged to Console. The panel SHALL use the existing inactive/running/success/failed/warning status dot and tooltip. Quantitative result validity, cadence, coverage and scope SHALL remain visible beside the measurements.

#### Scenario: Close while measurement is running
- **WHEN** the user closes the docked panel during a performance job
- **THEN** collection and cleanup continue, progress reaches Console, and reopening reveals the original result without starting a replacement session

### Requirement: Series-only plot legends

Legends SHALL list actual baseline/modified series only. Mean references and measurement-boundary markers SHALL NOT create redundant legend swatches. Mean references SHALL follow the visibility of their corresponding data series.

#### Scenario: Mean reference in a short graph
- **WHEN** a mean line is drawn alongside baseline data
- **THEN** the legend contains one baseline entry without a clipped secondary mean swatch

### Requirement: Compact Perf controls and contextual details

The window SHALL be named Perf and preserve existing GPU Performance dock placement on rename. Primary controls SHALL be Probe, Start (Cancel while active), and Open for a saved report. Completed new measurements SHALL display automatically. Persistent explanatory labels and expanded diagnostic dumps SHALL be replaced by Console messages and focused tooltips; an info tooltip SHALL show device, GPU and driver.

#### Scenario: Successful report with pacing warnings
- **WHEN** valid measurements complete but the requested cadence is missed or variants are incomparable
- **THEN** Perf shows an amber completed-with-warnings indicator, retaining the data and precise warning reason; red remains reserved for failures

#### Scenario: Narrow bursts in counter data
- **WHEN** sampled activity contains valid zeros between short rendering bursts
- **THEN** connected sampled lines preserve those zeros, with visible zero baselines and actual missing intervals kept as gaps, without fabricating sustained nonzero activity

### Requirement: Plot context actions without a metric filter

The Perf panel SHALL show all metric rows without a filter label or text input. Warm-up visibility and Fit SHALL be available in a right-click menu over the graphs, rather than a persistent toolbar. These shared actions SHALL update all selected graphs consistently and schedule axis refitting after the menu action.

#### Scenario: Enable warm-up from a graph
- **WHEN** the user right-clicks a plotted graph and toggles Warm-up
- **THEN** all plotted series update their warm-up visibility and shared time axis on the next frame

### Requirement: Timing edits preserve discovered support

A successful capability probe SHALL remain valid when only FPS, warm-up duration or measured duration changes. Invalid values SHALL still disable Start. Mode, frame, swapchain and counter interval changes SHALL invalidate discovery, and disabled Start SHALL explain why through a tooltip.

#### Scenario: Lower FPS after a successful probe
- **WHEN** the user changes a valid FPS from 30 to 6 after discovering supported precision counters
- **THEN** Start remains enabled without another device probe
