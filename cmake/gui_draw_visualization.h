#pragma once
#include "gui_visual_shaders.h"
#include "generated/generated_vulkan_dispatch_table.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <mutex>
#include "util/logging.h"

namespace gfx_gui_visual {
struct Pipelines { VkDevice device{}; VkPipeline pass{},fail{}; };
inline std::mutex mutex;
inline std::map<VkPipeline,Pipelines> pipelines;
inline std::string directory,mode="original";
inline uint64_t capture_pipeline=0;
inline int subdraw=-1;
inline bool enabled=false,applied=false,wire_supported=false,draw_id_safe=false;
inline std::string error;
inline void Report(){
    if(!enabled)return;
    std::ofstream out(directory+"/gui-visualization-result.json");
    out<<nlohmann::json{{"protocol",1},{"mode",mode},{"pipeline",capture_pipeline},{"subdraw",subdraw},{"applied",applied},{"error",error},{"wireframe_supported",wire_supported}}.dump(2);
}
inline void Load(std::string path,const std::string& request){
    // The CLI resolves DumpResourcesOptions after constructing this consumer.
    // Read its output directory here so overlay PSOs exist before creation replay.
    if(!request.empty()){
        std::ifstream input(request);
        if(input){const auto json=nlohmann::json::parse(input,nullptr,false);if(json.is_object()&&json.contains("DumpResourcesOptions"))path=json["DumpResourcesOptions"].value("OutputDir",std::string{});}
    }
    std::lock_guard<std::mutex> lock(mutex);directory=path;enabled=false;applied=false;error.clear();pipelines.clear();wire_supported=false;mode="original";subdraw=-1;
    std::ifstream in(path+"/gui-visualization.json");if(!in)return;
    const auto config=nlohmann::json::parse(in);mode=config.value("mode",std::string("original"));subdraw=config.value("subdraw",-1);capture_pipeline=config.value("pipeline",uint64_t(0));
    GFXRECON_LOG_INFO("GUI visualization mode: %s pipeline=%llu",mode.c_str(),static_cast<unsigned long long>(capture_pipeline));enabled=mode!="original"||subdraw>=0;draw_id_safe=config.value("draw_id_safe",false);Report();
}
inline VkCompareOp Invert(VkCompareOp op){switch(op){case VK_COMPARE_OP_NEVER:return VK_COMPARE_OP_ALWAYS;case VK_COMPARE_OP_LESS:return VK_COMPARE_OP_GREATER_OR_EQUAL;case VK_COMPARE_OP_EQUAL:return VK_COMPARE_OP_NOT_EQUAL;case VK_COMPARE_OP_LESS_OR_EQUAL:return VK_COMPARE_OP_GREATER;case VK_COMPARE_OP_GREATER:return VK_COMPARE_OP_LESS_OR_EQUAL;case VK_COMPARE_OP_NOT_EQUAL:return VK_COMPARE_OP_EQUAL;case VK_COMPARE_OP_GREATER_OR_EQUAL:return VK_COMPARE_OP_LESS;default:return VK_COMPARE_OP_NEVER;}}
template<typename Table> inline VkPipeline Create(Table table,PFN_vkCreateGraphicsPipelines func,VkDevice device,const VkGraphicsPipelineCreateInfo& original,const uint32_t* code,size_t bytes,bool fail){
    VkShaderModule shader{};VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=bytes;sm.pCode=code;
    if(table->CreateShaderModule(device,&sm,nullptr,&shader)!=VK_SUCCESS){error="Cannot create overlay fragment shader";return VK_NULL_HANDLE;}
    auto ci=original;ci.flags=0;ci.basePipelineHandle=VK_NULL_HANDLE;ci.basePipelineIndex=-1;
    std::vector<VkPipelineShaderStageCreateInfo> stages(original.pStages,original.pStages+original.stageCount);bool has_fragment=false;
    for(auto& stage:stages)if(stage.stage==VK_SHADER_STAGE_FRAGMENT_BIT){stage.module=shader;stage.pName="main";stage.pNext=nullptr;stage.pSpecializationInfo=nullptr;has_fragment=true;}
    if(!has_fragment){VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stage.stage=VK_SHADER_STAGE_FRAGMENT_BIT;stage.module=shader;stage.pName="main";stages.push_back(stage);}
    ci.stageCount=static_cast<uint32_t>(stages.size());ci.pStages=stages.data();
    VkPipelineRasterizationStateCreateInfo raster=*original.pRasterizationState;raster.polygonMode=mode=="wireframe"?VK_POLYGON_MODE_LINE:VK_POLYGON_MODE_FILL;raster.lineWidth=1;raster.rasterizerDiscardEnable=VK_FALSE;ci.pRasterizationState=&raster;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};if(original.pDepthStencilState)depth=*original.pDepthStencilState;
    depth.depthWriteEnable=VK_FALSE;depth.stencilTestEnable=VK_FALSE;depth.depthBoundsTestEnable=VK_FALSE;
    if(mode=="depth"){
        const auto compare=depth.depthTestEnable?depth.depthCompareOp:VK_COMPARE_OP_ALWAYS;depth.depthTestEnable=VK_TRUE;depth.depthCompareOp=fail?Invert(compare):compare;
    }else{depth.depthTestEnable=VK_FALSE;depth.depthCompareOp=VK_COMPARE_OP_ALWAYS;}
    ci.pDepthStencilState=&depth;
    auto blend=*original.pColorBlendState;std::vector<VkPipelineColorBlendAttachmentState> attachments(blend.attachmentCount);
    for(auto& attachment:attachments){attachment.blendEnable=VK_FALSE;attachment.colorWriteMask=15;}blend.logicOpEnable=VK_FALSE;blend.pAttachments=attachments.data();ci.pColorBlendState=&blend;
    VkPipeline pipeline{};const auto result=func(device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline);table->DestroyShaderModule(device,shader,nullptr);
    if(result!=VK_SUCCESS){error="Overlay pipeline creation failed: "+std::to_string(result);return VK_NULL_HANDLE;}return pipeline;
}
template<typename Table> inline void Prepare(Table table,PFN_vkCreateGraphicsPipelines func,VkDevice device,uint64_t id,VkPipeline original,const VkGraphicsPipelineCreateInfo& ci){
    if(!enabled||mode=="original"||id!=capture_pipeline)return;std::lock_guard<std::mutex> lock(mutex);
    if(!ci.pRasterizationState||!ci.pColorBlendState||!ci.pColorBlendState->attachmentCount){error="This pipeline has no color target for an overlay";Report();return;}
    if(mode=="wireframe"&&!wire_supported){error="The replay device does not support fillModeNonSolid";Report();return;}
    if(ci.pDynamicState)for(uint32_t i=0;i<ci.pDynamicState->dynamicStateCount;++i){const auto state=uint32_t(ci.pDynamicState->pDynamicStates[i]);if(state>=1000267000u){error="Extended dynamic pipeline states are not supported by this overlay";Report();return;}}
    Pipelines pair;pair.device=device;
    pair.pass=mode=="depth"?Create(table,func,device,ci,pass_shader,sizeof(pass_shader),false):Create(table,func,device,ci,solid_shader,sizeof(solid_shader),false);
    if(mode=="depth")pair.fail=Create(table,func,device,ci,fail_shader,sizeof(fail_shader),true);
    if(pair.pass&&(mode!="depth"||pair.fail))pipelines[original]=pair;
    else{if(pair.pass)table->DestroyPipeline(device,pair.pass,nullptr);if(pair.fail)table->DestroyPipeline(device,pair.fail,nullptr);}
    Report();
}
template<typename Table,typename Draw> inline void Execute(Table& table,VkPipeline original,VkCommandBuffer cb,Draw draw){
    if(!enabled){draw();return;}std::lock_guard<std::mutex> lock(mutex);
    if(mode=="original"){draw();applied=true;Report();return;}
    const auto found=pipelines.find(original);
    if(found==pipelines.end()){if(error.empty())error="No compatible overlay pipeline was created for this draw";Report();draw();return;}
    if(found->second.fail){table->CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,found->second.fail);draw();}
    table->CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,found->second.pass);draw();table->CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,original);applied=true;Report();
}
template<typename Table>inline void Release(Table table,VkDevice device){
    std::lock_guard<std::mutex> lock(mutex);for(auto it=pipelines.begin();it!=pipelines.end();)if(it->second.device==device){table->DestroyPipeline(device,it->second.pass,nullptr);if(it->second.fail)table->DestroyPipeline(device,it->second.fail,nullptr);it=pipelines.erase(it);}else ++it;
}
inline void SelectIndirect(bool target,VkDeviceSize& offset,uint32_t& count,uint32_t stride){
    if(!target||!enabled||subdraw<0)return;
    if(!draw_id_safe){error="Subdraw replay requires shaders without gl_DrawID";count=0;Report();return;}
    if(static_cast<uint32_t>(subdraw)>=count){error="Indirect subdraw index is outside this batch";count=0;Report();return;}
    offset+=VkDeviceSize(subdraw)*stride;count=1;
}
}
