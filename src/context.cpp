#include "context.hpp"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace lab {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed, VkResult=" + std::to_string(result));
}
std::string version_string(uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." +
           std::to_string(VK_API_VERSION_MINOR(v)) + "." +
           std::to_string(VK_API_VERSION_PATCH(v));
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* user_data) {
    auto& context = *static_cast<Context*>(user_data);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++context.validation_errors;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ++context.validation_warnings;
    std::cerr << "[validation "
              << ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" : "WARNING")
              << "] " << (data->pMessageIdName ? data->pMessageIdName : "(no id)")
              << "\n" << (data->pMessage ? data->pMessage : "") << "\n";
    return VK_FALSE;
}

static bool has_instance_extension(const char* name, const char* layer = nullptr) {
    uint32_t count = 0;
    check(vkEnumerateInstanceExtensionProperties(layer, &count, nullptr), "enumerate instance extensions");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateInstanceExtensionProperties(layer, &count, extensions.data()), "enumerate instance extensions");
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& p) {
        return std::strcmp(p.extensionName, name) == 0;
    });
}

void print_device_capabilities(VkPhysicalDevice physical) {
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &driver;
    vkGetPhysicalDeviceProperties2(physical, &properties);
    const auto& p = properties.properties;
    std::cout << "Device: " << p.deviceName << "\n"
              << "  API: " << version_string(p.apiVersion)
              << "; type: " << (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? "CPU/software" : "non-CPU device") << "\n"
              << "  driverName: " << driver.driverName << "; driverInfo: " << driver.driverInfo << "\n";
    if (p.apiVersion >= VK_API_VERSION_1_3) {
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &f13;
        vkGetPhysicalDeviceFeatures2(physical, &features);
        std::cout << "  dynamicRendering=" << f13.dynamicRendering
                  << "; synchronization2=" << f13.synchronization2
                  << "; maintenance4=" << f13.maintenance4 << "\n";
    } else {
        std::cout << "  Vulkan 1.3 core path unavailable on this device\n";
    }
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
    for (uint32_t i = 0; i < count; ++i)
        std::cout << "  queueFamily[" << i << "]: count=" << queues[i].queueCount
                  << "; flags=0x" << std::hex << queues[i].queueFlags << std::dec << "\n";
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        std::cout << "  memoryType[" << i << "]: heap=" << memory.memoryTypes[i].heapIndex
                  << "; flags=0x" << std::hex << memory.memoryTypes[i].propertyFlags << std::dec << "\n";
    std::cout << "  nonCoherentAtomSize=" << p.limits.nonCoherentAtomSize << "\n";
    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(physical, kColorFormat, &format);
    std::cout << "  RGBA8_UNORM optimalTilingFeatures=0x" << std::hex
              << format.optimalTilingFeatures << std::dec << "\n";
}

void create_context(Context& context, bool validation, bool need_device) {
    uint32_t loader_version = VK_API_VERSION_1_0;
    auto enumerate_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerate_version) check(enumerate_version(&loader_version), "vkEnumerateInstanceVersion");
    std::cout << "Loader API: " << version_string(loader_version)
              << "; header version: " << VK_HEADER_VERSION << "\n";
    if (loader_version < VK_API_VERSION_1_3)
        throw std::runtime_error("This lab requires a Vulkan 1.3+ loader.");

    std::vector<const char*> layers, extensions;
    constexpr const char* layer_name = "VK_LAYER_KHRONOS_validation";
    VkValidationFeatureEnableEXT sync_validation = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    validation_features.enabledValidationFeatureCount = 1;
    validation_features.pEnabledValidationFeatures = &sync_validation;
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debug_callback;
    debug.pUserData = &context;

    if (validation) {
        uint32_t count = 0;
        check(vkEnumerateInstanceLayerProperties(&count, nullptr), "enumerate layers");
        std::vector<VkLayerProperties> available(count);
        check(vkEnumerateInstanceLayerProperties(&count, available.data()), "enumerate layers");
        const auto found = std::find_if(available.begin(), available.end(), [](const auto& p) {
            return std::strcmp(p.layerName, layer_name) == 0;
        });
        if (found == available.end()) throw std::runtime_error("--validation requires VK_LAYER_KHRONOS_validation.");
        if (!has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            throw std::runtime_error("--validation requires VK_EXT_debug_utils.");
        if (!has_instance_extension(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) &&
            !has_instance_extension(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME, layer_name))
            throw std::runtime_error("--validation requires VK_EXT_validation_features for synchronization validation.");
        layers.push_back(layer_name);
        extensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
        validation_features.pNext = &debug;
        context.validation = true;
        std::cout << "Validation: " << layer_name << " " << version_string(found->specVersion)
                  << "; synchronization validation explicitly enabled\n";
    } else {
        std::cout << "Validation: disabled (pass --validation to require it)\n";
    }
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Vulkan self-study lessons";
    application.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &application;
    instance_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    instance_info.ppEnabledLayerNames = layers.data();
    instance_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instance_info.ppEnabledExtensionNames = extensions.data();
    instance_info.pNext = validation ? &validation_features : nullptr;
    check(vkCreateInstance(&instance_info, nullptr, &context.instance), "vkCreateInstance");
    if (validation) {
        auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(context.instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!create_debug) throw std::runtime_error("vkCreateDebugUtilsMessengerEXT unavailable");
        check(create_debug(context.instance, &debug, nullptr, &context.messenger), "create debug messenger");
    }
    if (!need_device) return;

    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(context.instance, &count, nullptr), "enumerate physical devices");
    std::vector<VkPhysicalDevice> physical_devices(count);
    check(vkEnumeratePhysicalDevices(context.instance, &count, physical_devices.data()), "enumerate physical devices");
    for (auto physical : physical_devices) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(physical, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        VkPhysicalDeviceVulkan13Features available13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &available13;
        vkGetPhysicalDeviceFeatures2(physical, &features);
        if (!available13.dynamicRendering || !available13.synchronization2) continue;
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, queues.data());
        for (uint32_t i = 0; i < queue_count; ++i) {
            if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                context.physical = physical;
                context.graphics_family = i;
                break;
            }
        }
        if (context.physical) break;
    }
    if (!context.physical) throw std::runtime_error("No graphics device supports Vulkan 1.3 dynamicRendering + synchronization2.");
    vkGetPhysicalDeviceProperties(context.physical, &context.properties);
    vkGetPhysicalDeviceMemoryProperties(context.physical, &context.memory_properties);
    print_device_capabilities(context.physical);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = context.graphics_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    // 查询到的结构不能直接当作启用清单；这里只启用本书实际使用的两个 1.3 feature。
    VkPhysicalDeviceVulkan13Features enabled13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    enabled13.dynamicRendering = VK_TRUE;
    enabled13.synchronization2 = VK_TRUE;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext = &enabled13;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    check(vkCreateDevice(context.physical, &device_info, nullptr, &context.device), "vkCreateDevice");
    vkGetDeviceQueue(context.device, context.graphics_family, 0, &context.graphics_queue);
    std::cout << "Enabled: dynamicRendering=1, synchronization2=1; graphicsFamily=" << context.graphics_family << "\n";

    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = context.graphics_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(context.device, &pool_info, nullptr, &context.command_pool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = context.command_pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(context.device, &allocate, &context.command), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(context.device, &fence_info, nullptr, &context.fence), "vkCreateFence");
}

void begin_commands(Context& context, VkCommandBuffer command) {
    (void)context;
    // 单次实验由上一轮submit_and_wait保证；frames必须在本调用前等待该slot的fence。
    // 录制后再等待，不能补救“重置仍pending的commandbuffer”这个错误。
    check(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer (must no longer be pending)");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
}

void submit_and_wait(Context& context, VkCommandBuffer command) {
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    check(vkWaitForFences(context.device, 1, &context.fence, VK_TRUE, UINT64_MAX), "wait previous submit fence");
    VkCommandBufferSubmitInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command_info.commandBuffer = command;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_info;
    // 录制成功后、确定会提交时，才把 signaled fence 变为 unsignaled。
    check(vkResetFences(context.device, 1, &context.fence), "vkResetFences");
    check(vkQueueSubmit2(context.graphics_queue, 1, &submit, context.fence), "vkQueueSubmit2");
    check(vkWaitForFences(context.device, 1, &context.fence, VK_TRUE, UINT64_MAX), "wait submitted work");
}

Context::~Context() {
    if (device) {
        vkDeviceWaitIdle(device);
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (command_pool) vkDestroyCommandPool(device, command_pool, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (messenger) {
        auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_debug) destroy_debug(instance, messenger, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
}
} // namespace lab
