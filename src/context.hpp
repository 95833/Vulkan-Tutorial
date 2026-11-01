#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>

namespace lab {
constexpr uint32_t kWidth = 512, kHeight = 512;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

void check(VkResult result, const char* operation);
std::string version_string(uint32_t version);

// 这些字段只记录所有权；Vulkan 操作在下列同名函数中明确展开。
struct Context {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    uint32_t graphics_family = 0;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool validation = false;
    uint32_t validation_errors = 0, validation_warnings = 0;
    Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context();
};

void create_context(Context& context, bool validation, bool need_device = true);
void print_device_capabilities(VkPhysicalDevice physical);
// 前置条件：该command上一次提交已经完成（首次使用除外）。本函数不会替调用者等待。
void begin_commands(Context& context, VkCommandBuffer command);
void submit_and_wait(Context& context, VkCommandBuffer command);

// 必须在本轮资源创建之后、首次提交之前声明：异常退出也先等设备，后释放资源。
// 仅用于整次实验的收尾，绝不放在帧循环中。
struct CompletionGuard {
    Context& context;
    ~CompletionGuard() { if (context.device) vkDeviceWaitIdle(context.device); }
};
} // namespace lab
