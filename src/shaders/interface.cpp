#include "shaders/interface.h"
#include "shaders/metadata.h"

#include <spirv_cross.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <map>
#include <set>

namespace gfx {
namespace {
using Json = nlohmann::json;
Json Type(spirv_cross::Compiler& c, const spirv_cross::SPIRType& type, unsigned depth = 0) {
    if (depth > 16) throw std::runtime_error("Shader interface type nesting exceeds supported limit");
    Json result{{"base", type.basetype}, {"width", type.width}, {"vector", type.vecsize},
        {"columns", type.columns}, {"arrays", type.array}};
    for (auto literal : type.array_size_literal) if (!literal)
        throw std::runtime_error("Specialized array interface requires additional compatibility validation");
    if (type.basetype == spirv_cross::SPIRType::Struct) {
        result["members"] = Json::array();
        for (size_t i = 0; i < type.member_types.size(); ++i) {
            auto member = Type(c, c.get_type(type.member_types[i]), depth + 1);
            if (c.has_member_decoration(type.self, static_cast<uint32_t>(i), spv::DecorationOffset))
                member["offset"] = c.get_member_decoration(type.self, static_cast<uint32_t>(i), spv::DecorationOffset);
            if (c.has_member_decoration(type.self, static_cast<uint32_t>(i), spv::DecorationMatrixStride))
                member["matrix_stride"] = c.get_member_decoration(type.self, static_cast<uint32_t>(i), spv::DecorationMatrixStride);
            member["row_major"] = c.has_member_decoration(type.self, static_cast<uint32_t>(i), spv::DecorationRowMajor);
            result["members"].push_back(std::move(member));
        }
    }
    if (c.has_decoration(type.self, spv::DecorationArrayStride)) result["array_stride"] = c.get_decoration(type.self, spv::DecorationArrayStride);
    if (type.basetype == spirv_cross::SPIRType::Image || type.basetype == spirv_cross::SPIRType::SampledImage)
        result["image"] = {type.image.dim, type.image.depth, type.image.arrayed, type.image.ms, type.image.sampled, type.image.format};
    return result;
}
Json Signature(const std::filesystem::path& path, const std::string& entry) {
    const auto words=ReadSpirvBinary(path);spirv_cross::Compiler c(words);
    std::map<uint32_t,uint32_t> imageDepth,elementType;
    for(size_t offset=5;offset<words.size();){
        const auto count=words[offset]>>16,opcode=words[offset]&0xffffu;
        if(!count||offset+count>words.size())throw std::runtime_error("Malformed shader type instruction");
        if(opcode==spv::OpTypeImage&&count>=9)imageDepth[words[offset+1]]=words[offset+4];
        if(opcode==spv::OpTypePointer&&count>=4)elementType[words[offset+1]]=words[offset+3];
        if((opcode==spv::OpTypeArray||opcode==spv::OpTypeRuntimeArray||opcode==spv::OpTypeSampledImage)&&count>=3)elementType[words[offset+1]]=words[offset+2];
        offset+=count;
    }
    auto declaredDepth=[&](uint32_t type){
        std::set<uint32_t> visited;
        while(!imageDepth.count(type)){
            if(!visited.insert(type).second||!elementType.count(type))throw std::runtime_error("Cannot resolve descriptor image depth type");
            type=elementType.at(type);
        }return imageDepth.at(type);
    };
    auto points = c.get_entry_points_and_stages();
    auto found = std::find_if(points.begin(), points.end(), [&](const auto& point) { return point.name == entry; });
    if(points.size()!=1)throw std::runtime_error("Editing supports single-entry-point shader modules only");
    if(found==points.end())throw std::runtime_error("Shader does not export the required entry point: "+entry);
    c.set_entry_point(found->name, found->execution_model);
    Json signature{{"entry", entry}, {"model", found->execution_model}, {"resources", Json::array()}};
    const auto resources = c.get_shader_resources();
    auto append = [&](const auto& list, const char* kind) {
        for (const auto& resource : list) {
            Json row{{"kind", kind}, {"type", Type(c, c.get_type(resource.type_id))}};
            // SPIRV-Cross folds the SPIR-V Depth operand (0/1/2) into a bool.
            // Retain 'unknown' so a recompiled declaration may safely refine it.
            if(row["type"].contains("image"))row["type"]["image"][1]=declaredDepth(resource.type_id);
            const std::pair<spv::Decoration, const char*> decorations[] = {
                {spv::DecorationDescriptorSet, "set"}, {spv::DecorationBinding, "binding"},
                {spv::DecorationLocation, "location"}, {spv::DecorationComponent, "component"},
                {spv::DecorationIndex, "index"}, {spv::DecorationInputAttachmentIndex, "input_attachment"}};
            for (const auto& [decoration, key] : decorations)
                if (c.has_decoration(resource.id, decoration)) row[key] = c.get_decoration(resource.id, decoration);
            for (const auto decoration : {spv::DecorationFlat, spv::DecorationNoPerspective, spv::DecorationCentroid, spv::DecorationSample, spv::DecorationPatch})
                if (c.has_decoration(resource.id, decoration)) row["qualifiers"].push_back(static_cast<uint32_t>(decoration));
            if (std::string(kind) == "push") row["size"] = c.get_declared_struct_size(c.get_type(resource.base_type_id));
            signature["resources"].push_back(std::move(row));
        }
    };
    append(resources.uniform_buffers, "uniform"); append(resources.storage_buffers, "storage");
    append(resources.sampled_images, "sampled"); append(resources.separate_images, "image");
    append(resources.separate_samplers, "sampler"); append(resources.storage_images, "storage_image");
    append(resources.subpass_inputs, "subpass"); append(resources.push_constant_buffers, "push");
    append(resources.stage_inputs, "input"); append(resources.stage_outputs, "output");
    c.update_active_builtins();
    auto builtins = [&](const auto& list, const char* kind, spv::StorageClass storage) {
        for (const auto& item : list) if (c.has_active_builtin(item.builtin, storage))
            signature["resources"].push_back({{"kind", kind}, {"builtin", item.builtin}, {"type", Type(c, c.get_type(item.value_type_id))}});
    };
    builtins(resources.builtin_inputs, "builtin_input", spv::StorageClassInput);
    builtins(resources.builtin_outputs, "builtin_output", spv::StorageClassOutput);
    if (!resources.acceleration_structures.empty() || !resources.atomic_counters.empty())
        throw std::runtime_error("This resource interface is not enabled for replay editing");
    auto& rows = signature["resources"];
    std::sort(rows.begin(), rows.end(), [](const Json& a, const Json& b) { return a.dump() < b.dump(); });
    signature["specialization"] = Json::array();
    for (auto constant : c.get_specialization_constants()) {
        const auto& value = c.get_constant(constant.id);
        signature["specialization"].push_back(Json{{"id", constant.constant_id}, {"type", Type(c, c.get_type(value.constant_type))}});
    }
    std::sort(signature["specialization"].begin(), signature["specialization"].end(),
        [](const Json& a, const Json& b) { return a["id"].get<uint32_t>() < b["id"].get<uint32_t>(); });
    signature["modes"] = Json::array();
    c.get_execution_mode_bitset().for_each_bit([&](uint32_t mode) {
        Json row = {mode};
        if (mode == spv::ExecutionModeLocalSizeId) throw std::runtime_error("Specialized workgroup sizes require additional compatibility validation");
        if (mode == spv::ExecutionModeLocalSize || mode == spv::ExecutionModeLocalSizeHint)
            for (uint32_t i = 0; i < 3; ++i) row.push_back(c.get_execution_mode_argument(static_cast<spv::ExecutionMode>(mode), i));
        if (mode == spv::ExecutionModeOutputVertices || mode == spv::ExecutionModeInvocations || mode == spv::ExecutionModeSubgroupSize || mode == spv::ExecutionModeSubgroupsPerWorkgroup)
            row.push_back(c.get_execution_mode_argument(static_cast<spv::ExecutionMode>(mode)));
        signature["modes"].push_back(std::move(row));
    });
    signature["capabilities"] = c.get_declared_capabilities();
    signature["extensions"] = c.get_declared_extensions();
    std::sort(signature["capabilities"].begin(), signature["capabilities"].end());
    std::sort(signature["extensions"].begin(), signature["extensions"].end());
    return signature;
}
} // namespace
std::string DescribeShaderInterface(const std::filesystem::path& binary,const std::string& entry){return Signature(binary,entry).dump(2);}
void CheckShaderInterface(const std::filesystem::path& original, const std::filesystem::path& candidate, const std::string& entry) {
    const auto before = Signature(original, entry);
    auto after = Signature(candidate, entry);
    auto remaining=before.at("resources");bool compatible=true;std::string resourceDifference;
    auto identify=[](const Json& row){
        std::string label=row.value("kind",std::string("resource"));
        for(const auto* key:{"set","binding","location","builtin"})if(row.contains(key))label+=" "+std::string(key)+"="+row[key].dump();
        return label;
    };
    for(const auto& resource:after.at("resources")){
        auto found=std::find_if(remaining.begin(),remaining.end(),[&](const Json& original){
            auto normalized=resource;
            if(original["type"].contains("image")&&normalized["type"].contains("image")&&original["type"]["image"][1]==2)
                normalized["type"]["image"][1]=2;
            return original==normalized;
        });
        if(found==remaining.end()){compatible=false;resourceDifference=" (changed or added "+identify(resource)+")";break;}remaining.erase(found);
    }
    if(compatible&&remaining.empty())after["resources"]=before["resources"];
    else if(resourceDifference.empty()&&!remaining.empty())resourceDifference=" (missing "+identify(remaining.front())+")";
    if (before != after) {
        std::string differences;
        for (auto it = before.begin(); it != before.end(); ++it) if (after.at(it.key()) != it.value()) differences += it.key() + " ";
        throw std::runtime_error("Candidate changes unsupported shader interface requirements: " + differences + resourceDifference);
    }
}
} // namespace gfx
