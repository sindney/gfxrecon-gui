#include "analysis/mesh.h"
#include <fstream>
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace gfx::tests {
void RunMeshTests(const std::filesystem::path& root) {
    auto require=[](bool condition,const char* message){if(!condition)throw std::runtime_error(message);};
    uint32_t size=0;
    require(BuildMeshPrimitives(4,{4},size)==std::vector<uint32_t>({0,1,2,2,1,3})&&size==3,"Triangle-strip winding is wrong");
    require(BuildMeshPrimitives(4,{3,3},size)==std::vector<uint32_t>({0,1,2,3,4,5}),"Primitive restart did not split the strip");
    require(BuildMeshPrimitives(2,{3},size)==std::vector<uint32_t>({0,1,1,2})&&size==2,"Line-strip assembly is wrong");
    std::vector<uint8_t> packed{0,255,128,64}; const auto normalized=DecodeMeshAttribute(packed,0,37);
    require(normalized[0]==0&&normalized[1]==1&&std::abs(normalized[2]-128.0f/255)<.00001f,"UNORM vertex conversion is wrong");
    const auto half=DecodeMeshAttribute({0,0x3c},0,76); require(half[0]==1,"Half-float vertex conversion is wrong");
    const auto bgra=DecodeMeshAttribute({0,64,255,128},0,44);require(bgra[0]==1&&bgra[2]==0&&std::abs(bgra[1]-64.0f/255)<.00001f,"BGRA vertex channels were not remapped");
    bool rejected=false;try{DecodeMeshAttribute(packed,1,106);}catch(...){rejected=true;}require(rejected,"Truncated vertex attribute was accepted");
    const auto dir=root/"mesh";std::filesystem::create_directory(dir);
    const float vertices[]={-1,-2,-3, 4,5,6, 7,8,9};
    {std::ofstream out(dir/"vertices.bin",std::ios::binary);out.write(reinterpret_cast<const char*>(vertices),sizeof(vertices));}
    const uint16_t indices[]={99,1,2,3};
    {std::ofstream out(dir/"indices.bin",std::ios::binary);out.write(reinterpret_cast<const char*>(indices),sizeof(indices));}
    Capture capture;Event event;event.name="vkCmdDrawIndexed";event.block=10;event.pipeline=5;
    event.state=std::make_shared<Json>(Json::parse(R"({"vertexBuffers":{"0":{"buffer":100,"offset":32}}})"));capture.events.push_back(event);
    Execution execution;execution.event=0;execution.submit=20;capture.executions.push_back(execution);
    capture.pipelineInfo[5]=Json::parse(R"({"pInputAssemblyState":{"topology":3,"primitiveRestartEnable":false}})");
    capture.pipelines[5]={{6,"VK_SHADER_STAGE_VERTEX_BIT","main"},{7,"VK_SHADER_STAGE_FRAGMENT_BIT","main"}};
    auto draw=Json::parse(R"({"drawIndex":10,"queueSubmitIndex":20,"parameters":{"indexCount":3,"instanceCount":1,"firstIndex":1,"vertexOffset":-1},"vertexInputState":{"attributes":[{"location":0,"binding":0,"format":106,"offset":0}],"bindings":[{"binding":0,"stride":12,"inputRate":"VK_VERTEX_INPUT_RATE_VERTEX"}],"vertexBuffers":[{"vertexBufferBinding":0,"file":"vertices.bin","offset":32}],"indexBuffer":{"indexType":"VK_INDEX_TYPE_UINT16","file":"indices.bin","offset":8}}})");
    {std::ofstream out(dir/"fixture_dr.json");out<<Json{{"drawCallCommands",Json::array({draw})}}.dump();}
    const auto mesh=ReadMeshData(capture,0,dir,dir/"missing.spv");
    require(mesh.vertexIds==std::vector<uint32_t>({0,1,2}),"firstIndex/negative base vertex resolution is wrong");
    require(mesh.attributes.size()==1&&mesh.attributes[0].values[1][2]==6,"Dump-relative vertex offsets are wrong");
    require(mesh.primitiveIndices==std::vector<uint32_t>({0,1,2}),"Indexed primitive mapping was lost");
    require(mesh.vertexSource.empty()&&!mesh.postDiagnostic.empty(),"Missing shader must preserve input geometry and explain post-VS unavailability");
    bool badInstance=false;try{ReadMeshData(capture,0,dir,{},1);}catch(...){badInstance=true;}require(badInstance,"Out-of-range instance was accepted");
    std::filesystem::remove_all(dir);
}
}
