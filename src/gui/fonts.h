#pragma once

namespace tpl {

// Windows: GetDpiForSystem()/96 (>= 1.0). Elsewhere: 1.0 - on macOS the OS
// already scales the drawable; applying a backing factor here would double the UI.
float SystemDpiScale();

// Loads the UI font rasterized at baseSize * dpiScale so text stays crisp
// (single scaling approach - no FontScaleMain on top). Falls back to the
// built-in default font when no file is found.
void LoadUiFonts(float dpiScale, float baseSize = 16.0f);

} // namespace tpl
