#include "cmake/gui_gpu_profiling_sha256.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <vulkan/vulkan.h>

using Json = nlohmann::json;
static uint64_t Now(clockid_t clock) {
    timespec t{};
    if (clock_gettime(clock, &t))
        throw std::runtime_error("clock_gettime failed");
    return uint64_t(t.tv_sec) * 1000000000 + uint64_t(t.tv_nsec);
}
static void Check(VkResult result, const char *operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
static std::vector<uint32_t> Spirv(const std::string &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("shader unavailable");
    const auto bytes = input.tellg();
    if (bytes <= 0 || bytes % 4)
        throw std::runtime_error("invalid SPIR-V length");
    std::vector<uint32_t> words(size_t(bytes) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char *>(words.data()), bytes);
    if (!input)
        throw std::runtime_error("shader read failed");
    return words;
}
struct Vulkan {
    VkInstance instance{};
    VkDevice device{};
    VkQueue queue{};
    VkCommandPool pool{};
    VkCommandBuffer command{};
    VkFence fence{};
    VkImage image{};
    VkDeviceMemory imageMemory{}, readMemory{};
    VkImageView view{};
    VkRenderPass pass{};
    VkFramebuffer framebuffer{};
    VkShaderModule vertex{}, fragment{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkBuffer readback{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties memory{};
    ~Vulkan() {
        if (device) {
            vkDeviceWaitIdle(device);
            if (pipeline)
                vkDestroyPipeline(device, pipeline, nullptr);
            if (layout)
                vkDestroyPipelineLayout(device, layout, nullptr);
            if (vertex)
                vkDestroyShaderModule(device, vertex, nullptr);
            if (fragment)
                vkDestroyShaderModule(device, fragment, nullptr);
            if (framebuffer)
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            if (pass)
                vkDestroyRenderPass(device, pass, nullptr);
            if (view)
                vkDestroyImageView(device, view, nullptr);
            if (image)
                vkDestroyImage(device, image, nullptr);
            if (imageMemory)
                vkFreeMemory(device, imageMemory, nullptr);
            if (readback)
                vkDestroyBuffer(device, readback, nullptr);
            if (readMemory)
                vkFreeMemory(device, readMemory, nullptr);
            if (fence)
                vkDestroyFence(device, fence, nullptr);
            if (pool)
                vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }
    uint32_t Type(uint32_t bits, VkMemoryPropertyFlags flags) {
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        throw std::runtime_error("required memory type unavailable");
    }
};
int main(int argc, char **argv) {
    alarm(120);
    if (argc != 10) {
        fprintf(stderr, "mode vertex.spv fragment.spv ready go output.json "
                        "iterations frames fps\n");
        return 2;
    }
    Json report{{"mode", argv[1]}, {"pid", getpid()}, {"status", "initializing"}};
    const std::string output = argv[6];
    try {
        Vulkan vk;
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "gfxrecon-precision-calibration";
        application.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo createInstance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInstance.pApplicationInfo = &application;
        Check(vkCreateInstance(&createInstance, nullptr, &vk.instance), "vkCreateInstance");
        uint32_t physicalCount = 0;
        Check(vkEnumeratePhysicalDevices(vk.instance, &physicalCount, nullptr), "vkEnumeratePhysicalDevices");
        if (!physicalCount)
            throw std::runtime_error("no physical device");
        std::vector<VkPhysicalDevice> physical(physicalCount);
        Check(vkEnumeratePhysicalDevices(vk.instance, &physicalCount, physical.data()),
              "vkEnumeratePhysicalDevices");
        vk.physical = physical[0];
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(vk.physical, &properties);
        vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memory);
        auto getFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceFeatures2"));
        auto getProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            vkGetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceProperties2"));
        if (!getFeatures2 || !getProperties2)
            throw std::runtime_error("Vulkan 1.2 queries unavailable");
        VkPhysicalDeviceShaderFloat16Int8Features arithmetic{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &arithmetic};
        getFeatures2(vk.physical, &features);
        VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver};
        getProperties2(vk.physical, &properties2);
        report["device"] = {{"name", properties.deviceName},
                            {"vendor_id", properties.vendorID},
                            {"device_id", properties.deviceID},
                            {"api_version", properties.apiVersion},
                            {"driver_version", properties.driverVersion},
                            {"driver_name", driver.driverName},
                            {"driver_info", driver.driverInfo},
                            {"shaderFloat16_supported", bool(arithmetic.shaderFloat16)}};
        if (properties.apiVersion < VK_API_VERSION_1_2 || !arithmetic.shaderFloat16) {
            report["status"] = "unsupported";
            std::ofstream(output) << report.dump(2);
            return 3;
        }
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &familyCount, families.data());
        uint32_t family = 0;
        while (family < familyCount && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            ++family;
        if (family == familyCount)
            throw std::runtime_error("no graphics queue");
        float priority = 1;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        arithmetic.shaderInt8 = VK_FALSE;
        arithmetic.shaderFloat16 = VK_TRUE;
        VkDeviceCreateInfo createDevice{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &arithmetic};
        createDevice.queueCreateInfoCount = 1;
        createDevice.pQueueCreateInfos = &queue;
        Check(vkCreateDevice(vk.physical, &createDevice, nullptr, &vk.device), "vkCreateDevice");
        vkGetDeviceQueue(vk.device, family, 0, &vk.queue);
        constexpr uint32_t size = 512;
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = VK_FORMAT_R8G8B8A8_UNORM;
        image.extent = {size, size, 1};
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Check(vkCreateImage(vk.device, &image, nullptr, &vk.image), "vkCreateImage");
        VkMemoryRequirements required{};
        vkGetImageMemoryRequirements(vk.device, vk.image, &required);
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = required.size;
        allocate.memoryTypeIndex = vk.Type(required.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(vk.device, &allocate, nullptr, &vk.imageMemory), "vkAllocateMemory(image)");
        Check(vkBindImageMemory(vk.device, vk.image, vk.imageMemory, 0), "vkBindImageMemory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = vk.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = image.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        Check(vkCreateImageView(vk.device, &view, nullptr, &vk.view), "vkCreateImageView");
        VkAttachmentDescription attachment{};
        attachment.format = image.format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = 1;
        pass.pAttachments = &attachment;
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        Check(vkCreateRenderPass(vk.device, &pass, nullptr, &vk.pass), "vkCreateRenderPass");
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = vk.pass;
        framebuffer.attachmentCount = 1;
        framebuffer.pAttachments = &vk.view;
        framebuffer.width = size;
        framebuffer.height = size;
        framebuffer.layers = 1;
        Check(vkCreateFramebuffer(vk.device, &framebuffer, nullptr, &vk.framebuffer), "vkCreateFramebuffer");
        auto vertex = Spirv(argv[2]), fragment = Spirv(argv[3]);
        report["vertex_sha256"] = gfx_gui_gpu::ShaderSha256(vertex.data(), vertex.size() * 4);
        report["fragment_sha256"] = gfx_gui_gpu::ShaderSha256(fragment.data(), fragment.size() * 4);
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize = vertex.size() * 4;
        shader.pCode = vertex.data();
        Check(vkCreateShaderModule(vk.device, &shader, nullptr, &vk.vertex), "vkCreateShaderModule(vertex)");
        shader.codeSize = fragment.size() * 4;
        shader.pCode = fragment.data();
        Check(vkCreateShaderModule(vk.device, &shader, nullptr, &vk.fragment),
              "vkCreateShaderModule(fragment)");
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        Check(vkCreatePipelineLayout(vk.device, &layout, nullptr, &vk.layout), "vkCreatePipelineLayout");
        VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                     {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vk.vertex;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = vk.fragment;
        stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vertices{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0, 0, float(size), float(size), 0, 1};
        VkRect2D scissor{{0, 0}, {size, size}};
        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = 1;
        blending.pAttachments = &blend;
        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = 2;
        pipeline.pStages = stages;
        pipeline.pVertexInputState = &vertices;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewportState;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &multisample;
        pipeline.pColorBlendState = &blending;
        pipeline.layout = vk.layout;
        pipeline.renderPass = vk.pass;
        Check(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &vk.pipeline),
              "vkCreateGraphicsPipelines");
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = family;
        Check(vkCreateCommandPool(vk.device, &pool, nullptr, &vk.pool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command.commandPool = vk.pool;
        command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command.commandBufferCount = 1;
        Check(vkAllocateCommandBuffers(vk.device, &command, &vk.command), "vkAllocateCommandBuffers");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        Check(vkCreateFence(vk.device, &fence, nullptr, &vk.fence), "vkCreateFence");
        VkBufferCreateInfo read{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        read.size = size * size * 4;
        read.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        Check(vkCreateBuffer(vk.device, &read, nullptr, &vk.readback), "vkCreateBuffer");
        vkGetBufferMemoryRequirements(vk.device, vk.readback, &required);
        allocate.allocationSize = required.size;
        allocate.memoryTypeIndex = vk.Type(required.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        Check(vkAllocateMemory(vk.device, &allocate, nullptr, &vk.readMemory), "vkAllocateMemory(read)");
        Check(vkBindBufferMemory(vk.device, vk.readback, vk.readMemory, 0), "vkBindBufferMemory");
        const uint32_t iterations = std::stoul(argv[7]), frames = std::stoul(argv[8]);
        const double fps = std::stod(argv[9]);
        if (!iterations || iterations > 8192 || !frames || frames > 120 || fps <= 0 || fps > 120)
            throw std::runtime_error("invalid bounded workload parameters");
        report["resolution"] = {size, size};
        report["iterations_per_fragment"] = iterations;
        report["frames_requested"] = frames;
        report["fps_requested"] = fps;
        report["shaderFloat16_enabled"] = true;
        {
            std::ofstream ready(argv[4]);
            ready << report.dump(2);
            if (!ready)
                throw std::runtime_error("ready-file write failed");
        }
        const auto waitDeadline = Now(CLOCK_MONOTONIC) + 60000000000ULL;
        while (access(argv[5], F_OK) != 0) {
            if (Now(CLOCK_MONOTONIC) >= waitDeadline)
                throw std::runtime_error("host counter-start handshake timed out");
            usleep(10000);
        }
        const auto period = uint64_t(1e9 / fps);
        uint64_t deadline = Now(CLOCK_MONOTONIC);
        report["window_begin_boot_ns"] = Now(CLOCK_BOOTTIME);
        Json sampleTimes = Json::array();
        uint32_t misses = 0;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            auto now = Now(CLOCK_MONOTONIC);
            if (now < deadline) {
                timespec target{time_t(deadline / 1000000000), long(deadline % 1000000000)};
                clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, nullptr);
            } else if (now > deadline + period) {
                ++misses;
                deadline = now;
            }
            deadline += period;
            const auto begin = Now(CLOCK_BOOTTIME);
            Check(vkResetCommandBuffer(vk.command, 0), "vkResetCommandBuffer");
            VkCommandBufferBeginInfo beginCommand{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            beginCommand.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vkBeginCommandBuffer(vk.command, &beginCommand), "vkBeginCommandBuffer");
            VkRenderPassBeginInfo beginPass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            beginPass.renderPass = vk.pass;
            beginPass.framebuffer = vk.framebuffer;
            beginPass.renderArea = {{0, 0}, {size, size}};
            vkCmdBeginRenderPass(vk.command, &beginPass, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline);
            struct {
                float seed;
                uint32_t iterations;
            } values{.125f, iterations};
            vkCmdPushConstants(vk.command, vk.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8, &values);
            vkCmdDraw(vk.command, 3, 1, 0, 0);
            vkCmdEndRenderPass(vk.command);
            Check(vkEndCommandBuffer(vk.command), "vkEndCommandBuffer");
            Check(vkResetFences(vk.device, 1, &vk.fence), "vkResetFences");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &vk.command;
            Check(vkQueueSubmit(vk.queue, 1, &submit, vk.fence), "vkQueueSubmit");
            Check(vkWaitForFences(vk.device, 1, &vk.fence, VK_TRUE, 5000000000ULL), "vkWaitForFences");
            sampleTimes.push_back(
                {{"frame", frame}, {"begin_boot_ns", begin}, {"end_boot_ns", Now(CLOCK_BOOTTIME)}});
        }
        report["window_end_boot_ns"] = Now(CLOCK_BOOTTIME);
        report["frames_completed"] = frames;
        report["missed_deadlines"] = misses;
        report["iterations"] = sampleTimes;
        // Readback is deliberately outside the counter comparison window.
        Check(vkResetCommandBuffer(vk.command, 0), "vkResetCommandBuffer(read)");
        VkCommandBufferBeginInfo beginRead{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Check(vkBeginCommandBuffer(vk.command, &beginRead), "vkBeginCommandBuffer(read)");
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = vk.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(vk.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {size, size, 1};
        vkCmdCopyImageToBuffer(vk.command, vk.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.readback, 1,
                               &copy);
        Check(vkEndCommandBuffer(vk.command), "vkEndCommandBuffer(read)");
        Check(vkResetFences(vk.device, 1, &vk.fence), "vkResetFences(read)");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &vk.command;
        Check(vkQueueSubmit(vk.queue, 1, &submit, vk.fence), "vkQueueSubmit(read)");
        Check(vkWaitForFences(vk.device, 1, &vk.fence, VK_TRUE, 5000000000ULL), "vkWaitForFences(read)");
        void *pixels = nullptr;
        Check(vkMapMemory(vk.device, vk.readMemory, 0, size * size * 4, 0, &pixels), "vkMapMemory");
        report["pixel_sha256"] = gfx_gui_gpu::ShaderSha256(pixels, size * size * 4);
        uint64_t sum = 0;
        uint8_t minimum = 255, maximum = 0;
        auto *bytes = static_cast<uint8_t *>(pixels);
        for (size_t i = 0; i < size * size * 4; ++i)
            if (i % 4 != 3) {
                sum += bytes[i];
                minimum = std::min(minimum, bytes[i]);
                maximum = std::max(maximum, bytes[i]);
            }
        report["pixel_rgb_mean"] = double(sum) / (size * size * 3);
        report["pixel_rgb_min"] = minimum;
        report["pixel_rgb_max"] = maximum;
        vkUnmapMemory(vk.device, vk.readMemory);
        report["status"] = "complete";
        std::ofstream(output) << report.dump(2);
        return 0;
    } catch (const std::exception &error) {
        report["status"] = "failed";
        report["error"] = error.what();
        std::ofstream(output) << report.dump(2);
        fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
