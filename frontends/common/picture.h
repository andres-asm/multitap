#pragma once

// The core's picture on screen: draws a StreamingTexture where viewport.h
// says, with the chosen filter, through Dear ImGui (so it can sit behind the
// UI or inside a window).
//
// Filters:
//   nearest         the texture as is, nearest sampling (sharp pixels).
//   bilinear        a copy of the shown part, bilinear sampling.
//   sharp bilinear  a copy scaled up by the whole part of the scale with
//                   nearest (vkCmdBlitImage), then bilinear for the rest:
//                   sharp pixels, with soft edges only where a pixel lands
//                   between two screen pixels.
// The copy is exactly the shown size (crop included), so bilinear never
// blends in pixels outside the picture.
//
// Each frame, in this order:
//   picture_prepare()   while building the UI, before picture_draw (it may
//                       recreate the copy, which waits for the GPU; only when
//                       its size changes)
//   picture_draw()      adds the picture to an ImGui draw list
//   picture_record()    after the texture upload, before rendering starts:
//                       records the copy

#include <cstdint>

#include <vulkan/vulkan.h>

#include "viewport.h"
#include "vk_texture.h"

struct ImDrawList;
struct VulkanContext;

struct Picture
{
    // The scaled copy for bilinear / sharp bilinear.
    VkImage        image        = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkImageView    view         = VK_NULL_HANDLE;
    VkImageLayout  layout       = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t       width        = 0;
    uint32_t       height       = 0;
    uint64_t       texture_id   = 0;   // ImGui texture id of the copy

    bool blit_checked   = false;
    bool blit_supported = false;   // the GPU can blit and filter our texture format

    // What this frame draws (set by picture_prepare).
    ViewportResult viewport = {};
    bool           use_copy = false;
    bool           ready    = false;
};

// Frees the copy. Waits for the GPU. Safe on an empty Picture.
void picture_destroy(Picture* picture, const VulkanContext* context);

// Decides what this frame draws. `viewport` must come from the source's
// current content size. False (nothing to draw) when there's no picture yet.
bool picture_prepare(Picture* picture, const VulkanContext* context, const StreamingTexture* source,
                     const ViewportResult* viewport);

// Adds the picture to `draw_list`. The viewport's (0, 0) is at (origin_x,
// origin_y) in ImGui coordinates. `source_texture_id` = the source's ImGui id.
void picture_draw(const Picture* picture, ImDrawList* draw_list, const StreamingTexture* source,
                  uint64_t source_texture_id, float origin_x, float origin_y);

// Records the copy into `command_buffer` (outside a rendering pass, after
// the source's upload). Does nothing for nearest.
void picture_record(Picture* picture, VkCommandBuffer command_buffer, StreamingTexture* source);
