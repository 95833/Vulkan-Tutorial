#include "lessons.hpp"
#include "math.hpp"
#include <array>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace lab {
static const std::array<Vertex, 3> triangle_vertices = {{
    {{ 0.00f,-0.75f,0.0f}, {1,0,0}, {0.5f,0}},
    {{ 0.75f, 0.65f,0.0f}, {0,1,0}, {1,1}},
    {{-0.75f, 0.65f,0.0f}, {0,0,1}, {0,1}}
}};

static std::array<Vertex, 6> quad_vertices(float half_size) {
    const float s = half_size;
    return {{
        {{-s,-s,0}, {1,0,0}, {0,0}}, {{ s,-s,0}, {0,1,0}, {1,0}}, {{ s, s,0}, {0,0,1}, {1,1}},
        {{-s,-s,0}, {1,0,0}, {0,0}}, {{ s, s,0}, {0,0,1}, {1,1}}, {{-s, s,0}, {1,1,0}, {0,1}}
    }};
}

void run_probe(Context& context, const Options&) {
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(context.instance, &count, nullptr), "probe physical device count");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(context.instance, &count, devices.data()), "probe physical devices");
    std::cout << "Physical device count: " << count << "\n";
    for (auto device : devices) print_device_capabilities(device);
    std::cout << "Probe complete; no image produced.\n";
}

void run_clear(Context& context, const Options& options) {
    // 第5章：没有shader、pipeline或draw；附件loadOp完成清屏。
    RenderTarget target = create_render_target(context);
    CompletionGuard completion{context};
    begin_commands(context, context.command);
    begin_rendering(context, context.command, target);
    end_rendering_and_copy(context.command, target);
    submit_and_wait(context, context.command);
    readback_ppm(target, options.output);
}

void run_triangle(Context& context, const Options& options) {
    // 第6章：顶点由gl_VertexIndex提供；vertex input仍为空。
    RenderTarget target = create_render_target(context);
    Pipeline pipeline = create_graphics_pipeline(context, options.shader_directory,
        "triangle.vert", "color.frag", VertexLayout::None);
    CompletionGuard completion{context};
    begin_commands(context, context.command);
    begin_rendering(context, context.command, target);
    vkCmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    vkCmdDraw(context.command, 3, 1, 0, 0);
    end_rendering_and_copy(context.command, target);
    submit_and_wait(context, context.command);
    readback_ppm(target, options.output);
}

void run_vertex(Context& context, const Options& options) {
    // 第7章：CPU数组 -> VkBuffer -> binding/attribute -> vertex shader。
    RenderTarget target = create_render_target(context);
    Buffer vertices = create_buffer(context, sizeof(triangle_vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    write_buffer(vertices, triangle_vertices.data(), sizeof(triangle_vertices));
    Pipeline pipeline = create_graphics_pipeline(context, options.shader_directory,
        "vertex.vert", "color.frag", VertexLayout::Color);
    CompletionGuard completion{context};
    begin_commands(context, context.command);
    begin_rendering(context, context.command, target);
    vkCmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(context.command, 0, 1, &vertices.buffer, &offset);
    vkCmdDraw(context.command, 3, 1, 0, 0);
    end_rendering_and_copy(context.command, target);
    submit_and_wait(context, context.command);
    readback_ppm(target, options.output);
}

void run_camera(Context& context, const Options& options) {
    // 第8章：64字节std140 mat4 UBO，由descriptor binding 0暴露给vertex stage。
    RenderTarget target = create_render_target(context);
    const auto mesh = quad_vertices(0.55f);
    Buffer vertices = create_buffer(context, sizeof(mesh), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    write_buffer(vertices, mesh.data(), sizeof(mesh));
    Buffer uniform = create_buffer(context, sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    const auto camera = make_camera_matrix(options.shift);
    write_buffer(uniform, &camera, sizeof(camera));
    DescriptorBindings bindings = create_bindings(context, {&uniform});
    Pipeline pipeline = create_graphics_pipeline(context, options.shader_directory,
        "camera.vert", "color.frag", VertexLayout::Color, bindings.layout);
    CompletionGuard completion{context};
    begin_commands(context, context.command);
    begin_rendering(context, context.command, target);
    vkCmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(context.command, 0, 1, &vertices.buffer, &offset);
    vkCmdBindDescriptorSets(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, bindings.sets.data(), 0, nullptr);
    vkCmdDraw(context.command, 6, 1, 0, 0);
    end_rendering_and_copy(context.command, target);
    submit_and_wait(context, context.command);
    readback_ppm(target, options.output);
    std::cout << "Scene NDC shift=" << options.shift << "; positive means image-right\n";
}

void run_texture(Context& context, const Options& options) {
    // 第9章：纹理必须先上传和转换layout；descriptor写入并不搬运像素。
    RenderTarget target = create_render_target(context);
    const auto mesh = quad_vertices(0.8f);
    Buffer vertices = create_buffer(context, sizeof(mesh), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    write_buffer(vertices, mesh.data(), sizeof(mesh));
    Buffer uniform = create_buffer(context, sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    const auto camera = make_camera_matrix(0);
    write_buffer(uniform, &camera, sizeof(camera));
    Image texture = create_image(context, 64, 64, kColorFormat,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    upload_texture(context, texture, make_texture_pixels(1), false);
    DescriptorBindings bindings = create_bindings(context, {&uniform}, &texture);
    Pipeline pipeline = create_graphics_pipeline(context, options.shader_directory,
        "texture.vert", "texture.frag", VertexLayout::Textured, bindings.layout);
    CompletionGuard completion{context};
    begin_commands(context, context.command);
    begin_rendering(context, context.command, target);
    vkCmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(context.command, 0, 1, &vertices.buffer, &offset);
    vkCmdBindDescriptorSets(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, bindings.sets.data(), 0, nullptr);
    vkCmdDraw(context.command, 6, 1, 0, 0);
    end_rendering_and_copy(context.command, target);
    submit_and_wait(context, context.command);
    readback_ppm(target, options.output);
}

void run_mips(Context& context, const Options& options) {
    // 第10章A：分配7级不等于生成7级；upload_texture逐级执行真实linear blit。
    RenderTarget target = create_render_target(context);
    const auto mesh = quad_vertices(0.8f);
    Buffer vertices = create_buffer(context, sizeof(mesh), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    write_buffer(vertices, mesh.data(), sizeof(mesh));
    Buffer uniform = create_buffer(context, sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    const auto camera = make_camera_matrix(0);
    write_buffer(uniform, &camera, sizeof(camera));
    Image texture = create_image(context, 64, 64, kColorFormat,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 7, 1);
    upload_texture(context, texture, make_texture_pixels(1), true);
    DescriptorBindings bindings = create_bindings(context, {&uniform}, &texture);
    Pipeline pipeline = create_graphics_pipeline(context, options.shader_directory,
        "texture.vert", "mips.frag", VertexLayout::Textured, bindings.layout, true);
    CompletionGuard completion{context};
    begin_commands(context, context.command);
    begin_rendering(context, context.command, target);
    vkCmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(context.command, 0, 1, &vertices.buffer, &offset);
    vkCmdBindDescriptorSets(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, bindings.sets.data(), 0, nullptr);
    const ShaderOptions shader_options{options.lod, 0};
    vkCmdPushConstants(context.command, pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(shader_options), &shader_options);
    vkCmdDraw(context.command, 6, 1, 0, 0);
    end_rendering_and_copy(context.command, target);
    submit_and_wait(context, context.command);
    readback_ppm(target, options.output);
    std::cout << "Explicit sampled LOD=" << options.lod << "; mipLevels=7\n";
}

void run_layers(Context& context, const Options& options) {
    // 第10章B：一个Image、3个array layer、一个2D_ARRAY view、sampler2DArray。
    RenderTarget target = create_render_target(context);
    const auto mesh = quad_vertices(0.8f);
    Buffer vertices = create_buffer(context, sizeof(mesh), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    write_buffer(vertices, mesh.data(), sizeof(mesh));
    Buffer uniform = create_buffer(context, sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    const auto camera = make_camera_matrix(0);
    write_buffer(uniform, &camera, sizeof(camera));
    Image texture = create_image(context, 64, 64, kColorFormat,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, 3);
    upload_texture(context, texture, make_texture_pixels(3), false);
    DescriptorBindings bindings = create_bindings(context, {&uniform}, &texture);
    Pipeline pipeline = create_graphics_pipeline(context, options.shader_directory,
        "texture.vert", "layers.frag", VertexLayout::Textured, bindings.layout, true);
    CompletionGuard completion{context};
    begin_commands(context, context.command);
    begin_rendering(context, context.command, target);
    vkCmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(context.command, 0, 1, &vertices.buffer, &offset);
    vkCmdBindDescriptorSets(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, bindings.sets.data(), 0, nullptr);
    const ShaderOptions shader_options{0, static_cast<int32_t>(options.layer)};
    vkCmdPushConstants(context.command, pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(shader_options), &shader_options);
    vkCmdDraw(context.command, 6, 1, 0, 0);
    end_rendering_and_copy(context.command, target);
    submit_and_wait(context, context.command);
    readback_ppm(target, options.output);
    std::cout << "Selected array layer=" << options.layer << "; arrayLayers=3\n";
}

void run_depth(Context& context, const Options& options) {
    // 第11章：近红面先画(z=.2)，远蓝面后画(z=.7)。中心重叠区检验LESS。
    const std::array<Vertex, 6> mesh = {{
        {{-0.75f, 0.60f,0.2f}, {1,0,0}, {0,0}},
        {{ 0.55f, 0.60f,0.2f}, {1,0,0}, {0,0}},
        {{-0.10f,-0.70f,0.2f}, {1,0,0}, {0,0}},
        {{-0.55f,-0.55f,0.7f}, {0,0,1}, {0,0}},
        {{ 0.75f,-0.55f,0.7f}, {0,0,1}, {0,0}},
        {{ 0.10f, 0.75f,0.7f}, {0,0,1}, {0,0}}
    }};
    RenderTarget target = create_render_target(context, true);
    Buffer vertices = create_buffer(context, sizeof(mesh), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    write_buffer(vertices, mesh.data(), sizeof(mesh));
    Pipeline pipeline = create_graphics_pipeline(context, options.shader_directory,
        "vertex.vert", "color.frag", VertexLayout::Color, VK_NULL_HANDLE, false, target.depth.format, !options.no_depth);
    CompletionGuard completion{context};
    begin_commands(context, context.command);
    begin_rendering(context, context.command, target);
    vkCmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(context.command, 0, 1, &vertices.buffer, &offset);
    vkCmdDraw(context.command, 3, 1, 0, 0); // near red, first
    vkCmdDraw(context.command, 3, 1, 3, 0); // far blue, second
    end_rendering_and_copy(context.command, target);
    submit_and_wait(context, context.command);
    readback_ppm(target, options.output);
    std::cout << "Depth test=" << (options.no_depth ? "OFF" : "LESS")
              << "; center expected=" << (options.no_depth ? "blue" : "red") << "\n";
}

FrameSlot::~FrameSlot() { if (fence) vkDestroyFence(device, fence, nullptr); }

static std::string frame_output_path(const std::string& output, uint32_t frame) {
    const std::filesystem::path path(output);
    std::ostringstream suffix;
    suffix << path.stem().string() << "_" << std::setw(3) << std::setfill('0') << frame << ".ppm";
    return (path.parent_path() / suffix.str()).string();
}

static void save_completed_frame(FrameSlot& slot, const Options& options) {
    if (slot.pending_frame < 0) return;
    const auto frame = static_cast<uint32_t>(slot.pending_frame);
    if (frame == 0 || frame == options.frames / 2 || frame == options.frames - 1)
        readback_ppm(slot.target, frame_output_path(options.output, frame));
    if (frame == options.frames - 1) readback_ppm(slot.target, options.output);
    slot.pending_frame = -1;
}

void run_frames(Context& context, const Options& options) {
    const auto mesh = quad_vertices(0.55f);
    Buffer vertices = create_buffer(context, sizeof(mesh), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    write_buffer(vertices, mesh.data(), sizeof(mesh));
    std::array<FrameSlot, 2> slots;
    std::vector<Buffer*> uniforms;
    for (uint32_t i = 0; i < options.inflight; ++i) {
        auto& slot = slots[i];
        slot.device = context.device;
        slot.target = create_render_target(context);
        slot.uniform = create_buffer(context, sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        uniforms.push_back(&slot.uniform);
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = context.command_pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(context.device, &allocate, &slot.command), "allocate frame slot command");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; // 首次使用slot没有前任工作，wait应立即通过。
        check(vkCreateFence(context.device, &fence_info, nullptr, &slot.fence), "create frame slot fence");
    }
    DescriptorBindings bindings = create_bindings(context, uniforms);
    Pipeline pipeline = create_graphics_pipeline(context, options.shader_directory,
        "camera.vert", "color.frag", VertexLayout::Color, bindings.layout);
    CompletionGuard completion{context};

    for (uint32_t frame = 0; frame < options.frames; ++frame) {
        const uint32_t slot_index = frame % options.inflight;
        auto& slot = slots[slot_index];
        std::cout << "Frame " << frame << ": slot=" << slot_index << " wait-before-reuse\n";
        check(vkWaitForFences(context.device, 1, &slot.fence, VK_TRUE, UINT64_MAX), "wait frame slot fence");
        // 读回前任帧在slot复用时进行。不会为了保存第0帧而马上等第0帧，破坏两槽提交。
        save_completed_frame(slot, options);
        const float t = options.frames == 1 ? 0.5f : static_cast<float>(frame) / static_cast<float>(options.frames - 1);
        const float shift = options.shift - 0.45f + 0.9f * t;
        const auto camera = make_camera_matrix(shift);
        write_buffer(slot.uniform, &camera, sizeof(camera)); // 只改已经fence完成的这个slot。
        begin_commands(context, slot.command);
        begin_rendering(context, slot.command, slot.target);
        vkCmdBindPipeline(slot.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(slot.command, 0, 1, &vertices.buffer, &offset);
        vkCmdBindDescriptorSets(slot.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &bindings.sets[slot_index], 0, nullptr);
        vkCmdDraw(slot.command, 6, 1, 0, 0);
        end_rendering_and_copy(slot.command, slot.target);
        check(vkEndCommandBuffer(slot.command), "end frame slot command");
        VkCommandBufferSubmitInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        command_info.commandBuffer = slot.command;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1; submit.pCommandBufferInfos = &command_info;
        check(vkResetFences(context.device, 1, &slot.fence), "reset fence just before frame submit");
        check(vkQueueSubmit2(context.graphics_queue, 1, &submit, slot.fence), "submit frame slot");
        slot.pending_frame = frame;
        std::cout << "Frame " << frame << ": slot=" << slot_index << " submitted; shift=" << shift << "\n";
        // 此处不wait。inflight=2时可以继续准备另一个slot；一条queue并不保证实际并行执行。
    }
    // drain：末尾尚未复用的slot也必须等待并保存，然后才能释放资源。
    for (uint32_t i = 0; i < options.inflight; ++i) {
        check(vkWaitForFences(context.device, 1, &slots[i].fence, VK_TRUE, UINT64_MAX), "drain frame slot");
        save_completed_frame(slots[i], options);
    }
    std::cout << "Frames complete: count=" << options.frames << "; inflight=" << options.inflight << "\n";
}
} // namespace lab
