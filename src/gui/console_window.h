#pragma once

#include "core/log.h"

namespace tpl {

// Dockable, display-only log view fed by Log's ring buffer.
class ConsoleWindow {
public:
    void Draw(bool* open = nullptr);

private:
    bool levelVisible_[4] = {true, true, true, true}; // Error, Warn, Info, Debug
    bool autoScroll_ = true;
    char filter_[128] = {};
};

} // namespace tpl
