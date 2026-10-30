#include "analysis/geometry_summary.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <limits>
#include <set>

namespace gfx {
namespace {
uint64_t Number(const Json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number_integer() || j[key] < 0) throw std::runtime_error(std::string("Missing argument: ") + key);
    return j[key].get<uint64_t>();
}
uint64_t Add(uint64_t a, uint64_t b) {
    if (b > UINT64_MAX - a) throw std::runtime_error("Geometry count overflow"); return a + b;
}
uint64_t Multiply(uint64_t a, uint64_t b) {
    if (b && a > UINT64_MAX / b) throw std::runtime_error("Geometry count overflow"); return a * b;
}
unsigned Topology(const Json& v) {
    const char* names[] = {"POINT_LIST", "LINE_LIST", "LINE_STRIP", "TRIANGLE_LIST", "TRIANGLE_STRIP", "TRIANGLE_FAN"};
    if (v.is_number_unsigned() || (v.is_number_integer() && v >= 0)) { auto n = v.get<uint64_t>(); if (n <= 5) return unsigned(n); }
    for (unsigned i = 0; i < 6; ++i) if (v == std::string("VK_PRIMITIVE_TOPOLOGY_") + names[i]) return i;
    throw std::runtime_error("Unsupported input-assembly topology (adjacency, patches or missing state)");
}
Json BufferAvailability(const Json& draw, const std::filesystem::path& directory) {
    std::set<std::string> names;
    std::function<void(const Json&, unsigned)> walk = [&](const Json& j, unsigned depth) {
        if (depth > 32) throw std::runtime_error("Buffer metadata exceeds nesting limit");
        if (j.is_object() && j.contains("file") && j["file"].is_string()) {
            const auto file = std::filesystem::u8path(j["file"].get<std::string>()).filename();
            if (file.extension() == ".bin") names.insert(file.u8string());
        }
        if (j.is_structured()) for (const auto& child : j) if (child.is_structured()) walk(child, depth + 1);
    };
    walk(draw, 0);
    Json available = Json::array(), missing = Json::array();
    for (const auto& name : names) {
        const auto file = directory / std::filesystem::u8path(name);
        (std::filesystem::is_regular_file(file) && !std::filesystem::is_symlink(file) ? available : missing).push_back(name);
    }
    return {{"status", names.empty() ? "not_requested_or_not_required" : missing.empty() ? "available" : "incomplete"},
        {"referenced", names.size()}, {"available", available}, {"missing", missing}};
}
}
Json SummarizeGeometry(const std::string& command, const Json& pipeline, const Json& parameters,
                       const Json& draw, const std::filesystem::path& directory) {
    Json result{{"status", "unavailable"}, {"submitted_triangles", nullptr}, {"unique_vertices", nullptr},
        {"scope", "Input assembly before shader amplification; triangles include instances and degenerates. Unique vertices are adjusted IDs across active records, independent of instances."},
        {"records", Json::array()}, {"buffers", BufferAvailability(draw, directory)}};
    if (command.rfind("vkCmdDraw", 0) != 0) { result["status"] = "not_applicable"; result["reason"] = "Command is not a graphics draw"; return result; }
    try {
        const bool indexed = command.find("DrawIndexed") != std::string::npos;
        const bool indirect = command.find("Indirect") != std::string::npos;
        const std::set<std::string> supported{"vkCmdDraw", "vkCmdDrawIndexed", "vkCmdDrawIndirect", "vkCmdDrawIndexedIndirect",
            "vkCmdDrawIndirectCount", "vkCmdDrawIndexedIndirectCount", "vkCmdDrawIndirectCountKHR", "vkCmdDrawIndexedIndirectCountKHR",
            "vkCmdDrawIndirectCountAMD", "vkCmdDrawIndexedIndirectCountAMD"};
        if (!supported.count(command)) throw std::runtime_error("This draw command has no supported geometry counting path");
        result["indexed"] = indexed;
        const auto assembly = pipeline.value("pInputAssemblyState", Json::object());
        const auto topologyValue = assembly.value("topology", Json{}); const auto topology = Topology(topologyValue);
        result["topology"] = topologyValue;
        const auto dynamic = pipeline.value("pDynamicState", Json::object());
        if (dynamic.is_object()) for (const auto& state : dynamic.value("pDynamicStates", Json::array()))
            if (state == 1000267002 || state == 1000377004 || (state.is_string() &&
                (state.get<std::string>().find("PRIMITIVE_TOPOLOGY") != std::string::npos || state.get<std::string>().find("PRIMITIVE_RESTART_ENABLE") != std::string::npos)))
                throw std::runtime_error("Dynamic topology/restart is not resolved for geometry counting");
        const auto restartValue = assembly.value("primitiveRestartEnable", Json{});
        const bool restart = restartValue == true || restartValue == 1 || restartValue == "VK_TRUE";
        result["primitive_restart"] = restart;
        Json records = indirect ? parameters.value("indirectParams", Json{}) : Json::array({parameters});
        if (indirect && ((parameters.contains("actualDrawCount") && Number(parameters, "actualDrawCount") == 0) ||
            (parameters.contains("drawCount") && Number(parameters, "drawCount") == 0))) records = Json::array();
        if (!records.is_array() || records.size() > 65536) throw std::runtime_error("Indirect argument readback is unavailable or exceeds the record limit");
        if (indirect && parameters.contains("actualDrawCount") && Number(parameters, "actualDrawCount") != records.size())
            throw std::runtime_error("Indirect argument readback does not cover the actual GPU draw count");
        if (indirect && records.empty() && !parameters.contains("actualDrawCount") &&
            !(parameters.contains("drawCount") && Number(parameters, "drawCount") == 0))
            throw std::runtime_error("Indirect argument readback is unavailable");
        result["argument_source"] = draw.is_null() || draw.empty() ? "capture" : "replay_readback";
        uint64_t total = 0, totalInstances = 0, scanned = 0;
        std::set<int64_t> vertices;
        bool allTriangles = true, allVertices = true;
        for (size_t record = 0; record < records.size(); ++record) {
            const auto& p = records[record];
            const auto count = Number(p, indexed ? "indexCount" : "vertexCount"), instances = Number(p, "instanceCount");
            Json row{{"record", record}, {"element_count", count}, {"instance_count", instances}, {"submitted_triangles", nullptr}, {"unique_vertices", nullptr}};
            totalInstances = Add(totalInstances, instances);
            std::vector<uint64_t> segments{count}; std::set<int64_t> ids;
            bool haveIndices = !indexed;
            if (!count || !instances) { segments = {0}; haveIndices = true; }
            else try {
                if (Add(scanned, count) > 2000000) throw std::runtime_error("Unique vertex scan exceeds 2,000,000 elements");
                scanned += count;
                if (indexed) {
                    const auto input = draw.value("vertexInputState", Json::object());
                    const auto index = input.value("indexBuffer", Json::object());
                    const auto type = index.value("indexType", std::string{});
                    const unsigned width = type.find("UINT16") != std::string::npos ? 2 : type.find("UINT32") != std::string::npos ? 4 : type.find("UINT8") != std::string::npos ? 1 : 0;
                    if (!width || !index.contains("file")) throw std::runtime_error("Index buffer readback is unavailable");
                    if (!index.value("compressedSize", Json{}).is_null()) throw std::runtime_error("Compressed index readback is unsupported");
                    const auto file = directory / std::filesystem::u8path(index.at("file").get<std::string>()).filename();
                    if (std::filesystem::is_symlink(file) || !std::filesystem::is_regular_file(file)) throw std::runtime_error("Index buffer file is unavailable");
                    const auto first = Number(p, "firstIndex");
                    const auto size = std::filesystem::file_size(file);
                    if (first > size / width || count > size / width - first) throw std::runtime_error("Index readback does not cover the draw");
                    std::ifstream in(file, std::ios::binary); in.seekg(static_cast<std::streamoff>(first * width));
                    segments = {0};
                    for (uint64_t i = 0; i < count; ++i) {
                        uint32_t id = 0; if (!in.read(reinterpret_cast<char*>(&id), width)) throw std::runtime_error("Index readback is truncated");
                        if (restart && id == (width == 4 ? UINT32_MAX : (1u << (8 * width)) - 1)) { segments.push_back(0); continue; }
                        const auto adjusted = int64_t(id) + p.value("vertexOffset", int64_t(0));
                        if (adjusted < 0 || adjusted > UINT32_MAX) throw std::runtime_error("Adjusted vertex ID is out of range");
                        ids.insert(adjusted); ++segments.back();
                    }
                    haveIndices = true;
                } else {
                    const auto first = Number(p, "firstVertex");
                    if (first > UINT32_MAX || count > UINT32_MAX - first + uint64_t(1)) throw std::runtime_error("Vertex ID range overflow");
                    for (uint64_t i = 0; i < count; ++i) ids.insert(static_cast<int64_t>(first + i));
                }
                row["unique_vertices"] = ids.size(); vertices.insert(ids.begin(), ids.end());
            } catch (const std::exception& e) { row["vertex_reason"] = e.what(); allVertices = false; }
            if (!count || !instances) row["unique_vertices"] = 0;
            if (indexed && !haveIndices && !restart) segments = {count};
            if (!indexed || !restart || haveIndices || topology < 3) {
                uint64_t triangles = 0;
                for (const auto n : segments) triangles = Add(triangles, topology == 3 ? n / 3 : topology >= 4 ? (n >= 3 ? n - 2 : 0) : 0);
                triangles = Multiply(triangles, instances); total = Add(total, triangles); row["submitted_triangles"] = triangles;
            } else { allTriangles = false; row["triangle_reason"] = "Primitive restart requires readable indices"; }
            result["records"].push_back(row);
        }
        result["record_count"] = records.size(); result["instance_count_sum"] = totalInstances;
        if (allTriangles) result["submitted_triangles"] = total;
        if (allVertices) result["unique_vertices"] = vertices.size();
        result["status"] = allTriangles && allVertices ? "available" : "partial";
    } catch (const std::exception& e) { result["reason"] = e.what(); }
    return result;
}
}
