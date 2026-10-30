#pragma once
#include "analysis/capture.h"

namespace gfx {
// Input-assembly counts, independent of attribute formats and shader evaluation.
Json SummarizeGeometry(const std::string& command, const Json& pipeline, const Json& parameters,
                       const Json& draw, const std::filesystem::path& directory);
}
