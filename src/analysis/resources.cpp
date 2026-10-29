#include "analysis/resources.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <tuple>
#include <functional>

namespace gfx {
bool DescriptorUsesSampler(const Json& type){
    return type==0||type==1||type=="VK_DESCRIPTOR_TYPE_SAMPLER"||type=="VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER";
}
std::vector<SamplerBinding> CollectStageSamplers(const Capture& capture,const Execution& execution,const ShaderStage& stage){
    auto id=[](const Json& value,const char* key)->uint64_t{const auto it=value.find(key);return it!=value.end()&&it->is_number()?it->get<uint64_t>():0;};
    const uint64_t mask=stage.stage.find("VERTEX")!=std::string::npos?1:stage.stage.find("FRAGMENT")!=std::string::npos?16:stage.stage.find("COMPUTE")!=std::string::npos?32:stage.stage.find("CONTROL")!=std::string::npos?2:stage.stage.find("EVALUATION")!=std::string::npos?4:8;
    auto visible=[&](const Json& flags){
        if(flags.is_number())return (flags.get<uint64_t>()&mask)!=0;
        if(!flags.is_string())return false;
        const auto text=flags.get<std::string>();
        if(text.find("ALL_GRAPHICS")!=std::string::npos)return (mask&31)!=0;
        if(text.find("ALL")!=std::string::npos||text.find(stage.stage)!=std::string::npos)return true;
        try{return (std::stoull(text,nullptr,0)&mask)!=0;}catch(...){return false;}
    };
    std::map<std::tuple<uint64_t,uint64_t,uint64_t>,SamplerBinding> bindings;
    for(auto set=execution.descriptorValues.begin();set!=execution.descriptorValues.end();++set){
        uint64_t setIndex=0;try{setIndex=std::stoull(set.key());}catch(...){continue;}
        const auto layout=capture.objects.find(id(set.value(),"layout"));
        const auto declared=layout!=capture.objects.end()?layout->second.createInfo.value("pBindings",Json::array()):Json::array();
        const auto values=set.value().value("bindings",Json::object());
        if(!declared.is_array())continue;
        // Use layout type/visibility and immutable samplers where available.
        for(const auto& entry:declared){
            if(!DescriptorUsesSampler(entry.value("descriptorType",Json{}))||!visible(entry.value("stageFlags",Json{})))continue;
            const auto binding=id(entry,"binding");const auto immutable=entry.value("pImmutableSamplers",Json{});
            if(immutable.is_array()){
                for(size_t i=0;i<immutable.size();++i)if(immutable[i].is_number()&&immutable[i].get<uint64_t>())bindings[{setIndex,binding,i}]={setIndex,binding,i,immutable[i].get<uint64_t>(),true};
                continue;
            }
            const auto array=values.find(std::to_string(binding));if(array==values.end()||!array->is_object())continue;
            for(auto element=array->begin();element!=array->end();++element){
                if(!DescriptorUsesSampler(element.value().value("type",Json{})))continue;
                const auto sampler=id(element.value().value("value",Json::object()),"sampler");if(!sampler)continue;
                try{const auto index=std::stoull(element.key());bindings[{setIndex,binding,index}]={setIndex,binding,index,sampler,false};}catch(...){}
            }
        }
    }
    std::vector<SamplerBinding> result;for(const auto& item:bindings)result.push_back(item.second);return result;
}
namespace {
void Walk(const Json& value, Json inherited, const std::string& role, const std::filesystem::path& root,
           std::map<std::string, ResourceArtifact>& files, unsigned depth = 0) {
    if (depth > 32) throw std::runtime_error("Replay metadata nesting exceeds the supported limit");
    if (value.is_object()) {
        for (auto key : {"drawIndex", "dispatchIndex", "beginCommandBufferIndex", "queueSubmitIndex", "imageId", "bufferId",
                         "format", "extent", "dimensions", "aspect", "mipLevel", "arrayLayer", "size", "offset", "binding", "stride", "type", "set", "arrayIndex", "location", "vertexBufferBinding"})
            if (value.contains(key)) inherited[key] = value[key];
        if (value.contains("file") && value["file"].is_string()) {
            const auto filename = std::filesystem::u8path(value["file"].get<std::string>()).filename();
            const auto path = root / filename;
            const auto key=filename.u8string()+"|"+inherited.value("category","")+"|"+inherited.value("stage","")+"|"+
                inherited.value("set",Json()).dump()+"|"+inherited.value("binding",Json()).dump()+"|"+inherited.value("arrayIndex",Json()).dump();
            if (std::filesystem::is_regular_file(path)) files[key] = {path, std::filesystem::file_size(path), role, inherited};
        }
        for (auto it = value.begin(); it != value.end(); ++it) if (it.value().is_structured()) {
            auto metadata=inherited;
            if(it.key()=="colorAttachments"||it.key()=="depthAttachment"||it.key()=="depthAttachments")metadata["category"]="output";
            if(it.key()=="descriptors")metadata["category"]="input";
            if(it.key()=="vertexInputState")metadata["category"]="mesh";
            if(inherited.value("category","")=="input" && (it.key()=="vertex"||it.key()=="fragment"||it.key()=="compute"))metadata["stage"]=it.key();
            Walk(it.value(),metadata,it.key()=="subresources"?role:it.key(),root,files,depth+1);
        }
    } else if (value.is_array()) for (const auto& entry : value) Walk(entry, inherited, role, root, files, depth + 1);
}
}
Json ReadDrawParameters(const std::filesystem::path& directory,uint64_t block){
    std::function<Json(const Json&,unsigned)> find=[&](const Json& value,unsigned depth)->Json{
        if(depth>32)return {};
        if(value.is_object()){
            if(value.value("drawIndex",uint64_t(0))==block&&value.contains("parameters"))return value["parameters"];
            for(const auto& item:value)if(item.is_structured()){auto found=find(item,depth+1);if(!found.is_null())return found;}
        }else if(value.is_array())for(const auto& item:value){auto found=find(item,depth+1);if(!found.is_null())return found;}
        return {};
    };
    for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.path().filename().u8string().find("_dr.json")!=std::string::npos){std::ifstream input(entry.path());auto result=find(Json::parse(input),0);if(!result.is_null())return result;}
    return {};
}
std::vector<ResourceArtifact> CollectResources(const std::filesystem::path& directory) {
    std::map<std::string, ResourceArtifact> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto filename = entry.path().filename().u8string();
        if (entry.is_regular_file() && filename.find("_dr.json") != std::string::npos) {
            std::ifstream in(entry.path());
            Walk(Json::parse(in), Json::object(), "resource", directory, files);
        }
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().u8string(), extension = entry.path().extension().u8string();
        if(name.size()>10&&name.compare(name.size()-10,10,".depth.bin")==0)continue;
        const bool listed=std::any_of(files.begin(),files.end(),[&](const auto& item){return item.second.file.filename()==entry.path().filename();});
        if ((extension == ".bin" || (extension == ".bmp" && name.find("_frame_") == std::string::npos)) && !listed)
            files[name] = {entry.path(), entry.file_size(), extension == ".bmp" ? "Image" : "Buffer / raw data", Json::object()};
    }
    std::vector<ResourceArtifact> result;
    for (auto& [name, artifact] : files) result.push_back(std::move(artifact));
    return result;
}
ResourcePage ReadResourcePage(const std::filesystem::path& file, uint64_t offset, size_t limit) {
    ResourcePage page; page.file = file; page.offset = offset; page.totalSize = std::filesystem::file_size(file);
    if (offset > page.totalSize || offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
        throw std::runtime_error("Byte offset is outside the resource");
    const auto count = static_cast<size_t>(std::min<uint64_t>(std::min<size_t>(limit, 65536), page.totalSize - offset));
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open resource file");
    input.seekg(static_cast<std::streamoff>(offset)); page.bytes.resize(count);
    if (count && !input.read(reinterpret_cast<char*>(page.bytes.data()), static_cast<std::streamsize>(count)))
        throw std::runtime_error("Resource file is truncated or changed");
    return page;
}
std::string DecodeResourceValue(const std::vector<uint8_t>& bytes, size_t offset, int type) {
    const size_t sizes[] = {1, 2, 4, 8, 4, 8};
    if (type < 0 || type > 5 || offset > bytes.size() || sizes[type] > bytes.size() - offset) return "-";
    std::ostringstream out; out << std::setprecision(8);
    if (type < 4) { uint64_t value = 0; std::memcpy(&value, bytes.data() + offset, sizes[type]); out << value; }
    else if (type == 4) { float value; std::memcpy(&value, bytes.data() + offset, 4); out << value; }
    else { double value; std::memcpy(&value, bytes.data() + offset, 8); out << value; }
    return out.str();
}
} // namespace gfx
