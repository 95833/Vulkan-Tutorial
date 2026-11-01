#pragma once
#include "resources.hpp"
#include <cstddef>
#include <string>

namespace lab {
struct Vertex { float position[3]; float color[3]; float uv[2]; };
static_assert(sizeof(Vertex) == 32, "vertex stride is 32 bytes");
static_assert(offsetof(Vertex, position) == 0 && offsetof(Vertex, color) == 12 && offsetof(Vertex, uv) == 24,
              "vertex fields must match the declared offsets");
enum class VertexLayout { None, Color, Textured };
struct ShaderOptions { float lod = 0; int32_t layer = 0; };
static_assert(sizeof(ShaderOptions) == 8 && offsetof(ShaderOptions, layer) == 4, "push constant offsets");

struct RenderTarget {
    Image color;
    Image depth;
    Buffer readback;
    VkImageLayout color_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
struct DescriptorBindings {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets;
    DescriptorBindings() = default;
    DescriptorBindings(const DescriptorBindings&) = delete;
    DescriptorBindings& operator=(const DescriptorBindings&) = delete;
    DescriptorBindings(DescriptorBindings&& other) noexcept;
    ~DescriptorBindings();
};
struct Pipeline {
    VkDevice device = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    Pipeline() = default;
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&& other) noexcept;
    ~Pipeline();
};

RenderTarget create_render_target(Context& context, bool with_depth = false);
DescriptorBindings create_bindings(Context& context, const std::vector<Buffer*>& camera_buffers, const Image* texture = nullptr);
Pipeline create_graphics_pipeline(Context& context, const std::string& shader_directory,
    const std::string& vertex_shader, const std::string& fragment_shader,
    VertexLayout vertex_layout, VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE,
    bool push_options = false, VkFormat depth_format = VK_FORMAT_UNDEFINED, bool depth_test = true);
void begin_rendering(Context& context, VkCommandBuffer command, RenderTarget& target);
void end_rendering_and_copy(VkCommandBuffer command, RenderTarget& target);
void readback_ppm(const RenderTarget& target, const std::string& output);
} // namespace lab
