#include "../../gfx.hpp"
#include "vulkan_internal.hpp"

#include "volk.h"

namespace nw::gfx {

inline VkPhysicalDevice choose_physical_device(VkInstance instance)
{
    uint32_t device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "vkEnumeratePhysicalDevices failed while choosing a Vulkan physical device: %d",
            static_cast<int>(result));
        return VK_NULL_HANDLE;
    }
    if (device_count == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No Vulkan physical devices were found");
        return VK_NULL_HANDLE;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    result = vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "vkEnumeratePhysicalDevices failed while enumerating Vulkan physical devices: %d",
            static_cast<int>(result));
        return VK_NULL_HANDLE;
    }

    VkPhysicalDevice selected = VK_NULL_HANDLE;
    for (auto device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selected = device;
            break;
        }
    }

    if (selected == VK_NULL_HANDLE && !devices.empty()) {
        selected = devices[0];
    }

    return selected;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data)
{
    const char* type_str = "";
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) type_str = "GENERAL";
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) type_str = "VALIDATION";
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) type_str = "PERFORMANCE";

    switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "[VK/%s] %s", type_str, callback_data->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[VK/%s] %s", type_str, callback_data->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[VK/%s] %s", type_str, callback_data->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[VK/%s] %s", type_str, callback_data->pMessage);
        break;
    default:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[VK/UNKNOWN] %s", callback_data->pMessage);
        break;
    }

    return VK_FALSE;
}

// SDL provides the platform surface extensions — query it directly.
inline void get_sdl_instance_extensions(std::vector<const char*>& exts, bool enable_validation)
{
    uint32_t count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&count);
    GFX_CHECK(sdl_exts != nullptr && count > 0,
        "SDL_Vulkan_GetInstanceExtensions returned no extensions. Ensure SDL video is initialized before create_core(). SDL error: %s",
        SDL_GetError());
    for (uint32_t i = 0; i < count; ++i) {
        exts.push_back(sdl_exts[i]);
    }
    if (enable_validation) {
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
}

inline void get_default_device_extensions(std::vector<const char*>& exts)
{
    exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    exts.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    exts.push_back(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
}

inline void get_platform_surface_extensions_unused(std::vector<const char*>& exts)
{
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
    exts.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
#else
#pragma error Platform not supported
#endif
}

VkInstance create_instance(VulkanCore* vk, const CoreConfig& desc)
{
    std::vector<const char*> layers;
    if (desc.enable_validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = desc.app_name;
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "nw::gfx";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(vk->enabled_instance_extensions.size());
    create_info.ppEnabledExtensionNames = vk->enabled_instance_extensions.data();
    create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();

    VkInstance instance = VK_NULL_HANDLE;
    VK_CHECK(vkCreateInstance(&create_info, nullptr, &instance));

    volkLoadInstance(instance);

    if (desc.enable_validation) {
        VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};

        debug_create_info = {};
        debug_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_create_info.pfnUserCallback = debug_callback;

        create_info.pNext = &debug_create_info;

        VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &debug_create_info, nullptr, &vk->debug_messenger));
    }

    return instance;
}

VkDevice create_logical_device(VulkanCore* vk)
{
    if (vk->physical_device == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot create Vulkan logical device without a physical device");
        return VK_NULL_HANDLE;
    }

    // Query queue families
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk->physical_device, &queue_family_count, nullptr);
    if (queue_family_count == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Selected Vulkan physical device exposes no queue families");
        return VK_NULL_HANDLE;
    }

    std::vector<VkQueueFamilyProperties> queue_props(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(vk->physical_device, &queue_family_count, queue_props.data());

    // Pick first queue that supports graphics
    bool found_graphics_queue = false;
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            vk->graphics_queue_family = i;
            found_graphics_queue = true;
            break;
        }
    }
    if (!found_graphics_queue) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Selected Vulkan physical device exposes no graphics queue");
        return VK_NULL_HANDLE;
    }

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = vk->graphics_queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceFeatures2 device_features2{};
    device_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    // Enable Vulkan 1.3 features
    vk->features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk->features_13.dynamicRendering = VK_TRUE;
    vk->features_13.synchronization2 = VK_TRUE;
    device_features2.pNext = &vk->features_13;

    // Enable Vulkan 1.2 features required by VMA allocator flags.
    vk->features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk->features_12.bufferDeviceAddress = VK_TRUE;
    vk->features_13.pNext = &vk->features_12;

    vk->features_12.runtimeDescriptorArray = VK_TRUE;
    vk->features_12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    vk->features_12.descriptorBindingPartiallyBound = VK_TRUE;
    vk->features_12.descriptorBindingVariableDescriptorCount = VK_TRUE;

    // Enable descriptor buffer extension (bindless-first descriptor model).
    static VkPhysicalDeviceDescriptorBufferFeaturesEXT desc_buffer_features{};
    desc_buffer_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    desc_buffer_features.descriptorBuffer = VK_TRUE;
    vk->features_12.pNext = &desc_buffer_features;

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pNext = &device_features2;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(vk->enabled_device_extensions.size());
    create_info.ppEnabledExtensionNames = vk->enabled_device_extensions.data();

    VkDevice device;
    VK_CHECK(vkCreateDevice(vk->physical_device, &create_info, nullptr, &device));

    vkGetDeviceQueue(device, vk->graphics_queue_family, 0, &vk->graphics_queue);
    volkLoadDevice(device);

    // Query descriptor buffer properties (sizes, alignment).
    vk->descriptor_buffer_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &vk->descriptor_buffer_props;
    vkGetPhysicalDeviceProperties2(vk->physical_device, &props2);

    return device;
}

VmaAllocator create_allocator(VulkanCore* vk)
{
    VmaVulkanFunctions vulkan_funcs{};
    vulkan_funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkan_funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocator_info.physicalDevice = vk->physical_device;
    allocator_info.device = vk->device;
    allocator_info.instance = vk->instance;
    allocator_info.pVulkanFunctions = &vulkan_funcs;
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_3;

    VmaAllocator allocator;
    VK_CHECK(vmaCreateAllocator(&allocator_info, &allocator));
    return allocator;
}

Core* create_core(const CoreConfig& desc)
{
    auto* vk = new VulkanCore();

    VkResult volk_result = volkInitialize();
    if (volk_result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "volkInitialize failed: %d", (int)volk_result);
        std::abort();
    }

    get_sdl_instance_extensions(vk->enabled_instance_extensions, desc.enable_validation);
    get_default_device_extensions(vk->enabled_device_extensions);

    vk->instance = create_instance(vk, desc);
    vk->physical_device = choose_physical_device(vk->instance);
    if (vk->physical_device == VK_NULL_HANDLE) {
        destroy_core(as_gfx(vk));
        return nullptr;
    }

    vk->device = create_logical_device(vk);
    if (vk->device == VK_NULL_HANDLE) {
        destroy_core(as_gfx(vk));
        return nullptr;
    }

    vk->allocator = create_allocator(vk);

    return reinterpret_cast<Core*>(vk);
}

void destroy_core(Core* core)
{
    auto* vk = as_vulkan(core);

    if (vk->allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(vk->allocator);
        vk->allocator = VK_NULL_HANDLE;
    }

    if (vk->device != VK_NULL_HANDLE) {
        vkDestroyDevice(vk->device, nullptr);
        vk->device = VK_NULL_HANDLE;
    }

    if (vk->debug_messenger) {
        vkDestroyDebugUtilsMessengerEXT(vk->instance, vk->debug_messenger, nullptr);
        vk->debug_messenger = VK_NULL_HANDLE;
    }

    if (vk->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(vk->instance, nullptr);
        vk->instance = VK_NULL_HANDLE;
    }

    delete vk;
}

} // namespace nw::gfx
