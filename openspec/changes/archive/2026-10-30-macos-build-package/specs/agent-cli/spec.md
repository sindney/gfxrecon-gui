## MODIFIED Requirements

### Requirement: Shared core between GUI and CLI
The shared services SHALL be built once as a UI-independent core library for
capture import, tool resolution, capture analysis, replay, capture and shaders, linked by the
desktop GUI and gfxrecon-cli on Windows and macOS. Both front ends SHALL resolve
tools from the same sources in the same precedence and SHALL use the same capture
index cache, replay result cache and session directory root. Windows SHALL retain
its executable-directory default; macOS SHALL default to per-user Application
Support. An explicit --settings-dir SHALL override that root.

#### Scenario: Capture opened in the GUI is reused by the CLI
- **WHEN** a capture has already been indexed by the GUI and the CLI runs info
  on the same file with the same tools and settings root
- **THEN** the CLI loads the cached index without re-running conversion and reports cache_hit: true

#### Scenario: macOS package remains signed after use
- **WHEN** the GUI and CLI run from a relocated macOS distribution
- **THEN** both use the shared per-user state root and leave app resources unchanged

#### Scenario: Windows portable configuration
- **WHEN** the Windows GUI and CLI run beside their bundled tools
- **THEN** they retain their shared portable settings, sessions and caches
