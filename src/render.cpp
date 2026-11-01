#include "render.hpp"
#include "math.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace lab {
DescriptorBindings::DescriptorBindings(DescriptorBindings&& other) noexcept {
    device = other.device;
    layout = std::exchange(other.layout, VK_NULL_HANDLE);
    pool = std::exchange(other.pool, VK_NULL_HANDLE);
    sampler = std::exchange(other.sampler, VK_NULL_HANDLE);
    sets = std::move(other.sets);
}
DescriptorBindings::~DescriptorBindings() {
    if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
    if (sampler) vkDestroySampler(device, sampler, nullptr);
    if (layout) vkDestroyDescriptorSetLayout(device, layout, nullptr);
}
Pipeline::Pipeline(Pipeline&& other) noexcept {
    device = other.device;
    pipeline = std::exchange(other.pipeline, VK_NULL_HANDLE);
    layout = std::exchange(other.layout, VK_NULL_HANDLE);
}
Pipeline::~Pipeline() {
    if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
    if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
}

RenderTarget create_render_target(Context& context, bool with_depth) {
    RenderTarget target;
    target.color = create_image(context, kWidth, kHeight, kColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    target.readback = create_buffer(context, kWidth * kHeight * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    if (with_depth) {
        const VkFormat depth_format = choose_depth_format(context);
        target.depth = create_image(context, kWidth, kHeight, depth_format,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        std::cout << "Depth attachment format=" << depth_format << "; clearDepth=1; compareOp=LESS\n";
    }
    return target;
}

DescriptorBindings create_bindings(Context& context, const std::vector<Buffer*>& camera_buffers, const Image* texture) {
    DescriptorBindings bindings;
    bindings.device = context.device;
    const uint32_t count = static_cast<uint32_t>(camera_buffers.size());
    VkDescriptorSetLayoutBinding layout_bindings[2]{};
    layout_bindings[0].binding = 0;
    layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layout_bindings[0].descriptorCount = 1;
    layout_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layout_bindings[1].binding = 1;
    layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layout_bindings[1].descriptorCount = 1;
    layout_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = texture ? 2u : 1u;
    layout_info.pBindings = layout_bindings;
    check(vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr, &bindings.layout), "vkCreateDescriptorSetLayout");
    VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = count; pool_info.poolSizeCount = texture ? 2u : 1u; pool_info.pPoolSizes = pool_sizes;
    check(vkCreateDescriptorPool(context.device, &pool_info, nullptr, &bindings.pool), "vkCreateDescriptorPool");
    std::vector<VkDescriptorSetLayout> layouts(count, bindings.layout);
    bindings.sets.resize(count);
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = bindings.pool; allocate.descriptorSetCount = count; allocate.pSetLayouts = layouts.data();
    check(vkAllocateDescriptorSets(context.device, &allocate, bindings.sets.data()), "vkAllocateDescriptorSets");

    if (texture) {
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_NEAREST; sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.minLod = 0; sampler_info.maxLod = static_cast<float>(texture->mip_levels - 1);
        check(vkCreateSampler(context.device, &sampler_info, nullptr, &bindings.sampler), "vkCreateSampler");
    }
    for (uint32_t i = 0; i < count; ++i) {
        VkDescriptorBufferInfo buffer_info{camera_buffers[i]->buffer, 0, sizeof(CameraUniform)};
        VkDescriptorImageInfo image_info{};
        if (texture) image_info = {bindings.sampler, texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = bindings.sets[i]; writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &buffer_info;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = bindings.sets[i]; writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &image_info;
        vkUpdateDescriptorSets(context.device, texture ? 2u : 1u, writes, 0, nullptr);
    }
    return bindings;
}

struct ShaderModule {
    VkDevice device;
    VkShaderModule module = VK_NULL_HANDLE;
    ~ShaderModule() { if (module) vkDestroyShaderModule(device, module, nullptr); }
};
static void load_shader(ShaderModule& shader, const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open SPIR-V: " + path.string());
    const auto size = file.tellg();
    if (size <= 0 || size % 4 != 0) throw std::runtime_error("Invalid SPIR-V byte size: " + path.string());
    std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
    file.seekg(0); file.read(reinterpret_cast<char*>(words.data()), size);
    if (!file || words[0] != 0x07230203u) throw std::runtime_error("Invalid SPIR-V contents: " + path.string());
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = static_cast<size_t>(size); info.pCode = words.data();
    check(vkCreateShaderModule(shader.device, &info, nullptr, &shader.module), "vkCreateShaderModule");
}

Pipeline create_graphics_pipeline(Context& context, const std::string& shader_directory,
    const std::string& vertex_shader, const std::string& fragment_shader,
    VertexLayout vertex_layout, VkDescriptorSetLayout descriptor_layout,
    bool push_options, VkFormat depth_format, bool depth_test) {
    Pipeline result;
    result.device = context.device;
    ShaderModule vertex{context.device}, fragment{context.device};
    load_shader(vertex, std::filesystem::path(shader_directory) / (vertex_shader + ".spv"));
    load_shader(fragment, std::filesystem::path(shader_directory) / (fragment_shader + ".spv"));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex.module; stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment.module; stages[1].pName = "main";
    VkPushConstantRange push_range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShaderOptions)};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = descriptor_layout ? 1u : 0u;
    layout_info.pSetLayouts = descriptor_layout ? &descriptor_layout : nullptr;
    layout_info.pushConstantRangeCount = push_options ? 1u : 0u;
    layout_info.pPushConstantRanges = push_options ? &push_range : nullptr;
    check(vkCreatePipelineLayout(context.device, &layout_info, nullptr, &result.layout), "vkCreatePipelineLayout");

    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
    attributes[1] = vertex_layout == VertexLayout::Textured
        ? VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)}
        : VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (vertex_layout != VertexLayout::None) {
        vertex_input.vertexBindingDescriptionCount = 1; vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 2; vertex_input.pVertexAttributeDescriptions = attributes;
    }
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1; viewport.scissorCount = 1;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamic_states;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1; blend.pAttachments = &blend_attachment;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = depth_format != VK_FORMAT_UNDEFINED && depth_test;
    depth.depthWriteEnable = depth.depthTestEnable; depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &kColorFormat;
    rendering.depthAttachmentFormat = depth_format;
    VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.pNext = &rendering; pipeline_info.stageCount = 2; pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input; pipeline_info.pInputAssemblyState = &assembly;
    pipeline_info.pViewportState = &viewport; pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState = &multisample; pipeline_info.pColorBlendState = &blend;
    pipeline_info.pDepthStencilState = &depth; pipeline_info.pDynamicState = &dynamic;
    pipeline_info.layout = result.layout; pipeline_info.renderPass = VK_NULL_HANDLE;
    check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &result.pipeline), "vkCreateGraphicsPipelines");
    return result;
}

void begin_rendering(Context& context, VkCommandBuffer command, RenderTarget& target) {
    (void)context;
    const bool first_use = target.color_layout == VK_IMAGE_LAYOUT_UNDEFINED;
    transition_image(command, target.color.image, target.color_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        first_use ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_COPY_BIT,
        first_use ? 0 : VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    target.color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (target.depth.image) {
        const bool first_depth = target.depth_layout == VK_IMAGE_LAYOUT_UNDEFINED;
        transition_image(command, target.depth.image, target.depth_layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            first_depth ? VK_PIPELINE_STAGE_2_NONE : (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT),
            first_depth ? 0 : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);
        target.depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = target.color.view; color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color.float32[0] = 6.0f / 255.0f;
    color.clearValue.color.float32[1] = 9.0f / 255.0f;
    color.clearValue.color.float32[2] = 14.0f / 255.0f;
    color.clearValue.color.float32[3] = 1.0f;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = target.depth.view; depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil.depth = 1.0f;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, {kWidth, kHeight}}; info.layerCount = 1;
    info.colorAttachmentCount = 1; info.pColorAttachments = &color;
    info.pDepthAttachment = target.depth.image ? &depth : nullptr;
    vkCmdBeginRendering(command, &info);
    const VkViewport viewport{0, 0, static_cast<float>(kWidth), static_cast<float>(kHeight), 0, 1};
    const VkRect2D scissor{{0, 0}, {kWidth, kHeight}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
}

void end_rendering_and_copy(VkCommandBuffer command, RenderTarget& target) {
    vkCmdEndRendering(command);
    // A: 颜色附件写 -> 图像copy读；布局也必须改为TRANSFER_SRC。
    transition_image(command, target.color.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    target.color_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkBufferImageCopy copy{};
    copy.bufferRowLength = 0; copy.bufferImageHeight = 0; // 紧密排列，RGBA8每像素4字节。
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {kWidth, kHeight, 1};
    vkCmdCopyImageToBuffer(command, target.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.readback.buffer, 1, &copy);
    // B: Buffer copy写 -> host读；这并不代替host随后等待fence。
    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = target.readback.buffer; barrier.offset = 0; barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

void readback_ppm(const RenderTarget& target, const std::string& output) {
    const auto& readback = target.readback;
    void* mapped = nullptr;
    // 调用者必须先等待本次提交fence。fence解决执行完成，invalidate解决非coherent缓存。
    check(vkMapMemory(readback.device, readback.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map readback allocation");
    if (!(readback.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = readback.memory; range.offset = 0; range.size = VK_WHOLE_SIZE;
        const VkResult status = vkInvalidateMappedMemoryRanges(readback.device, 1, &range);
        if (status != VK_SUCCESS) {
            vkUnmapMemory(readback.device, readback.memory);
            check(status, "vkInvalidateMappedMemoryRanges");
        }
    }
    const auto* rgba = static_cast<const uint8_t*>(mapped);
    std::vector<uint8_t> rgb(kWidth * kHeight * 3);
    for (size_t i = 0; i < kWidth * kHeight; ++i) std::memcpy(rgb.data() + i * 3, rgba + i * 4, 3);
    vkUnmapMemory(readback.device, readback.memory);
    const std::filesystem::path path(output);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << kWidth << " " << kHeight << "\n255\n";
    file.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!file) throw std::runtime_error("Cannot save PPM: " + output);
    std::cout << "Saved " << output << " (" << kWidth << "x" << kHeight << ", P6 RGB)\n";
}
} // namespace lab
