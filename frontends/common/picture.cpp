#include "picture.h"

#include <SDL3/SDL_log.h>

#include "imgui.h"

#include "debug_ui.h"
#include "vk_barrier.h"
#include "vk_context.h"

// ---------------------------------------------------------------------------
// The scaled copy
// ---------------------------------------------------------------------------

void picture_destroy(Picture* picture, const VulkanContext* context)
{
    if (picture->image != VK_NULL_HANDLE || picture->texture_id != 0)
    {
        vkDeviceWaitIdle(context->device);   // earlier frames may still be drawing it
    }
    debug_ui_remove_texture(picture->texture_id);
    if (picture->view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(context->device, picture->view, nullptr);
    }
    if (picture->image != VK_NULL_HANDLE)
    {
        vkDestroyImage(context->device, picture->image, nullptr);
    }
    if (picture->image_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(context->device, picture->image_memory, nullptr);
    }
    bool blit_checked = picture->blit_checked;
    bool blit_supported = picture->blit_supported;
    *picture = Picture{};
    picture->blit_checked = blit_checked;
    picture->blit_supported = blit_supported;
}

static bool create_copy(Picture* picture, const VulkanContext* context, uint32_t width, uint32_t height)
{
    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = STREAMING_TEXTURE_FORMAT,
        .extent        = { width, height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!vk_succeeded(vkCreateImage(context->device, &image_info, nullptr, &picture->image), "vkCreateImage (picture)"))
    {
        return false;
    }

    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(context->device, picture->image, &requirements);
    uint32_t type_index = 0;
    if (!vulkan_find_memory_type(context, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &type_index))
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "No device-local memory for the picture copy");
        return false;
    }
    VkMemoryAllocateInfo allocate_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = type_index,
    };
    if (!vk_succeeded(vkAllocateMemory(context->device, &allocate_info, nullptr, &picture->image_memory),
                      "vkAllocateMemory (picture)") ||
        !vk_succeeded(vkBindImageMemory(context->device, picture->image, picture->image_memory, 0),
                      "vkBindImageMemory (picture)"))
    {
        return false;
    }

    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = picture->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = STREAMING_TEXTURE_FORMAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    if (!vk_succeeded(vkCreateImageView(context->device, &view_info, nullptr, &picture->view),
                      "vkCreateImageView (picture)"))
    {
        return false;
    }
    picture->texture_id = debug_ui_add_texture(picture->view);
    picture->width = width;
    picture->height = height;
    picture->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

// Blit (both ways) and linear filtering are required for this format by the
// Vulkan spec, but check once instead of trusting every driver.
static void check_blit_support(Picture* picture, const VulkanContext* context)
{
    picture->blit_checked = true;
    VkFormatProperties properties = {};
    vkGetPhysicalDeviceFormatProperties(context->physical_device, STREAMING_TEXTURE_FORMAT, &properties);
    VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    picture->blit_supported = (properties.optimalTilingFeatures & needed) == needed;
    if (!picture->blit_supported)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "This GPU can't blit/filter the picture format: bilinear filters fall back");
    }
}

// ---------------------------------------------------------------------------
// Per frame
// ---------------------------------------------------------------------------

bool picture_prepare(Picture* picture, const VulkanContext* context, const StreamingTexture* source,
                     const ViewportResult* viewport)
{
    picture->ready = false;
    picture->use_copy = false;
    picture->path = context->path;
    picture->barrier2 = context->cmd_pipeline_barrier2;
    if (!source->has_content || viewport->width == 0 || viewport->height == 0 || viewport->source_width == 0 ||
        viewport->source_height == 0)
    {
        return false;
    }
    picture->viewport = *viewport;
    picture->ready = true;

    if (viewport->filter == ViewportFilter::nearest)
    {
        return true;
    }
    if (!picture->blit_checked)
    {
        check_blit_support(picture, context);
    }
    if (!picture->blit_supported)
    {
        return true;   // drawn straight from the source, bilinear
    }

    // Bilinear: a copy of the shown part, same size. Sharp bilinear: scaled
    // up by the whole part of the scale, within the GPU's image size limit.
    uint32_t prescale_x = viewport->filter == ViewportFilter::sharp_bilinear ? viewport->prescale_x : 1;
    uint32_t prescale_y = viewport->filter == ViewportFilter::sharp_bilinear ? viewport->prescale_y : 1;
    uint32_t limit = context->physical_device_properties.limits.maxImageDimension2D;
    while (prescale_x > 1 && static_cast<uint64_t>(viewport->source_width) * prescale_x > limit)
    {
        prescale_x--;
    }
    while (prescale_y > 1 && static_cast<uint64_t>(viewport->source_height) * prescale_y > limit)
    {
        prescale_y--;
    }
    uint32_t width = viewport->source_width * prescale_x;
    uint32_t height = viewport->source_height * prescale_y;

    if (picture->image == VK_NULL_HANDLE || width != picture->width || height != picture->height)
    {
        picture_destroy(picture, context);
        picture->viewport = *viewport;
        picture->ready = true;
        if (!create_copy(picture, context, width, height))
        {
            picture_destroy(picture, context);
            picture->viewport = *viewport;
            picture->ready = true;
            return true;   // drawn straight from the source this frame
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "Picture copy: %ux%u (x%u, x%u)", width, height, prescale_x, prescale_y);
    }
    picture->use_copy = true;
    return true;
}

void picture_draw(const Picture* picture, ImDrawList* draw_list, const StreamingTexture* source,
                  uint64_t source_texture_id, float origin_x, float origin_y)
{
    if (!picture->ready)
    {
        return;
    }
    const ViewportResult* viewport = &picture->viewport;
    ImVec2 top_left(origin_x + static_cast<float>(viewport->x), origin_y + static_cast<float>(viewport->y));
    ImVec2 bottom_right(top_left.x + static_cast<float>(viewport->width), top_left.y + static_cast<float>(viewport->height));
    const ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    if (picture->use_copy)
    {
        draw_list->AddCallback(platform_io.DrawCallback_SetSamplerLinear, nullptr);
        draw_list->AddImage(ImTextureRef(static_cast<ImTextureID>(picture->texture_id)), top_left, bottom_right,
                            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        return;
    }

    // Straight from the source: the shown part of its (bigger) texture.
    float capacity_width = static_cast<float>(source->capacity_width);
    float capacity_height = static_cast<float>(source->capacity_height);
    ImVec2 uv_min(static_cast<float>(viewport->source_x) / capacity_width,
                  static_cast<float>(viewport->source_y) / capacity_height);
    ImVec2 uv_max(static_cast<float>(viewport->source_x + viewport->source_width) / capacity_width,
                  static_cast<float>(viewport->source_y + viewport->source_height) / capacity_height);
    bool nearest = viewport->filter == ViewportFilter::nearest;
    draw_list->AddCallback(nearest ? platform_io.DrawCallback_SetSamplerNearest : platform_io.DrawCallback_SetSamplerLinear,
                           nullptr);
    draw_list->AddImage(ImTextureRef(static_cast<ImTextureID>(source_texture_id)), top_left, bottom_right, uv_min, uv_max);
    // Back to ImGui's default for whatever this draw list draws next.
    draw_list->AddCallback(platform_io.DrawCallback_SetSamplerLinear, nullptr);
}

void picture_record(Picture* picture, VkCommandBuffer command_buffer, StreamingTexture* source)
{
    if (!picture->ready || !picture->use_copy || picture->image == VK_NULL_HANDLE || !source->has_content ||
        source->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        return;
    }
    const ViewportResult* viewport = &picture->viewport;

    // Source: readable by the blit. Its last writer was this or an earlier
    // frame's upload copy; its last readers, earlier frames' fragment shaders.
    // Copy: writable. Earlier frames may still be sampling it.
    bool first_use = picture->layout == VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageMemoryBarrier2 before[2] = {
        {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout     = source->layout,
            .newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image         = source->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
        {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask  = first_use ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .srcAccessMask = first_use ? VK_ACCESS_2_NONE : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout     = picture->layout,
            .newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image         = picture->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
    };
    vk_cmd_image_barriers(command_buffer, picture->path, picture->barrier2, before, 2);

    // Nearest: the copy is a whole multiple of the shown part, so every
    // source pixel becomes an exact block.
    VkImageBlit region = {
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .srcOffsets = {
            { static_cast<int32_t>(viewport->source_x), static_cast<int32_t>(viewport->source_y), 0 },
            { static_cast<int32_t>(viewport->source_x + viewport->source_width),
              static_cast<int32_t>(viewport->source_y + viewport->source_height), 1 },
        },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstOffsets = {
            { 0, 0, 0 },
            { static_cast<int32_t>(picture->width), static_cast<int32_t>(picture->height), 1 },
        },
    };
    vkCmdBlitImage(command_buffer, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, picture->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);

    // Both back to being sampled. The next upload of the source waits on the
    // fragment stage, which this barrier chains after the blit.
    VkImageMemoryBarrier2 after[2] = {
        {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image         = source->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
        {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image         = picture->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
    };
    vk_cmd_image_barriers(command_buffer, picture->path, picture->barrier2, after, 2);

    picture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    source->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
