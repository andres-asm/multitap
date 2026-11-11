#include "debug_ui.h"

#include <cstdlib>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

#include "vk_context.h"
#include "vk_swapchain.h"

// The imgui Vulkan backend calls this on every VkResult it gets.
// Errors (negative values) are not recoverable inside imgui, so we stop.
static void imgui_check_vk_result(VkResult result)
{
    if (result == VK_SUCCESS)
    {
        return;
    }
    SDL_LogError(SDL_LOG_CATEGORY_GPU, "imgui Vulkan backend: %s (%d)",
                 vk_result_to_string(result), static_cast<int>(result));
    if (result < 0)
    {
        std::abort();
    }
}

bool debug_ui_init(SDL_Window* window, const VulkanContext* context, const VulkanSwapchain* swapchain)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Scale for high-DPI displays (e.g. 150% on Windows, Retina on macOS).
    float display_scale = SDL_GetWindowDisplayScale(window);
    if (display_scale <= 0.0f)
    {
        display_scale = 1.0f;
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 3.0f;
    style.GrabRounding  = 3.0f;
    style.ScaleAllSizes(display_scale);
    style.FontScaleDpi = display_scale;

    if (!ImGui_ImplSDL3_InitForVulkan(window))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImGui_ImplSDL3_InitForVulkan failed");
        return false;
    }

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion     = VULKAN_REQUIRED_API_VERSION;
    init_info.Instance       = context->instance;
    init_info.PhysicalDevice = context->physical_device;
    init_info.Device         = context->device;
    init_info.QueueFamily    = context->queue_family;
    init_info.Queue          = context->queue;
    init_info.DescriptorPool = context->imgui_descriptor_pool;
    init_info.MinImageCount  = swapchain->min_image_count;
    init_info.ImageCount     = swapchain->image_count;
    init_info.CheckVkResultFn = imgui_check_vk_result;

    // Dynamic rendering: imgui builds its pipeline for our swapchain format.
    // (The backend copies the format array, so a stack variable is fine.)
    VkFormat color_format = swapchain->surface_format.format;
    init_info.UseDynamicRendering = true;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &color_format,
    };
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init_info))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImGui_ImplVulkan_Init failed");
        return false;
    }

    return true;
}

void debug_ui_shutdown()
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.BackendRendererUserData != nullptr)
    {
        ImGui_ImplVulkan_Shutdown();
    }
    if (io.BackendPlatformUserData != nullptr)
    {
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::DestroyContext();
}

bool debug_ui_process_event(const SDL_Event* event)
{
    return ImGui_ImplSDL3_ProcessEvent(event);
}

void debug_ui_begin_frame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void debug_ui_render(VkCommandBuffer command_buffer)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
}

void debug_ui_end_frame_without_render()
{
    ImGui::EndFrame();
}

void debug_ui_on_swapchain_recreated(const VulkanSwapchain* swapchain)
{
    ImGui_ImplVulkan_SetMinImageCount(swapchain->min_image_count);
}
