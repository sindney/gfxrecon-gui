#pragma once
#include "analysis/capture.h"
#include <atomic>

namespace gfx {
// Front-end-neutral shader representations shared by the GUI shader panel, the dump and `shader` commands.
struct DecodedShader {
    ShaderStage stage;          // stage resolved from the binary's entry point
    std::string glsl;           // SPIRV-Cross reconstruction; empty when unavailable
    std::string assembly;       // disassembler output; empty when unavailable
    std::string reflection;     // human-readable resource summary (+ unavailable reasons)
    std::string capturedSource; // embedded debug source, if any
    Json metadata;              // debug info, binary hash, module/stage/entry/specialization
    Json resources = Json::array(); // structured reflection: {kind,name,set,binding,location}
    std::string glslError, assemblyError;
};
DecodedShader DecodeShader(const std::filesystem::path& binary, const ShaderStage& stage,
                           const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel = nullptr);
// glslangValidator -S value for a Vulkan stage name.
std::string ShaderStageFlag(const std::string& stage);
// Compiles GLSL (.glsl/.vert/.frag/.comp) or SPIR-V assembly (.spvasm) for the captured stage/entry and
// validates it with spirv-val. A .spv input is validated and copied. Throws with tool diagnostics on failure.
void CompileShader(const std::filesystem::path& source, const ShaderStage& stage, const std::filesystem::path& output,
                   const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel = nullptr);
} // namespace gfx
