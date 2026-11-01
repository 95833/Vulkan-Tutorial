#include "resources.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace lab {
Buffer::Buffer(Buffer&& other) noexcept { *this = std::move(other); }
Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        device = other.device; buffer = std::exchange(other.buffer, VK_NULL_HANDLE);
        memory = std::exchange(other.memory, VK_NULL_HANDLE);
        size = other.size; allocation_size = other.allocation_size; properties = other.properties;
    }
    return *this;
}
Buffer::~Buffer() {
    if (buffer) vkDestroyBuffer(device, buffer, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
}
Image::Image(Image&& other) noexcept { *this = std::move(other); }
Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (image) vkDestroyImage(device, image, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        device = other.device; image = std::exchange(other.image, VK_NULL_HANDLE);
        view = std::exchange(other.view, VK_NULL_HANDLE); memory = std::exchange(other.memory, VK_NULL_HANDLE);
        format = other.format; width = other.width; height = other.height;
        mip_levels = other.mip_levels; layers = other.layers;
    }
    return *this;
}
Image::~Image() {
    if (view) vkDestroyImageView(device, view, nullptr);
    if (image) vkDestroyImage(device, image, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
}

static uint32_t choose_memory_type(Context& context, uint32_t allowed,
                                   VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) {
    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < context.memory_properties.memoryTypeCount; ++i) {
            const auto flags = context.memory_properties.memoryTypes[i].propertyFlags;
            if ((allowed & (1u << i)) && (flags & required) == required &&
                (pass == 1 || (flags & preferred) == preferred)) return i;
        }
    }
    throw std::runtime_error("No compatible memory type.");
}

Buffer create_buffer(Context& context, VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible) {
    Buffer result;
    result.device = context.device; result.size = size;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(context.device, &info, nullptr, &result.buffer), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device, result.buffer, &requirements);
    const VkMemoryPropertyFlags required = host_visible ? VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT} : 0u;
    const auto preferred = host_visible ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const uint32_t memory_type = choose_memory_type(context, requirements.memoryTypeBits, required, preferred);
    result.properties = context.memory_properties.memoryTypes[memory_type].propertyFlags;
    result.allocation_size = requirements.size;
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.buffer = result.buffer;
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.pNext = &dedicated; allocate.allocationSize = requirements.size; allocate.memoryTypeIndex = memory_type;
    check(vkAllocateMemory(context.device, &allocate, nullptr, &result.memory), "allocate buffer memory");
    check(vkBindBufferMemory(context.device, result.buffer, result.memory, 0), "vkBindBufferMemory");
    std::cout << "Buffer: " << size << " bytes; memoryType=" << memory_type
              << "; hostCoherent=" << ((result.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0) << "\n";
    return result;
}

void write_buffer(const Buffer& buffer, const void* source, size_t bytes) {
    if (bytes > buffer.size) throw std::runtime_error("CPU write exceeds buffer size.");
    void* mapped = nullptr;
    // 独立 allocation、offset=0、映射整块：VK_WHOLE_SIZE 的 flush 满足 atom 对齐规则。
    check(vkMapMemory(buffer.device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map writable buffer");
    std::memcpy(mapped, source, bytes);
    if (!(buffer.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = buffer.memory; range.offset = 0; range.size = VK_WHOLE_SIZE;
        const VkResult status = vkFlushMappedMemoryRanges(buffer.device, 1, &range);
        vkUnmapMemory(buffer.device, buffer.memory);
        check(status, "vkFlushMappedMemoryRanges");
    } else vkUnmapMemory(buffer.device, buffer.memory);
}

Image create_image(Context& context, uint32_t width, uint32_t height, VkFormat format,
                   VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t mip_levels, uint32_t layers) {
    VkImageFormatProperties supported{};
    check(vkGetPhysicalDeviceImageFormatProperties(context.physical, format, VK_IMAGE_TYPE_2D,
          VK_IMAGE_TILING_OPTIMAL, usage, 0, &supported), "query image format/usage support");
    if (width > supported.maxExtent.width || height > supported.maxExtent.height ||
        mip_levels > supported.maxMipLevels || layers > supported.maxArrayLayers)
        throw std::runtime_error("Requested image dimensions/subresources unsupported.");
    Image result;
    result.device = context.device; result.format = format;
    result.width = width; result.height = height; result.mip_levels = mip_levels; result.layers = layers;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D; info.format = format; info.extent = {width, height, 1};
    info.mipLevels = mip_levels; info.arrayLayers = layers; info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(context.device, &info, nullptr, &result.image), "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context.device, result.image, &requirements);
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = result.image;
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.pNext = &dedicated; allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = choose_memory_type(context, requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(context.device, &allocate, nullptr, &result.memory), "allocate image memory");
    check(vkBindImageMemory(context.device, result.image, result.memory, 0), "vkBindImageMemory");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = result.image; view.viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view.format = format; view.subresourceRange = {aspect, 0, mip_levels, 0, layers};
    check(vkCreateImageView(context.device, &view, nullptr, &result.view), "vkCreateImageView");
    return result;
}

void transition_image(VkCommandBuffer command, VkImage image,
    VkImageLayout old_layout, VkImageLayout new_layout,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access,
    VkImageAspectFlags aspect, uint32_t base_mip, uint32_t mip_count, uint32_t base_layer, uint32_t layer_count) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = source_stage; barrier.srcAccessMask = source_access;
    barrier.dstStageMask = destination_stage; barrier.dstAccessMask = destination_access;
    barrier.oldLayout = old_layout; barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image; barrier.subresourceRange = {aspect, base_mip, mip_count, base_layer, layer_count};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

std::vector<uint8_t> make_texture_pixels(uint32_t layers) {
    std::vector<uint8_t> pixels(64 * 64 * layers * 4);
    for (uint32_t layer = 0; layer < layers; ++layer)
        for (uint32_t y = 0; y < 64; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                uint8_t color[4] = {240, 240, 240, 255};
                if (layer == 0 && ((x / 8 + y / 8) & 1u) == 0) {
                    color[0] = 230; color[1] = 50; color[2] = 40;
                } else if (layer == 1) {
                    const bool stripe = ((y / 8) & 1u) == 0;
                    color[0] = stripe ? 30 : 15; color[1] = stripe ? 220 : 25; color[2] = stripe ? 80 : 15;
                } else if (layer == 2) {
                    const bool stripe = ((x / 8) & 1u) == 0;
                    color[0] = stripe ? 40 : 245; color[1] = stripe ? 90 : 215; color[2] = stripe ? 230 : 40;
                }
                std::memcpy(pixels.data() + ((layer * 64 + y) * 64 + x) * 4, color, 4);
            }
    return pixels;
}

void upload_texture(Context& context, Image& texture, const std::vector<uint8_t>& pixels, bool generate_mips) {
    if (generate_mips) {
        VkFormatProperties p{};
        vkGetPhysicalDeviceFormatProperties(context.physical, texture.format, &p);
        const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((p.optimalTilingFeatures & required) != required)
            throw std::runtime_error("RGBA8 linear blit mip generation is unsupported by this device.");
    }
    Buffer staging = create_buffer(context, pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    write_buffer(staging, pixels.data(), pixels.size());
    CompletionGuard completion{context};
    begin_commands(context, context.command);
    transition_image(context.command, texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.mip_levels, 0, texture.layers);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, texture.layers};
    copy.imageExtent = {texture.width, texture.height, 1};
    vkCmdCopyBufferToImage(context.command, staging.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    if (generate_mips) {
        int32_t source_width = static_cast<int32_t>(texture.width), source_height = static_cast<int32_t>(texture.height);
        for (uint32_t level = 1; level < texture.mip_levels; ++level) {
            // 上一级：TRANSFER_DST -> TRANSFER_SRC。第1级前源写来自copy，其后来自blit。
            transition_image(context.command, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, texture.layers);
            const int32_t destination_width = std::max(1, source_width / 2);
            const int32_t destination_height = std::max(1, source_height / 2);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, texture.layers};
            blit.srcOffsets[1] = {source_width, source_height, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, texture.layers};
            blit.dstOffsets[1] = {destination_width, destination_height, 1};
            vkCmdBlitImage(context.command, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            transition_image(context.command, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, texture.layers);
            std::cout << "Generated mip " << level << ": " << destination_width << "x" << destination_height << " via linear blit\n";
            source_width = destination_width; source_height = destination_height;
        }
    }
    transition_image(context.command, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, texture.mip_levels - 1, 1, 0, texture.layers);
    submit_and_wait(context, context.command);
    // staging 至此才可以释放；descriptor 更新本身没有完成以上上传/布局转换。
}

VkFormat choose_depth_format(Context& context) {
    for (VkFormat format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties p{};
        vkGetPhysicalDeviceFormatProperties(context.physical, format, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return format;
    }
    throw std::runtime_error("No supported depth attachment format.");
}
} // namespace lab
