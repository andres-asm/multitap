#pragma once

// Per-frame command recording and presentation for the main window.
//
// Usage each frame:
//
//   RenderFrameResult result = renderer_begin_frame(&renderer, &context, &swapchain);
//   if (result == RenderFrameResult::ok)
//   {
//       ... record uploads (texture copies) into renderer_command_buffer(&renderer) ...
//       renderer_begin_rendering(&renderer, &swapchain, clear_color);
//       ... record draws into renderer_command_buffer(&renderer) ...
//       result = renderer_end_frame(&renderer, &context, &swapchain);
//   }
//   if (result == RenderFrameResult::swapchain_needs_recreate) { recreate the swapchain }
//
// After renderer_begin_rendering() the swapchain image is in COLOR_ATTACHMENT_OPTIMAL
// layout inside an active rendering pass that has already been cleared: dynamic
// rendering on the Vulkan 1.3 path, the swapchain's render pass on the 1.1 path
// (see VulkanPath in vk_context.h). Copies are not allowed inside a rendering
// pass, which is why uploads come first.

#include <cstdint>
#include <vulkan/vulkan.h>

#include "vk_context.h"   // VulkanPath

struct VulkanSwapchain;

// The most frames the CPU may record ahead of the GPU (arrays are this big).
// How many are actually used is renderer->frames_in_flight (1 or 2): 1 is
// lower latency (the CPU waits for the GPU every frame), 2 is smoother.
constexpr uint32_t FRAMES_IN_FLIGHT = 2;

enum class RenderFrameResult
{
    ok,
    swapchain_needs_recreate,   // out of date / suboptimal: recreate, then try again next frame
    surface_lost,               // the window's surface is gone (Android, going to the background)
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
    uint32_t frames_in_flight = FRAMES_IN_FLIGHT;   // 1 or 2 (renderer_set_frames_in_flight)
    uint32_t frame_index = 0;          // which FrameResources we are using, 0..frames_in_flight-1
    uint32_t image_index = 0;          // which swapchain image we acquired this frame
    bool     swapchain_suboptimal = false;

    // Low latency: after presenting, wait until the frame is on screen
    // (present wait, when the context has it) or else until the GPU has
    // finished it. The next frame then starts as late as possible.
    bool     wait_for_present = false;
    uint64_t present_id = 0;           // id of the last present (present wait)

    // From the context at creation (renderer_begin_rendering has no context).
    VulkanPath                path                  = VulkanPath::vulkan_1_3;
    PFN_vkCmdPipelineBarrier2 cmd_pipeline_barrier2 = nullptr;
    PFN_vkCmdBeginRendering   cmd_begin_rendering   = nullptr;
    PFN_vkCmdEndRendering     cmd_end_rendering     = nullptr;
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

// Waits for this frame slot, acquires a swapchain image, starts recording.
RenderFrameResult renderer_begin_frame(Renderer* renderer, const VulkanContext* context,
                                       const VulkanSwapchain* swapchain);

// Clears the swapchain image and starts the rendering pass.
void renderer_begin_rendering(Renderer* renderer, const VulkanSwapchain* swapchain, ClearColor clear_color);
RenderFrameResult renderer_end_frame(Renderer* renderer, const VulkanContext* context,
                                     const VulkanSwapchain* swapchain);

VkCommandBuffer renderer_command_buffer(const Renderer* renderer);

// 1 or 2 (clamped). Changing it waits for the GPU; call between frames.
void renderer_set_frames_in_flight(Renderer* renderer, const VulkanContext* context, uint32_t count);

// Which FrameResources slot (0..frames_in_flight-1) is recording. Per-frame
// resources elsewhere (e.g. staging buffers) are indexed with this.
uint32_t renderer_frame_index(const Renderer* renderer);
