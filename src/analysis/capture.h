#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace gfx {
using Json = nlohmann::json;
struct Event {
    uint64_t block = 0;
    uint64_t offset = 0;
    uint64_t thread = 0;
    uint64_t commandBuffer = 0;
    uint64_t pipeline = 0;
    uint32_t frame = 1;
    std::string name;
    bool draw = false;
    std::string label;
    std::shared_ptr<const Json> state;
    std::vector<uint64_t> objects;
};
struct ShaderStage {
    uint64_t module = 0;
    std::string stage;
    std::string entry;
    Json specialization;
};
struct ObjectInfo {
    std::string type, name;
    uint64_t created = 0, destroyed = 0;
    Json createInfo;
};
struct Invocation {
    uint64_t commandBuffer = 0, begin = 0, generation = 0;
    uint64_t execute = 0;
    uint32_t ordinal = 0;
    std::vector<uint64_t> renderScope;
    Json renderTargets;
};
struct Execution {
    size_t event = 0;
    uint64_t submit = 0;
    uint64_t begin = 0;
    uint64_t generation = 0;
    uint32_t frame = 1;
    std::vector<uint64_t> renderScope;
    std::vector<uint64_t> invocationPath;
    std::vector<Invocation> invocations; // root to leaf; identities include each execute call/array slot
    bool primary = true;
    bool valid = true;
    std::string unavailable;
    Json descriptorValues;
    Json renderTargets;
    std::vector<std::string> notes;
    Json DumpRequest() const;
};
struct Capture {
    std::filesystem::path jsonl;
    std::filesystem::path source;
    Json header;
    std::vector<Event> events;
    std::vector<Execution> executions;
    std::map<uint64_t, std::vector<ShaderStage>> pipelines;
    std::map<uint64_t, Json> pipelineInfo;
    std::map<uint64_t, ObjectInfo> objects;
    uint32_t frames = 1;
    bool android = false;
    std::vector<std::string> limitations;
    std::string fingerprint;
    Json ReadEvent(size_t i) const;
    Json DumpRequest(size_t execution) const;
    std::string SearchText(size_t event) const;
    std::string ModuleEditLimitation(uint64_t module) const;
};
std::shared_ptr<Capture> ReadCapture(const std::filesystem::path& jsonl,
                                    const std::atomic_bool* cancel = nullptr);
} // namespace gfx
