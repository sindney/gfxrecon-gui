#include "shaders/diagnostics.h"
#include "shaders/metadata.h"
#include <spirv_cross.hpp>
#include <algorithm>
#include <cctype>

namespace gfx {
std::string UvCheckerboardSource(const std::filesystem::path& fragment,const std::string& entry,std::string& detail){
    spirv_cross::Compiler shader(ReadSpirvBinary(fragment));shader.set_entry_point(entry,spv::ExecutionModelFragment);
    const auto resources=shader.get_shader_resources();
    const spirv_cross::Resource* chosen=nullptr;int best=-1;
    for(const auto& input:resources.stage_inputs){
        const auto& type=shader.get_type(input.type_id);
        const bool arraySupported=type.array.empty()||(type.array.size()==1&&type.array_size_literal.size()==1&&type.array_size_literal[0]&&type.array[0]>0&&type.array[0]<=16);
        if(type.basetype!=spirv_cross::SPIRType::Float||type.width!=32||type.columns!=1||!arraySupported||type.vecsize<2||type.vecsize>4||
           !shader.has_decoration(input.id,spv::DecorationLocation)||shader.get_decoration(input.id,spv::DecorationComponent)!=0||shader.has_decoration(input.id,spv::DecorationFlat))continue;
        auto name=input.name;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        const bool named=name.find("texcoord")!=std::string::npos||name.find("uv")!=std::string::npos;
        if(!named&&type.vecsize!=2)continue;
        // TEXCOORD10/11 frequently carry tangent frames. Prefer the primary
        // texture coordinate semantic even when those frames have lower locations.
        const auto primary=name.find("texcoord0");
        const bool primaryUv=primary!=std::string::npos&&(primary+9==name.size()||!std::isdigit(static_cast<unsigned char>(name[primary+9])));
        const int score=(named?100:0)+(primaryUv?100:0)+(type.vecsize==2?10:0);
        if(score>best||(score==best&&shader.get_decoration(input.id,spv::DecorationLocation)<shader.get_decoration(chosen->id,spv::DecorationLocation))){best=score;chosen=&input;}
    }
    if(!chosen)throw std::runtime_error("UV density unavailable: no compatible UV/TEXCOORD varying or float vec2 fragment input");
    const auto location=shader.get_decoration(chosen->id,spv::DecorationLocation);const auto size=shader.get_type(chosen->type_id).vecsize;
    const auto& type=shader.get_type(chosen->type_id);
    const auto array=type.array.empty()?std::string{}:"["+std::to_string(type.array[0])+"]";
    const auto uv=type.array.empty()?std::string("gfx_uv.xy"):std::string("gfx_uv[0].xy");
    detail="UV location "+std::to_string(location)+" ("+chosen->name+") | 32 checker cells per UV unit";
    std::string qualifiers;
    if(shader.has_decoration(chosen->id,spv::DecorationNoPerspective))qualifiers+="noperspective ";
    if(shader.has_decoration(chosen->id,spv::DecorationCentroid))qualifiers+="centroid ";
    if(shader.has_decoration(chosen->id,spv::DecorationSample))qualifiers+="sample ";
    return "#version 450\nlayout(location="+std::to_string(location)+") "+qualifiers+"in vec"+std::to_string(size)+" gfx_uv"+array+";\nlayout(location=0) out vec4 color;\nvoid main(){\n    vec2 p="+uv+"*32.0;\n"+R"(
    // Analytic box-filtered checker integral avoids flicker for dense UVs.
    vec2 w=max(fwidth(p),vec2(0.0001));
    vec2 a=p-0.5*w,b=p+0.5*w;
    vec2 ia=floor(a*0.5)+max(fract(a*0.5)*2.0-1.0,0.0);
    vec2 ib=floor(b*0.5)+max(fract(b*0.5)*2.0-1.0,0.0);
    vec2 c=(ib-ia)/w;
    float checker=c.x+c.y-2.0*c.x*c.y;
    color=vec4(mix(vec3(0.04),vec3(0.95),checker),1.0);
})";
}
}
