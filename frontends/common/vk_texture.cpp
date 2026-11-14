#include "vk_texture.h"

#include <cstring>

#include <SDL3/SDL_log.h>

#include "vk_barrier.h"
#include "vk_context.h"

// ---------------------------------------------------------------------------
// Create / destroy
// ---------------------------------------------------------------------------

static bool allocate_memory(const VulkanContext* context, VkMemoryRequirements requirements,
                            VkMemoryPropertyFlags properties, VkDeviceMemory* memory, const char* what)
{
    uint32_t type_index = 0;
    if (!vulkan_find_memory_type(context, requirements.memoryTypeBits, properties, &type_index))
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "No suitable memory type for %s", what);
        return false;
    }

    VkMemoryAllocateInfo allocate_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = type_index,
    };
    return vk_succeeded(vkAllocateMemory(context->device, &allocate_info, nullptr, memory), what);
}

static bool create_image(StreamingTexture* texture, const VulkanContext* context)
{
    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = STREAMING_TEXTURE_FORMAT,
        .extent        = { texture->capacity_width, texture->capacity_height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        // TRANSFER_SRC: picture.h blits from it for the bilinear filters.
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!vk_succeeded(vkCreateImage(context->device, &image_info, nullptr, &texture->image), "vkCreateImage (streaming)"))
    {
        return false;
    }

    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(context->device, texture->image, &requirements);
    if (!allocate_memory(context, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &texture->image_memory,
                         "vkAllocateMemory (streaming image)"))
    {
        return false;
    }
    if (!vk_succeeded(vkBindImageMemory(context->device, texture->image, texture->image_memory, 0), "vkBindImageMemory"))
    {
        return false;
    }

    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = texture->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = STREAMING_TEXTURE_FORMAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    return vk_succeeded(vkCreateImageView(context->device, &view_info, nullptr, &texture->view),
                        "vkCreateImageView (streaming)");
}

static bool create_staging_buffer(StreamingTexture* texture, const VulkanContext* context, uint32_t slot)
{
    VkBufferCreateInfo buffer_info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = static_cast<VkDeviceSize>(texture->capacity_width) * texture->capacity_height * 4u,
        .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!vk_succeeded(vkCreateBuffer(context->device, &buffer_info, nullptr, &texture->staging[slot]), "vkCreateBuffer (staging)"))
    {
        return false;
    }

    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(context->device, texture->staging[slot], &requirements);

    // Host-coherent: our memcpy is visible to the GPU without a flush.
    if (!allocate_memory(context, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &texture->staging_memory[slot], "vkAllocateMemory (staging)"))
    {
        return false;
    }
    if (!vk_succeeded(vkBindBufferMemory(context->device, texture->staging[slot], texture->staging_memory[slot], 0),
                      "vkBindBufferMemory (staging)"))
    {
        return false;
    }
    return vk_succeeded(vkMapMemory(context->device, texture->staging_memory[slot], 0, VK_WHOLE_SIZE, 0,
                                    &texture->staging_mapped[slot]),
                        "vkMapMemory (staging)");
}

bool streaming_texture_create(StreamingTexture* texture, const VulkanContext* context, uint32_t width, uint32_t height)
{
    *texture = StreamingTexture{};
    texture->path = context->path;
    texture->barrier2 = context->cmd_pipeline_barrier2;
    texture->capacity_width = width;
    texture->capacity_height = height;

    if (width == 0 || height == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Streaming texture needs a non-zero size");
        return false;
    }
    if (!create_image(texture, context))
    {
        streaming_texture_destroy(texture, context);
        return false;
    }
    for (uint32_t slot = 0; slot < FRAMES_IN_FLIGHT; slot++)
    {
        if (!create_staging_buffer(texture, context, slot))
        {
            streaming_texture_destroy(texture, context);
            return false;
        }
    }
    return true;
}

void streaming_texture_destroy(StreamingTexture* texture, const VulkanContext* context)
{
    if (context->device == VK_NULL_HANDLE)
    {
        return;
    }

    for (uint32_t slot = 0; slot < FRAMES_IN_FLIGHT; slot++)
    {
        if (texture->staging_mapped[slot] != nullptr)
        {
            vkUnmapMemory(context->device, texture->staging_memory[slot]);
        }
        if (texture->staging[slot] != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(context->device, texture->staging[slot], nullptr);
        }
        if (texture->staging_memory[slot] != VK_NULL_HANDLE)
        {
            vkFreeMemory(context->device, texture->staging_memory[slot], nullptr);
        }
    }
    if (texture->view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(context->device, texture->view, nullptr);
    }
    if (texture->image != VK_NULL_HANDLE)
    {
        vkDestroyImage(context->device, texture->image, nullptr);
    }
    if (texture->image_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(context->device, texture->image_memory, nullptr);
    }
    *texture = StreamingTexture{};
}

// ---------------------------------------------------------------------------
// Upload
// ---------------------------------------------------------------------------

bool streaming_texture_upload(StreamingTexture* texture, VkCommandBuffer command_buffer, uint32_t frame_index,
                              const uint32_t* pixels, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > texture->capacity_width || height > texture->capacity_height ||
        frame_index >= FRAMES_IN_FLIGHT)
    {
        return false;
    }

    // 1. CPU: into this slot's staging buffer. The renderer already waited for
    //    this slot's fence, so the GPU is done with the previous copy from it.
    std::memcpy(texture->staging_mapped[frame_index], pixels, static_cast<size_t>(width) * height * 4u);

    // 2. GPU: make the image writable. Earlier frames may still be sampling it
    //    in their fragment shaders (the write must wait for those reads).
    bool first_use = texture->layout == VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageMemoryBarrier2 to_transfer = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask  = first_use ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = first_use ? VK_ACCESS_2_NONE : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout     = texture->layout,
        .newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image         = texture->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vk_cmd_image_barriers(command_buffer, texture->path, texture->barrier2, &to_transfer, 1);

    // 3. GPU: copy the frame into the top-left corner of the image.
    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = width,    // tightly packed rows
        .bufferImageHeight = height,
        .imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset       = { 0, 0, 0 },
        .imageExtent       = { width, height, 1 },
    };
    vkCmdCopyBufferToImage(command_buffer, texture->staging[frame_index], texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // 4. GPU: make the image readable by the fragment shader that draws it.
    VkImageMemoryBarrier2 to_shader = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image         = texture->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vk_cmd_image_barriers(command_buffer, texture->path, texture->barrier2, &to_shader, 1);

    texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    texture->content_width = width;
    texture->content_height = height;
    texture->has_content = true;
    return true;
}

void streaming_texture_uv_max(const StreamingTexture* texture, float* u, float* v)
{
    *u = texture->capacity_width > 0 ? static_cast<float>(texture->content_width) / static_cast<float>(texture->capacity_width) : 0.0f;
    *v = texture->capacity_height > 0 ? static_cast<float>(texture->content_height) / static_cast<float>(texture->capacity_height) : 0.0f;
}
