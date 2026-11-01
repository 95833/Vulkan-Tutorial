#pragma once
#include "render.hpp"
#include <string>

namespace lab {
struct Options {
    std::string lesson = "triangle";
    std::string output = "out/triangle.ppm";
    std::string shader_directory;
    bool validation = false;
    bool no_depth = false;
    float shift = 0.0f;
    float lod = 0.0f;
    uint32_t layer = 0;
    uint32_t frames = 9;
    uint32_t inflight = 1;
};

// 第13章：slot索引不是frame编号。每个slot持有所有会在帧间改写的资源。
struct FrameSlot {
    VkDevice device = VK_NULL_HANDLE;
    RenderTarget target;
    Buffer uniform;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    int64_t pending_frame = -1;
    FrameSlot() = default;
    FrameSlot(const FrameSlot&) = delete;
    FrameSlot& operator=(const FrameSlot&) = delete;
    ~FrameSlot();
};

void run_probe(Context& context, const Options& options);
void run_clear(Context& context, const Options& options);
void run_triangle(Context& context, const Options& options);
void run_vertex(Context& context, const Options& options);
void run_camera(Context& context, const Options& options);
void run_texture(Context& context, const Options& options);
void run_depth(Context& context, const Options& options);
void run_mips(Context& context, const Options& options);
void run_layers(Context& context, const Options& options);
void run_frames(Context& context, const Options& options);
} // namespace lab
