#include "vk_swapchain.h"

#include <SDL3/SDL_log.h>

#include "vk_context.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const char* vk_present_mode_to_string(VkPresentModeKHR mode)
{
    switch (mode)
    {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
        default:                               return "OTHER";
    }
}

static uint32_t clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

// Dear ImGui authors its colors in sRGB, so a UNORM target with the sRGB color
// space is the correct match for the debug UI. (Core video output gets its own
// decision when we add it.)
static VkSurfaceFormatKHR choose_surface_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);

    VkSurfaceFormatKHR chosen = {};
    if (format_count == 0)
    {
        return chosen;
    }

    VkSurfaceFormatKHR* formats = new VkSurfaceFormatKHR[format_count];
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats);

    // Fallback: whatever the driver lists first.
    chosen = formats[0];

    const VkFormat preferred[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
    bool found = false;
    for (uint32_t p = 0; p < 2 && !found; p++)
    {
        for (uint32_t i = 0; i < format_count; i++)
        {
            if (formats[i].format == preferred[p] && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                chosen = formats[i];
                found = true;
                break;
            }
        }
    }

    delete[] formats;
    return chosen;
}

// FIFO is guaranteed by the spec and is "vsync on". Mode switching
// (MAILBOX as "vsync off") comes later with VK_KHR_present_wait pacing.
static VkPresentModeKHR choose_present_mode()
{
    return VK_PRESENT_MODE_FIFO_KHR;
}

static void destroy_per_image_resources(VulkanSwapchain* swapchain, const VulkanContext* context)
{
    for (uint32_t i = 0; i < SWAPCHAIN_MAX_IMAGES; i++)
    {
        if (swapchain->image_views[i] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(context->device, swapchain->image_views[i], nullptr);
            swapchain->image_views[i] = VK_NULL_HANDLE;
        }
        if (swapchain->render_finished[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(context->device, swapchain->render_finished[i], nullptr);
            swapchain->render_finished[i] = VK_NULL_HANDLE;
        }
        swapchain->images[i] = VK_NULL_HANDLE;   // owned by the swapchain, not destroyed by us
    }
    swapchain->image_count = 0;
}

// Creates a new VkSwapchainKHR (reusing `old_swapchain` if given) plus its
// views and per-image semaphores. Does not destroy `old_swapchain`.
static bool create_swapchain_resources(VulkanSwapchain* swapchain, const VulkanContext* context, VkSurfaceKHR surface,
                                       uint32_t width, uint32_t height, VkSwapchainKHR old_swapchain)
{
    VkSurfaceCapabilitiesKHR capabilities = {};
    if (!vk_succeeded(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device, surface, &capabilities),
                      "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
    {
        return false;
    }

    swapchain->surface_format = choose_surface_format(context->physical_device, surface);
    if (swapchain->surface_format.format == VK_FORMAT_UNDEFINED)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Surface reports no formats");
        return false;
    }
    swapchain->present_mode = choose_present_mode();

    // currentExtent == 0xFFFFFFFF means "the surface size follows the swapchain", so we pick.
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        swapchain->extent = capabilities.currentExtent;
    }
    else
    {
        swapchain->extent.width  = clamp_u32(width,  capabilities.minImageExtent.width,  capabilities.maxImageExtent.width);
        swapchain->extent.height = clamp_u32(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    // One more than the minimum so we are not blocked waiting on the driver.
    // maxImageCount == 0 means "no limit".
    uint32_t max_images = SWAPCHAIN_MAX_IMAGES;
    if (capabilities.maxImageCount != 0 && capabilities.maxImageCount < max_images)
    {
        max_images = capabilities.maxImageCount;
    }
    swapchain->min_image_count = clamp_u32(capabilities.minImageCount + 1, capabilities.minImageCount, max_images);

    VkSwapchainCreateInfoKHR create_info = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = surface,
        .minImageCount    = swapchain->min_image_count,
        .imageFormat      = swapchain->surface_format.format,
        .imageColorSpace  = swapchain->surface_format.colorSpace,
        .imageExtent      = swapchain->extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = capabilities.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = swapchain->present_mode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = old_swapchain,
    };

    if (!vk_succeeded(vkCreateSwapchainKHR(context->device, &create_info, nullptr, &swapchain->swapchain),
                      "vkCreateSwapchainKHR"))
    {
        swapchain->swapchain = VK_NULL_HANDLE;
        return false;
    }

    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(context->device, swapchain->swapchain, &image_count, nullptr);
    if (image_count == 0 || image_count > SWAPCHAIN_MAX_IMAGES)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Driver gave %u swapchain images, we support 1..%u",
                     image_count, SWAPCHAIN_MAX_IMAGES);
        return false;
    }
    vkGetSwapchainImagesKHR(context->device, swapchain->swapchain, &image_count, swapchain->images);
    swapchain->image_count = image_count;

    for (uint32_t i = 0; i < image_count; i++)
    {
        VkImageViewCreateInfo view_info = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = swapchain->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = swapchain->surface_format.format,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        if (!vk_succeeded(vkCreateImageView(context->device, &view_info, nullptr, &swapchain->image_views[i]),
                          "vkCreateImageView (swapchain)"))
        {
            return false;
        }

        VkSemaphoreCreateInfo semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        if (!vk_succeeded(vkCreateSemaphore(context->device, &semaphore_info, nullptr, &swapchain->render_finished[i]),
                          "vkCreateSemaphore (render finished)"))
        {
            return false;
        }
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "Swapchain %ux%u, %u images, format %d, present mode %s",
                swapchain->extent.width, swapchain->extent.height, swapchain->image_count,
                static_cast<int>(swapchain->surface_format.format), vk_present_mode_to_string(swapchain->present_mode));
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool vulkan_swapchain_create(VulkanSwapchain* swapchain, const VulkanContext* context, VkSurfaceKHR surface,
                             uint32_t width, uint32_t height)
{
    return create_swapchain_resources(swapchain, context, surface, width, height, VK_NULL_HANDLE);
}

bool vulkan_swapchain_recreate(VulkanSwapchain* swapchain, const VulkanContext* context, VkSurfaceKHR surface,
                               uint32_t width, uint32_t height)
{
    // Simple and safe: nothing may still use the old images or semaphores.
    vkDeviceWaitIdle(context->device);

    VkSwapchainKHR old_swapchain = swapchain->swapchain;
    destroy_per_image_resources(swapchain, context);

    bool ok = create_swapchain_resources(swapchain, context, surface, width, height, old_swapchain);

    if (old_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(context->device, old_swapchain, nullptr);
    }
    return ok;
}

void vulkan_swapchain_destroy(VulkanSwapchain* swapchain, const VulkanContext* context)
{
    if (context->device == VK_NULL_HANDLE)
    {
        return;
    }

    destroy_per_image_resources(swapchain, context);

    if (swapchain->swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(context->device, swapchain->swapchain, nullptr);
        swapchain->swapchain = VK_NULL_HANDLE;
    }
}
