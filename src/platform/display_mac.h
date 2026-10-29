// Declares the Objective-C helpers used by the SFML backend on macOS.
#pragma once

#include "platform/platform.h"

#if PLATFORM_MACOS
namespace macdisplay {

void EnableHighDpi(void* nsWindow);
// Polls the backing scale factor of the window's current screen (2.0 Retina).
float BackingScaleFactor(void* nsWindow);

} // namespace macdisplay
#endif
