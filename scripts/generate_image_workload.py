"""Generate a deterministic mip/depth/stencil validation app from the mesh fixture."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build, compiler = map(Path, sys.argv[1:])
target = build / 'gui-images'
target.mkdir(exist_ok=True)
source = (build/'gui-mesh/app.cpp').read_text()
header = (build/'gui-mesh/triangle_app.h').read_text()
header = header.replace('    VkQueue graphics_queue_;', '''    VkImage texture_, depth_;
    VkImageView texture_view_, depth_view_;
    VkDeviceMemory texture_memory_, depth_memory_;
    VkSampler texture_sampler_;
    void create_images();
    void prepare_texture(VkCommandBuffer);
    VkQueue graphics_queue_;''')
source = source.replace('mesh-preview/shaders/', 'image-preview/shaders/')
source = source.replace('void App::create_mesh_resources() {', 'void App::create_mesh_resources() {\n    create_images();')
source = source.replace('layout.bindingCount=1;layout.pBindings=&binding;', '''VkDescriptorSetLayoutBinding bindings[]{binding,{1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
    layout.bindingCount=2;layout.pBindings=bindings;''')
source = source.replace('pool.poolSizeCount=1;pool.pPoolSizes=&size;', '''VkDescriptorPoolSize sizes[]{size,{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1}};
    pool.poolSizeCount=2;pool.pPoolSizes=sizes;''')
source = source.replace('    init.disp.updateDescriptorSets(1,&write,0,nullptr);', '''    init.disp.updateDescriptorSets(1,&write,0,nullptr);
    VkDescriptorImageInfo image{texture_sampler_,texture_view_,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    write.dstBinding=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pBufferInfo=nullptr;write.pImageInfo=&image;
    init.disp.updateDescriptorSets(1,&write,0,nullptr);''')
source = source.replace('    VkSubpassDescription subpass = {};', '''    VkAttachmentDescription depthAttachment{};depthAttachment.format=VK_FORMAT_D24_UNORM_S8_UINT;depthAttachment.samples=VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp=depthAttachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp=depthAttachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;depthAttachment.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription attachments[]{color_attachment,depthAttachment};
    VkAttachmentReference depthRef{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {};''')
source = source.replace('subpass.pColorAttachments    = &color_attachment_ref;', 'subpass.pColorAttachments    = &color_attachment_ref;\n    subpass.pDepthStencilAttachment=&depthRef;')
source = source.replace('render_pass_info.attachmentCount        = 1;', 'render_pass_info.attachmentCount        = 2;')
source = source.replace('render_pass_info.pAttachments           = &color_attachment;', 'render_pass_info.pAttachments           = attachments;')
source = source.replace('VkImageView attachments[] = { init.swapchain_image_views[i] };', 'VkImageView attachments[] = { init.swapchain_image_views[i], depth_view_ };')
source = source.replace('framebuffer_info.attachmentCount         = 1;', 'framebuffer_info.attachmentCount         = 2;')
source = source.replace('    VkGraphicsPipelineCreateInfo pipeline_info = {};', '''    VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthState.depthTestEnable=depthState.depthWriteEnable=VK_TRUE;depthState.depthCompareOp=VK_COMPARE_OP_LESS;
    depthState.stencilTestEnable=VK_TRUE;depthState.front={VK_STENCIL_OP_KEEP,VK_STENCIL_OP_REPLACE,VK_STENCIL_OP_KEEP,VK_COMPARE_OP_ALWAYS,255,255,7};depthState.back=depthState.front;
    VkGraphicsPipelineCreateInfo pipeline_info = {};''')
source = source.replace('pipeline_info.pDynamicState                = &dynamic_info;', 'pipeline_info.pDynamicState                = &dynamic_info;\n    pipeline_info.pDepthStencilState=&depthState;')
source = source.replace('        init.disp.cmdBeginRenderPass(command_buffer, &render_pass_info,', '''        prepare_texture(command_buffer);
        VkClearValue clears[2]{};clears[0].color={{0.05f,0.1f,0.15f,1}};clears[1].depthStencil={1,0};
        render_pass_info.clearValueCount=2;render_pass_info.pClearValues=clears;
        init.disp.cmdBeginRenderPass(command_buffer, &render_pass_info,''')
source = source.replace('    init.disp.destroyRenderPass(render_pass_, nullptr);', '''    init.disp.destroyRenderPass(render_pass_, nullptr);
    init.disp.destroyImageView(depth_view_,nullptr);init.disp.destroyImage(depth_,nullptr);init.disp.freeMemory(depth_memory_,nullptr);
    init.disp.destroyImageView(texture_view_,nullptr);init.disp.destroyImage(texture_,nullptr);init.disp.freeMemory(texture_memory_,nullptr);init.disp.destroySampler(texture_sampler_,nullptr);''')
source += (root/'tests/workloads/image_resources.inc').read_text()
(target/'app.cpp').write_text(source)
(target/'triangle_app.h').write_text(header)
shaders = target/'res/image-preview/shaders'
shaders.mkdir(parents=True,exist_ok=True)
for stage, source_file in [('vert',root/'tests/workloads/mesh.vert'),('frag',root/'tests/workloads/mips.frag')]:
    subprocess.run([str(compiler),'-V','--target-env','vulkan1.1',str(source_file),'-o',str(shaders/(stage+'.spv'))],check=True)
