## Why

The five-step capture wizard exposes setup details during every capture and replaces action buttons as state changes. Capture should focus on selecting a connected device, launching an app once, and recording when ready, with setup guidance shown only when needed.

## What Changes

- Replace the wizard with a compact device/app panel and stable Launch, Capture, and End session actions. End session asks whether to close the app.
- Automatically select the first ready connected Android device when opening an idle capture panel, independently of saved replay history; preserve explicit selection while using the panel.
- Probe automatically before launch and offer a focused repair popup on missing tools, layer installation, authorization, or target prerequisites.
- Keep capture environment controls accessible in Settings > Tools and devices, including device probe and APK installation.
- Show recording mode, arguments, and the Windows working directory inline; use concise status text and tooltips for explanations. The title-bar X hides controls without ending the session.
- Preserve repeated capture, restoration, finalized-output handling, saving, and Windows capture behavior; verify the UI and refresh dist artifacts.

## Capabilities

### New Capabilities

- `capture-interface`: Compact capture controls, progressive setup guidance, persistent disabled actions, and settings access.

### Modified Capabilities

- `android-vulkan-capture`: Capture device selection defaults to the first ready connected phone instead of persisted replay history.

## Impact

GUI capture dialog, capture device/probe state, shared tool settings, GUI tests and documentation. Capture services publish pending requests immediately; advanced manual start/stop remains available in the API and gains explicit CLI commands. No new runtime dependencies.
