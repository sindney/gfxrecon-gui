## Why

CLI inspection currently requires repeated imports and manual artifact selection and counting. Agents need the same diagnostic views as the GUI, predictable image selection, and enough structured evidence to inspect several candidate draws efficiently.

## What Changes

- Expose original, solid, wireframe, depth, outline and UV views with verified replay provenance.
- Select output attachment or image ID, mip and layer explicitly; retain COLOR 0 as the default.
- Add bounded batch draw readback without regenerating a static dump.
- Return geometry counts and buffer availability, distinguishing measured values from unavailable data.
- Separate total inspection time, replay calls and frontend processing time.

## Capabilities

### New Capabilities

- `cli-inspection-workflows`: Image selection, lightweight batch inspection, geometry summaries and operation timing.

### Modified Capabilities

- `agent-replay`: Complete CLI diagnostic mode parity and capability failures.

## Impact

CLI replay commands, shared resource selection and geometry analysis, JSON results, help and CLI guide. Existing commands and result fields remain compatible. Tests use public triangle, cube and indirect samples. No capture-specific information is added to public files.
