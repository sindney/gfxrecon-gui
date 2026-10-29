#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tpl {

// The caller owns command IDs and dispatches the returned ID through the same
// handlers as its menus/toolbars. Append entries to expose your tool's actions.
struct CommandPaletteEntry {
    int id;
    std::string label;
};

class CommandPalette {
public:
    std::optional<int> Draw(bool& open, const std::vector<CommandPaletteEntry>& entries);

private:
    char filter_[256] = {};
    int selected_ = 0;
    bool scrollToSelection_ = false;
};

} // namespace tpl
