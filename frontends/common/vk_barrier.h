#pragma once

// Image barriers for both Vulkan paths (see VulkanPath in vk_context.h).
//
// Barriers are always written in synchronization2 form (VkImageMemoryBarrier2:
// each barrier carries its own stages and accesses). On the 1.3 path they go
// to vkCmdPipelineBarrier2 as they are; on the 1.1 path they are translated
// to vkCmdPipelineBarrier. So every barrier is written once, and its comment
// explains it once.
//
// What the translation does (only for the flags we use):
//   COPY / BLIT stage                 -> TRANSFER stage
//   NONE as source stage              -> TOP_OF_PIPE      (wait for nothing)
//   NONE as destination stage         -> BOTTOM_OF_PIPE   (block nothing)
//   SHADER_SAMPLED_READ access        -> SHADER_READ
//   other stages/accesses: the same bit (sync2 kept the old values)
// vkCmdPipelineBarrier takes one source and one destination stage mask for
// all its barriers, so with several barriers those masks are the OR of
// theirs: possibly a little more waiting, never less.

#include <cstdint>
#include <vulkan/vulkan.h>

#include "vk_context.h"

void vk_cmd_image_barriers(VkCommandBuffer command_buffer, VulkanPath path, PFN_vkCmdPipelineBarrier2 barrier2,
                           const VkImageMemoryBarrier2* barriers, uint32_t barrier_count);
