// piccolo: basic libretro frontend built on host-libretro.
//
// Current scope: open a 1920x1080 SDL3 window, render with Vulkan, and show
// one floating Dear ImGui window. No cores, no host, no audio yet.

#include <cstdint>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#include "imgui.h"

#include "debug_ui.h"
#include "renderer.h"
#include "vk_context.h"
#include "vk_swapchain.h"

constexpr int32_t DEFAULT_WINDOW_WIDTH  = 1920;
constexpr int32_t DEFAULT_WINDOW_HEIGHT = 1080;

static const ClearColor CLEAR_COLOR = { 0.08f, 0.08f, 0.10f, 1.0f };

// Everything piccolo owns. Zero/null-initialized, so app_shutdown() can be
// called at any point during startup and only destroys what was created.
struct App
{
    SDL_Window*     window = nullptr;
    VulkanContext   vulkan = {};
    VkSurfaceKHR    surface = VK_NULL_HANDLE;
    VulkanSwapchain swapchain = {};
    Renderer        renderer = {};
    bool            debug_ui_ready = false;

    bool running = false;
    bool show_imgui_demo = false;
};

// ---------------------------------------------------------------------------
// Startup / shutdown
// ---------------------------------------------------------------------------

static bool window_size_in_pixels(SDL_Window* window, uint32_t* width, uint32_t* height)
{
    int32_t w = 0;
    int32_t h = 0;
    if (!SDL_GetWindowSizeInPixels(window, &w, &h) || w <= 0 || h <= 0)
    {
        return false;
    }
    *width  = static_cast<uint32_t>(w);
    *height = static_cast<uint32_t>(h);
    return true;
}

static bool app_create_window(App* app)
{
    int32_t width  = DEFAULT_WINDOW_WIDTH;
    int32_t height = DEFAULT_WINDOW_HEIGHT;

    // Don't open a window bigger than the screen (laptops, 1366x768 panels, macOS points).
    SDL_Rect usable = {};
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
    {
        if (width > usable.w)
        {
            width = usable.w;
        }
        if (height > usable.h)
        {
            height = usable.h;
        }
    }

    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    app->window = SDL_CreateWindow("piccolo", width, height, flags);
    if (app->window == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

static bool app_init(App* app)
{
    SDL_SetAppMetadata("piccolo", "0.1.0", "org.multitap.piccolo");

    // SDL hides everything below ERROR for most categories by default, which
    // would also hide Vulkan validation warnings (logged as SDL_LOG_CATEGORY_GPU).
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (!app_create_window(app))
    {
        return false;
    }

    // Instance -> surface -> device (the device choice depends on the surface).
    uint32_t sdl_extension_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
    if (sdl_extensions == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
        return false;
    }
    if (!vulkan_context_create_instance(&app->vulkan, "piccolo", sdl_extensions, sdl_extension_count))
    {
        return false;
    }

    if (!SDL_Vulkan_CreateSurface(app->window, app->vulkan.instance, nullptr, &app->surface))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        return false;
    }

    if (!vulkan_context_create_device(&app->vulkan, app->surface))
    {
        return false;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!window_size_in_pixels(app->window, &width, &height))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not read window size: %s", SDL_GetError());
        return false;
    }
    if (!vulkan_swapchain_create(&app->swapchain, &app->vulkan, app->surface, width, height))
    {
        return false;
    }

    if (!renderer_create(&app->renderer, &app->vulkan))
    {
        return false;
    }

    if (!debug_ui_init(app->window, &app->vulkan, &app->swapchain))
    {
        return false;
    }
    app->debug_ui_ready = true;

    SDL_SetWindowPosition(app->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(app->window);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Window %ux%u pixels, display scale %.2f",
                width, height, static_cast<double>(SDL_GetWindowDisplayScale(app->window)));
    return true;
}

static void app_shutdown(App* app)
{
    if (app->vulkan.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(app->vulkan.device);
    }

    if (app->debug_ui_ready)
    {
        debug_ui_shutdown();
        app->debug_ui_ready = false;
    }

    renderer_destroy(&app->renderer, &app->vulkan);
    vulkan_swapchain_destroy(&app->swapchain, &app->vulkan);

    if (app->surface != VK_NULL_HANDLE)
    {
        SDL_Vulkan_DestroySurface(app->vulkan.instance, app->surface, nullptr);
        app->surface = VK_NULL_HANDLE;
    }

    vulkan_context_destroy(&app->vulkan);

    if (app->window != nullptr)
    {
        SDL_DestroyWindow(app->window);
        app->window = nullptr;
    }

    SDL_Quit();
}

// ---------------------------------------------------------------------------
// Per frame
// ---------------------------------------------------------------------------

static void app_handle_events(App* app)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        debug_ui_process_event(&event);

        if (event.type == SDL_EVENT_QUIT)
        {
            app->running = false;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(app->window))
        {
            app->running = false;
        }
    }
}

static bool app_recreate_swapchain(App* app)
{
    uint32_t width = 0;
    uint32_t height = 0;
    if (!window_size_in_pixels(app->window, &width, &height))
    {
        return true;   // minimized / zero-sized: try again later
    }
    if (!vulkan_swapchain_recreate(&app->swapchain, &app->vulkan, app->surface, width, height))
    {
        return false;
    }
    debug_ui_on_swapchain_recreated(&app->swapchain);
    return true;
}

static void app_draw_debug_ui(App* app)
{
    const ImGuiIO& io = ImGui::GetIO();
    const VkPhysicalDeviceProperties* gpu = &app->vulkan.physical_device_properties;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("piccolo");

    ImGui::Text("GPU: %s", gpu->deviceName);
    ImGui::Text("Vulkan %u.%u.%u", VK_API_VERSION_MAJOR(gpu->apiVersion), VK_API_VERSION_MINOR(gpu->apiVersion),
                VK_API_VERSION_PATCH(gpu->apiVersion));
    ImGui::Text("Swapchain: %ux%u, %u images, %s", app->swapchain.extent.width, app->swapchain.extent.height,
                app->swapchain.image_count, vk_present_mode_to_string(app->swapchain.present_mode));
    ImGui::Text("Validation: %s", app->vulkan.validation_enabled ? "on" : "off");
    ImGui::Separator();
    ImGui::Text("%.3f ms/frame (%.1f FPS)", static_cast<double>(1000.0f / io.Framerate), static_cast<double>(io.Framerate));
    ImGui::Separator();
    ImGui::Checkbox("Dear ImGui demo window", &app->show_imgui_demo);

    ImGui::End();

    if (app->show_imgui_demo)
    {
        ImGui::ShowDemoWindow(&app->show_imgui_demo);
    }
}

// Returns false on a fatal error.
static bool app_run_frame(App* app)
{
    if (SDL_GetWindowFlags(app->window) & SDL_WINDOW_MINIMIZED)
    {
        SDL_Delay(10);
        return true;
    }

    // Window resized since the swapchain was built?
    uint32_t width = 0;
    uint32_t height = 0;
    if (window_size_in_pixels(app->window, &width, &height) &&
        (width != app->swapchain.extent.width || height != app->swapchain.extent.height))
    {
        if (!app_recreate_swapchain(app))
        {
            return false;
        }
    }

    debug_ui_begin_frame();
    app_draw_debug_ui(app);

    RenderFrameResult result = renderer_begin_frame(&app->renderer, &app->vulkan, &app->swapchain, CLEAR_COLOR);
    if (result == RenderFrameResult::ok)
    {
        debug_ui_render(renderer_command_buffer(&app->renderer));
        result = renderer_end_frame(&app->renderer, &app->vulkan, &app->swapchain);
    }
    else
    {
        debug_ui_end_frame_without_render();
    }

    if (result == RenderFrameResult::swapchain_needs_recreate)
    {
        return app_recreate_swapchain(app);
    }
    return result == RenderFrameResult::ok;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    App app = {};

    if (!app_init(&app))
    {
        app_shutdown(&app);
        return 1;
    }

    app.running = true;
    while (app.running)
    {
        app_handle_events(&app);
        if (!app_run_frame(&app))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal rendering error, exiting");
            break;
        }
    }

    app_shutdown(&app);
    return 0;
}
