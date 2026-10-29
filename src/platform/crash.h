#pragma once

#include "platform/platform.h"

namespace tpl {

// Installs unhandled-exception/signal handlers. Call first thing in main().
void InstallCrashHandler();

} // namespace tpl
