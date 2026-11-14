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

// Two rendering paths, picked when the device is created:
//   vulkan_1_3  dynamic rendering + synchronization2 (core in 1.3). Desktop.
//   vulkan_1_1  render passes, classic barriers and vkQueueSubmit. For GPUs
//               whose drivers stop at 1.1, like the Tegra X1 (Shield) on Android.
// The instance always asks for 1.3; a device older than 1.1 is not used.
constexpr uint32_t VULKAN_MINIMUM_API_VERSION   = VK_API_VERSION_1_1;
constexpr uint32_t VULKAN_PREFERRED_API_VERSION = VK_API_VERSION_1_3;

enum class VulkanPath
{
    vulkan_1_3,
    vulkan_1_1,
};

const char* vulkan_path_to_string(VulkanPath path);

struct VulkanContext
{
    // Set before vulkan_context_create_device(): use the 1.1 path even when the
    // device has 1.3 (to test that path on a desktop GPU).
    bool force_vulkan_1_1 = false;

    VkInstance               instance        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    bool                     validation_enabled = false;

    VkPhysicalDevice           physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physical_device_properties = {};

    VulkanPath path        = VulkanPath::vulkan_1_3;
    uint32_t   api_version = 0;   // what the device is used as: min(device, instance)

    // Vulkan 1.3 functions, loaded from the device on the 1.3 path only
    // (nullptr on the 1.1 path). Not called through the loader's exports:
    // Android's libvulkan.so only exports 1.0/1.1, so calling them directly
    // wouldn't even link there.
    PFN_vkCmdPipelineBarrier2 cmd_pipeline_barrier2 = nullptr;
    PFN_vkQueueSubmit2        queue_submit2         = nullptr;
    PFN_vkCmdBeginRendering   cmd_begin_rendering   = nullptr;
    PFN_vkCmdEndRendering     cmd_end_rendering     = nullptr;

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

// Picks a GPU that can present to `surface`, then creates the device and queue,
// and decides the path (1.3 when the device has it, unless force_vulkan_1_1).
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
