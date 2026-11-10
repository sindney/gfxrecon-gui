# scalar-diagnostic-overlays Specification

## Purpose
TBD - created by archiving change image-mips-and-depth-overlays. Update Purpose after archive.
## Requirements
### Requirement: Geometry diagnostics over scalar attachments
Depth and stencil previews SHALL support screen-space draw diagnostic annotations when a matching color diagnostic mask is available. Original scalar values, depth precision, range mapping and raw pixel readout SHALL remain unchanged. Overlay transforms SHALL match the image. Coverage SHALL be derived independently of application colors. UI descriptions SHALL distinguish geometry/depth-test results from actual depth/stencil writes. Unsupported or mismatched attachments SHALL retain their original preview with an explicit reason. Switching diagnostic modes SHALL preserve the selected scalar attachment.

#### Scenario: Outline over depth
- **WHEN** orange outline is selected while viewing a matching depth attachment
- **THEN** the geometry boundary appears over the original depth display, and depth pixel readout and range controls retain their source values

#### Scenario: Stencil diagnostic
- **WHEN** a matching stencil image is viewed with a prepared diagnostic
- **THEN** the annotation appears without replacing the stencil values or claiming stencil-test success

#### Scenario: No compatible mask
- **WHEN** a scalar attachment cannot be matched to a readable color diagnostic
- **THEN** the scalar preview remains visible with an unavailable explanation
