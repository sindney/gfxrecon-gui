#include "shaders/metadata.h"
#include <spirv.hpp>
#include <map>
#include <stdexcept>
#include <fstream>

namespace gfx {
bool ShaderUsesDrawIndex(const std::filesystem::path& binary){
    const auto words=ReadSpirvBinary(binary);
    for(size_t at=5;at<words.size();){const auto count=words[at]>>16,op=words[at]&65535;
        if(!count||at+count>words.size())throw std::runtime_error("Malformed shader decoration");
        if(op==71&&count>=4&&words[at+2]==11&&words[at+3]==4426)return true;
        if(op==72&&count>=5&&words[at+3]==11&&words[at+4]==4426)return true;
        at+=count;
    }return false;
}

namespace {
std::string String(const std::vector<uint32_t>& words, size_t first, size_t end) {
    std::string text;
    for (size_t i = first; i < end; ++i) for (unsigned b = 0; b < 4; ++b) {
        const auto ch = static_cast<char>((words[i] >> (8 * b)) & 255);
        if (!ch) return text;
        text += ch;
    }
    throw std::runtime_error("SPIR-V debug string lacks a terminator");
}
}
std::vector<uint32_t> ReadSpirvBinary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot read SPIR-V binary " + path.u8string());
    const auto length = input.tellg();
    if (length < 20 || length % 4 || length > 64 * 1024 * 1024) throw std::runtime_error("Invalid SPIR-V byte count");
    std::vector<uint32_t> words(static_cast<size_t>(length) / 4); input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(words.data()), length) || words[0] != 0x07230203) throw std::runtime_error("Invalid SPIR-V binary");
    return words;
}
ShaderMetadata ReadShaderMetadata(const std::vector<uint32_t>& words) {
    if (words.size() < 5 || words[0] != 0x07230203 || words.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid SPIR-V metadata input");
    ShaderMetadata result; auto& info = result.debug;
    info = {{"provenance", "Captured SPIR-V debug instructions; paths are labels and are never opened automatically"},
        {"sources", nlohmann::json::array()}, {"line_mappings", nlohmann::json::array()}, {"imports", nlohmann::json::array()}};
    std::map<uint32_t, std::string> files;
    size_t instruction = 0;
    for (size_t at = 5; at < words.size(); ++instruction) {
        const auto count = words[at] >> 16, op = words[at] & 65535;
        if (!count || count > words.size() - at) throw std::runtime_error("Truncated SPIR-V instruction");
        const auto end = at + count;
        if (op == spv::OpString) {
            if (count < 3) throw std::runtime_error("Truncated OpString");
            files[words[at + 1]] = String(words, at + 2, end);
        } else if (op == spv::OpSource) {
            if (count < 3) throw std::runtime_error("Truncated OpSource");
            nlohmann::json source{{"language", words[at + 1]}, {"version", words[at + 2]}, {"file_id", count > 3 ? words[at + 3] : 0}, {"text", count > 4 ? String(words, at + 4, end) : ""}};
            info["sources"].push_back(std::move(source));
        } else if (op == spv::OpSourceContinued) {
            if (count < 2 || info["sources"].empty()) throw std::runtime_error("Orphan OpSourceContinued");
            auto& source = info["sources"].back();
            source["text"] = source["text"].get<std::string>() + String(words, at + 1, end);
        } else if (op == spv::OpLine) {
            if (count != 4) throw std::runtime_error("Invalid OpLine");
            info["line_mappings"].push_back({{"instruction", instruction}, {"file_id", words[at + 1]}, {"line", words[at + 2]}, {"column", words[at + 3]}});
        } else if (op == spv::OpExtInstImport) {
            if (count < 3) throw std::runtime_error("Truncated OpExtInstImport");
            info["imports"].push_back(String(words, at + 2, end));
        }
        at = end;
    }
    for (auto& source : info["sources"]) {
        const auto file = files.find(source["file_id"].get<uint32_t>());
        source["file"] = file == files.end() ? "Unknown / not recorded" : file->second;
        if (!source["text"].get_ref<const std::string&>().empty()) {
            result.capturedSource += "// Captured source: " + source["file"].get<std::string>() + "\n";
            result.capturedSource += source["text"].get<std::string>() + "\n";
        }
    }
    info["has_embedded_source"] = !result.capturedSource.empty();
    info["has_line_mappings"] = !info["line_mappings"].empty();
    info["source_stepping_available"] = false;
    return result;
}
} // namespace gfx
