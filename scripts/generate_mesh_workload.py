"""Generate a validation derivative without modifying pinned upstream sources."""
from pathlib import Path
import re
import shutil
import subprocess
import sys


def main():
    upstream, output, compiler = map(Path, sys.argv[1:])
    app = upstream / 'test/test_apps/triangle'
    target = output / 'gui-mesh'
    target.mkdir(exist_ok=True)
    source = (app / 'app.cpp').read_text()
    header = (app / 'triangle_app.h').read_text()

    def replace(before, after):
        nonlocal source
        if before not in source:
            raise ValueError('Pinned upstream fixture changed: ' + before[:80])
        source = source.replace(before, after)

    header = header.replace('    VkQueue graphics_queue_;', '''    VkBuffer mesh_vertices_, mesh_indices_, mesh_uniform_;
    VkDeviceMemory mesh_vertex_memory_, mesh_index_memory_, mesh_uniform_memory_;
    VkDescriptorSetLayout mesh_layout_;
    VkDescriptorPool mesh_pool_;
    VkDescriptorSet mesh_set_;
    void create_mesh_resources();
    VkQueue graphics_queue_;''')
    replace('triangle/shaders/vert.spv', 'mesh-preview/shaders/vert.spv')
    replace('triangle/shaders/frag.spv', 'mesh-preview/shaders/frag.spv')
    marker = '    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};'
    replace(marker, '''    VkVertexInputBindingDescription mesh_binding{0, 12, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription mesh_attribute{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
''' + marker)
    source = re.sub(r'(vertex_input_info\.vertexBindingDescriptionCount\s*=\s*)0;', r'\g<1>1;', source)
    source = re.sub(r'(vertex_input_info\.vertexAttributeDescriptionCount\s*=\s*)0;',
                    r'\g<1>1;\n    vertex_input_info.pVertexBindingDescriptions = &mesh_binding;\n    vertex_input_info.pVertexAttributeDescriptions = &mesh_attribute;', source)
    source = re.sub(r'(pipeline_layout_info\.setLayoutCount\s*=\s*)0;',
                    r'\g<1>1;\n    pipeline_layout_info.pSetLayouts = &mesh_layout_;', source)
    replace('    create_graphics_pipeline();', '    create_mesh_resources();\n    create_graphics_pipeline();')
    replace('        init.disp.cmdDraw(command_buffer, 3, 1, 0, 0);', '''        const VkDeviceSize mesh_offset = 0;
        init.disp.cmdBindVertexBuffers(command_buffer, 0, 1, &mesh_vertices_, &mesh_offset);
        init.disp.cmdBindIndexBuffer(command_buffer, mesh_indices_, 0, VK_INDEX_TYPE_UINT16);
        init.disp.cmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &mesh_set_, 0, nullptr);
        init.disp.cmdDrawIndexed(command_buffer, 36, 1, 0, 0, 0);''')
    replace('void App::cleanup()\n{', '''void App::cleanup()
{
    init.disp.destroyDescriptorPool(mesh_pool_, nullptr);
    init.disp.destroyDescriptorSetLayout(mesh_layout_, nullptr);
    init.disp.destroyBuffer(mesh_vertices_, nullptr); init.disp.freeMemory(mesh_vertex_memory_, nullptr);
    init.disp.destroyBuffer(mesh_indices_, nullptr); init.disp.freeMemory(mesh_index_memory_, nullptr);
    init.disp.destroyBuffer(mesh_uniform_, nullptr); init.disp.freeMemory(mesh_uniform_memory_, nullptr);''')
    resources = r'''
void App::create_mesh_resources() {
    const float vertices[] = {-1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1, -1,-1,1, 1,-1,1, 1,1,1, -1,1,1};
    const uint16_t indices[] = {0,1,2,2,3,0, 4,6,5,4,7,6, 0,4,5,0,5,1, 3,2,6,3,6,7, 0,3,7,0,7,4, 1,5,6,1,6,2};
    const float mvp[] = {.5f,0,0,0, 0,.5f,0,0, .15f,.1f,.2f,.25f, 0,0,.5f,1};
    auto upload = [&](const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory) {
        VkBufferCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; info.size=size; info.usage=usage;
        VERIFY_VK_RESULT("mesh buffer", init.disp.createBuffer(&info,nullptr,&buffer));
        VkMemoryRequirements requirements; init.disp.getBufferMemoryRequirements(buffer,&requirements);
        VkMemoryAllocateInfo allocation{}; allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocation.allocationSize=requirements.size;
        allocation.memoryTypeIndex=find_memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VERIFY_VK_RESULT("mesh memory",init.disp.allocateMemory(&allocation,nullptr,&memory));
        VERIFY_VK_RESULT("mesh bind",init.disp.bindBufferMemory(buffer,memory,0));
        void* mapped=nullptr;VERIFY_VK_RESULT("mesh map",init.disp.mapMemory(memory,0,size,0,&mapped));memcpy(mapped,data,size);init.disp.unmapMemory(memory);
    };
    upload(vertices,sizeof(vertices),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,mesh_vertices_,mesh_vertex_memory_);
    upload(indices,sizeof(indices),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,mesh_indices_,mesh_index_memory_);
    upload(mvp,sizeof(mvp),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,mesh_uniform_,mesh_uniform_memory_);
    VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;layout.bindingCount=1;layout.pBindings=&binding;
    VERIFY_VK_RESULT("mesh descriptor layout",init.disp.createDescriptorSetLayout(&layout,nullptr,&mesh_layout_));
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1};
    VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&size;
    VERIFY_VK_RESULT("mesh descriptor pool",init.disp.createDescriptorPool(&pool,nullptr,&mesh_pool_));
    VkDescriptorSetAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate.descriptorPool=mesh_pool_;allocate.descriptorSetCount=1;allocate.pSetLayouts=&mesh_layout_;
    VERIFY_VK_RESULT("mesh descriptor allocation",init.disp.allocateDescriptorSets(&allocate,&mesh_set_));
    VkDescriptorBufferInfo buffer{mesh_uniform_,0,sizeof(mvp)};
    VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=mesh_set_;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;write.pBufferInfo=&buffer;
    init.disp.updateDescriptorSets(1,&write,0,nullptr);
}
'''
    replace('void App::create_render_pass()', resources + '\nvoid App::create_render_pass()')
    source = '#include <cstring>\n' + source
    (target / 'app.cpp').write_text(source)
    (target / 'triangle_app.h').write_text(header)
    res = target / 'res/mesh-preview/shaders'
    res.mkdir(parents=True, exist_ok=True)
    shader = Path(__file__).resolve().parents[1] / 'tests/workloads/mesh.vert'
    subprocess.run([str(compiler), '-V', '--target-env', 'vulkan1.1', str(shader), '-o', str(res / 'vert.spv')], check=True)
    shutil.copy2(upstream / 'test/test_apps/res/triangle/shaders/frag.spv', res / 'frag.spv')

    # Three spatially disjoint GPU argument records make isolation failures visible.
    indirect = output / 'gui-indirect'
    indirect.mkdir(exist_ok=True)
    indirect_header = header.replace('    VkQueue graphics_queue_;', '''    VkBuffer indirect_buffer_;
    VkDeviceMemory indirect_memory_;
    void configure_physical_device_selector(gfxrecon::test::PhysicalDeviceSelector&, vkmock::TestConfig*) override;
    VkQueue graphics_queue_;''')
    indirect_source = source.replace(
        'init.disp.cmdDrawIndexed(command_buffer, 36, 1, 0, 0, 0);',
        'init.disp.cmdDrawIndexedIndirect(command_buffer, indirect_buffer_, 0, 3, sizeof(VkDrawIndexedIndirectCommand));')
    indirect_source = indirect_source.replace('VK_CULL_MODE_BACK_BIT', 'VK_CULL_MODE_NONE')
    indirect_source = re.sub(r'const float vertices\[\] = \{[^}]+\};',
        'const float vertices[] = {-.9f,-.6f,.5f, -.7f,.6f,.5f, -.5f,-.6f,.5f, -.2f,-.6f,.5f, 0,.6f,.5f, .2f,-.6f,.5f, .5f,-.6f,.5f, .7f,.6f,.5f, .9f,-.6f,.5f};', indirect_source)
    indirect_source = re.sub(r'const uint16_t indices\[\] = \{[^}]+\};',
        'const uint16_t indices[] = {0,1,2,3,4,5,6,7,8};', indirect_source)
    indirect_source = re.sub(r'const float mvp\[\] = \{[^}]+\};',
        'const float mvp[] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};', indirect_source)
    indirect_source = indirect_source.replace('    upload(vertices,sizeof(vertices),', '''    const VkDrawIndexedIndirectCommand draws[] = {{3,1,0,0,0},{3,1,3,0,0},{3,1,6,0,0}};
    upload(draws,sizeof(draws),VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,indirect_buffer_,indirect_memory_);
    upload(vertices,sizeof(vertices),''')
    indirect_source = indirect_source.replace('    init.disp.destroyDescriptorPool(mesh_pool_, nullptr);', '''    init.disp.destroyBuffer(indirect_buffer_, nullptr); init.disp.freeMemory(indirect_memory_, nullptr);
    init.disp.destroyDescriptorPool(mesh_pool_, nullptr);''')
    indirect_source = indirect_source.replace('void App::create_mesh_resources() {', '''void App::configure_physical_device_selector(gfxrecon::test::PhysicalDeviceSelector& selector, vkmock::TestConfig* config) {
    TestAppBase::configure_physical_device_selector(selector,config);
    VkPhysicalDeviceFeatures features{};features.multiDrawIndirect=VK_TRUE;
    selector.set_required_features(features);
}
void App::create_mesh_resources() {''')
    (indirect / 'app.cpp').write_text(indirect_source)
    (indirect / 'triangle_app.h').write_text(indirect_header)


if __name__ == '__main__':
    main()
