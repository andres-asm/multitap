#include "vk_context.h"

#include <cstring>

#include <SDL3/SDL_log.h>

#include "imgui_impl_vulkan.h"   // only for the IMGUI_IMPL_VULKAN_MINIMUM_* pool sizes

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const char* const VALIDATION_LAYER_NAME = "VK_LAYER_KHRONOS_validation";

constexpr uint32_t MAX_ENABLED_INSTANCE_EXTENSIONS = 32;
constexpr uint32_t MAX_ENABLED_DEVICE_EXTENSIONS   = 8;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

const char* vk_result_to_string(VkResult result)
{
    switch (result)
    {
        case VK_SUCCESS:                        return "VK_SUCCESS";
        case VK_NOT_READY:                      return "VK_NOT_READY";
        case VK_TIMEOUT:                        return "VK_TIMEOUT";
        case VK_INCOMPLETE:                     return "VK_INCOMPLETE";
        case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR:         return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        default:                                return "VkResult (unknown)";
    }
}

bool vk_succeeded(VkResult result, const char* what)
{
    if (result == VK_SUCCESS)
    {
        return true;
    }
    SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s failed: %s (%d)", what, vk_result_to_string(result), static_cast<int>(result));
    return false;
}

static bool instance_layer_available(const char* layer_name)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    if (count == 0)
    {
        return false;
    }

    VkLayerProperties* layers = new VkLayerProperties[count];
    vkEnumerateInstanceLayerProperties(&count, layers);

    bool found = false;
    for (uint32_t i = 0; i < count; i++)
    {
        if (std::strcmp(layers[i].layerName, layer_name) == 0)
        {
            found = true;
            break;
        }
    }

    delete[] layers;
    return found;
}

static bool instance_extension_available(const char* extension_name)
{
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    if (count == 0)
    {
        return false;
    }

    VkExtensionProperties* extensions = new VkExtensionProperties[count];
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions);

    bool found = false;
    for (uint32_t i = 0; i < count; i++)
    {
        if (std::strcmp(extensions[i].extensionName, extension_name) == 0)
        {
            found = true;
            break;
        }
    }

    delete[] extensions;
    return found;
}

static bool device_extension_available(VkPhysicalDevice physical_device, const char* extension_name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
    if (count == 0)
    {
        return false;
    }

    VkExtensionProperties* extensions = new VkExtensionProperties[count];
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions);

    bool found = false;
    for (uint32_t i = 0; i < count; i++)
    {
        if (std::strcmp(extensions[i].extensionName, extension_name) == 0)
        {
            found = true;
            break;
        }
    }

    delete[] extensions;
    return found;
}

// ---------------------------------------------------------------------------
// Debug messenger (validation output)
// ---------------------------------------------------------------------------

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                               VkDebugUtilsMessageTypeFlagsEXT type,
                                                               const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                               void* user_data)
{
    (void)type;
    (void)user_data;

    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "[vulkan] %s", data->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "[vulkan] %s", data->pMessage);
    }
    else
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "[vulkan] %s", data->pMessage);
    }

    // Returning VK_FALSE means: do not abort the call that triggered the message.
    return VK_FALSE;
}

static VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info()
{
    VkDebugUtilsMessengerCreateInfoEXT info = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_messenger_callback,
    };
    return info;
}

// ---------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------

bool vulkan_context_create_instance(VulkanContext* context,
                                    const char* const* required_extensions,
                                    uint32_t required_extension_count)
{
    uint32_t loader_version = 0;
    if (!vk_succeeded(vkEnumerateInstanceVersion(&loader_version), "vkEnumerateInstanceVersion"))
    {
        return false;
    }
    if (loader_version < VULKAN_REQUIRED_API_VERSION)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Vulkan loader is %u.%u, piccolo needs 1.3",
                     VK_API_VERSION_MAJOR(loader_version), VK_API_VERSION_MINOR(loader_version));
        return false;
    }

    // Collect instance extensions: the window system's, plus our optional ones.
    const char* extensions[MAX_ENABLED_INSTANCE_EXTENSIONS] = {};
    uint32_t extension_count = 0;

    if (required_extension_count > MAX_ENABLED_INSTANCE_EXTENSIONS - 2)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Too many required instance extensions (%u)", required_extension_count);
        return false;
    }
    for (uint32_t i = 0; i < required_extension_count; i++)
    {
        extensions[extension_count] = required_extensions[i];
        extension_count++;
    }

    VkInstanceCreateFlags instance_flags = 0;

    // MoltenVK (macOS/iOS) only shows up when portability enumeration is enabled.
    if (instance_extension_available(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        extensions[extension_count] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        extension_count++;
        instance_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    // Validation: Debug builds only, and only if the layer is installed.
    const char* layers[1] = {};
    uint32_t layer_count = 0;
    context->validation_enabled = false;

#if defined(PICCOLO_VULKAN_VALIDATION)
    if (instance_layer_available(VALIDATION_LAYER_NAME) &&
        instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        layers[0] = VALIDATION_LAYER_NAME;
        layer_count = 1;
        extensions[extension_count] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        extension_count++;
        context->validation_enabled = true;
    }
    else
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "Validation requested but %s is not installed; continuing without it",
                    VALIDATION_LAYER_NAME);
    }
#endif

    VkApplicationInfo app_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "piccolo",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName        = "multitap",
        .engineVersion      = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion         = VULKAN_REQUIRED_API_VERSION,
    };

    // Chaining the messenger info here also reports problems inside vkCreateInstance itself.
    VkDebugUtilsMessengerCreateInfoEXT messenger_info = debug_messenger_create_info();

    VkInstanceCreateInfo create_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = context->validation_enabled ? &messenger_info : nullptr,
        .flags                   = instance_flags,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = layer_count,
        .ppEnabledLayerNames     = layers,
        .enabledExtensionCount   = extension_count,
        .ppEnabledExtensionNames = extensions,
    };

    if (!vk_succeeded(vkCreateInstance(&create_info, nullptr, &context->instance), "vkCreateInstance"))
    {
        return false;
    }

    if (context->validation_enabled)
    {
        PFN_vkCreateDebugUtilsMessengerEXT create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create_messenger == nullptr ||
            !vk_succeeded(create_messenger(context->instance, &messenger_info, nullptr, &context->debug_messenger),
                          "vkCreateDebugUtilsMessengerEXT"))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "Could not create debug messenger; validation output will be lost");
        }
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "Vulkan instance created (loader %u.%u.%u, validation %s)",
                VK_API_VERSION_MAJOR(loader_version), VK_API_VERSION_MINOR(loader_version),
                VK_API_VERSION_PATCH(loader_version), context->validation_enabled ? "on" : "off");
    return true;
}

// ---------------------------------------------------------------------------
// Physical device selection
// ---------------------------------------------------------------------------

// Returns UINT32_MAX when the device has no queue family that does both graphics and present.
static uint32_t find_graphics_present_queue_family(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
    if (family_count == 0)
    {
        return UINT32_MAX;
    }

    VkQueueFamilyProperties* families = new VkQueueFamilyProperties[family_count];
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families);

    uint32_t result = UINT32_MAX;
    for (uint32_t i = 0; i < family_count; i++)
    {
        VkBool32 can_present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &can_present);

        bool can_draw = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (can_draw && can_present == VK_TRUE)
        {
            result = i;
            break;
        }
    }

    delete[] families;
    return result;
}

static bool device_has_required_features(VkPhysicalDevice physical_device)
{
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features13,
    };
    vkGetPhysicalDeviceFeatures2(physical_device, &features2);

    return features13.dynamicRendering == VK_TRUE && features13.synchronization2 == VK_TRUE;
}

// Higher is better. 0 means "cannot be used".
static uint32_t score_physical_device(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(physical_device, &properties);

    if (properties.apiVersion < VULKAN_REQUIRED_API_VERSION)
    {
        return 0;
    }
    if (!device_extension_available(physical_device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
    {
        return 0;
    }
    if (!device_has_required_features(physical_device))
    {
        return 0;
    }
    if (find_graphics_present_queue_family(physical_device, surface) == UINT32_MAX)
    {
        return 0;
    }

    switch (properties.deviceType)
    {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 4;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 2;
        default:                                     return 1;   // CPU (lavapipe) and others
    }
}

static bool select_physical_device(VulkanContext* context, VkSurfaceKHR surface)
{
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(context->instance, &device_count, nullptr);
    if (device_count == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "No Vulkan devices found");
        return false;
    }

    VkPhysicalDevice* devices = new VkPhysicalDevice[device_count];
    vkEnumeratePhysicalDevices(context->instance, &device_count, devices);

    VkPhysicalDevice best_device = VK_NULL_HANDLE;
    uint32_t best_score = 0;

    for (uint32_t i = 0; i < device_count; i++)
    {
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(devices[i], &properties);

        uint32_t score = score_physical_device(devices[i], surface);
        SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "  GPU %u: %s (Vulkan %u.%u) score %u", i, properties.deviceName,
                    VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion), score);

        if (score > best_score)
        {
            best_score = score;
            best_device = devices[i];
        }
    }

    delete[] devices;

    if (best_device == VK_NULL_HANDLE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "No GPU supports Vulkan 1.3 + dynamic rendering + presenting to this window");
        return false;
    }

    context->physical_device = best_device;
    vkGetPhysicalDeviceProperties(best_device, &context->physical_device_properties);
    context->queue_family = find_graphics_present_queue_family(best_device, surface);
    return true;
}

// ---------------------------------------------------------------------------
// Logical device
// ---------------------------------------------------------------------------

static bool create_imgui_descriptor_pool(VulkanContext* context)
{
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
        { VK_DESCRIPTOR_TYPE_SAMPLER,       IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
    };
    constexpr uint32_t pool_size_count = sizeof(pool_sizes) / sizeof(pool_sizes[0]);

    uint32_t max_sets = 0;
    for (uint32_t i = 0; i < pool_size_count; i++)
    {
        max_sets += pool_sizes[i].descriptorCount;
    }

    VkDescriptorPoolCreateInfo pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets       = max_sets,
        .poolSizeCount = pool_size_count,
        .pPoolSizes    = pool_sizes,
    };

    return vk_succeeded(vkCreateDescriptorPool(context->device, &pool_info, nullptr, &context->imgui_descriptor_pool),
                        "vkCreateDescriptorPool (imgui)");
}

bool vulkan_context_create_device(VulkanContext* context, VkSurfaceKHR surface)
{
    if (!select_physical_device(context, surface))
    {
        return false;
    }

    const char* extensions[MAX_ENABLED_DEVICE_EXTENSIONS] = {};
    uint32_t extension_count = 0;

    extensions[extension_count] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    extension_count++;

    // Required by the spec on MoltenVK and other non-conformant implementations.
    if (device_extension_available(context->physical_device, "VK_KHR_portability_subset"))
    {
        extensions[extension_count] = "VK_KHR_portability_subset";
        extension_count++;
    }

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = context->queue_family,
        .queueCount       = 1,
        .pQueuePriorities = &queue_priority,
    };

    VkPhysicalDeviceVulkan13Features features13 = {
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    VkDeviceCreateInfo device_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &features13,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queue_info,
        .enabledExtensionCount   = extension_count,
        .ppEnabledExtensionNames = extensions,
    };

    if (!vk_succeeded(vkCreateDevice(context->physical_device, &device_info, nullptr, &context->device), "vkCreateDevice"))
    {
        return false;
    }

    vkGetDeviceQueue(context->device, context->queue_family, 0, &context->queue);

    if (!create_imgui_descriptor_pool(context))
    {
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "Using GPU: %s (queue family %u)",
                context->physical_device_properties.deviceName, context->queue_family);
    return true;
}

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------

void vulkan_context_destroy(VulkanContext* context)
{
    if (context->device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(context->device);

        if (context->imgui_descriptor_pool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(context->device, context->imgui_descriptor_pool, nullptr);
            context->imgui_descriptor_pool = VK_NULL_HANDLE;
        }

        vkDestroyDevice(context->device, nullptr);
        context->device = VK_NULL_HANDLE;
        context->queue = VK_NULL_HANDLE;
    }

    if (context->debug_messenger != VK_NULL_HANDLE)
    {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(context->instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_messenger != nullptr)
        {
            destroy_messenger(context->instance, context->debug_messenger, nullptr);
        }
        context->debug_messenger = VK_NULL_HANDLE;
    }

    if (context->instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(context->instance, nullptr);
        context->instance = VK_NULL_HANDLE;
    }

    context->physical_device = VK_NULL_HANDLE;
}
