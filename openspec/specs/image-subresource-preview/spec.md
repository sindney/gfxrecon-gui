# image-subresource-preview Specification

## Purpose
TBD - created by archiving change image-mips-and-depth-overlays. Update Purpose after archive.
## Requirements
### Requirement: Accurate mip preview and selection
Image previews SHALL distinguish the zero-based selected mip index from the image's recorded mip count. Missing counts SHALL be unknown, never zero. A mip dropdown SHALL allow available image levels to be selected. Alongside other nonempty preview controls it SHALL remain visible, show only Mip N, and be disabled when no alternative level is usable (including frame presentation and single-level images). Total level count SHALL be available in its tooltip. Visualization, indirect-subdraw and mip selectors SHALL use the same compact width as the mip selector. Missing input subresources SHALL be fetched asynchronously for the selected binding and execution without enabling all bound image subresources. Successful results SHALL replace that document's displayed mip without creating duplicate binding entries. Failed or stale requests SHALL preserve the last valid preview.

#### Scenario: Multi-mip input
- **WHEN** a texture with several recorded mip levels opens at level zero
- **THEN** the dropdown exposes its recorded levels and selecting a nonzero level displays that level's readback with reduced dimensions

#### Scenario: Return to a fetched level
- **WHEN** a previously fetched mip is selected in the same draw
- **THEN** its verified local artifact opens without another replay

#### Scenario: Select another draw during readback
- **WHEN** a mip request finishes after the selected execution changes
- **THEN** its artifacts do not replace the new selection
