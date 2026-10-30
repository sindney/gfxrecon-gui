## ADDED Requirements

### Requirement: Primary text history controls
Shader Undo and Redo SHALL be primary toolbar actions, disabled whenever the corresponding editor action is unavailable, read-only or blocked. Tooltips SHALL show their actual keyboard shortcuts and clarify that they change text without automatically changing an applied replay override.

#### Scenario: Undo availability
- **WHEN** editing starts with no text history
- **THEN** Undo and Redo are disabled; after an edit Undo becomes available, and after undo Redo becomes available

### Requirement: Reset shader to captured state
Reset shader SHALL restore captured document text, clear candidate and text history, remove only this module's replay override, and automatically replay with the captured binary and remaining overrides. Successful reset SHALL leave a read-only original document without stale comparison images. A failed, cancelled or superseded reset SHALL retain authored text and the last successful override/image. Standalone/offline documents SHALL support text reset without requiring replay.

#### Scenario: Reset an applied edit
- **WHEN** Reset shader is clicked after a modified shader is applied
- **THEN** original captured behavior returns through fresh replay without separate Compile/Apply clicks, and the document is original with no undo history

#### Scenario: Reset replay fails
- **WHEN** the replay fails or is cancelled
- **THEN** edited text and the previous working override/image remain available
