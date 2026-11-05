#include "replay/gpu_counters.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace gfx::tests {
namespace {
using Buffer = std::vector<uint8_t>;
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void Fails(F function, const char* reason) {
    try { function(); } catch (const std::exception& error) {
        Require(std::string(error.what()).find(reason) != std::string::npos, error.what()); return;
    }
    throw std::runtime_error(std::string("Counter test accepted invalid evidence: ") + reason);
}
const Json& Counter(const Json& result, uint64_t id) {
    const auto& counters = result.at("counters");
    const auto found = std::find_if(counters.begin(), counters.end(), [&](const Json& row) { return row.at("id") == id; });
    if (found == counters.end()) throw std::runtime_error("Selected counter row disappeared from the report");
    return *found;
}
bool QualityContains(const Json& counter, const char* text) {
    return std::any_of(counter.at("quality").begin(), counter.at("quality").end(), [&](const Json& value) {
        return value.is_string() && value.get_ref<const std::string&>().find(text) != std::string::npos;
    });
}
void Varint(Buffer& output, uint64_t number) { while (number > 127) { output.push_back(uint8_t(number) | 128); number >>= 7; } output.push_back(uint8_t(number)); }
void Number(Buffer& output, uint32_t id, uint64_t number) { Varint(output, uint64_t(id) << 3); Varint(output, number); }
void Message(Buffer& output, uint32_t id, const Buffer& bytes) { Varint(output, (uint64_t(id) << 3) | 2); Varint(output, bytes.size()); output.insert(output.end(), bytes.begin(), bytes.end()); }
void Text(Buffer& output, uint32_t id, const std::string& value) { Message(output, id, {value.begin(), value.end()}); }
Buffer Descriptor(bool half = true) {
    Buffer descriptor;
    for (uint32_t id : {26u, 132u}) {
        if (id == 132 && !half) continue;
        Buffer spec; Number(spec, 1, id); Text(spec, 2, id == 26 ? "Fragment ALU Instructions / Sec (Full)" : "Fragment ALU Instructions / Sec (Half)");
        Text(spec, 3, "Provider-labelled scalar fragment instruction rate."); Number(spec, 7, 40); Number(spec, 8, 22); Message(descriptor, 1, spec);
    }
    Number(descriptor, 3, 1000000); Number(descriptor, 5, 0); return descriptor;
}
Buffer Service(const Buffer& descriptor = Descriptor()) {
    Buffer source, row, service; Text(source, 1, "gpu.counters"); Message(source, 5, descriptor); Message(row, 1, source); Number(row, 2, 17);
    Message(service, 2, row); Number(service, 4, 0); Number(service, 7, 1); Text(service, 5, "fixture-v49"); return service;
}
void Packet(Buffer& trace, uint64_t timestamp, const Buffer& event, uint32_t clock = 6, bool dropped = false) {
    Buffer packet; Number(packet, 10, 17); Number(packet, 8, timestamp); Number(packet, 58, clock); if (dropped) Number(packet, 42, 1); Message(packet, 52, event); Message(trace, 1, packet);
}
Buffer Value(uint32_t id, double value) {
    Buffer result; Number(result, 1, id); Varint(result, (3u << 3) | 1); uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits)); for (unsigned i = 0; i < 8; ++i) result.push_back(uint8_t(bits >> (i * 8))); return result;
}
Buffer Trace(uint32_t clock = 6, bool missingHalf = false, bool dropped = false) {
    Buffer trace, snapshot, boot, clockPacket, descriptors;
    Number(boot, 1, 6); Number(boot, 2, 1000000000); Message(snapshot, 1, boot); Number(snapshot, 2, 6); Message(clockPacket, 6, snapshot); Message(trace, 1, clockPacket);
    // The service flags every sequence's first packet as previous_packet_dropped;
    // this initial marker must not be mistaken for real data loss.
    Message(descriptors, 1, Descriptor()); Packet(trace, 0, descriptors, 6, true);
    for (uint64_t index = 0; index < 4; ++index) {
        Buffer event; Message(event, 2, Value(26, index == 0 ? 0 : double(index * 10)));
        if (!missingHalf) Message(event, 2, Value(132, 0)); // Valid zero activity must stay usable.
        Packet(trace, 1000000000 + index * 1000000, event, clock, dropped && index == 2);
    }
    return trace;
}
const uint32_t BandwidthIds[]{5001, 9002, 4003, 7004, 6005}; // Deliberately unrelated to OEM catalog IDs.
Buffer BandwidthDescriptor(bool optional = true) {
    const char* names[]{"Read Total (Bytes/sec)", "Write Total (Bytes/sec)", "Texture Memory Read BW (Bytes/Second)",
        "Vertex Memory Read (Bytes/Second)", "SP Memory Read (Bytes/Second)"};
    Buffer descriptor;
    for (unsigned i = 0; i < (optional ? 5u : 2u); ++i) {
        Buffer spec; Number(spec, 1, BandwidthIds[i]); Text(spec, 2, names[i]); Text(spec, 3, "Provider-labelled device memory byte rate.");
        Number(spec, 7, 7); Number(spec, 8, 22); Message(descriptor, 1, spec); // BYTE / SECOND.
    }
    Number(descriptor, 3, 1000000); Number(descriptor, 4, 10000000); return descriptor;
}
Buffer BandwidthTrace(bool optionalSamples = true, bool omitRead = false) {
    Buffer trace, snapshot, boot, clockPacket, descriptors;
    Number(boot, 1, 6); Number(boot, 2, 1000000000); Message(snapshot, 1, boot); Number(snapshot, 2, 6);
    Message(clockPacket, 6, snapshot); Message(trace, 1, clockPacket);
    Message(descriptors, 1, BandwidthDescriptor()); Packet(trace, 0, descriptors, 6, true);
    for (uint64_t i = 0; i < 4; ++i) {
        Buffer event;
        for (unsigned metric = 0; metric < (optionalSamples ? 5u : 2u); ++metric) {
            if (omitRead && metric == 0) continue;
            Message(event, 2, Value(BandwidthIds[metric], metric == 0 ? 0 : double((i + 1) * metric * 1000)));
        }
        Packet(trace, 1000000000 + i * 1000000, event);
    }
    return trace;
}
}

void GpuCounterTests(const std::filesystem::path& root) {
    const auto cancelledOutput = root / "counter-cancelled";
    {
        std::atomic_bool cancel{true};
        AndroidGpuCounters cancelled("this-adb-must-never-launch", "fixture", cancelledOutput, {}, &cancel);
        Fails([&] { cancelled.DiscoverPrecision(); }, "cancelled");
    }
    std::filesystem::remove(cancelledOutput);
    const auto state = DecodeGpuCounterServiceState(Service());
    Require(state["sources"].size() == 1 && state["sessions_supported"] == true, "GPU counter service descriptor decode failed");
    const auto selection = SelectGpuPrecisionCounters(state, 1000000);
    auto catalog=state;auto& specs=catalog["sources"][0]["specs"];
    auto unknown=specs[0];unknown["id"]=700;unknown["name"]="Vendor metric with no known semantic mapping";
    unknown["unit"]="CELSIUS";unknown["numerator_units"]=Json::array({"CELSIUS"});unknown["denominator_units"]=Json::array();
    specs.push_back(unknown);
    auto all=SelectGpuCounters(catalog,1000000,"all");
    Require(all["specs"].size()==3&&all["selected_counter_count"]==3,"All counters excluded an unfamiliar provider metric");
    auto signedTrace=DecodeGpuCounterTrace(Trace());signedTrace["descriptor"]["specs"].push_back(unknown);
    signedTrace["samples"].push_back({{"counter_id",700},{"timestamp_boot_ns",1000000000ull},{"value",-5.0}});
    Require(Counter(SummarizeGpuCounterWindow(signedTrace,all,1000000000,1003000000),700)["summary"]["mean"]==-5,
        "Signed vendor metrics were discarded by assumptions about rate counters");
    auto catalogWithoutHalf=catalog;catalogWithoutHalf["sources"][0]["specs"].erase(1);
    Require(SelectGpuCounters(catalogWithoutHalf,1000000,"all")["specs"].size()==2,"All counter collection still requires a half-precision metric");
    catalog["sources"][0]["blocks"]=Json::array({{{"id",1},{"capacity",1},{"counter_ids",Json::array({26,132})}}});
    all=SelectGpuCounters(catalog,1000000,"all");
    Require(all["selected_counter_count"]==2&&all["advertised_counter_count"]==3&&all["specs"][1]["collectable"]==false&&all["specs"][1].contains("unavailable_reason"),
        "Provider block capacity hid counters or prevented collection of a supported subset");
    const auto limited=SummarizeGpuCounterWindow(DecodeGpuCounterTrace(Trace()),all,1000000000,1003000000);
    Require(Counter(limited,132)["summary"]["mean"].is_null()&&Counter(limited,132)["reason"].get<std::string>().find("capacity")!=std::string::npos,
        "A hardware-limited counter fabricated values or lost its reason");
    auto changedDefinition=DecodeGpuCounterTrace(Trace());changedDefinition["descriptor"]["specs"][0]["description"]="Changed normalization";
    const auto partial=SummarizeGpuCounterWindow(changedDefinition,SelectGpuCounters(state,1000000,"all"),1000000000,1003000000);
    Require(Counter(partial,26)["status"]=="unavailable"&&Counter(partial,26).contains("trace_definition")&&Counter(partial,132)["status"]=="available",
        "One changed provider metric invalidated other counters or lost its actual descriptor evidence");
    Require(selection["specs"].size() == 2 && selection["specs"][0]["unit"] == "INSTRUCTION/SECOND", "Precision profile lost instruction units");
    Fails([&] { SelectGpuPrecisionCounters(state, 999999); }, "supported range");
    Fails([&] { SelectGpuPrecisionCounters(DecodeGpuCounterServiceState(Service(Descriptor(false))), 1000000); }, "full and half");
    auto badUnits = state; badUnits["sources"][0]["specs"][0]["unit"] = "PERCENT";
    Fails([&] { SelectGpuPrecisionCounters(badUnits, 1000000); }, "units");
    auto capacity = state; capacity["sources"][0]["blocks"] = Json::array({{{"capacity", 1}, {"counter_ids", {26, 132}}}});
    Fails([&] { SelectGpuPrecisionCounters(capacity, 1000000); }, "capacity");
    Require(SelectGpuCounters(state, 1000000, "precision") == selection, "Generic precision selector differs from compatibility wrapper");
    Fails([&] { SelectGpuCounters(state, 1000000, "precision+bandwidth"); }, "Unknown");
    const auto bandwidthState = DecodeGpuCounterServiceState(Service(BandwidthDescriptor()));
    const auto bandwidth = SelectGpuCounters(bandwidthState, 1000000, "bandwidth");
    Require(bandwidth["profile"] == "bandwidth" && bandwidth["specs"].size() == 5 && bandwidth["missing_optional_metrics"].empty(),
        "Complete bandwidth profile selection failed without precision metrics");
    Require(bandwidth["specs"][0]["id"] == BandwidthIds[0] && bandwidth["specs"][0]["semantic"] == "memory_read_total" &&
        bandwidth["specs"][0]["required"] == true && bandwidth["specs"][2]["required"] == false,
        "Bandwidth selection hardcoded counter IDs or lost required/optional policy");
    for (const auto& metric : bandwidth["specs"]) Require(metric["unit"] == "BYTE/SECOND", "Bandwidth metric lost strict byte rate units");
    Fails([&] { SelectGpuPrecisionCounters(bandwidthState, 1000000); }, "full and half");
    Fails([&] { SelectGpuCounters(state, 1000000, "bandwidth"); }, "Read Total and Write Total");
    Fails([&] { SelectGpuCounters(bandwidthState, 999999, "bandwidth"); }, "supported range");
    Fails([&] { SelectGpuCounters(bandwidthState, 10000001, "bandwidth"); }, "supported range");
    Fails([&] { SelectGpuCounters(bandwidthState, 0, "bandwidth"); }, "Counter period");
    const auto bandwidthMinimal = SelectGpuCounters(DecodeGpuCounterServiceState(Service(BandwidthDescriptor(false))), 1000000, "bandwidth");
    Require(bandwidthMinimal["specs"].size() == 2 && bandwidthMinimal["missing_optional_metrics"] ==
        Json::array({"texture_memory_read", "vertex_memory_read", "sp_memory_read"}), "Missing optional bandwidth metrics were hidden");
    auto aliases = bandwidthState;
    aliases["sources"][0]["specs"][0]["name"] = "READ TOTAL (bytes / second)";
    Require(SelectGpuCounters(aliases, 1000000, "bandwidth")["specs"][0]["semantic"] == "memory_read_total",
        "Bandwidth canonical names rejected equivalent case/spacing/sec alias");
    auto ambiguous = bandwidthState; auto alias = ambiguous["sources"][0]["specs"][0]; alias["id"] = 8090;
    alias["name"] = "Read Total (Bytes/Second)"; ambiguous["sources"][0]["specs"].push_back(alias);
    Fails([&] { SelectGpuCounters(ambiguous, 1000000, "bandwidth"); }, "ambiguous");
    auto duplicateId = bandwidthState; duplicateId["sources"][0]["specs"][1]["id"] = BandwidthIds[0];
    Fails([&] { SelectGpuCounters(duplicateId, 1000000, "bandwidth"); }, "ambiguous");
    auto multipleProviders = bandwidthState; multipleProviders["sources"].push_back(multipleProviders["sources"][0]);
    Fails([&] { SelectGpuCounters(multipleProviders, 1000000, "bandwidth"); }, "ambiguous");
    auto bandwidthBadUnits = bandwidthState; bandwidthBadUnits["sources"][0]["specs"][0]["unit"] = "KILOBYTE/SECOND";
    Fails([&] { SelectGpuCounters(bandwidthBadUnits, 1000000, "bandwidth"); }, "units");
    bandwidthBadUnits = bandwidthState; bandwidthBadUnits["sources"][0]["specs"][2]["numerator_units"] = Json::array({"BIT"});
    Fails([&] { SelectGpuCounters(bandwidthBadUnits, 1000000, "bandwidth"); }, "units");
    auto bandwidthCapacity = bandwidthState;
    bandwidthCapacity["sources"][0]["blocks"] = Json::array({{{"capacity", 4}, {"counter_ids", {5001, 9002, 4003, 7004, 6005}}}});
    Fails([&] { SelectGpuCounters(bandwidthCapacity, 1000000, "bandwidth"); }, "capacity");
    const auto bandwidthTrace = DecodeGpuCounterTrace(BandwidthTrace());
    const auto bandwidthSummary = SummarizeGpuCounterWindow(bandwidthTrace, bandwidth, 1000000000, 1003000000);
    Require(bandwidthSummary["profile"] == "bandwidth" && bandwidthSummary["counters"].size() == 5 &&
        Counter(bandwidthSummary, BandwidthIds[0])["summary"]["sampled_mean"] == 0 &&
        Counter(bandwidthSummary, BandwidthIds[1])["summary"]["sampled_mean"] == 2500,
        "Bandwidth samples or valid zero rates were misreported");
    Require(Counter(bandwidthSummary, BandwidthIds[0]).contains("bandwidth_semantics") &&
        !Counter(bandwidthSummary, BandwidthIds[0]).contains("precision_semantics") &&
        bandwidthSummary["scope"].get<std::string>().find("selected GPU interval") != std::string::npos,
        "Bandwidth rate semantics did not identify the selected GPU sample interval");
    const auto optionalMissing = SummarizeGpuCounterWindow(DecodeGpuCounterTrace(BandwidthTrace(false)), bandwidth, 1000000000, 1003000000);
    const auto& absentOptional = Counter(optionalMissing, BandwidthIds[2]);
    Require(optionalMissing["counters"].size() == 5 && optionalMissing["unavailable_metrics"].size() == 3 &&
        absentOptional["status"] == "unavailable" && absentOptional["samples"].empty() && absentOptional["summary"]["mean"].is_null(),
        "Absent optional samples were dropped or fabricated as zero values");
    auto optionalUncovered = bandwidthTrace;
    for (auto& sample : optionalUncovered["samples"]) if (sample["counter_id"].get<uint64_t>() == BandwidthIds[2])
        sample["timestamp_boot_ns"] = sample["timestamp_boot_ns"].get<uint64_t>() - 100000000;
    const auto uncoveredResult = SummarizeGpuCounterWindow(optionalUncovered, bandwidth, 1000000000, 1003000000);
    const auto& uncoveredRate = Counter(uncoveredResult, BandwidthIds[2]);
    Require(uncoveredResult["counters"].size() == 5 && uncoveredRate["status"] == "unavailable" &&
        uncoveredRate["raw_sample_count"] == 4 && uncoveredRate["samples"].empty() && uncoveredRate["summary"]["sample_count"] == 0 &&
        uncoveredRate["summary"]["mean"].is_null(), "Outside-window optional points entered the plotted samples or became a zero mean");
    auto optionalLowCoverage = bandwidthTrace;
    auto& sparse = optionalLowCoverage["samples"];
    sparse.erase(std::remove_if(sparse.begin(), sparse.end(), [](const Json& sample) {
        return sample["counter_id"].get<uint64_t>() == BandwidthIds[2] && sample["timestamp_boot_ns"].get<uint64_t>() >= 1002000000;
    }), sparse.end());
    const auto lowCoverageResult = SummarizeGpuCounterWindow(optionalLowCoverage, bandwidth, 1000000000, 1003000000);
    const auto& lowCoverageRate = Counter(lowCoverageResult, BandwidthIds[2]);
    Require(lowCoverageResult["counters"].size() == 5 && lowCoverageRate["summary"]["coverage_estimate"].get<double>() < .8 &&
        lowCoverageRate["summary"]["sample_count"] == 2 && lowCoverageRate["samples"].size() == 2 &&
        lowCoverageRate["summary"]["mean"] == 3000 && QualityContains(lowCoverageRate, "Low"),
        "Low interval coverage discarded actual samples or omitted their reference-quality disclosure");
    auto absentOptionalDefinition = bandwidthTrace;
    auto& emitted = absentOptionalDefinition["descriptor"]["specs"];
    emitted.erase(std::remove_if(emitted.begin(), emitted.end(), [](const Json& spec) { return spec["id"].get<uint64_t>() == BandwidthIds[2]; }), emitted.end());
    const auto missingDefinitionResult = SummarizeGpuCounterWindow(absentOptionalDefinition, bandwidth, 1000000000, 1003000000);
    Require(missingDefinitionResult["counters"].size() == 5 && missingDefinitionResult["unavailable_metrics"].size() == 1 &&
        Counter(missingDefinitionResult, BandwidthIds[2])["status"] == "unavailable" &&
        Counter(missingDefinitionResult, BandwidthIds[2])["reason"].get<std::string>().find("descriptor") != std::string::npos &&
        Counter(missingDefinitionResult, BandwidthIds[2])["summary"]["mean"].is_null(),
        "Absent optional descriptor erased the row or reported unverified values");
    auto absentRequiredDefinition = bandwidthTrace;
    auto& requiredDefinitions = absentRequiredDefinition["descriptor"]["specs"];
    requiredDefinitions.erase(std::remove_if(requiredDefinitions.begin(), requiredDefinitions.end(), [](const Json& spec) { return spec["id"].get<uint64_t>() == BandwidthIds[0]; }), requiredDefinitions.end());
    const auto missingRequired = SummarizeGpuCounterWindow(absentRequiredDefinition, bandwidth, 1000000000, 1003000000);
    Require(missingRequired["status"] == "complete" && Counter(missingRequired, BandwidthIds[0])["status"] == "unavailable" &&
        Counter(missingRequired, BandwidthIds[0])["samples"].empty() && Counter(missingRequired, BandwidthIds[0])["summary"]["mean"].is_null() &&
        Counter(missingRequired, BandwidthIds[1])["summary"]["mean"] == 2500,
        "Missing requested descriptor invalidated other samples or fabricated its mean");
    const auto minimalSummary = SummarizeGpuCounterWindow(bandwidthTrace, bandwidthMinimal, 1000000000, 1003000000);
    Require(minimalSummary["missing_optional_metrics"].size() == 3, "Missing discovered bandwidth optionals disappeared from provenance");
    const auto missingRead = SummarizeGpuCounterWindow(DecodeGpuCounterTrace(BandwidthTrace(false, true)), bandwidth, 1000000000, 1003000000);
    Require(Counter(missingRead, BandwidthIds[0])["status"] == "unavailable" && Counter(missingRead, BandwidthIds[0])["summary"]["mean"].is_null() &&
        Counter(missingRead, BandwidthIds[1])["status"] == "available", "A missing requested rate invalidated the available GPU-counter reference data");
    const auto bandwidthSingle = SummarizeGpuCounterWindow(bandwidthTrace, bandwidth, 1002500000, 1003500000);
    Require(Counter(bandwidthSingle, BandwidthIds[0])["status"] == "sparse" && Counter(bandwidthSingle, BandwidthIds[0])["summary"]["mean"] == 0 &&
        Counter(bandwidthSingle, BandwidthIds[1])["summary"]["mean"] == 4000 &&
        Counter(bandwidthSingle, BandwidthIds[1])["samples"][0]["time_from_gpu_start_ms"] == .5,
        "Single real bandwidth observation was rejected, expanded, or confused with no sample");
    Fails([&] { DecodeGpuCounterServiceState({0x80}); }, "truncated");
    Fails([&] { DecodeGpuCounterServiceState({0x12, 0x10, 0x01}); }, "truncated");

    const auto decoded = DecodeGpuCounterTrace(Trace());
    const auto summarized = SummarizeGpuCounterWindow(decoded, selection, 1000000000, 1003000000);
    Require(summarized["counters"].size() == 2 && summarized["counters"][0]["summary"]["sample_count"] == 4, "Measured counter samples missing");
    Require(summarized["counters"][0]["summary"]["mean"] == 15.0 && summarized["counters"][1]["summary"]["mean"] == 0,
        "Valid zero or sampled mean was changed");
    Require(summarized["counters"][0]["summary"]["time_weighted_mean_estimate"] == 20.0,
        "Backward-adjacent interval estimate is wrong");
    Require(summarized["clock_domain"] == "BOOTTIME" && summarized["scope"].get<std::string>().find("selected GPU interval") != std::string::npos &&
        summarized["counters"][0]["samples"][0]["time_from_gpu_start_ms"] == 0 &&
        summarized["counters"][0]["samples"][3]["time_from_gpu_start_ms"] == 3,
        "GPU interval scope, clock domain or frame-relative millisecond points were lost");
    Fails([&] { DecodeGpuCounterTrace(Trace(3)); }, "BOOTTIME");
    Fails([&] { DecodeGpuCounterTrace(Trace(6, false, true)); }, "packet loss");
    const auto noHalf = SummarizeGpuCounterWindow(DecodeGpuCounterTrace(Trace(6, true)), selection, 1000000000, 1003000000);
    Require(Counter(noHalf, 132)["status"] == "unavailable" && Counter(noHalf, 132)["summary"]["mean"].is_null() &&
        Counter(summarized, 132)["status"] == "available" && Counter(summarized, 132)["summary"]["mean"] == 0,
        "No precision sample was confused with a real zero-valued sample");
    const auto onePoint = SummarizeGpuCounterWindow(decoded, selection, 1002500000, 1003000000);
    Require(Counter(onePoint, 26)["status"] == "sparse" && Counter(onePoint, 26)["samples"].size() == 1 &&
        Counter(onePoint, 26)["summary"]["mean"] == 30 && Counter(onePoint, 26)["summary"]["coverage_estimate"] == 0 &&
        Counter(onePoint, 26)["summary"]["time_weighted_mean_estimate"].is_null() && QualityContains(Counter(onePoint, 26), "Only one"),
        "A single precision observation lost its real value or invented interval coverage");
    const auto emptyWindow = SummarizeGpuCounterWindow(decoded, selection, 1000250000, 1000750000);
    Require(Counter(emptyWindow, 26)["status"] == "unavailable" && Counter(emptyWindow, 26)["raw_sample_count"] == 4 &&
        Counter(emptyWindow, 26)["samples"].empty() && Counter(emptyWindow, 26)["summary"]["mean"].is_null() &&
        Counter(emptyWindow, 26)["summary"]["coverage_estimate"] == 0,
        "A GPU interval between sample timestamps fabricated a value or retained outside points");
    auto poisoned = decoded;
    for (auto& point : poisoned["samples"]) if (point["counter_id"] == 26 &&
        (point["timestamp_boot_ns"] == 1000000000 || point["timestamp_boot_ns"] == 1003000000)) point["value"] = 1e90;
    const auto narrow = SummarizeGpuCounterWindow(poisoned, selection, 1000500000, 1002500000);
    const auto& narrowCounter = Counter(narrow, 26);
    Require(narrowCounter["samples"].size() == 2 && narrowCounter["raw_sample_count"] == 4 && narrowCounter["summary"]["mean"] == 15 &&
        narrowCounter["summary"]["time_weighted_mean_estimate"] == 20 && narrowCounter["summary"]["coverage_estimate"] == .5 &&
        narrowCounter["samples"][0]["time_from_gpu_start_ms"] == .5 && narrowCounter["samples"][1]["time_from_gpu_start_ms"] == 1.5,
        "Outside-window poison leaked into sample means, weighted estimates, points or coverage");
    auto noDefinitions = decoded; noDefinitions["descriptor"]["specs"] = Json::array();
    const auto unavailableDefinitions = SummarizeGpuCounterWindow(noDefinitions, selection, 1000000000, 1003000000);
    Require(unavailableDefinitions["counters"].size() == 2 && unavailableDefinitions["unavailable_metrics"].size() == 2 &&
        Counter(unavailableDefinitions, 26)["summary"]["mean"].is_null() && Counter(unavailableDefinitions, 132)["samples"].empty(),
        "Empty emitted descriptors erased selected rows or accepted samples with unverified meaning");
    auto wrapperDifference = decoded; wrapperDifference["descriptor"]["descriptor_sha256"] = "different-wrapper";
    Require(SummarizeGpuCounterWindow(wrapperDifference, selection, 1000000000, 1003000000)["trace_descriptor_sha256"] == "different-wrapper",
        "Harmless query/trace wrapper differences were not preserved independently");
    auto stale = selection; stale["specs"][0]["description"] = "different metric meaning";
    Fails([&] { SummarizeGpuCounterWindow(decoded, stale, 1000000000, 1003000000); }, "definitions differ");
    auto duplicates = decoded; duplicates["samples"].push_back(decoded["samples"][0]);
    Fails([&] { SummarizeGpuCounterWindow(duplicates, selection, 1000000000, 1003000000); }, "duplicate");
    auto negative = decoded; negative["samples"][0]["value"] = -1;
    Fails([&] { SummarizeGpuCounterWindow(negative, selection, 1000000000, 1003000000); }, "negative");
    auto nonfinite = decoded; nonfinite["samples"][0]["value"] = std::numeric_limits<double>::infinity();
    Fails([&] { SummarizeGpuCounterWindow(nonfinite, selection, 1000000000, 1003000000); }, "nonfinite");
    nonfinite["samples"][0]["value"] = std::numeric_limits<double>::quiet_NaN();
    Fails([&] { SummarizeGpuCounterWindow(nonfinite, selection, 1000000000, 1003000000); }, "nonfinite");
    auto badClock = decoded; badClock["clock_domain"] = "MONOTONIC";
    Fails([&] { SummarizeGpuCounterWindow(badClock, selection, 1000000000, 1003000000); }, "BOOTTIME");
    auto packetLoss = decoded; packetLoss["packet_loss"] = true;
    Fails([&] { SummarizeGpuCounterWindow(packetLoss, selection, 1000000000, 1003000000); }, "lost samples");
    auto gap = decoded;
    for (auto& row : gap["samples"]) if (row["timestamp_boot_ns"].get<uint64_t>() >= 1002000000) row["timestamp_boot_ns"] = row["timestamp_boot_ns"].get<uint64_t>() + 10000000;
    const auto gapResult = SummarizeGpuCounterWindow(gap, selection, 1000000000, 1013000000);
    Require(gapResult["counters"][0]["summary"]["coverage_estimate"].get<double>() < .2 &&
        gapResult["counters"][0]["summary"]["interval_ns"]["max"] == 11000000 && QualityContains(gapResult["counters"][0], "Low"),
        "A long missing interval was interpolated as full coverage or lost its quality/gap evidence");
}
}
