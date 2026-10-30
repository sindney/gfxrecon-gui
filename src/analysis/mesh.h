#pragma once
#include "analysis/capture.h"
#include <array>

namespace gfx {
using MeshPosition = std::array<float, 4>;
struct MeshAttribute {
    uint32_t location = 0, binding = 0;
    std::string format;
    bool integer = false, signedInteger = false;
    std::vector<MeshPosition> values;
    std::vector<std::array<uint32_t, 4>> integers;
};
struct MeshUniform { std::string block; std::vector<uint8_t> bytes; };
struct MeshData {
    struct Range { uint32_t first=0,count=0,instanceId=0; };
    std::vector<Range> ranges;
    std::vector<std::shared_ptr<MeshData>> records;
    std::vector<std::string> recordDiagnostics;
    uint64_t block = 0, submit = 0, pipeline = 0;
    uint32_t firstInstance = 0, instance = 0;
    std::string topology, target, provenance, diagnostic, postDiagnostic;
    Json parameters, viewport;
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> vertexIds, primitiveIndices;
    std::vector<MeshAttribute> attributes;
    std::vector<MeshUniform> uniforms;
    std::vector<uint32_t> shaderInputs;
    uint32_t primitiveSize = 3, idLocation = 15;
    std::string vertexSource;
};
bool SupportsMeshDraw(const std::string& command);
MeshData ReadMeshData(const Capture& capture, size_t execution, const std::filesystem::path& directory,
                      const std::filesystem::path& vertexBinary, uint32_t instance = 0, const std::atomic_bool* cancel = nullptr);
std::vector<uint32_t> BuildMeshPrimitives(uint32_t topology, const std::vector<uint32_t>& segmentSizes, uint32_t& primitiveSize);
MeshPosition DecodeMeshAttribute(const std::vector<uint8_t>& bytes, size_t offset, uint32_t format);
} // namespace gfx
