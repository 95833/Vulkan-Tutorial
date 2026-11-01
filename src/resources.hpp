#pragma once
#include "context.hpp"
#include <cstddef>
#include <vector>

namespace lab {
struct Buffer {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0, allocation_size = 0;
    VkMemoryPropertyFlags properties = 0;
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    ~Buffer();
};
struct Image {
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0, height = 0, mip_levels = 1, layers = 1;
    Image() = default;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    ~Image();
};

Buffer create_buffer(Context& context, VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible);
void write_buffer(const Buffer& buffer, const void* source, size_t bytes);
Image create_image(Context& context, uint32_t width, uint32_t height, VkFormat format,
                   VkImageUsageFlags usage, VkImageAspectFlags aspect,
                   uint32_t mip_levels = 1, uint32_t layers = 1);

// 仅把明确的同步参数装入 VkImageMemoryBarrier2；不猜测、不跟踪隐含状态。
void transition_image(VkCommandBuffer command, VkImage image,
    VkImageLayout old_layout, VkImageLayout new_layout,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    uint32_t base_mip = 0, uint32_t mip_count = 1,
    uint32_t base_layer = 0, uint32_t layer_count = 1);

std::vector<uint8_t> make_texture_pixels(uint32_t layers);
void upload_texture(Context& context, Image& texture, const std::vector<uint8_t>& pixels, bool generate_mips);
VkFormat choose_depth_format(Context& context);
} // namespace lab
