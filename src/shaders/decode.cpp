#include "shaders/decode.h"
#include "shaders/interface.h"
#include "shaders/metadata.h"
#include "core/artifacts.h"
#include "platform/process.h"

#include <spirv_glsl.hpp>
#include <fstream>
#include <sstream>

namespace gfx {
namespace {
std::string Run(const std::string& exe, const std::vector<std::string>& args, const std::atomic_bool* cancel) {
    if (exe.empty()) throw std::runtime_error("Required shader tool is not configured");
    auto result = RunProcess({exe, args}, cancel);
    if (!result.Success()) throw std::runtime_error(result.cancelled ? "Operation cancelled" :
        result.timedOut ? "Tool timed out" : "Tool exited " + std::to_string(result.exitCode) + ":\n" + result.output);
    return result.output;
}
std::string Tool(const std::map<std::string, std::string>& tools, const char* name) {
    const auto found = tools.find(name); return found == tools.end() ? "" : found->second;
}
} // namespace
std::string ShaderStageFlag(const std::string& s) {
    if (s.find("VERTEX") != std::string::npos) return "vert";
    if (s.find("COMPUTE") != std::string::npos) return "comp";
    if (s.find("FRAGMENT") != std::string::npos) return "frag";
    throw std::runtime_error("Shader compilation currently supports vertex, fragment, and compute stages");
}
DecodedShader DecodeShader(const std::filesystem::path& path, const ShaderStage& stage,
                           const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel) {
    const auto words = ReadSpirvBinary(path);
    DecodedShader r;
    r.stage = stage;
    const auto debug = ReadShaderMetadata(words);
    r.capturedSource = debug.capturedSource;
    r.metadata = debug.debug;
    r.metadata["binary_sha256"] = Sha256File(path, cancel);
    r.metadata["module"] = stage.module; r.metadata["stage"] = stage.stage;
    r.metadata["entry"] = stage.entry; r.metadata["specialization"] = stage.specialization;
    try {
        spirv_cross::CompilerGLSL compiler(words);
        const auto entryPoints = compiler.get_entry_points_and_stages();
        bool found = false;
        for (const auto& entry : entryPoints) {
            if (entry.name == stage.entry && (entryPoints.size() == 1 ||
                (stage.stage.find("VERTEX") != std::string::npos && entry.execution_model == spv::ExecutionModelVertex) ||
                (stage.stage.find("FRAGMENT") != std::string::npos && entry.execution_model == spv::ExecutionModelFragment) ||
                (stage.stage.find("COMPUTE") != std::string::npos && entry.execution_model == spv::ExecutionModelGLCompute))) {
                compiler.set_entry_point(entry.name, entry.execution_model);
                if (entry.execution_model == spv::ExecutionModelVertex) r.stage.stage = "VK_SHADER_STAGE_VERTEX_BIT";
                if (entry.execution_model == spv::ExecutionModelFragment) r.stage.stage = "VK_SHADER_STAGE_FRAGMENT_BIT";
                if (entry.execution_model == spv::ExecutionModelGLCompute) r.stage.stage = "VK_SHADER_STAGE_COMPUTE_BIT";
                found = true; break;
            }
        }
        if (!found) throw std::runtime_error("Captured entry point not found in binary");
        auto options = compiler.get_common_options();
        options.version = 450;
        options.vulkan_semantics = true;
        options.separate_shader_objects = true;
        compiler.set_common_options(options);
        const auto resources = compiler.get_shader_resources();
        std::ostringstream metadata;
        metadata << "SPIR-V " << ((words[1] >> 16) & 255) << '.' << ((words[1] >> 8) & 255) << '\n';
        auto describe = [&](const auto& list, const char* type, const char* kind) {
            for (const auto& resource : list) {
                Json item{{"kind", kind}, {"name", resource.name}};
                metadata << type << " " << resource.name;
                if (compiler.has_decoration(resource.id, spv::DecorationDescriptorSet))
                    metadata << "  set=" << (item["set"] = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet)).get<uint32_t>();
                if (compiler.has_decoration(resource.id, spv::DecorationBinding))
                    metadata << "  binding=" << (item["binding"] = compiler.get_decoration(resource.id, spv::DecorationBinding)).get<uint32_t>();
                if (compiler.has_decoration(resource.id, spv::DecorationLocation))
                    metadata << "  location=" << (item["location"] = compiler.get_decoration(resource.id, spv::DecorationLocation)).get<uint32_t>();
                metadata << '\n';
                r.resources.push_back(std::move(item));
            }
        };
        describe(resources.uniform_buffers, "Uniform buffer", "uniform_buffer");
        describe(resources.storage_buffers, "Storage buffer", "storage_buffer");
        describe(resources.sampled_images, "Sampled image", "sampled_image");
        describe(resources.separate_images, "Image", "image");
        describe(resources.separate_samplers, "Sampler", "sampler");
        describe(resources.push_constant_buffers, "Push constants", "push_constants");
        describe(resources.stage_inputs, "Input", "input");
        describe(resources.stage_outputs, "Output", "output");
        r.reflection = metadata.str();
        r.glsl = compiler.compile();
    } catch (const std::exception& e) {
        r.glslError = e.what();
        r.reflection += std::string("GLSL reconstruction unavailable: ") + e.what();
    }
    const auto disassembler = Tool(tools, "disassembler");
    if (!disassembler.empty()) {
        try { r.assembly = Run(disassembler, {path.u8string()}, cancel); }
        catch (const std::exception& e) { r.assemblyError = e.what(); r.reflection += std::string("\nAssembly unavailable: ") + e.what(); }
    } else r.assemblyError = "No SPIR-V disassembler is configured";
    return r;
}
void CompileShader(const std::filesystem::path& source, const ShaderStage& stage, const std::filesystem::path& output,
                   const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel) {
    const auto extension = source.extension().u8string();
    if (extension == ".spv") {
        if (source != output) std::filesystem::copy_file(source, output, std::filesystem::copy_options::overwrite_existing);
    } else if (extension == ".spvasm") {
        Run(Tool(tools, "assembler"), {"--target-env", "vulkan1.1", source.u8string(), "-o", output.u8string()}, cancel);
    } else {
        const auto flag = stage.stage.empty() ? std::string("frag") : ShaderStageFlag(stage.stage);
        Run(Tool(tools, "compiler"), {"-V", "--target-env", "vulkan1.1", "-S", flag, "--source-entrypoint", "main", "-e",
            stage.entry.empty() ? "main" : stage.entry, source.u8string(), "-o", output.u8string()}, cancel);
    }
    Run(Tool(tools, "validator"), {"--target-env", "vulkan1.1", output.u8string()}, cancel);
}
} // namespace gfx
