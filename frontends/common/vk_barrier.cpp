#include "vk_barrier.h"

#include <SDL3/SDL_log.h>

constexpr uint32_t MAX_IMAGE_BARRIERS = 8;

// Stage bits that exist only in synchronization2 (above bit 31).
constexpr VkPipelineStageFlags2 TRANSFER_STAGES_2 = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
                                                    VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT;
constexpr VkPipelineStageFlags2 LOW_32_BITS = 0xFFFFFFFFull;

static VkPipelineStageFlags stages_to_1_1(VkPipelineStageFlags2 stages)
{
    VkPipelineStageFlags result = static_cast<VkPipelineStageFlags>(stages & LOW_32_BITS);
    VkPipelineStageFlags2 high = stages & ~LOW_32_BITS;
    if ((high & TRANSFER_STAGES_2) != 0)
    {
        result |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        high &= ~TRANSFER_STAGES_2;
    }
    if (high != 0)
    {
        // A sync2-only stage we don't translate yet: wait on everything (slow but correct).
        result |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    return result;
}

static VkAccessFlags access_to_1_1(VkAccessFlags2 access)
{
    VkAccessFlags result = static_cast<VkAccessFlags>(access & LOW_32_BITS);
    VkAccessFlags2 high = access & ~LOW_32_BITS;
    if ((high & (VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT)) != 0)
    {
        result |= VK_ACCESS_SHADER_READ_BIT;
        high &= ~(VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }
    if ((high & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) != 0)
    {
        result |= VK_ACCESS_SHADER_WRITE_BIT;
        high &= ~VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    }
    if (high != 0)
    {
        // A sync2-only access we don't translate yet: cover all memory accesses.
        result |= VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
    return result;
}

void vk_cmd_image_barriers(VkCommandBuffer command_buffer, VulkanPath path, PFN_vkCmdPipelineBarrier2 barrier2,
                           const VkImageMemoryBarrier2* barriers, uint32_t barrier_count)
{
    if (path == VulkanPath::vulkan_1_3)
    {
        VkDependencyInfo dependency = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = barrier_count,
            .pImageMemoryBarriers    = barriers,
        };
        barrier2(command_buffer, &dependency);
        return;
    }

    if (barrier_count > MAX_IMAGE_BARRIERS)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "vk_cmd_image_barriers: %u barriers, at most %u", barrier_count,
                     MAX_IMAGE_BARRIERS);
        return;
    }

    VkImageMemoryBarrier classic[MAX_IMAGE_BARRIERS] = {};
    VkPipelineStageFlags2 src_stages = 0;
    VkPipelineStageFlags2 dst_stages = 0;
    for (uint32_t i = 0; i < barrier_count; i++)
    {
        const VkImageMemoryBarrier2* barrier = &barriers[i];
        src_stages |= barrier->srcStageMask;
        dst_stages |= barrier->dstStageMask;
        classic[i] = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = access_to_1_1(barrier->srcAccessMask),
            .dstAccessMask       = access_to_1_1(barrier->dstAccessMask),
            .oldLayout           = barrier->oldLayout,
            .newLayout           = barrier->newLayout,
            .srcQueueFamilyIndex = barrier->srcQueueFamilyIndex,
            .dstQueueFamilyIndex = barrier->dstQueueFamilyIndex,
            .image               = barrier->image,
            .subresourceRange    = barrier->subresourceRange,
        };
    }

    // An empty stage mask isn't allowed here: "after nothing" is TOP_OF_PIPE,
    // "before nothing" is BOTTOM_OF_PIPE.
    VkPipelineStageFlags src = stages_to_1_1(src_stages);
    VkPipelineStageFlags dst = stages_to_1_1(dst_stages);
    if (src == 0)
    {
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    if (dst == 0)
    {
        dst = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    vkCmdPipelineBarrier(command_buffer, src, dst, 0, 0, nullptr, 0, nullptr, barrier_count, classic);
}
