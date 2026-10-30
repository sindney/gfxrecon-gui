#pragma once

#include "analysis/capture.h"

namespace gfx {
struct ResourceArtifact {
    std::filesystem::path file;
    uint64_t size = 0;
    std::string role;
    Json metadata;
};
struct ResourcePage {
    std::filesystem::path file;
    uint64_t offset = 0, totalSize = 0;
    std::vector<uint8_t> bytes;
};
struct SamplerBinding {
    uint64_t set=0,binding=0,arrayIndex=0,sampler=0;
    bool immutable=false;
};
bool DescriptorUsesSampler(const Json& type);
std::vector<SamplerBinding> CollectStageSamplers(const Capture& capture,const Execution& execution,const ShaderStage& stage);
Json ReadDrawParameters(const std::filesystem::path& directory,uint64_t block);
std::vector<ResourceArtifact> CollectResources(const std::filesystem::path& directory);
std::filesystem::path PrimaryOutputImage(const std::vector<ResourceArtifact>& resources);
ResourcePage ReadResourcePage(const std::filesystem::path& file, uint64_t offset, size_t limit = 65536);
std::string DecodeResourceValue(const std::vector<uint8_t>& bytes, size_t offset, int type);
} // namespace gfx
