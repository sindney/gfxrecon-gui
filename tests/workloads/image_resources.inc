
namespace gfxrecon::test_app::triangle {
void App::create_images(){
    auto make=[&](VkImage& image,VkDeviceMemory& memory,VkImageView& view,VkFormat format,VkExtent3D extent,uint32_t levels,VkImageUsageFlags usage,VkImageAspectFlags aspect){
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent=extent;
        info.mipLevels=levels;info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=usage;
        VERIFY_VK_RESULT("image fixture",init.disp.createImage(&info,nullptr,&image));
        VkMemoryRequirements requirements;init.disp.getImageMemoryRequirements(image,&requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;
        allocation.memoryTypeIndex=find_memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VERIFY_VK_RESULT("image fixture memory",init.disp.allocateMemory(&allocation,nullptr,&memory));
        VERIFY_VK_RESULT("image fixture bind",init.disp.bindImageMemory(image,memory,0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;vi.subresourceRange={aspect,0,levels,0,1};
        VERIFY_VK_RESULT("image fixture view",init.disp.createImageView(&vi,nullptr,&view));
    };
    make(texture_,texture_memory_,texture_view_,VK_FORMAT_R8G8B8A8_UNORM,{8,8,1},4,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
    make(depth_,depth_memory_,depth_view_,VK_FORMAT_D24_UNORM_S8_UINT,{init.swapchain.extent.width,init.swapchain.extent.height,1},1,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;sampler.maxLod=3;
    VERIFY_VK_RESULT("image fixture sampler",init.disp.createSampler(&sampler,nullptr,&texture_sampler_));
}
void App::prepare_texture(VkCommandBuffer command){
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=texture_;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,4,0,1};barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    init.disp.cmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    const VkClearColorValue colors[]={{{1,0,0,1}},{{0,1,0,1}},{{0,0,1,1}},{{1,1,0,1}}};
    for(uint32_t mip=0;mip<4;++mip){VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,mip,1,0,1};init.disp.cmdClearColorImage(command,texture_,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&colors[mip],1,&range);}
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    init.disp.cmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
}
}
