#pragma once

// A GPU texture that the CPU updates every frame (e.g. a software-rendered
// core's video). Pixels are B8G8R8A8 in memory, which is what host-libretro's
// XRGB8888-with-alpha frames are.
//
// Each frame slot (renderer_frame_index) has its own staging buffer: the CPU
// writes slot N while the GPU may still be copying from the other slot.
// The image itself is shared; barriers order the copy after earlier frames'
// reads, because barriers cover everything submitted earlier on the queue.
//
// The texture has a fixed capacity; uploads may be smaller (the core's current
// size). Draw only the uploaded region: see streaming_texture_uv_max().

#include <cstdint>
#include <vulkan/vulkan.h>

#include "renderer.h"   // FRAMES_IN_FLIGHT
#include "vk_context.h"   // VulkanPath

struct VulkanContext;

constexpr VkFormat STREAMING_TEXTURE_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;

struct StreamingTexture
{
    VkImage        image        = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkImageView    view         = VK_NULL_HANDLE;
    VkImageLayout  layout       = VK_IMAGE_LAYOUT_UNDEFINED;   // tracked so barriers know the old layout

    VkBuffer       staging[FRAMES_IN_FLIGHT]        = {};
    VkDeviceMemory staging_memory[FRAMES_IN_FLIGHT] = {};
    void*          staging_mapped[FRAMES_IN_FLIGHT] = {};      // persistently mapped, host-coherent

    uint32_t capacity_width  = 0;
    uint32_t capacity_height = 0;

    uint32_t content_width  = 0;   // size of the last upload
    uint32_t content_height = 0;
    bool     has_content    = false;   // false until the first upload: don't draw it before that

    // From the context at creation: how uploads record their barriers (vk_barrier.h).
    VulkanPath                path     = VulkanPath::vulkan_1_3;
    PFN_vkCmdPipelineBarrier2 barrier2 = nullptr;
};

bool streaming_texture_create(StreamingTexture* texture, const VulkanContext* context, uint32_t width, uint32_t height);

// The GPU must be done with the texture (call vkDeviceWaitIdle first). Safe on a
// partially created texture.
void streaming_texture_destroy(StreamingTexture* texture, const VulkanContext* context);

// Copies `pixels` (width * height, tightly packed, 4 bytes each) into this frame
// slot's staging buffer and records the copy into `command_buffer`. Must be
// recorded OUTSIDE a rendering pass (before renderer_begin_rendering).
// Fails if the frame is bigger than the capacity.
bool streaming_texture_upload(StreamingTexture* texture, VkCommandBuffer command_buffer, uint32_t frame_index,
                              const uint32_t* pixels, uint32_t width, uint32_t height);

// Bottom-right texture coordinate of the uploaded region (top-left is 0,0).
void streaming_texture_uv_max(const StreamingTexture* texture, float* u, float* v);
