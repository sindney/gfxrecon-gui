## MODIFIED Requirements

### Requirement: Full-width shader editing and one public distribution
Pipeline references SHALL appear in a resizable area below the shader editor and remain clickable for pipeline navigation. The public Windows executable SHALL be gfxrecon-gui.exe in one canonical distribution. The companion console executable gfxrecon-cli.exe SHALL ship in the same distribution directory, sharing its bundled tools, configuration, sessions and caches. Retiring review distributions SHALL preserve runtime settings, sessions and authored files with verified backups before replacement.

#### Scenario: Narrow shader dock
- **WHEN** a shader with pipeline references is opened in a narrow dock
- **THEN** the editor occupies the full content width and references remain accessible beneath it

#### Scenario: CLI in the distribution
- **WHEN** the canonical distribution is packaged
- **THEN** `gfxrecon-cli.exe` is present beside `gfxrecon-gui.exe`, is included in the release inventory hashes, and resolves the same bundled tools as the GUI
