#pragma once

// Swapchain for the main window.
//
// Synchronization rule (the reason we don't use ImGui_ImplVulkanH_Window):
//   - "image acquired" semaphores belong to the frame in flight (see renderer.h).
//   - "render finished" semaphores belong to the SWAPCHAIN IMAGE, stored here.
// A render-finished semaphore is waited on by vkQueuePresentKHR, and we only
// know that wait is over when the same image is acquired again. Indexing it
// by swapchain image is what guarantees it is never re-signaled too early.
//
// On the Vulkan 1.1 path (VulkanPath in vk_context.h) the swapchain also owns
// the render pass that draws into its images and one framebuffer per image.
// The render pass lives as long as the swapchain object: pipelines (ImGui's)
// are built for it, so recreating the swapchain keeps it (the image format
// must stay the same).

#include <cstdint>
#include <vulkan/vulkan.h>

struct VulkanContext;

constexpr uint32_t SWAPCHAIN_MAX_IMAGES = 8;

struct VulkanSwapchain
{
    VkSwapchainKHR     swapchain      = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surface_format = {};
    VkPresentModeKHR   present_mode   = VK_PRESENT_MODE_FIFO_KHR;   // what we got

    // What the frontend wants. true = FIFO (vsync on). false = MAILBOX, else
    // IMMEDIATE (vsync off), else FIFO if the driver has neither. Read on every
    // (re)create, so change it and recreate to switch.
    bool               vsync          = true;
    VkExtent2D         extent         = {};

    uint32_t    min_image_count = 0;   // what we asked for
    uint32_t    image_count     = 0;   // what the driver gave us
    VkImage     images[SWAPCHAIN_MAX_IMAGES]           = {};
    VkImageView image_views[SWAPCHAIN_MAX_IMAGES]      = {};
    VkSemaphore render_finished[SWAPCHAIN_MAX_IMAGES]  = {};

    // Vulkan 1.1 path only (VK_NULL_HANDLE on the 1.3 path).
    VkRenderPass  render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffers[SWAPCHAIN_MAX_IMAGES] = {};
};

// width/height: window size in PIXELS (SDL_GetWindowSizeInPixels).
bool vulkan_swapchain_create(VulkanSwapchain* swapchain, const VulkanContext* context, VkSurfaceKHR surface,
                             uint32_t width, uint32_t height);

// Waits for the device to go idle, then replaces the swapchain (passing the old
// one as oldSwapchain). Call after resize, VK_ERROR_OUT_OF_DATE_KHR or VK_SUBOPTIMAL_KHR.
bool vulkan_swapchain_recreate(VulkanSwapchain* swapchain, const VulkanContext* context, VkSurfaceKHR surface,
                               uint32_t width, uint32_t height);

void vulkan_swapchain_destroy(VulkanSwapchain* swapchain, const VulkanContext* context);

const char* vk_present_mode_to_string(VkPresentModeKHR mode);
