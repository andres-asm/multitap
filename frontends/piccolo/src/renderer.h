#pragma once

// Per-frame command recording and presentation for the main window.
//
// Usage each frame:
//
//   RenderFrameResult result = renderer_begin_frame(&renderer, &context, &swapchain, clear_color);
//   if (result == RenderFrameResult::ok)
//   {
//       ... record draws into renderer_command_buffer(&renderer) ...
//       result = renderer_end_frame(&renderer, &context, &swapchain);
//   }
//   if (result == RenderFrameResult::swapchain_needs_recreate) { recreate the swapchain }
//
// Between begin and end the current swapchain image is in COLOR_ATTACHMENT_OPTIMAL
// layout inside an active dynamic-rendering pass that has already been cleared.

#include <cstdint>
#include <vulkan/vulkan.h>

struct VulkanContext;
struct VulkanSwapchain;

// How many frames the CPU may record ahead of the GPU.
constexpr uint32_t FRAMES_IN_FLIGHT = 2;

enum class RenderFrameResult
{
    ok,
    swapchain_needs_recreate,   // out of date / suboptimal: recreate, then try again next frame
    error,
};

struct FrameResources
{
    VkCommandPool   command_pool    = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer  = VK_NULL_HANDLE;
    VkFence         in_flight_fence = VK_NULL_HANDLE;   // signaled when the GPU finished this frame
    VkSemaphore     image_acquired  = VK_NULL_HANDLE;   // signaled when the swapchain image is ready
};

struct Renderer
{
    FrameResources frames[FRAMES_IN_FLIGHT] = {};
    uint32_t frame_index = 0;          // which FrameResources we are using, 0..FRAMES_IN_FLIGHT-1
    uint32_t image_index = 0;          // which swapchain image we acquired this frame
    bool     swapchain_suboptimal = false;
};

struct ClearColor
{
    float r;
    float g;
    float b;
    float a;
};

bool renderer_create(Renderer* renderer, const VulkanContext* context);
void renderer_destroy(Renderer* renderer, const VulkanContext* context);

RenderFrameResult renderer_begin_frame(Renderer* renderer, const VulkanContext* context,
                                       const VulkanSwapchain* swapchain, ClearColor clear_color);
RenderFrameResult renderer_end_frame(Renderer* renderer, const VulkanContext* context,
                                     const VulkanSwapchain* swapchain);

VkCommandBuffer renderer_command_buffer(const Renderer* renderer);
