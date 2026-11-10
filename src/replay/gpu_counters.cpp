#include "replay/gpu_counters.h"
#include "core/artifacts.h"
#include "platform/adb.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace gfx {
namespace {
constexpr size_t kMaxTraceBytes = 128 * 1024 * 1024;
constexpr size_t kMaxSamples = 2000000;
struct Bytes { const uint8_t* data = nullptr; size_t size = 0; };
struct Field { uint32_t id; uint32_t wire; uint64_t integer = 0; Bytes bytes; };
[[noreturn]] void Bad(const std::string& reason) { throw std::runtime_error("GPU counter protocol: " + reason); }
uint64_t Varint(Bytes bytes, size_t& pos) {
    uint64_t value = 0;
    for (unsigned i = 0; i != 10; ++i) {
        if (pos == bytes.size) Bad("truncated varint");
        const auto byte = bytes.data[pos++];
        if (i == 9 && byte > 1) Bad("overflowed varint");
        value |= uint64_t(byte & 127) << (i * 7);
        if (!(byte & 128)) return value;
    }
    Bad("unterminated varint");
}
std::vector<Field> Fields(Bytes bytes) {
    std::vector<Field> result;
    size_t pos = 0;
    while (pos < bytes.size) {
        if (result.size() >= kMaxSamples) Bad("message exceeds field budget");
        const auto tag = Varint(bytes, pos);
        if ((tag >> 3) == 0 || (tag >> 3) > 0x1fffffff) Bad("invalid field tag");
        Field field{uint32_t(tag >> 3), uint32_t(tag & 7)};
        if (field.wire == 0) field.integer = Varint(bytes, pos);
        else {
            uint64_t length = field.wire == 1 ? 8 : field.wire == 5 ? 4 : field.wire == 2 ? Varint(bytes, pos) : UINT64_MAX;
            if (length > bytes.size - pos) Bad("truncated or unsupported field");
            field.bytes = {bytes.data + pos, size_t(length)}; pos += size_t(length);
        }
        result.push_back(field);
    }
    return result;
}
Bytes View(const std::vector<uint8_t>& bytes) { return {bytes.data(), bytes.size()}; }
std::string String(Bytes bytes) { return {reinterpret_cast<const char*>(bytes.data), bytes.size}; }
const Field* Find(const std::vector<Field>& fields, uint32_t id) {
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const Field& f) { return f.id == id; });
    return found == fields.end() ? nullptr : &*found;
}
uint64_t Integer(const std::vector<Field>& fields, uint32_t id, uint64_t fallback = 0) {
    const auto* field = Find(fields, id);
    if (!field) return fallback;
    if (field->wire != 0) Bad("integer has unexpected wire type");
    return field->integer;
}
std::string Text(const std::vector<Field>& fields, uint32_t id) {
    const auto* field = Find(fields, id);
    if (!field) return {};
    if (field->wire != 2 || field->bytes.size > 1024 * 1024) Bad("invalid text field");
    return String(field->bytes);
}
std::vector<uint64_t> RepeatedIntegers(const std::vector<Field>& fields, uint32_t id) {
    std::vector<uint64_t> values;
    for (const auto& field : fields) if (field.id == id) {
        if (field.wire == 0) values.push_back(field.integer);
        else if (field.wire == 2) { size_t pos = 0; while (pos < field.bytes.size) values.push_back(Varint(field.bytes, pos)); }
        else Bad("invalid repeated integer");
    }
    return values;
}
std::string Unit(uint64_t unit) {
    static const char* names[] = {"NONE", "BIT", "KILOBIT", "MEGABIT", "GIGABIT", "TERABIT", "PETABIT",
        "BYTE", "KILOBYTE", "MEGABYTE", "GIGABYTE", "TERABYTE", "PETABYTE", "HERTZ", "KILOHERTZ",
        "MEGAHERTZ", "GIGAHERTZ", "TERAHERTZ", "PETAHERTZ", "NANOSECOND", "MICROSECOND", "MILLISECOND",
        "SECOND", "MINUTE", "HOUR", "VERTEX", "PIXEL", "TRIANGLE", "MILLIWATT", "WATT", "KILOWATT",
        "JOULE", "VOLT", "AMPERE", "CELSIUS", "FAHRENHEIT", "KELVIN", "PERCENT", "PRIMITIVE", "FRAGMENT", "INSTRUCTION"};
    return unit < sizeof(names) / sizeof(*names) ? names[unit] : "UNKNOWN(" + std::to_string(unit) + ")";
}
Json Units(const std::vector<Field>& fields, uint32_t id) {
    Json result = Json::array(); for (auto unit : RepeatedIntegers(fields, id)) result.push_back(Unit(unit)); return result;
}
std::string UnitLabel(const Json& numerator, const Json& denominator) {
    auto join = [](const Json& values) { std::string result; for (const auto& value : values) { if (!result.empty()) result += '*'; result += value.get<std::string>(); } return result; };
    auto result = join(numerator); if (result.empty()) result = "1";
    const auto divisor = join(denominator); if (!divisor.empty()) result += '/' + divisor;
    return result;
}
Json Descriptor(Bytes bytes) {
    const auto fields = Fields(bytes);
    Json specs = Json::array(); std::set<uint64_t> ids;
    for (const auto& field : fields) if (field.id == 1 && field.wire == 2) {
        const auto row = Fields(field.bytes); const auto id = Integer(row, 1, UINT64_MAX);
        if (id == UINT64_MAX || !ids.insert(id).second) Bad("missing/duplicate counter id");
        const auto numerator = Units(row, 7), denominator = Units(row, 8);
        const auto direction = Integer(row, 11);
        if (direction > 2) Bad("unknown counter value direction");
        specs.push_back({{"id", id}, {"name", Text(row, 2)}, {"description", Text(row, 3)},
            {"numerator_units", numerator}, {"denominator_units", denominator}, {"unit", UnitLabel(numerator, denominator)},
            {"groups", RepeatedIntegers(row, 10)}, {"value_direction", direction},
            {"value_direction_semantics", direction == 2 ? "forward" : direction == 1 ? "backward" : "unspecified; Perfetto historical backward convention"}});
    }
    Json blocks = Json::array();
    for (const auto& field : fields) if (field.id == 2 && field.wire == 2) {
        const auto row = Fields(field.bytes);
        blocks.push_back({{"id", Integer(row, 1)}, {"capacity", Integer(row, 2)}, {"counter_ids", RepeatedIntegers(row, 5)}});
    }
    return {{"specs", specs}, {"blocks", blocks}, {"min_sampling_period_ns", Integer(fields, 3)},
        {"max_sampling_period_ns", Integer(fields, 4)}, {"supports_instrumented_sampling", Integer(fields, 5) != 0},
        {"descriptor_sha256", Sha256Text(String(bytes))}, {"sample_window_semantics", "point values; hardware averaging-window extent unverified"}};
}
std::vector<uint8_t> ReadBytes(const std::filesystem::path& file) {
    if (!std::filesystem::is_regular_file(file) || std::filesystem::file_size(file) > kMaxTraceBytes)
        Bad("missing file or exceeded 128 MiB artifact budget: " + file.u8string());
    std::ifstream input(file, std::ios::binary); std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    if (input.bad()) Bad("artifact read failed"); return bytes;
}
void WriteBytes(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!output) throw std::runtime_error("Cannot write counter artifact " + file.u8string());
}
void EncodeVarint(std::vector<uint8_t>& bytes, uint64_t value) {
    while (value > 127) { bytes.push_back(uint8_t(value) | 128); value >>= 7; } bytes.push_back(uint8_t(value));
}
void Number(std::vector<uint8_t>& bytes, uint32_t tag, uint64_t value) { EncodeVarint(bytes, uint64_t(tag) << 3); EncodeVarint(bytes, value); }
void Message(std::vector<uint8_t>& bytes, uint32_t tag, const std::vector<uint8_t>& value) {
    EncodeVarint(bytes, (uint64_t(tag) << 3) | 2); EncodeVarint(bytes, value.size()); bytes.insert(bytes.end(), value.begin(), value.end());
}
void Text(std::vector<uint8_t>& bytes, uint32_t tag, const std::string& value) { Message(bytes, tag, {value.begin(), value.end()}); }
std::vector<uint8_t> Base64(const std::vector<uint8_t>& bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> result; result.reserve((bytes.size() + 2) / 3 * 4 + 1);
    for (size_t offset = 0; offset < bytes.size(); offset += 3) {
        const uint32_t value = uint32_t(bytes[offset]) << 16 |
            (offset + 1 < bytes.size() ? uint32_t(bytes[offset + 1]) << 8 : 0) |
            (offset + 2 < bytes.size() ? bytes[offset + 2] : 0);
        result.push_back(alphabet[(value >> 18) & 63]); result.push_back(alphabet[(value >> 12) & 63]);
        result.push_back(offset + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
        result.push_back(offset + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }
    result.push_back('\n'); return result;
}
double Median(std::vector<double> values) {
    if (values.empty()) return 0; std::sort(values.begin(), values.end());
    return values.size() % 2 ? values[values.size() / 2] : (values[values.size() / 2 - 1] + values[values.size() / 2]) / 2;
}
std::string Canonical(std::string text) {
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return !std::isalnum(c); }), text.end());
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); }); return text;
}
void Cancelled(const std::atomic_bool* cancel) { if (cancel && cancel->load()) throw std::runtime_error("GPU counter collection cancelled"); }
void NormalizeCounterClocks(Json& trace) {
    struct Anchor { uint64_t source, boot; int64_t offset; };
    std::map<uint64_t, std::vector<Anchor>> bridges;
    std::set<uint64_t> usedClocks;
    for (const auto& sample : trace.at("samples"))
        if (sample.contains("source_clock_id")) usedClocks.insert(sample.at("source_clock_id").get<uint64_t>());
    for (const auto& snapshot : trace.at("clock_snapshots")) {
        const auto& clocks = snapshot.at("clocks");
        const auto boot = std::find_if(clocks.begin(), clocks.end(), [](const Json& c) { return c.at("id") == 6; });
        if (boot == clocks.end()) continue;
        const auto bootTime = boot->at("timestamp").get<uint64_t>();
        for (const auto& clock : clocks) {
            const auto id = clock.at("id").get<uint64_t>();
            if (!usedClocks.count(id)) continue;
            const auto source = clock.at("timestamp").get<uint64_t>();
            if (source > INT64_MAX || bootTime > INT64_MAX) Bad("clock snapshot timestamp overflow");
            bridges[id].push_back({source, bootTime, int64_t(bootTime) - int64_t(source)});
        }
    }
    std::map<uint64_t, int64_t> spreads;
    for (auto& [id, anchors] : bridges) {
        std::sort(anchors.begin(), anchors.end(), [](const Anchor& a, const Anchor& b) { return a.source < b.source; });
        int64_t low = anchors.front().offset, high = low;
        for (size_t i = 0; i < anchors.size(); ++i) {
            const auto& anchor = anchors[i];
            if (i && ((anchor.source == anchors[i-1].source && anchor.boot != anchors[i-1].boot) || anchor.boot < anchors[i-1].boot))
                Bad("contradictory clock snapshots");
            low = std::min(low, anchor.offset); high = std::max(high, anchor.offset);
        }
        if (static_cast<long double>(high) - low > 1000000) Bad("clock snapshot offset variation exceeds 1 ms");
        spreads[id] = high - low;
    }
    std::map<uint64_t, uint64_t> extrapolation;
    for (auto& sample : trace["samples"]) {
        if (!sample.contains("source_clock_id")) continue;
        const auto id = sample.at("source_clock_id").get<uint64_t>(), time = sample.at("source_timestamp_ns").get<uint64_t>();
        const auto bridge = bridges.find(id);
        if (bridge == bridges.end()) Bad("counter clock lacks a direct BOOTTIME snapshot bridge");
        const auto& anchors = bridge->second;
        auto nearest = std::lower_bound(anchors.begin(), anchors.end(), time, [](const Anchor& a, uint64_t t) { return a.source < t; });
        if (nearest == anchors.end()) nearest = anchors.end() - 1;
        else if (nearest != anchors.begin() && time - (nearest-1)->source <= nearest->source - time) --nearest;
        const uint64_t outside = time < anchors.front().source ? anchors.front().source - time : time > anchors.back().source ? time - anchors.back().source : 0;
        // Perfetto retains new snapshots only when it observes clock drift;
        // stable clocks can have startup anchors only. Bound offset reuse to
        // the collector's existing ten-minute maximum trace lifetime.
        if (outside > 600000000000ull) Bad("counter clock snapshot extrapolation exceeds ten minutes");
        extrapolation[id] = std::max(extrapolation[id], outside);
        uint64_t mapped;
        if (time >= nearest->source) {
            const auto delta = time - nearest->source;
            if (delta > UINT64_MAX - nearest->boot) Bad("converted counter timestamp overflow");
            mapped = nearest->boot + delta;
        } else {
            const auto delta = nearest->source - time;
            if (delta > nearest->boot) Bad("converted counter timestamp underflow");
            mapped = nearest->boot - delta;
        }
        sample["timestamp_boot_ns"] = mapped;
    }
    trace["counter_clock_bridges"] = Json::array();
    for (const auto& [id, distance] : extrapolation)
        trace["counter_clock_bridges"].push_back({{"source_clock_id",id},{"source_clock",id == 5 ? "MONOTONIC_RAW" : "MONOTONIC"},
            {"target_clock","BOOTTIME"},{"method","nearest direct clock snapshot offset"},{"snapshot_count",bridges.at(id).size()},
            {"observed_offset_spread_ns",spreads.at(id)},{"max_extrapolation_ns",distance},{"uncertainty_ns",nullptr},
            {"limitation","Snapshot acquisition latency and drift between snapshots are unverified; observed offset spread is not an uncertainty bound"}});
}
}

Json DecodeGpuCounterServiceState(const std::vector<uint8_t>& bytes) {
    if (bytes.size() > kMaxTraceBytes) Bad("oversized service state");
    const auto fields = Fields(View(bytes)); Json sources = Json::array(), producers = Json::array(), sessions = Json::array();
    for (const auto& field : fields) {
        if (field.wire != 2 || (field.id != 1 && field.id != 2 && field.id != 6)) continue;
        const auto row = Fields(field.bytes);
        if (field.id == 1) producers.push_back({{"id", Integer(row, 1)}, {"name", Text(row, 2)}, {"pid", Integer(row, 5)}, {"uid", Integer(row, 3)}});
        else if (field.id == 6) sessions.push_back({{"id", Integer(row, 1)}, {"name", Text(row, 4)}, {"state", Text(row, 3)}, {"started", Integer(row, 11) != 0}});
        else if (field.id == 2) {
            const auto* ds = Find(row, 1); if (!ds || ds->wire != 2) continue;
            const auto source = Fields(ds->bytes); if (Text(source, 1) != "gpu.counters") continue;
            const auto* descriptor = Find(source, 5);
            if (!descriptor || descriptor->wire != 2) Bad("GPU provider omitted descriptors");
            auto decoded = Descriptor(descriptor->bytes); decoded["name"] = "gpu.counters";
            decoded["producer_id"] = Integer(row, 2); sources.push_back(std::move(decoded));
        }
    }
    return {{"sources", sources}, {"producers", producers}, {"sessions", sessions},
        {"sessions_supported", Integer(fields, 7) != 0}, {"active_sessions", Integer(fields, 4)},
        {"service_version", Text(fields, 5)}};
}

Json DecodeGpuCounterTrace(const std::vector<uint8_t>& bytes) {
    if (bytes.size() > kMaxTraceBytes) Bad("oversized trace");
    Json result{{"samples", Json::array()}, {"clock_snapshots", Json::array()}, {"clock_domain", "BOOTTIME"},
        {"timestamp_unit", "nanoseconds"}, {"packet_loss", false}, {"packet_count", 0}, {"descriptor", nullptr}};
    std::map<uint64_t, uint64_t> defaults; std::set<uint64_t> seenSequences;
    uint64_t primaryClock = 6; bool bootSnapshot = false;
    for (const auto& packetField : Fields(View(bytes))) {
        if (packetField.id != 1 || packetField.wire != 2) continue;
        result["packet_count"] = result["packet_count"].get<uint64_t>() + 1;
        const auto packet = Fields(packetField.bytes); const auto sequence = Integer(packet, 10);
        const bool firstOnSequence = seenSequences.insert(sequence).second;
        if (Integer(packet, 13) & 1) defaults.erase(sequence);
        if (const auto* f = Find(packet, 59)) {
            if (f->wire != 2) Bad("invalid packet defaults");
            const auto row = Fields(f->bytes); if (Find(row, 58)) defaults[sequence] = Integer(row, 58);
        }
        // Perfetto explicitly sets this flag on a sequence's FIRST packet too.
        // It denotes actual missing data only after we have already seen that
        // sequence; a self-contained initial descriptor is not packet loss.
        if (Integer(packet, 42) && !firstOnSequence) result["packet_loss"] = true;
        if (const auto* snapshot = Find(packet, 6)) {
            if (snapshot->wire != 2) Bad("invalid clock snapshot");
            const auto clocks = Fields(snapshot->bytes); if (Find(clocks, 2)) primaryClock = Integer(clocks, 2);
            Json rows = Json::array(); std::set<uint64_t> clockIds;
            for (const auto& clock : clocks) if (clock.id == 1 && clock.wire == 2) {
                const auto row = Fields(clock.bytes); const auto id = Integer(row, 1), multiplier = Integer(row, 4, 1);
                if (Integer(row, 3)) Bad("incremental clock unsupported");
                if (!clockIds.insert(id).second) Bad("duplicate clock in snapshot");
                if ((id == 3 || id == 5 || id == 6) && (!Find(row, 2) || multiplier != 1)) Bad("supported clock snapshot must contain absolute nanoseconds");
                if (id == 6) { if (multiplier != 1) Bad("BOOTTIME clock is not nanoseconds"); bootSnapshot = true; }
                rows.push_back({{"id", id}, {"timestamp", Integer(row, 2)}, {"unit_multiplier_ns", multiplier}});
            }
            result["clock_snapshots"].push_back({{"primary_clock", primaryClock}, {"clocks", rows}});
        }
        if (const auto* stats = Find(packet, 35)) {
            if (stats->wire != 2) Bad("invalid trace stats");
            for (const auto& buffer : Fields(stats->bytes)) if (buffer.id == 1 && buffer.wire == 2) {
                const auto row = Fields(buffer.bytes);
                if (Integer(row, 3) || Integer(row, 6) || Integer(row, 13) || Integer(row, 18) || Integer(row, 19)) result["packet_loss"] = true;
            }
        }
        for (const auto& event : packet) if (event.id == 52 && event.wire == 2) {
            const auto fields = Fields(event.bytes);
            if (Find(fields, 4)) Bad("interned counter descriptors unsupported by this provider adapter");
            if (const auto* descriptor = Find(fields, 1)) {
                if (descriptor->wire != 2) Bad("invalid trace counter descriptor");
                auto decoded = Descriptor(descriptor->bytes);
                if (!result["descriptor"].is_null() && result["descriptor"]["descriptor_sha256"] != decoded["descriptor_sha256"])
                    Bad("counter definitions changed within trace");
                result["descriptor"] = std::move(decoded);
            }
            const auto clock = Integer(packet, 58, defaults.count(sequence) ? defaults.at(sequence) : primaryClock);
            for (const auto& value : fields) if (value.id == 2 && value.wire == 2) {
                if (!Find(packet, 8)) Bad("counter value has no timestamp");
                if (clock != 6 && clock != 3 && clock != 5) Bad("GPU counters use an unsupported clock; BOOTTIME bridge unavailable");
                const auto row = Fields(value.bytes); const auto id = Integer(row, 1, UINT64_MAX);
                if (id == UINT64_MAX) Bad("counter sample has no id");
                const auto* integer = Find(row, 2); const auto* real = Find(row, 3); double number;
                if ((integer != nullptr) == (real != nullptr)) Bad("counter sample needs exactly one value");
                if (integer) { if (integer->wire != 0) Bad("invalid integer sample"); number = double(static_cast<int64_t>(integer->integer)); }
                else { if (real->wire != 1 || real->bytes.size != 8) Bad("invalid double sample"); uint64_t bits = 0; for (size_t i = 0; i < 8; ++i) bits |= uint64_t(real->bytes.data[i]) << (i * 8); std::memcpy(&number, &bits, sizeof(number)); }
                if (!std::isfinite(number)) Bad("non-finite counter sample");
                if (result["samples"].size() >= kMaxSamples) Bad("counter sample budget exceeded");
                Json sample{{"timestamp_boot_ns", Integer(packet, 8)}, {"counter_id", id}, {"value", number}};
                if (clock != 6) { sample["source_clock_id"] = clock; sample["source_timestamp_ns"] = Integer(packet, 8); }
                result["samples"].push_back(std::move(sample));
            }
        }
    }
    if (!bootSnapshot) Bad("trace lacks BOOTTIME clock snapshot evidence");
    if (result["descriptor"].is_null()) Bad("trace lacks counter descriptors");
    if (result["packet_loss"].get<bool>()) Bad("trace reports packet loss/overwritten buffers");
    NormalizeCounterClocks(result);
    return result;
}

Json SummarizeGpuCounterWindow(const Json& trace, const Json& selection, uint64_t beginBootNs, uint64_t endBootNs) {
    if (!beginBootNs || endBootNs <= beginBootNs) Bad("invalid measured BOOTTIME window");
    if (trace.value("clock_domain", "") != "BOOTTIME" || trace.value("timestamp_unit", "") != "nanoseconds") Bad("counter clock is not BOOTTIME nanoseconds");
    if (trace.value("packet_loss", false)) Bad("trace contains lost samples");
    const auto period = selection.at("period_ns").get<uint64_t>();
    if (!period || period > 1000000000) Bad("invalid selected counter period");
    Json definitions = Json::array(), counters = Json::array(), unavailable = Json::array();
    // Index once: Mali catalogs can emit hundreds of values per packet.
    std::map<uint64_t, std::vector<std::pair<uint64_t,double>>> byCounter;
    for (const auto& point : trace.at("samples"))
        byCounter[point.at("counter_id").get<uint64_t>()].emplace_back(point.at("timestamp_boot_ns").get<uint64_t>(), point.at("value").get<double>());
    for (const auto& descriptor : selection.at("specs")) {
        const auto id = descriptor.at("id").get<uint64_t>();
        const auto& emitted = trace.at("descriptor").at("specs");
        const auto found = std::find_if(emitted.begin(), emitted.end(), [&](const Json& value) { return value.at("id") == descriptor.at("id"); });
        const bool definitionAvailable = found != emitted.end();
        Json definition;std::string definitionMismatch;
        for (const char* key : {"id", "name", "description", "numerator_units", "denominator_units", "value_direction"}) {
            if (definitionAvailable && descriptor.at(key) != found->at(key)) {
                const auto reason=std::string("discovery and trace counter definitions differ: ")+key;
                if(selection.value("profile",std::string{})!="all")Bad(reason);
                definitionMismatch=reason;
            }
            definition[key] = descriptor.at(key);
        }
        definitions.push_back(std::move(definition));
        const bool usableDefinition=definitionAvailable&&definitionMismatch.empty()&&descriptor.value("collectable",true);
        std::vector<std::pair<uint64_t,double>> rows;
        if (descriptor.value("collectable",true)) rows = std::move(byCounter[id]);
        std::sort(rows.begin(),rows.end());
        Json samples = Json::array(), quality = Json::array();
        if (!trace.value("counter_clock_bridges",Json::array()).empty())
            quality.push_back("Counter timestamps converted from trace clock snapshots; acquisition latency and between-snapshot drift are unverified");
        std::vector<double> measured, intervals;
        long double covered = 0, weighted = 0;
        const auto gapLimit = std::max<uint64_t>(period*4,period+2000000);
        for (size_t i=0;i<rows.size();++i) {
            const auto time = rows[i].first; const auto value = rows[i].second;
            const bool inside = time>=beginBootNs && time<=endBootNs;
            if (inside && (!std::isfinite(value) || (selection.value("profile",std::string{})!="all" && value<0))) Bad("selected GPU counter returned a negative or nonfinite value");
            if (inside && i && rows[i-1].first==time) Bad("duplicate measured counter timestamps");
            if (inside && usableDefinition) {
                measured.push_back(value);
                samples.push_back({{"timestamp_boot_ns",time},{"value",value},{"in_measurement_window",true},
                    {"time_from_gpu_start_ms",double(time-beginBootNs)/1e6}});
            }
            if (!i || rows[i-1].first==time) continue;
            const auto delta = time-rows[i-1].first;
            if (time>=beginBootNs && rows[i-1].first<=endBootNs) intervals.push_back(double(delta));
            if (!usableDefinition || rows[i-1].first<beginBootNs || time>endBootNs) continue;
            if (delta>gapLimit) continue;
            const auto left = std::max(rows[i-1].first,beginBootNs), right = std::min(time,endBootNs);
            if (right<=left) continue;
            const auto intervalValue = descriptor.value("value_direction",0)==2 ? rows[i-1].second : value;
            covered += right-left; weighted += static_cast<long double>(intervalValue)*(right-left);
        }
        const double coverage = double(covered/(endBootNs-beginBootNs));
        std::string reason;
        if (!descriptor.value("collectable",true)) reason=descriptor.value("unavailable_reason",std::string("Provider simultaneous-counter limit"));
        else if (!definitionAvailable) reason="Selected counter descriptor was not emitted by the trace";
        else if (!definitionMismatch.empty())reason=definitionMismatch;
        else if (measured.empty()) reason="No counter sample timestamp falls inside the GPU interval";
        else if (measured.size()==1) quality.push_back("Only one sample; average is a single observation");
        if (coverage<.8) quality.push_back("Low estimated interval coverage; values are reference samples");
        if (!reason.empty()) quality.push_back(reason);
        Json summary{{"mean",nullptr},{"sampled_mean",nullptr},{"min",nullptr},{"max",nullptr},{"median",nullptr},{"p95",nullptr},
            {"sample_count",measured.size()},{"coverage_estimate",usableDefinition?coverage:0.0},{"gap_threshold_ns",gapLimit},
            {"aggregation","sampled mean of timestamps within the GPU interval; hardware averaging-window extent unverified"},
            {"time_weighted_mean_estimate",usableDefinition && !measured.empty() && covered>0 ? Json(double(weighted/covered)) : Json(nullptr)}};
        if (!measured.empty()) {
            auto sorted=measured; std::sort(sorted.begin(),sorted.end());
            long double sum=0; for (auto value:measured) sum+=value;
            summary["mean"]=summary["sampled_mean"]=double(sum/measured.size());
            summary["min"]=sorted.front();summary["max"]=sorted.back();summary["median"]=Median(sorted);
            summary["p95"]=sorted[std::min(sorted.size()-1,size_t(std::ceil(sorted.size()*.95)-1))];
        }
        summary["interval_ns"]=intervals.empty()?Json(nullptr):Json{{"min",*std::min_element(intervals.begin(),intervals.end())},
            {"median",Median(intervals)},{"max",*std::max_element(intervals.begin(),intervals.end())}};
        Json counter=descriptor;counter["samples"]=std::move(samples);counter["summary"]=std::move(summary);
        if(!definitionMismatch.empty())counter["trace_definition"]=*found;
        counter["raw_sample_count"]=rows.size();counter["quality"]=quality;
        counter["status"]=measured.empty()?"unavailable":measured.size()==1?"sparse":"available";
        counter["scope"]="system-wide samples filtered to the selected GPU interval";
        if (!reason.empty()) counter["reason"]=reason;
        if (descriptor.at("unit")=="BYTE/SECOND") counter["bandwidth_semantics"]="Provider-labelled sampled byte rate; not exact bytes attributable to a frame";
        else if (descriptor.value("stage_scope",std::string{})=="fragment") counter["precision_semantics"]="Provider-labelled fragment instruction rate; no shader-level precision causality established";
        if (measured.empty()) unavailable.push_back({{"id",id},{"semantic",descriptor.value("semantic",std::string{})},{"reason",reason}});
        counters.push_back(std::move(counter));
    }
    return {{"status","complete"},{"clock_domain","BOOTTIME"},{"timestamp_unit","nanoseconds"},
        {"scope","system-wide samples filtered to the selected GPU interval; averaging windows and clock alignment may blur boundaries"},
        {"window_begin_boot_ns",beginBootNs},{"window_end_boot_ns",endBootNs},{"period_ns",period},{"profile",selection.value("profile","precision")},
        {"missing_optional_metrics",selection.value("missing_optional_metrics",Json::array())},{"unavailable_metrics",unavailable},
        {"descriptor_sha256",selection.at("descriptor_sha256")},{"trace_descriptor_sha256",trace.at("descriptor").at("descriptor_sha256")},
        {"selected_definition_sha256",Sha256Text(definitions.dump())},{"counters",counters},{"clock_snapshots",trace.at("clock_snapshots")},
        {"counter_clock_bridges",trace.value("counter_clock_bridges",Json::array())},{"packet_loss",false}};
}

Json SelectGpuCounters(const Json& service, uint64_t periodNs, const std::string& profile) {
    if (profile != "all" && profile != "precision" && profile != "bandwidth") throw std::runtime_error("Unknown GPU counter profile: " + profile);
    if (!periodNs || periodNs > 1000000000) throw std::runtime_error("Counter period must be in (0, 1 second]");
    const auto& sources = service.at("sources");
    if (sources.size() != 1) throw std::runtime_error("Missing or multiple GPU counter producers are ambiguous for this session");
    auto descriptor = sources[0];
    descriptor["producer"] = nullptr;
    for (const auto& producer : service.at("producers"))
        if (producer.at("id") == descriptor.at("producer_id")) descriptor["producer"] = producer;
    const auto minimum = descriptor.at("min_sampling_period_ns").get<uint64_t>();
    const auto maximum = descriptor.at("max_sampling_period_ns").get<uint64_t>();
    if ((minimum && periodNs < minimum) || (maximum && periodNs > maximum)) throw std::runtime_error("Requested counter period is outside the provider's supported range");
    if (profile == "all") {
        Json result=descriptor;
        result["period_ns"]=periodNs;result["profile"]="all";
        result["required_metrics"]=Json::array();result["optional_metrics"]=Json::array();result["missing_optional_metrics"]=Json::array();
        auto& specs=result["specs"];
        if(specs.empty())throw std::runtime_error("GPU counter provider advertises no counters");
        // Hardware co-collection limits are the only selection constraint.
        // Stable ID order makes repeated baseline/modified selections identical.
        std::vector<size_t> order(specs.size());
        for(size_t i=0;i<order.size();++i)order[i]=i;
        std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return specs[a].at("id").get<uint64_t>()<specs[b].at("id").get<uint64_t>();});
        std::vector<uint64_t> used(descriptor.at("blocks").size());std::set<uint64_t> ids;
        size_t selected=0;
        for(const auto index:order){
            auto& spec=specs[index];const auto id=spec.at("id").get<uint64_t>();
            if(!ids.insert(id).second)throw std::runtime_error("Counter ID mapping is ambiguous");
            std::vector<size_t> memberships;bool available=true;
            for(size_t b=0;b<used.size();++b){
                const auto& block=descriptor.at("blocks")[b];const auto& counters=block.at("counter_ids");
                if(std::find(counters.begin(),counters.end(),Json(id))==counters.end())continue;
                memberships.push_back(b);const auto capacity=block.at("capacity").get<uint64_t>();
                if(capacity&&used[b]>=capacity)available=false;
            }
            spec["collectable"]=available;spec["required"]=false;
            spec["semantic"]="counter_"+std::to_string(id);spec["stage_scope"]="provider-defined";
            if(available){++selected;for(auto b:memberships)++used[b];}
            else spec["unavailable_reason"]="Provider simultaneous-counter block capacity; not sampled in this replay";
        }
        result["selected_counter_count"]=selected;result["advertised_counter_count"]=specs.size();
        result["measurement_semantics"]="All provider counters, subject only to simultaneous-counter block capacity; device-wide reference samples within a GPU interval";
        return result;
    }
    const std::map<std::string, std::string> precision{{"fragmentaluinstructionssecfull", "fragment_full"}, {"fragmentaluinstructionssechalf", "fragment_half"},
        {"shaderalucapacityutilized", "alu_capacity"}, {"timealusworking", "alu_working"}, {"clockssecond", "clock_rate"}, {"shadersbusy", "shader_busy"}};
    const std::map<std::string, std::string> bandwidth{
        {"readtotalbytessec", "memory_read_total"}, {"readtotalbytessecond", "memory_read_total"},
        {"writetotalbytessec", "memory_write_total"}, {"writetotalbytessecond", "memory_write_total"},
        {"texturememoryreadbwbytessec", "texture_memory_read"}, {"texturememoryreadbwbytessecond", "texture_memory_read"},
        {"vertexmemoryreadbytessec", "vertex_memory_read"}, {"vertexmemoryreadbytessecond", "vertex_memory_read"},
        {"spmemoryreadbytessec", "sp_memory_read"}, {"spmemoryreadbytessecond", "sp_memory_read"}};
    const auto& semantics = profile == "precision" ? precision : bandwidth;
    const std::vector<std::string> required = profile == "precision" ? std::vector<std::string>{"fragment_full", "fragment_half"} :
        std::vector<std::string>{"memory_read_total", "memory_write_total"};
    const std::vector<std::string> optional = profile == "precision" ? std::vector<std::string>{"alu_capacity", "alu_working", "clock_rate", "shader_busy"} :
        std::vector<std::string>{"texture_memory_read", "vertex_memory_read", "sp_memory_read"};
    Json specs = Json::array(); std::set<std::string> found;
    for (const auto& source : descriptor.at("specs")) {
        const auto meaning = semantics.find(Canonical(source.at("name").get<std::string>()));
        if (meaning == semantics.end()) continue;
        if (!found.insert(meaning->second).second) throw std::runtime_error("Counter semantic mapping is ambiguous");
        auto spec = source; spec["semantic"] = meaning->second;
        spec["stage_scope"] = profile == "bandwidth" ? "memory/device aggregate" :
            meaning->second == "fragment_full" || meaning->second == "fragment_half" ? "fragment" : "shader/device aggregate";
        if (spec["stage_scope"] == "fragment" && spec.at("unit") != "INSTRUCTION/SECOND") throw std::runtime_error("Full/half fragment counter units are not instruction/second");
        if (profile == "bandwidth" && (spec.at("unit") != "BYTE/SECOND" || spec.at("numerator_units") != Json::array({"BYTE"}) ||
            spec.at("denominator_units") != Json::array({"SECOND"}))) throw std::runtime_error("Bandwidth counter units must be BYTE/SECOND");
        // Precision retains its existing all-selected-metrics sampling policy.
        spec["required"] = profile == "precision" || std::find(required.begin(), required.end(), meaning->second) != required.end();
        specs.push_back(std::move(spec));
    }
    for (const auto& metric : required) if (!found.count(metric)) {
        if (profile == "precision") throw std::runtime_error("Precision counter profile requires actual full and half fragment instruction metrics");
        throw std::runtime_error("Bandwidth counter profile requires actual Read Total and Write Total BYTE/SECOND metrics");
    }
    std::set<uint64_t> selected;
    for (const auto& spec : specs) if (!selected.insert(spec.at("id").get<uint64_t>()).second)
        throw std::runtime_error("Counter ID mapping is ambiguous");
    for (const auto& block : descriptor.at("blocks")) {
        const auto capacity = block.at("capacity").get<uint64_t>(); size_t requested = 0;
        for (const auto& id : block.at("counter_ids")) if (selected.count(id.get<uint64_t>())) ++requested;
        if (capacity && requested > capacity) throw std::runtime_error("Selected " + profile + " counters exceed a provider block capacity; multipass automation is not supported");
    }
    Json missing = Json::array(); for (const auto& metric : optional) if (!found.count(metric)) missing.push_back(metric);
    Json result = descriptor; result["specs"] = std::move(specs); result["period_ns"] = periodNs; result["profile"] = profile;
    result["required_metrics"] = required; result["optional_metrics"] = optional; result["missing_optional_metrics"] = missing;
    if (profile == "bandwidth") result["measurement_semantics"] = "Provider-labelled sampled memory byte rates filtered to a GPU interval; device-wide averaging may cross boundaries, not exact bytes per frame";
    return result;
}
Json SelectGpuPrecisionCounters(const Json& service, uint64_t periodNs) {
    return SelectGpuCounters(service, periodNs, "precision");
}

struct AndroidGpuCounters::Impl {
    AdbConnection adb;
    std::filesystem::path output, helper;
    const std::atomic_bool* cancel;
    std::string tag, remoteHelper, remoteHelperLog, remoteHelperPid, remoteTrace, remoteTracePid;
    uint64_t helperPid = 0, tracePid = 0;
    bool reusedProvider = false, started = false, stopped = false;
    bool helperUploaded = false, helperLaunchAttempted = false, traceLaunchAttempted = false;
    Json selection;
    Impl(std::string executable, std::string serial, std::filesystem::path directory, std::filesystem::path helperPath, const std::atomic_bool* cancellation)
        : adb{std::move(executable), std::move(serial), cancellation}, output(std::move(directory)), helper(std::move(helperPath)), cancel(cancellation) {
        std::filesystem::create_directories(output);
        tag = "gfxrecon_perf_" + Sha256Text(output.u8string() + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())).substr(0, 24);
        remoteHelper = "/data/local/tmp/" + tag + "_producer"; remoteHelperLog = remoteHelper + ".log";
        remoteHelperPid = remoteHelper + ".pid";
        remoteTrace = "/data/misc/perfetto-traces/" + tag + ".perfetto-trace";
        remoteTracePid = "/data/local/tmp/" + tag + "_trace.pid";
    }
    ProcessResult Command(std::vector<std::string> arguments, const std::filesystem::path& input = {}, const std::filesystem::path& destination = {}, bool cleanup = false, std::chrono::milliseconds timeout = std::chrono::seconds(15)) {
        if (!cleanup) Cancelled(cancel);
        arguments.insert(arguments.begin(), {"-s", adb.serial}); ProcessRequest process{adb.executable, std::move(arguments)};
        process.allowChildServices = true;
        process.stdinFile = input; process.stdoutFile = destination; process.timeout = timeout;
        const auto result = RunProcess(process, cleanup ? nullptr : cancel);
        if (result.cancelled) throw std::runtime_error("GPU counter collection cancelled");
        if (!result.Success()) throw std::runtime_error("GPU counter command failed: " + result.output + (result.timedOut ? " (timeout)" : ""));
        return result;
    }
    Json Query(const std::string& name) {
        const auto file = output / name; Command({"exec-out", "perfetto", "--query-raw"}, {}, file);
        auto state = DecodeGpuCounterServiceState(ReadBytes(file)); AtomicJson(output / (name + ".json"), state); return state;
    }
    std::string Shell(const std::vector<std::string>& words, bool cleanup = false) {
        return Command({"shell", AndroidCommand(words)}, {}, {}, cleanup,
            cleanup ? std::chrono::seconds(1) : std::chrono::seconds(15)).output;
    }
    std::string SaveHelperDiagnostics() noexcept {
        if (!helperLaunchAttempted) return {};
        try {
            const auto log = Shell({"cat", remoteHelperLog}, true);
            std::ofstream file(output / "counter-producer.log", std::ios::binary | std::ios::trunc);
            file << log;
            return log.substr(0, 4096);
        } catch (...) { return {}; }
    }
    uint64_t SavedPid(const std::string& file) {
        try {
            const auto text = Shell({"cat", file}, true);
            const auto begin = text.find_first_not_of(" \r\n\t"), end = text.find_last_not_of(" \r\n\t");
            if (begin == std::string::npos) return 0;
            const auto token = text.substr(begin, end - begin + 1);
            if (token.empty() || !std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c); })) return 0;
            return std::stoull(token);
        } catch (...) { return 0; }
    }
    bool OwnsPid(uint64_t pid, const std::string& expected, bool cleanup) {
        if (!pid) return false;
        try {
            const auto value = Shell({"cat", "/proc/" + std::to_string(pid) + "/cmdline"}, cleanup);
            const auto first = value.substr(0, value.find('\0'));
            if (expected != "/system/bin/perfetto") return first == expected;
            // PID reuse must not turn ownership of one trace into permission to
            // stop a different Perfetto client. Our unique output argument is
            // present in the actual background client's command line.
            return (first == "perfetto" || first == "/system/bin/perfetto") &&
                value.find(std::string(1, '\0') + remoteTrace + std::string(1, '\0')) != std::string::npos;
        } catch (...) { return false; }
    }
    uint64_t FindTracePid(bool cleanup) {
        uint64_t found = 0;
        try {
            std::istringstream candidates(Shell({"pidof", "perfetto"}, cleanup));
            uint64_t pid;
            while (candidates >> pid) if (OwnsPid(pid, "/system/bin/perfetto", cleanup)) {
                if (found) throw std::runtime_error("Multiple clients claim the owned counter trace path");
                found = pid;
            }
        } catch (const std::exception&) { if (found) throw; }
        return found;
    }
    void StopOwned(uint64_t& pid, const std::string& expected) {
        if (!pid) return;
        if (!OwnsPid(pid, expected, true)) {
            bool exists = false;
            try { Shell({"test", "-d", "/proc/" + std::to_string(pid)}, true); exists = true; } catch (...) {}
            if (exists) throw std::runtime_error("Counter process identity changed or cannot be verified; refusing to signal it");
            pid = 0; return; // A finite-duration trace may already have completed.
        }
        if (OwnsPid(pid, expected, true)) {
            try { Shell({"kill", "-TERM", std::to_string(pid)}, true); } catch (...) {}
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < deadline && OwnsPid(pid, expected, true))
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (OwnsPid(pid, expected, true)) {
                // Trace clients get longer to flush; killing an unflushed client is
                // a failed trace, never a silently usable result.
                Shell({"kill", "-KILL", std::to_string(pid)}, true);
                pid = 0;
                if (expected == "/system/bin/perfetto") throw std::runtime_error("Owned Perfetto client failed to flush; partial trace is invalid");
            }
        }
        pid = 0;
    }
};

AndroidGpuCounters::AndroidGpuCounters(std::string adb, std::string serial, std::filesystem::path output, std::filesystem::path helper, const std::atomic_bool* cancel)
    : impl_(std::make_unique<Impl>(std::move(adb), std::move(serial), std::move(output), std::move(helper), cancel)) {}
AndroidGpuCounters::~AndroidGpuCounters() { Cleanup(); }

Json AndroidGpuCounters::Discover(const std::string& profile, uint64_t periodNs) {
    auto& state = *impl_; Cancelled(state.cancel);
    if (profile != "all" && profile != "precision" && profile != "bandwidth") throw std::runtime_error("Unknown GPU counter profile: " + profile);
    if (!periodNs || periodNs > 1000000000) throw std::runtime_error("Counter period must be in (0, 1 second]");
    auto service = state.Query("counter-service-before.pb");
    if (!service.value("sessions_supported", false)) throw std::runtime_error("Perfetto cannot establish tracing-session ownership on this device");
    if (service.value("active_sessions", uint64_t(0))) throw std::runtime_error("Device busy: another Perfetto tracing session is active");
    auto sources = service.at("sources");
    if (sources.empty()) {
        if (!std::filesystem::is_regular_file(state.helper)) throw std::runtime_error("OEM GPU counter producer helper is missing; configure gpu_counter_producer and build the Android helper (Adreno/Mali)");
        const auto digest = Sha256File(state.helper, state.cancel);
        std::ifstream capability(state.helper.u8string() + ".capabilities.json");
        if (!capability) throw std::runtime_error("Counter producer helper lacks an identity-bound capability file");
        const auto identity = Json::parse(capability);
        if (identity.value("gpu_counter_producer_protocol", 0) != 1 || identity.value("binary_sha256", "") != digest)
            throw std::runtime_error("Counter producer helper capability/hash mismatch");
        state.helperUploaded = true;
        state.Command({"push", state.helper.u8string(), state.remoteHelper});
        const auto remoteHash = state.Shell({"sha256sum", state.remoteHelper});
        if (remoteHash.substr(0, 64) != digest) throw std::runtime_error("Counter producer helper transfer hash mismatch");
        state.Shell({"chmod", "700", state.remoteHelper});
        state.helperLaunchAttempted = true;
        const auto launched = state.Shell({"sh", "-c", QuoteAndroidArgument(state.remoteHelper) + " > " + QuoteAndroidArgument(state.remoteHelperLog) +
            " 2>&1 & echo $! > " + QuoteAndroidArgument(state.remoteHelperPid) + "; cat " + QuoteAndroidArgument(state.remoteHelperPid)});
        std::smatch match; static const std::regex pid("(?:^|\\n)([0-9]+)\\s*(?:$|\\n)");
        if (!std::regex_search(launched, match, pid)) throw std::runtime_error("Counter producer helper did not return an owned PID");
        state.helperPid = std::stoull(match[1]);
        for (int attempt = 0; attempt < 40; ++attempt) {
            Cancelled(state.cancel);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            service = state.Query("counter-service-ready.pb"); sources = service.at("sources");
            if (!sources.empty()) break;
            if (!state.OwnsPid(state.helperPid, state.remoteHelper, false))
                throw std::runtime_error("OEM GPU counter producer helper exited before registration: " + state.SaveHelperDiagnostics());
        }
        if (sources.empty()) throw std::runtime_error("OEM GPU counter provider did not register: " + state.SaveHelperDiagnostics());
    } else state.reusedProvider = true;
    state.selection = SelectGpuCounters(service, periodNs, profile);
    state.selection["provider_reused"] = state.reusedProvider;
    state.selection["helper_sha256"] = state.helperPid ? Json(Sha256File(state.helper, state.cancel)) : Json(nullptr);
    state.selection["helper_watchdog_seconds"] = state.helperPid ? Json(900) : Json(nullptr);
    state.selection["semantics_verification"] = profile == "all" ? "provider-labelled counters; individual metric normalization and frame attribution unverified" : profile == "bandwidth" ?
        "provider-labelled sampled byte rates; render-only byte attribution not established" :
        "provider-labelled; controlled native FP16 calibration not established";
    AtomicJson(state.output / "counter-selection.json", state.selection); return state.selection;
}
Json AndroidGpuCounters::DiscoverPrecision(uint64_t periodNs) { return Discover("precision", periodNs); }

void AndroidGpuCounters::Start(uint64_t durationMs) {
    auto& state = *impl_; Cancelled(state.cancel);
    if (state.selection.is_null()) throw std::runtime_error("Counter discovery must complete before trace start");
    if (state.started || durationMs == 0 || durationMs > 600000) throw std::runtime_error("Invalid or duplicate counter trace start; duration must be at most ten minutes");
    const auto service = state.Query("counter-service-prestart.pb");
    if (service.value("active_sessions", uint64_t(0))) throw std::runtime_error("Device busy: another trace started before measurement");
    std::vector<uint8_t> buffer, gpu, config, dataSource, source, builtin;
    Number(builtin, 5, 6); Number(builtin, 6, 100); Number(builtin, 7, 1); Message(config, 20, builtin);
    Number(buffer, 1, 32768); Number(buffer, 4, 1); Message(config, 1, buffer); // RING_BUFFER, detect any loss during decode.
    Number(gpu, 1, state.selection.at("period_ns").get<uint64_t>());
    for (const auto& descriptor : state.selection.at("specs")) if(descriptor.value("collectable",true)) Number(gpu, 2, descriptor.at("id").get<uint64_t>());
    Text(source, 1, "gpu.counters"); Number(source, 2, 0); Message(source, 108, gpu); Message(dataSource, 1, source); Message(config, 2, dataSource);
    Number(config, 3, durationMs); Text(config, 22, state.tag);
    const auto path = state.output / "counter-trace-config.pb"; WriteBytes(path, config);
    // Some Windows adb shell stdin paths treat Ctrl-Z as EOF. The binary config
    // contains counter id 26 (0x1a), so send ASCII and decode on the device.
    const auto transport = state.output / "counter-trace-config.pb.base64"; WriteBytes(transport, Base64(config));
    state.traceLaunchAttempted = true;
    const auto script = "base64 -d | " + AndroidCommand({"perfetto", "--background-wait", "-c", "-", "-o", state.remoteTrace}) +
        " > " + QuoteAndroidArgument(state.remoteTracePid) + "; gpu_trace_status=$?; cat " + QuoteAndroidArgument(state.remoteTracePid) + "; exit $gpu_trace_status";
    const auto launched = state.Command({"shell", AndroidCommand({"sh", "-c", script})}, transport, {}, false, std::chrono::seconds(35)).output;
    AtomicJson(state.output / "counter-trace-ack.json", {{"output", launched}, {"start_acknowledged", true}});
    std::smatch match; static const std::regex pid("(?:^|\\n)([0-9]+)\\s*(?:$|\\n)");
    if (std::regex_search(launched, match, pid)) state.tracePid = std::stoull(match[1]);
    // Some Android builds acknowledge -D without printing the -d PID. Identify
    // only the client whose argv contains this session's unique output path.
    if (!state.tracePid || !state.OwnsPid(state.tracePid, "/system/bin/perfetto", false)) state.tracePid = state.FindTracePid(false);
    if (!state.tracePid) throw std::runtime_error("Perfetto acknowledged start but its uniquely owned client PID is unavailable");
    state.Shell({"sh", "-c", "printf '%s\\n' " + std::to_string(state.tracePid) + " > " + QuoteAndroidArgument(state.remoteTracePid)});
    state.started = true;
    const auto ready = state.Query("counter-service-started.pb");
    bool ownSession = false;
    for (const auto& session : ready.at("sessions")) if (session.value("name", "") == state.tag) ownSession = true;
    if (!ownSession) throw std::runtime_error("Started Perfetto session identity could not be confirmed");
    AtomicJson(state.output / "counter-trace-start.json", {{"session_name", state.tag}, {"pid", state.tracePid}, {"duration_ms", durationMs}, {"provider_start_acknowledged", true}});
}

Json AndroidGpuCounters::StopAndDecode(uint64_t beginBootNs, uint64_t endBootNs) {
    auto& state = *impl_; Cancelled(state.cancel);
    if (!state.started || state.stopped) throw std::runtime_error("Counter trace is not active");
    state.StopOwned(state.tracePid, "/system/bin/perfetto"); state.stopped = true;
    const auto tracePath = state.output / "gpu-counters.perfetto-trace";
    state.Command({"pull", state.remoteTrace, tracePath.u8string()}, {}, {}, false, std::chrono::seconds(30));
    auto decoded = DecodeGpuCounterTrace(ReadBytes(tracePath));
    AtomicJson(state.output / "gpu-counters-decoded.json", decoded);
    Json result;
    if (!beginBootNs && !endBootNs)
        result = {{"status","unavailable"},{"reason","GPU-to-counter clock window unavailable"},{"profile",state.selection.value("profile","precision")},{"counters",Json::array()}};
    else result = SummarizeGpuCounterWindow(decoded,state.selection,beginBootNs,endBootNs);
    result["trace_file"] = tracePath.u8string(); result["decoded_file"] = (state.output / "gpu-counters-decoded.json").u8string();
    result["provider"] = state.selection;
    AtomicJson(state.output / "gpu-counters.json",result);
    Cleanup(); return result;
}

void AndroidGpuCounters::Cleanup() noexcept {
    if (!impl_) return; auto& state = *impl_;
    if (state.traceLaunchAttempted && !state.tracePid && !state.stopped) state.tracePid = state.SavedPid(state.remoteTracePid);
    if (state.traceLaunchAttempted && !state.tracePid && !state.stopped) try { state.tracePid = state.FindTracePid(true); } catch (...) {}
    if (state.helperLaunchAttempted && !state.helperPid) state.helperPid = state.SavedPid(state.remoteHelperPid);
    try { state.StopOwned(state.tracePid, "/system/bin/perfetto"); } catch (...) {}
    try { state.StopOwned(state.helperPid, state.remoteHelper); } catch (...) {}
    state.SaveHelperDiagnostics();
    // Preserve a partial trace when cancellation/protocol failure interrupted
    // normal readback. It remains diagnostic data, never a successful result.
    std::error_code ignored;
    if (state.traceLaunchAttempted && !std::filesystem::is_regular_file(state.output / "gpu-counters.perfetto-trace", ignored))
        try { state.Command({"pull", state.remoteTrace, (state.output / "gpu-counters.partial.perfetto-trace").u8string()}, {}, {}, true, std::chrono::seconds(5)); } catch (...) {}
    if (state.helperUploaded) for (const auto& path : {state.remoteHelper, state.remoteHelperLog, state.remoteHelperPid}) try { state.Shell({"rm", "-f", path}, true); } catch (...) {}
    if (state.traceLaunchAttempted) for (const auto& path : {state.remoteTrace, state.remoteTracePid}) try { state.Shell({"rm", "-f", path}, true); } catch (...) {}
    state.helperUploaded = state.helperLaunchAttempted = state.traceLaunchAttempted = false;
}
}
