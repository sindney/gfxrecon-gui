## MODIFIED Requirements

### Requirement: Explicit Android device and target selection
The application SHALL enumerate ADB device serials, connection/authorization status, ABI, Android version, and candidate packages. Opening an idle capture panel SHALL select the first ready connected Android device when one is present, independently of persisted replay-device history. An explicit current capture-device choice SHALL remain selected while it is connected during that panel session; discovery SHALL fall back to the first ready device if that choice disappears. Capture and replay selections SHALL remain independent, and an active capture target SHALL not be silently retargeted. Every device operation SHALL address the selected serial. Capture preflight SHALL check target debuggability, compatible layer deployment, and a usable capture output path.

#### Scenario: Multiple phones are connected
- **WHEN** the user selects one authorized device and a capture target
- **THEN** preflight and subsequent deployment/capture commands address only that serial, while replay retains its independently selected target

#### Scenario: Target cannot load the capture layer
- **WHEN** a selected target is non-debuggable or incompatible with the available layer
- **THEN** the application explains the failing prerequisite and does not attempt capture or repackage the application

#### Scenario: Historical replay phone is absent
- **WHEN** an idle capture panel opens with a saved replay serial and another ready phone connected
- **THEN** the capture panel selects the first ready connected phone and leaves the replay preference unchanged

#### Scenario: Explicit choice remains connected
- **WHEN** the user chooses the second ready phone and refreshes discovery
- **THEN** capture keeps that phone selected rather than reverting to history or the first phone
