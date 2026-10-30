## ADDED Requirements

### Requirement: Stable image refresh documents
Automatic draw refresh SHALL retain the previous image tabs, canvas and sidebar under refresh feedback until a new result is successfully published. Failed or cancelled work SHALL retain the last successful images with selection-unavailable feedback and SHALL NOT present them as the new selection's data.

#### Scenario: Select the next draw
- **WHEN** COLOR 0 is open and the next draw is selected
- **THEN** COLOR 0 remains visible under the refresh overlay until the completed result replaces it

### Requirement: Stable compute image sidebar
Image Inputs and Outputs SHALL respect explicit user selection even when no readable attachment tabs exist. Unsupported compute image inputs SHALL display an explanation without switching to Outputs or requesting another replay.

#### Scenario: Select compute inputs
- **WHEN** a compute dispatch is selected and the user clicks Inputs
- **THEN** Inputs remains selected and displays an unsupported explanation

### Requirement: Editor navigation ownership
The focused shader editor SHALL consume caret and selection navigation keys so that arrows, Home, End and page navigation do not scroll or navigate the parent dock/window.

#### Scenario: Move the caret in a long shader
- **WHEN** the user presses Right while editing a vec4 expression
- **THEN** the caret moves and the surrounding dock/window position and scroll remain unchanged
