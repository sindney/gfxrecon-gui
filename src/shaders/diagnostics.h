#pragma once
#include "analysis/capture.h"
namespace gfx {
// Uses an actual fragment-stage varying; never substitutes screen coordinates.
std::string UvCheckerboardSource(const std::filesystem::path& fragment, const std::string& entry, std::string& detail);
}
