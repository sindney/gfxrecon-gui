## MODIFIED Requirements

### Requirement: Perf opens in the inspector dock

Events SHALL provide an icon-only Perf clock immediately right of its filter arrow, followed by the shared replay-device phone button, with glyph-safe symbols and stable identities. View SHALL also expose Perf. Opening SHALL select a regular closable dockable Perf panel alongside the Pipeline/Image inspector tabs by default. Older floating Perf preferences SHALL join the inspector on reopening without resetting other panel layouts; existing valid docked custom placements SHALL remain usable. Automation SHALL exercise the normal docked layout rather than visibly detaching, resizing or repositioning Perf.

#### Scenario: Open from Events
- **WHEN** the user clicks the Events Perf clock with a capture open
- **THEN** the inspector dock selects Perf and its frame derives from the selected execution/event/current frame without blocking the GUI thread

#### Scenario: Legacy floating preference
- **WHEN** a previous installation saved Perf as a floating window
- **THEN** opening Perf immediately joins the inspector tabs without a visible resize/move sequence

## REMOVED Requirements

### Requirement: Compact passive device and metric tooltips
**Reason**: The passive Perf phone duplicates the shared replay-device control now owned by Events; metric tooltip behavior remains required separately.
**Migration**: Use the Events phone for target selection/identity and retain provider/quality detail in metric tooltips and saved evidence.

## ADDED Requirements

### Requirement: Compact metric tooltips and one shared target selector
Perf SHALL not show its former passive phone/info symbol. Device selection SHALL be shared with the Events selector. Metric tooltips SHALL retain readable bounded width, compact paragraphs, provider/availability details and reference-data limitations without replacing unknown hardware normalization with stronger claims. Saved reports SHALL retain device/GPU/driver evidence.

#### Scenario: Metric unavailable from a changed descriptor
- **WHEN** the user hovers its row
- **THEN** the tooltip exposes the mismatch reason and reference-data limits while unrelated valid rows remain usable
