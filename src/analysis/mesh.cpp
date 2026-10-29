#include "analysis/mesh.h"
#include "shaders/metadata.h"
#include <spirv_glsl.hpp>
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>

namespace gfx {
namespace {
constexpr uint32_t kMaxVertices = 200000;
struct Format { unsigned bits, count, type; }; // type: UNORM,SNORM,USCALED,SSCALED,UINT,SINT,SFLOAT
Format Layout(uint32_t format) {
    if (format >= 98 && format <= 109) return {32, (format - 98) / 3 + 1, 4 + (format - 98) % 3};
    if (format >= 70 && format <= 97) return {16, (format - 70) / 7 + 1, (format - 70) % 7};
    if (format >= 9 && format <= 29) return {8, (format - 9) / 7 + 1, (format - 9) % 7};
    if (format >= 37 && format <= 42) return {8, 4, format - 37};
    if (format >= 44 && format <= 49) return {8, 4, format - 44};
    throw std::runtime_error("Packed, compressed, 64-bit or unknown vertex format is unavailable in this preview");
}
uint64_t Number(const Json& j, const char* key, uint64_t fallback = 0) {
    const auto found = j.find(key);
    return found != j.end() && found->is_number() ? found->get<uint64_t>() : fallback;
}
float Half(uint16_t value) {
    const auto sign = value >> 15, exponent = (value >> 10) & 31, mantissa = value & 1023;
    const float magnitude = exponent == 31 ? (mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity()) :
        exponent ? std::ldexp(1.0f + mantissa / 1024.0f, static_cast<int>(exponent) - 15) : std::ldexp(mantissa / 1024.0f, -14);
    return sign ? -magnitude : magnitude;
}
std::vector<uint8_t> Bytes(const std::filesystem::path& directory, const Json& object) {
    if (!object.value("compressedSize", Json{}).is_null()) throw std::runtime_error("Compressed mesh buffer is unsupported");
    const auto file = directory / std::filesystem::u8path(object.at("file").get<std::string>()).filename();
    if (std::filesystem::is_symlink(file) || std::filesystem::file_size(file) > 64 * 1024 * 1024) throw std::runtime_error("Mesh buffer is outside the supported 64 MiB limit");
    std::ifstream input(file, std::ios::binary); if (!input) throw std::runtime_error("Mesh buffer readback is missing");
    return {std::istreambuf_iterator<char>(input), {}};
}
uint32_t Topology(const Json& value) {
    if (value.is_number()) return value.get<uint32_t>();
    const std::string names[] = {"VK_PRIMITIVE_TOPOLOGY_POINT_LIST", "VK_PRIMITIVE_TOPOLOGY_LINE_LIST", "VK_PRIMITIVE_TOPOLOGY_LINE_STRIP", "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST", "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP", "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN"};
    for (uint32_t i = 0; i < 6; ++i) if (value == names[i]) return i;
    throw std::runtime_error("This primitive topology requires an unsupported tessellation/adjacency path");
}
Json FindDraw(const Json& j, uint64_t block, uint64_t submit, unsigned depth = 0) {
    if (depth > 32) throw std::runtime_error("Mesh metadata nesting exceeds the supported limit");
    if (j.is_object() && Number(j, "drawIndex") == block && Number(j, "queueSubmitIndex") == submit) return j;
    if (j.is_structured()) for (const auto& child : j) if (child.is_structured()) { const auto found = FindDraw(child, block, submit, depth + 1); if (!found.is_null()) return found; }
    return {};
}
uint32_t FormatNumber(const Json& value) {
    if (value.is_number()) return value.get<uint32_t>();
    if (!value.is_string()) throw std::runtime_error("Missing vertex format");
    // Upstream emits numeric VkFormat for vertex attributes. Accept common named formats too.
    const auto text = value.get<std::string>();
    for (uint32_t f = 98; f <= 109; ++f) {
        const auto info = Layout(f); std::string name = "VK_FORMAT_R32";
        if (info.count > 1) name += "G32"; if (info.count > 2) name += "B32"; if (info.count > 3) name += "A32";
        name += info.type == 4 ? "_UINT" : info.type == 5 ? "_SINT" : "_SFLOAT";
        if (name == text) return f;
    }
    throw std::runtime_error("Unknown named vertex format: " + text);
}
void PostShader(MeshData& mesh, const std::filesystem::path& binary, const Json& draw, const std::filesystem::path& directory, const std::string& entry) {
    spirv_cross::CompilerGLSL shader(ReadSpirvBinary(binary)); shader.set_entry_point(entry, spv::ExecutionModelVertex);
    for (const auto capability : shader.get_declared_capabilities()) if (capability != spv::CapabilityShader && capability != spv::CapabilityMatrix)
        throw std::runtime_error("This vertex capability has not been validated for local post-VS evaluation");
    shader.update_active_builtins();
    if (!shader.has_active_builtin(spv::BuiltInPosition, spv::StorageClassOutput)) throw std::runtime_error("Vertex shader does not write Position");
    const auto active=shader.get_active_interface_variables();shader.set_enabled_interface_variables(active);
    const auto res = shader.get_shader_resources(active);
    if (!res.storage_buffers.empty() || !res.storage_images.empty() || !res.sampled_images.empty() || !res.separate_images.empty() ||
        !res.separate_samplers.empty() || !res.push_constant_buffers.empty() || !res.acceleration_structures.empty() || !res.atomic_counters.empty() || !res.subpass_inputs.empty())
        throw std::runtime_error("Post-VS preview requires unsupported storage/image/push-constant state; input geometry remains inspectable");
    for (const auto& builtin : res.builtin_inputs) if (shader.has_active_builtin(builtin.builtin, spv::StorageClassInput) &&
        builtin.builtin != spv::BuiltInVertexIndex && builtin.builtin != spv::BuiltInInstanceIndex && builtin.builtin != spv::BuiltInVertexId && builtin.builtin != spv::BuiltInInstanceId)
        throw std::runtime_error("Post-VS builtin input is not reconstructed for this path");
    std::vector<uint32_t> locations;
    for (const auto& attr : res.stage_inputs) {
        const auto location = shader.get_decoration(attr.id, spv::DecorationLocation);
        const auto& type = shader.get_type(attr.type_id);
        if (type.columns != 1 || !type.array.empty() || type.width != 32 || type.vecsize > 4) throw std::runtime_error("Matrix/array/64-bit shader input is unsupported");
        const auto found = std::find_if(mesh.attributes.begin(), mesh.attributes.end(), [&](const auto& a) { return a.location == location; });
        if (found == mesh.attributes.end()) throw std::runtime_error("Active vertex input location " + std::to_string(location) + " is unavailable. " + mesh.diagnostic);
        mesh.shaderInputs.push_back(location); locations.push_back(location);
    }
    for (mesh.idLocation = 0; mesh.idLocation < 16 && std::find(locations.begin(), locations.end(), mesh.idLocation) != locations.end(); ++mesh.idLocation) {}
    if (mesh.idLocation == 16) throw std::runtime_error("No free attribute slot is available for original vertex identities");
    const auto descriptors = draw.value("descriptors", Json::object());
    for (const auto& buffer : res.uniform_buffers) {
        if (!shader.get_type(buffer.type_id).array.empty() || mesh.uniforms.size() >= 12) throw std::runtime_error("Uniform descriptor arrays or more than 12 uniform blocks are unsupported");
        const auto set = shader.get_decoration(buffer.id, spv::DecorationDescriptorSet), binding = shader.get_decoration(buffer.id, spv::DecorationBinding);
        bool found = false;
        for (auto stage = descriptors.begin(); stage != descriptors.end(); ++stage) {
            if (stage.key().find("VERTEX") == std::string::npos && stage.key() != "vertex") continue;
            for (const auto& descriptor : stage.value()) if (Number(descriptor, "set") == set && Number(descriptor, "binding") == binding && descriptor.contains("file")) {
                if (Number(descriptor, "arrayIndex")) throw std::runtime_error("Descriptor arrays are unsupported in post-VS preview");
                auto bytes = Bytes(directory, descriptor);
                if (bytes.size() < shader.get_declared_struct_size(shader.get_type(buffer.base_type_id))) throw std::runtime_error("Uniform readback is truncated");
                mesh.uniforms.push_back({shader.get_name(buffer.base_type_id).empty() ? buffer.name : shader.get_name(buffer.base_type_id), std::move(bytes)}); found = true; break;
            }
            if (found) break;
        }
        if (!found) throw std::runtime_error("A vertex uniform-buffer readback is unavailable");
        shader.unset_decoration(buffer.id, spv::DecorationDescriptorSet); shader.unset_decoration(buffer.id, spv::DecorationBinding);
    }
    if (!shader.get_specialization_constants().empty()) throw std::runtime_error("Specialization constants require additional post-VS validation");
    auto options = shader.get_common_options(); options.version = 330; options.vulkan_semantics = false;
    options.enable_420pack_extension = false; options.vertex.fixup_clipspace = false; options.vertex.flip_vert_y = false;
    shader.set_common_options(options);
    auto source = shader.compile();
    for (size_t i=0;i<res.uniform_buffers.size();++i) mesh.uniforms[i].block = shader.get_remapped_declared_block_name(res.uniform_buffers[i].id);
    if (source.size() > 8 * 1024 * 1024) throw std::runtime_error("Reconstructed vertex source exceeds the local preview limit");
    auto replace = [&](const std::string& from, const std::string& to) { size_t pos = 0; while ((pos = source.find(from, pos)) != std::string::npos) { source.replace(pos, from.size(), to); pos += to.size(); } };
    replace("gl_VertexID", "gfx_original_vertex_id");
    replace("gl_InstanceID", "gfx_original_instance_id");
    const auto version = source.find('\n');
    source.insert(version + 1, "layout(location=" + std::to_string(mesh.idLocation) + ") in int gfx_original_vertex_id;\nuniform int gfx_original_instance_id;\n");
    mesh.vertexSource = std::move(source);
}
}
MeshPosition DecodeMeshAttribute(const std::vector<uint8_t>& bytes, size_t offset, uint32_t format) {
    const auto info = Layout(format); const size_t length = info.bits / 8 * info.count;
    if (offset > bytes.size() || length > bytes.size() - offset) throw std::runtime_error("Vertex attribute extends outside the replay buffer");
    MeshPosition result{0,0,0,1};
    for (unsigned c = 0; c < info.count; ++c) {
        uint32_t raw = 0; std::memcpy(&raw, bytes.data() + offset + c * info.bits / 8, info.bits / 8);
        const int64_t signedValue = info.bits == 8 ? static_cast<int8_t>(raw) : info.bits == 16 ? static_cast<int16_t>(raw) : static_cast<int32_t>(raw);
        if (info.type == 6) { if (info.bits == 32) std::memcpy(&result[c], &raw, 4); else if (info.bits == 16) result[c] = Half(static_cast<uint16_t>(raw)); else throw std::runtime_error("sRGB is not a supported vertex input format"); }
        else if (info.type == 0) result[c] = static_cast<float>(raw / (std::pow(2.0, info.bits) - 1));
        else if (info.type == 1) result[c] = std::max(-1.0f, static_cast<float>(signedValue / (std::pow(2.0, info.bits - 1) - 1)));
        else result[c] = info.type == 3 || info.type == 5 ? static_cast<float>(signedValue) : static_cast<float>(raw);
    }
    if(format>=44&&format<=49)std::swap(result[0],result[2]);
    return result;
}
std::vector<uint32_t> BuildMeshPrimitives(uint32_t topology, const std::vector<uint32_t>& sizes, uint32_t& primitiveSize) {
    primitiveSize = topology == 0 ? 1 : topology <= 2 ? 2 : 3;
    if (topology > 5) throw std::runtime_error("Unsupported mesh topology");
    std::vector<uint32_t> result; uint32_t base = 0;
    for (auto count : sizes) {
        if (topology == 0) for (uint32_t i = 0; i < count; ++i) result.push_back(base + i);
        if (topology == 1 || topology == 3) for (uint32_t i = 0; i + primitiveSize <= count; i += primitiveSize) for (uint32_t j = 0; j < primitiveSize; ++j) result.push_back(base + i + j);
        if (topology == 2) for (uint32_t i = 1; i < count; ++i) result.insert(result.end(), {base + i - 1, base + i});
        if (topology == 4) for (uint32_t i = 2; i < count; ++i) result.insert(result.end(), {base + i - 2 + ((i & 1) ? 1u : 0u), base + i - 1 - ((i & 1) ? 1u : 0u), base + i});
        if (topology == 5) for (uint32_t i = 2; i < count; ++i) result.insert(result.end(), {base, base + i - 1, base + i});
        base += count;
    }
    return result;
}
MeshData ReadMeshData(const Capture& capture, size_t occurrence, const std::filesystem::path& directory, const std::filesystem::path& binary, uint32_t instance, const std::atomic_bool* cancel) {
    const auto& execution = capture.executions.at(occurrence); const auto& event = capture.events.at(execution.event);
    if (event.name != "vkCmdDraw" && event.name != "vkCmdDrawIndexed") throw std::runtime_error("Mesh preview currently supports direct draw and indexed draw occurrences");
    MeshData mesh; mesh.block = event.block; mesh.submit = execution.submit; mesh.pipeline = event.pipeline; mesh.instance = instance;
    auto cancelled = [&] { if (cancel && cancel->load()) throw std::runtime_error("Mesh readback cancelled"); }; cancelled();
    Json draw;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) if (entry.path().filename().u8string().find("_dr.json") != std::string::npos) {
        if (entry.file_size() > 16 * 1024 * 1024) throw std::runtime_error("Mesh metadata exceeds 16 MiB limit");
        std::ifstream input(entry.path()); draw = FindDraw(Json::parse(input), mesh.block, mesh.submit); if (!draw.is_null()) break;
    }
    if (draw.is_null()) throw std::runtime_error("No matching draw readback metadata was produced");
    mesh.parameters = draw.at("parameters"); mesh.firstInstance = static_cast<uint32_t>(Number(mesh.parameters, "firstInstance"));
    if (instance >= Number(mesh.parameters, "instanceCount")) throw std::runtime_error("Selected instance is outside the draw");
    const auto info = capture.pipelineInfo.at(event.pipeline), input = draw.value("vertexInputState", Json::object());
    const auto dynamicInfo = info.value("pDynamicState", Json::object());
    const auto dynamicStates = dynamicInfo.is_object() ? dynamicInfo.value("pDynamicStates", Json::array()) : Json::array();
    auto dynamic = [&](uint32_t number, const char* label) {
        const std::string token(label);
        return std::any_of(dynamicStates.begin(), dynamicStates.end(), [number,token](const Json& item) { return item == number || (item.is_string() && item.get<std::string>().find(token) != std::string::npos); });
    };
    if (dynamic(1000267002,"PRIMITIVE_TOPOLOGY") || dynamic(1000377004,"PRIMITIVE_RESTART_ENABLE"))
        throw std::runtime_error("Dynamic topology/restart requires additional mesh state resolution");
    if (info.value("pVertexInputState", Json::object()).is_object() && !info.value("pVertexInputState", Json::object()).value("pNext", Json{}).is_null())
        throw std::runtime_error("Vertex-input extension/divisor state is not enabled for mesh preview");
    const auto assembly = info.value("pInputAssemblyState", Json::object());
    const auto topologyValue = assembly.value("topology", Json{}); const auto topology = Topology(topologyValue); mesh.topology = topologyValue.is_string() ? topologyValue.get<std::string>() : topologyValue.dump();
    const auto restartValue = assembly.value("primitiveRestartEnable", Json{}); const bool restart = restartValue == true || restartValue == 1 || restartValue == "VK_TRUE";
    const bool indexed = event.name == "vkCmdDrawIndexed";
    const auto count = Number(mesh.parameters, indexed ? "indexCount" : "vertexCount");
    if (!count || count > kMaxVertices) throw std::runtime_error("Mesh draw is empty or exceeds the 200,000-vertex preview limit");
    std::vector<uint32_t> segments{0};
    if (indexed) {
        const auto index = input.at("indexBuffer"); const auto type = index.value("indexType", std::string{});
        const unsigned width = type.find("UINT16") != std::string::npos ? 2 : type.find("UINT32") != std::string::npos ? 4 : type.find("UINT8") != std::string::npos ? 1 : 0;
        if (!width) throw std::runtime_error("Unsupported index type");
        const auto bytes = Bytes(directory, index); const auto first = Number(mesh.parameters, "firstIndex");
        if (first > bytes.size() / width || count > bytes.size() / width - first) throw std::runtime_error("Index readback does not cover the draw");
        for (uint64_t i = 0; i < count; ++i) {
            uint32_t id = 0; std::memcpy(&id, bytes.data() + (first + i) * width, width);
            if (restart && id == (width == 4 ? UINT32_MAX : (1u << (width * 8)) - 1)) { segments.push_back(0); continue; }
            const auto adjusted = int64_t(id) + mesh.parameters.value("vertexOffset", int64_t(0));
            if (adjusted < 0 || adjusted > INT32_MAX) throw std::runtime_error("Vertex identity is outside the supported range");
            mesh.vertexIds.push_back(static_cast<uint32_t>(adjusted)); ++segments.back();
        }
    } else for (uint32_t i = 0; i < count; ++i) { const auto id = Number(mesh.parameters, "firstVertex") + i; if (id > INT32_MAX) throw std::runtime_error("Vertex identity exceeds supported range"); mesh.vertexIds.push_back(static_cast<uint32_t>(id)); ++segments.back(); }
    mesh.primitiveIndices = BuildMeshPrimitives(topology, segments, mesh.primitiveSize);
    if (mesh.vertexIds.empty()) throw std::runtime_error("The draw contains no vertices after primitive restart");
    if (input.value("attributes", Json::array()).size() > 32) throw std::runtime_error("Mesh preview supports at most 32 vertex attributes");
    for (const auto& attr : input.value("attributes", Json::array())) {
        try {
            MeshAttribute decoded; decoded.location = static_cast<uint32_t>(Number(attr, "location")); decoded.binding = static_cast<uint32_t>(Number(attr, "binding"));
            const auto format = FormatNumber(attr.at("format")); const auto layout = Layout(format); decoded.format = "VkFormat " + std::to_string(format);
            decoded.integer = layout.type == 4 || layout.type == 5; decoded.signedInteger = layout.type == 5;
            Json binding, buffer;
            for (const auto& item : input.value("bindings", Json::array())) if (Number(item, "binding") == decoded.binding) binding = item;
            for (const auto& item : input.value("vertexBuffers", Json::array())) if (Number(item, "vertexBufferBinding") == decoded.binding) buffer = item;
            if (binding.is_null() || buffer.is_null() || !event.state) throw std::runtime_error("Vertex binding readback is unavailable");
            const auto recorded = event.state->at("vertexBuffers").at(std::to_string(decoded.binding));
            const auto bytes = Bytes(directory, buffer); auto stride = Number(binding, "stride");
            if (dynamic(1000267005,"VERTEX_INPUT_BINDING_STRIDE")) {
                if (!recorded.value("pStrides", Json{}).is_number()) throw std::runtime_error("Dynamic vertex stride was not recorded for this binding");
                stride = Number(recorded,"pStrides");
            }
            const bool instanced = binding.value("inputRate", Json{}) == "VK_VERTEX_INPUT_RATE_INSTANCE" || binding.value("inputRate", Json{}) == 1;
            for (const auto id : mesh.vertexIds) {
                if ((decoded.values.size() & 1023) == 0) cancelled();
                const auto element = instanced ? uint64_t(mesh.firstInstance) + instance : id;
                if (stride && element > (UINT64_MAX - Number(recorded, "offset") - Number(attr, "offset")) / stride) throw std::runtime_error("Vertex offset overflow");
                const auto absolute = Number(recorded, "offset") + element * stride + Number(attr, "offset");
                if (absolute < Number(buffer, "offset")) throw std::runtime_error("Vertex data precedes the dumped range");
                const auto offset = absolute - Number(buffer, "offset");
                decoded.values.push_back(DecodeMeshAttribute(bytes, static_cast<size_t>(offset), format));
                if (decoded.integer) {
                    std::array<uint32_t,4> values{0,0,0,1};
                    for (unsigned c = 0; c < layout.count; ++c) { uint32_t raw=0; std::memcpy(&raw, bytes.data()+offset+c*layout.bits/8, layout.bits/8); values[c] = decoded.signedInteger ? static_cast<uint32_t>(layout.bits==8 ? int32_t(static_cast<int8_t>(raw)) : layout.bits==16 ? int32_t(static_cast<int16_t>(raw)) : int32_t(raw)) : raw; }
                    if(format>=44&&format<=49)std::swap(values[0],values[2]);
                    decoded.integers.push_back(values);
                }
            }
            mesh.attributes.push_back(std::move(decoded));
        } catch (const std::exception& error) { mesh.diagnostic += "Location " + std::to_string(Number(attr, "location")) + ": " + error.what() + "\n"; }
    }
    if (!input.contains("attributes") || input["attributes"].empty()) mesh.diagnostic = "No vertex input attributes: geometry is generated by the shader. Use Post-VS screen space.";
    if (!draw.value("colorAttachments", Json::array()).empty()) {
        const auto extent = draw["colorAttachments"][0].value("extent", Json::array());
        if (extent.size() >= 2) { mesh.width = extent[0].get<uint32_t>(); mesh.height = extent[1].get<uint32_t>(); }
    }
    if (event.state && event.state->value("viewports", Json{}).is_object() && event.state->at("viewports").contains("0")) mesh.viewport = event.state->at("viewports")["0"];
    else if (info.value("pViewportState", Json{}).is_object() && !info["pViewportState"].value("pViewports", Json::array()).empty()) mesh.viewport = info["pViewportState"]["pViewports"][0];
    try {
        cancelled();
        const auto& stages = capture.pipelines.at(event.pipeline);
        if (stages.size() != 2) throw std::runtime_error("Tessellation, geometry and mesh-shader stages are not evaluated by the local post-VS preview");
        const auto stage = std::find_if(stages.begin(), stages.end(), [](const auto& s) { return s.stage.find("VERTEX") != std::string::npos; });
        if (stage == stages.end() || !stage->specialization.is_null()) throw std::runtime_error("Missing vertex stage or unsupported specialization data");
        PostShader(mesh, binary, draw, directory, stage->entry);
    } catch (const std::exception& error) { mesh.postDiagnostic = error.what(); }
    mesh.provenance = "Input bytes read back by replay; post-VS positions evaluated locally in OpenGL. Device-specific floating-point behavior may differ.";
    return mesh;
}
} // namespace gfx
