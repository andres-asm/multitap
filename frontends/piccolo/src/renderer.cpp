#include "renderer.h"

#include <SDL3/SDL_log.h>

#include "vk_context.h"
#include "vk_swapchain.h"

// ---------------------------------------------------------------------------
// Create / destroy
// ---------------------------------------------------------------------------

bool renderer_create(Renderer* renderer, const VulkanContext* context)
{
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        FrameResources* frame = &renderer->frames[i];

        VkCommandPoolCreateInfo pool_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = context->queue_family,
        };
        if (!vk_succeeded(vkCreateCommandPool(context->device, &pool_info, nullptr, &frame->command_pool),
                          "vkCreateCommandPool"))
        {
            return false;
        }

        VkCommandBufferAllocateInfo allocate_info = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = frame->command_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (!vk_succeeded(vkAllocateCommandBuffers(context->device, &allocate_info, &frame->command_buffer),
                          "vkAllocateCommandBuffers"))
        {
            return false;
        }

        // Created signaled so the very first wait in renderer_begin_frame() returns immediately.
        VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        if (!vk_succeeded(vkCreateFence(context->device, &fence_info, nullptr, &frame->in_flight_fence),
                          "vkCreateFence"))
        {
            return false;
        }

        VkSemaphoreCreateInfo semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        if (!vk_succeeded(vkCreateSemaphore(context->device, &semaphore_info, nullptr, &frame->image_acquired),
                          "vkCreateSemaphore (image acquired)"))
        {
            return false;
        }
    }

    renderer->frame_index = 0;
    renderer->image_index = 0;
    renderer->swapchain_suboptimal = false;
    return true;
}

void renderer_destroy(Renderer* renderer, const VulkanContext* context)
{
    if (context->device == VK_NULL_HANDLE)
    {
        return;
    }

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        FrameResources* frame = &renderer->frames[i];

        if (frame->image_acquired != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(context->device, frame->image_acquired, nullptr);
            frame->image_acquired = VK_NULL_HANDLE;
        }
        if (frame->in_flight_fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(context->device, frame->in_flight_fence, nullptr);
            frame->in_flight_fence = VK_NULL_HANDLE;
        }
        if (frame->command_pool != VK_NULL_HANDLE)
        {
            // Destroying the pool also frees its command buffers.
            vkDestroyCommandPool(context->device, frame->command_pool, nullptr);
            frame->command_pool = VK_NULL_HANDLE;
            frame->command_buffer = VK_NULL_HANDLE;
        }
    }
}

VkCommandBuffer renderer_command_buffer(const Renderer* renderer)
{
    return renderer->frames[renderer->frame_index].command_buffer;
}

// ---------------------------------------------------------------------------
// Layout transitions
// ---------------------------------------------------------------------------

static void transition_to_color_attachment(VkCommandBuffer command_buffer, VkImage image)
{
    // UNDEFINED: we clear the whole image anyway, so the old contents don't matter.
    // The stage matches the wait stage of the image-acquired semaphore in the submit.
    VkImageMemoryBarrier2 barrier = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image         = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };

    VkDependencyInfo dependency = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier,
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);
}

static void transition_to_present(VkCommandBuffer command_buffer, VkImage image)
{
    // Presentation is ordered by the render-finished semaphore, so no dst stage/access is needed.
    VkImageMemoryBarrier2 barrier = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask = 0,
        .oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image         = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };

    VkDependencyInfo dependency = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier,
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

RenderFrameResult renderer_begin_frame(Renderer* renderer, const VulkanContext* context,
                                       const VulkanSwapchain* swapchain, ClearColor clear_color)
{
    FrameResources* frame = &renderer->frames[renderer->frame_index];

    // 1. Wait until the GPU is done with the last use of this FrameResources.
    if (!vk_succeeded(vkWaitForFences(context->device, 1, &frame->in_flight_fence, VK_TRUE, UINT64_MAX),
                      "vkWaitForFences"))
    {
        return RenderFrameResult::error;
    }

    // 2. Get the next swapchain image.
    VkResult acquire_result = vkAcquireNextImageKHR(context->device, swapchain->swapchain, UINT64_MAX,
                                                    frame->image_acquired, VK_NULL_HANDLE, &renderer->image_index);
    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // The fence was NOT reset, so the next wait on it won't deadlock.
        return RenderFrameResult::swapchain_needs_recreate;
    }
    if (acquire_result == VK_SUBOPTIMAL_KHR)
    {
        // Still usable: draw this frame, recreate after presenting it.
        renderer->swapchain_suboptimal = true;
    }
    else if (!vk_succeeded(acquire_result, "vkAcquireNextImageKHR"))
    {
        return RenderFrameResult::error;
    }

    // 3. Only now that we will definitely submit, reset the fence.
    vkResetFences(context->device, 1, &frame->in_flight_fence);

    // 4. Start recording.
    vkResetCommandPool(context->device, frame->command_pool, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (!vk_succeeded(vkBeginCommandBuffer(frame->command_buffer, &begin_info), "vkBeginCommandBuffer"))
    {
        return RenderFrameResult::error;
    }

    transition_to_color_attachment(frame->command_buffer, swapchain->images[renderer->image_index]);

    VkRenderingAttachmentInfo color_attachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = swapchain->image_views[renderer->image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
    };
    color_attachment.clearValue.color.float32[0] = clear_color.r;
    color_attachment.clearValue.color.float32[1] = clear_color.g;
    color_attachment.clearValue.color.float32[2] = clear_color.b;
    color_attachment.clearValue.color.float32[3] = clear_color.a;

    VkRenderingInfo rendering_info = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = { { 0, 0 }, swapchain->extent },
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_attachment,
    };
    vkCmdBeginRendering(frame->command_buffer, &rendering_info);

    return RenderFrameResult::ok;
}

RenderFrameResult renderer_end_frame(Renderer* renderer, const VulkanContext* context,
                                     const VulkanSwapchain* swapchain)
{
    FrameResources* frame = &renderer->frames[renderer->frame_index];
    VkSemaphore render_finished = swapchain->render_finished[renderer->image_index];

    vkCmdEndRendering(frame->command_buffer);
    transition_to_present(frame->command_buffer, swapchain->images[renderer->image_index]);

    if (!vk_succeeded(vkEndCommandBuffer(frame->command_buffer), "vkEndCommandBuffer"))
    {
        return RenderFrameResult::error;
    }

    // Submit: wait for the image, signal render_finished (per IMAGE) and the frame fence.
    VkSemaphoreSubmitInfo wait_info = {
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame->image_acquired,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkSemaphoreSubmitInfo signal_info = {
        .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = render_finished,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    VkCommandBufferSubmitInfo command_info = {
        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame->command_buffer,
    };
    VkSubmitInfo2 submit_info = {
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount   = 1,
        .pWaitSemaphoreInfos      = &wait_info,
        .commandBufferInfoCount   = 1,
        .pCommandBufferInfos      = &command_info,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &signal_info,
    };
    if (!vk_succeeded(vkQueueSubmit2(context->queue, 1, &submit_info, frame->in_flight_fence), "vkQueueSubmit2"))
    {
        return RenderFrameResult::error;
    }

    // Present.
    VkPresentInfoKHR present_info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &render_finished,
        .swapchainCount     = 1,
        .pSwapchains        = &swapchain->swapchain,
        .pImageIndices      = &renderer->image_index,
    };
    VkResult present_result = vkQueuePresentKHR(context->queue, &present_info);

    // Advance to the next FrameResources whatever happened, the submit already went through.
    renderer->frame_index = (renderer->frame_index + 1) % FRAMES_IN_FLIGHT;

    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR ||
        renderer->swapchain_suboptimal)
    {
        renderer->swapchain_suboptimal = false;
        return RenderFrameResult::swapchain_needs_recreate;
    }
    if (!vk_succeeded(present_result, "vkQueuePresentKHR"))
    {
        return RenderFrameResult::error;
    }
    return RenderFrameResult::ok;
}
