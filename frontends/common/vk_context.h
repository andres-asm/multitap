#pragma once

// Vulkan instance, physical device and logical device.
// Shared by all frontends (frontends/common).
//
// Creation is split in two steps because choosing a GPU needs a surface
// (to check it can present to our window), and the surface needs an instance:
//
//   vulkan_context_create_instance()  ->  SDL_Vulkan_CreateSurface()  ->  vulkan_context_create_device()
//
// Every handle starts as VK_NULL_HANDLE, so vulkan_context_destroy() is safe
// to call on a partially created context.

#include <cstdint>
#include <vulkan/vulkan.h>

// We require Vulkan 1.3 (dynamic rendering + synchronization2 are core there).
constexpr uint32_t VULKAN_REQUIRED_API_VERSION = VK_API_VERSION_1_3;

struct VulkanContext
{
    VkInstance               instance        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    bool                     validation_enabled = false;

    VkPhysicalDevice           physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physical_device_properties = {};

    VkDevice device       = VK_NULL_HANDLE;
    uint32_t queue_family = UINT32_MAX;     // one family that does graphics + present
    VkQueue  queue        = VK_NULL_HANDLE;

    // Present wait (waiting until a presented frame is on screen), when the
    // GPU and this window's surface support it: 2 = VK_KHR_present_wait2,
    // 1 = VK_KHR_present_wait, 0 = not available. The matching function is
    // loaded; swapchains need extra flags for version 2.
    bool                     surface_capabilities2 = false;   // instance has VK_KHR_get_surface_capabilities2
    uint32_t                 present_wait_version = 0;
    PFN_vkWaitForPresentKHR  wait_for_present = nullptr;
#if defined(VK_KHR_present_wait2)
    PFN_vkWaitForPresent2KHR wait_for_present2 = nullptr;
#endif

    // Descriptor pool reserved for Dear ImGui (font atlas + any debug textures).
    VkDescriptorPool imgui_descriptor_pool = VK_NULL_HANDLE;
};

// application_name: shown in tools like RenderDoc and in driver profiles (e.g. "piccolo").
// required_extensions: what the window system needs (from SDL_Vulkan_GetInstanceExtensions).
bool vulkan_context_create_instance(VulkanContext* context,
                                    const char* application_name,
                                    const char* const* required_extensions,
                                    uint32_t required_extension_count);

// Picks a GPU that can present to `surface`, then creates the device and queue.
bool vulkan_context_create_device(VulkanContext* context, VkSurfaceKHR surface);

void vulkan_context_destroy(VulkanContext* context);

// Finds a memory type allowed by `type_bits` (from VkMemoryRequirements) that has
// all `properties`. Returns false if the device has none.
bool vulkan_find_memory_type(const VulkanContext* context, uint32_t type_bits, VkMemoryPropertyFlags properties,
                             uint32_t* memory_type_index);

// Logs `what` and the result name when `result` is not VK_SUCCESS.
// Returns true on VK_SUCCESS.
bool vk_succeeded(VkResult result, const char* what);

const char* vk_result_to_string(VkResult result);
