#include "analysis/index.h"
#include "core/artifacts.h"
#include <fstream>
#include <unordered_map>
#include <stdexcept>

namespace gfx {
namespace {
struct Writer {
    std::ofstream stream;
    const std::atomic_bool* cancel;
    Writer(const std::filesystem::path& file, const std::atomic_bool* flag) : stream(file, std::ios::binary), cancel(flag) {}
    template<class T> void Number(T value) {
        if (cancel && cancel->load()) throw std::runtime_error("Index write cancelled");
        if (!stream.write(reinterpret_cast<const char*>(&value), sizeof(value))) throw std::runtime_error("Index write failed");
    }
    void String(const std::string& value) { Number<uint64_t>(value.size()); stream.write(value.data(), static_cast<std::streamsize>(value.size())); }
    void Document(const Json& value) { String(value.dump()); }
    void Ids(const std::vector<uint64_t>& ids) { Number<uint64_t>(ids.size()); for (auto id : ids) Number(id); }
    void Notes(const std::vector<std::string>& notes) { Number<uint64_t>(notes.size()); for (const auto& note : notes) String(note); }
};
struct Reader {
    std::ifstream stream;
    uint64_t length;
    const std::atomic_bool* cancel;
    Reader(const std::filesystem::path& file, const std::atomic_bool* flag) : stream(file, std::ios::binary), length(std::filesystem::file_size(file)), cancel(flag) {}
    template<class T> T Number() {
        if (cancel && cancel->load()) throw std::runtime_error("Index read cancelled");
        T value{};
        if (!stream.read(reinterpret_cast<char*>(&value), sizeof(value))) throw std::runtime_error("Truncated index");
        return value;
    }
    uint64_t Count(uint64_t minimumBytes = 1) {
        auto count = Number<uint64_t>();
        if (count > length / minimumBytes || count > 100000000) throw std::runtime_error("Invalid index count");
        return count;
    }
    std::string String() {
        const auto size = Count(); if (size > 64 * 1024 * 1024) throw std::runtime_error("Index string exceeds supported limit");
        std::string value(static_cast<size_t>(size), '\0');
        if (size && !stream.read(value.data(), static_cast<std::streamsize>(size))) throw std::runtime_error("Truncated index string");
        return value;
    }
    Json Document() { return Json::parse(String()); }
    std::vector<uint64_t> Ids() { std::vector<uint64_t> ids; const auto count = Count(8); ids.reserve(static_cast<size_t>(count)); for (uint64_t n = 0; n < count; ++n) ids.push_back(Number<uint64_t>()); return ids; }
    std::vector<std::string> Notes() { std::vector<std::string> notes; const auto count = Count(8); for (uint64_t n = 0; n < count; ++n) notes.push_back(String()); return notes; }
};
}
void WriteCaptureIndex(const Capture& c, const std::filesystem::path& file, const std::atomic_bool* cancel) {
    Writer out(file, cancel);
    out.Number<uint64_t>(0x5844495547524347ull); out.Number(kIndexSchema);
    out.Number<uint64_t>(std::filesystem::file_size(c.jsonl)); out.Number(c.frames); out.Number<uint8_t>(c.android);
    out.String(Sha256File(c.jsonl, cancel));
    out.Document(c.header); out.Notes(c.limitations);
    std::unordered_map<const Json*, uint64_t> ids;
    std::vector<std::shared_ptr<const Json>> states;
    for (const auto& event : c.events) if (event.state && !ids.count(event.state.get())) { ids[event.state.get()] = states.size() + 1; states.push_back(event.state); }
    out.Number<uint64_t>(states.size()); for (const auto& state : states) out.Document(*state);
    out.Number<uint64_t>(c.events.size());
    for (const auto& event : c.events) {
        out.Number(event.block); out.Number(event.offset); out.Number(event.thread); out.Number(event.commandBuffer); out.Number(event.pipeline);
        out.Number(event.frame); out.Number<uint8_t>(event.draw); out.String(event.name); out.String(event.label); out.Ids(event.objects);
        out.Number<uint64_t>(event.state ? ids.at(event.state.get()) : 0);
    }
    out.Number<uint64_t>(c.executions.size());
    for (const auto& execution : c.executions) {
        out.Number<uint64_t>(execution.event); out.Number(execution.submit); out.Number(execution.begin); out.Number(execution.generation); out.Number(execution.frame);
        out.Number<uint8_t>(execution.primary); out.Number<uint8_t>(execution.valid); out.String(execution.unavailable);
        out.Ids(execution.renderScope); out.Ids(execution.invocationPath); out.Document(execution.descriptorValues); out.Document(execution.renderTargets); out.Notes(execution.notes);
        out.Number<uint64_t>(execution.invocations.size());
        for (const auto& invocation : execution.invocations) {
            out.Number(invocation.commandBuffer); out.Number(invocation.begin); out.Number(invocation.generation); out.Number(invocation.execute); out.Number(invocation.ordinal);
            out.Ids(invocation.renderScope); out.Document(invocation.renderTargets);
        }
    }
    out.Number<uint64_t>(c.pipelines.size());
    for (const auto& [id, stages] : c.pipelines) {
        out.Number(id); out.Number<uint64_t>(stages.size());
        for (const auto& stage : stages) { out.Number(stage.module); out.String(stage.stage); out.String(stage.entry); out.Document(stage.specialization); }
    }
    out.Number<uint64_t>(c.pipelineInfo.size()); for (const auto& [id, info] : c.pipelineInfo) { out.Number(id); out.Document(info); }
    out.Number<uint64_t>(c.objects.size());
    for (const auto& [id, object] : c.objects) {
        out.Number(id); out.String(object.type); out.String(object.name); out.Number(object.created); out.Number(object.destroyed); out.Document(object.createInfo);
    }
    out.stream.flush(); if (!out.stream) throw std::runtime_error("Incomplete index write");
}
std::shared_ptr<Capture> ReadCaptureIndex(const std::filesystem::path& jsonl, const std::filesystem::path& file, const std::atomic_bool* cancel) {
    Reader in(file, cancel);
    if (in.Number<uint64_t>() != 0x5844495547524347ull || in.Number<uint32_t>() != kIndexSchema) throw std::runtime_error("Unsupported index schema");
    if (in.Number<uint64_t>() != std::filesystem::file_size(jsonl)) throw std::runtime_error("Index source size changed");
    auto c = std::make_shared<Capture>(); c->jsonl = jsonl; c->source = jsonl;
    c->frames = in.Number<uint32_t>(); c->android = in.Number<uint8_t>() != 0;
    if (in.String() != Sha256File(jsonl, cancel)) throw std::runtime_error("Index source fingerprint changed");
    c->header = in.Document(); c->limitations = in.Notes();
    std::vector<std::shared_ptr<const Json>> states{nullptr};
    auto count = in.Count(8); for (uint64_t n = 0; n < count; ++n) states.push_back(std::make_shared<const Json>(in.Document()));
    count = in.Count(53); c->events.reserve(static_cast<size_t>(count));
    const auto jsonlSize = std::filesystem::file_size(jsonl);
    for (uint64_t n = 0; n < count; ++n) {
        Event e; e.block = in.Number<uint64_t>(); e.offset = in.Number<uint64_t>(); e.thread = in.Number<uint64_t>();
        e.commandBuffer = in.Number<uint64_t>(); e.pipeline = in.Number<uint64_t>(); e.frame = in.Number<uint32_t>(); e.draw = in.Number<uint8_t>() != 0;
        e.name = in.String(); e.label = in.String(); e.objects = in.Ids(); const auto state = in.Number<uint64_t>();
        if (state >= states.size() || e.offset >= jsonlSize || (!c->events.empty() && e.block <= c->events.back().block)) throw std::runtime_error("Invalid cached event identity");
        e.state = states[static_cast<size_t>(state)]; c->events.push_back(std::move(e));
    }
    count = in.Count(8); c->executions.reserve(static_cast<size_t>(count));
    for (uint64_t n = 0; n < count; ++n) {
        Execution e; e.event = static_cast<size_t>(in.Number<uint64_t>()); e.submit = in.Number<uint64_t>(); e.begin = in.Number<uint64_t>();
        e.generation = in.Number<uint64_t>(); e.frame = in.Number<uint32_t>(); e.primary = in.Number<uint8_t>() != 0; e.valid = in.Number<uint8_t>() != 0; e.unavailable = in.String();
        e.renderScope = in.Ids(); e.invocationPath = in.Ids(); e.descriptorValues = in.Document(); e.renderTargets = in.Document(); e.notes = in.Notes();
        const auto contexts = in.Count(44); if (contexts > 32 || e.event >= c->events.size()) throw std::runtime_error("Invalid cached invocation");
        for (uint64_t j = 0; j < contexts; ++j) {
            Invocation i; i.commandBuffer = in.Number<uint64_t>(); i.begin = in.Number<uint64_t>(); i.generation = in.Number<uint64_t>(); i.execute = in.Number<uint64_t>();
            i.ordinal = in.Number<uint32_t>(); i.renderScope = in.Ids(); i.renderTargets = in.Document(); e.invocations.push_back(std::move(i));
        }
        c->executions.push_back(std::move(e));
    }
    count = in.Count(16);
    for (uint64_t n = 0; n < count; ++n) {
        const auto id = in.Number<uint64_t>(), stageCount = in.Count(8);
        for (uint64_t j = 0; j < stageCount; ++j) { ShaderStage stage; stage.module = in.Number<uint64_t>(); stage.stage = in.String(); stage.entry = in.String(); stage.specialization = in.Document(); c->pipelines[id].push_back(std::move(stage)); }
    }
    count = in.Count(16); for (uint64_t n = 0; n < count; ++n) { const auto id = in.Number<uint64_t>(); c->pipelineInfo[id] = in.Document(); }
    count = in.Count(32);
    for (uint64_t n = 0; n < count; ++n) {
        const auto id = in.Number<uint64_t>(); ObjectInfo object; object.type = in.String(); object.name = in.String(); object.created = in.Number<uint64_t>(); object.destroyed = in.Number<uint64_t>(); object.createInfo = in.Document(); c->objects[id] = std::move(object);
    }
    if (c->events.empty() || in.stream.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Incomplete or trailing index data");
    return c;
}
} // namespace gfx
