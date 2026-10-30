# draw-outline Specification

## Purpose
TBD - created by archiving change refine-inspection-controls. Update Purpose after archive.
## Requirements
### Requirement: Orange boundary outline
The image diagnostic selector SHALL offer Orange outline in place of a filled silhouette. It SHALL draw a thin orange boundary around the selected draw's actual coverage over its original image, preserving interior and background pixels. Coverage SHALL follow all/individual indirect selection and SHALL NOT be inferred from orange pixels in the original scene. Switching cached modes SHALL require no replay.

#### Scenario: Outline an indirect record
- **WHEN** one indirect record is selected and Orange outline is displayed
- **THEN** only that record's coverage boundary is orange and its interior retains original image pixels

#### Scenario: Scene contains orange pixels
- **WHEN** the original image already contains orange pixels outside the selected draw
- **THEN** those pixels do not create extra outline geometry
