#include "app_shell.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "debug_ui.h"

// ---------------------------------------------------------------------------
// Helpers
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

static bool create_window(AppShell* shell, const AppShellConfig* config)
{
    int32_t width  = config->window_width;
    int32_t height = config->window_height;

    // Don't open a window bigger than the screen (laptops, small panels, macOS points).
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
    shell->window = SDL_CreateWindow(config->name, width, height, flags);
    if (shell->window == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

static bool recreate_swapchain(AppShell* shell)
{
    uint32_t width = 0;
    uint32_t height = 0;
    if (!window_size_in_pixels(shell->window, &width, &height))
    {
        return true;   // minimized / zero-sized: try again later
    }
    if (!vulkan_swapchain_recreate(&shell->swapchain, &shell->vulkan, shell->surface, width, height))
    {
        return false;
    }
    debug_ui_on_swapchain_recreated(&shell->swapchain);
    return true;
}

// ---------------------------------------------------------------------------
// Create / destroy
// ---------------------------------------------------------------------------

bool app_shell_create(AppShell* shell, const AppShellConfig* config)
{
    SDL_SetAppMetadata(config->name, config->version, config->identifier);

    // SDL hides everything below ERROR for most categories by default, which
    // would also hide Vulkan validation warnings (logged as SDL_LOG_CATEGORY_GPU).
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    shell->sdl_ready = true;

    if (!create_window(shell, config))
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
    if (!vulkan_context_create_instance(&shell->vulkan, config->name, sdl_extensions, sdl_extension_count))
    {
        return false;
    }

    if (!SDL_Vulkan_CreateSurface(shell->window, shell->vulkan.instance, nullptr, &shell->surface))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        return false;
    }

    if (!vulkan_context_create_device(&shell->vulkan, shell->surface))
    {
        return false;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!window_size_in_pixels(shell->window, &width, &height))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not read window size: %s", SDL_GetError());
        return false;
    }
    if (!vulkan_swapchain_create(&shell->swapchain, &shell->vulkan, shell->surface, width, height))
    {
        return false;
    }

    if (!renderer_create(&shell->renderer, &shell->vulkan))
    {
        return false;
    }

    if (!debug_ui_init(shell->window, &shell->vulkan, &shell->swapchain))
    {
        return false;
    }
    shell->debug_ui_ready = true;

    SDL_SetWindowPosition(shell->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(shell->window);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Window %ux%u pixels, display scale %.2f",
                width, height, static_cast<double>(SDL_GetWindowDisplayScale(shell->window)));
    return true;
}

void app_shell_destroy(AppShell* shell)
{
    if (shell->vulkan.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(shell->vulkan.device);
    }

    if (shell->debug_ui_ready)
    {
        debug_ui_shutdown();
        shell->debug_ui_ready = false;
    }

    renderer_destroy(&shell->renderer, &shell->vulkan);
    vulkan_swapchain_destroy(&shell->swapchain, &shell->vulkan);

    if (shell->surface != VK_NULL_HANDLE)
    {
        SDL_Vulkan_DestroySurface(shell->vulkan.instance, shell->surface, nullptr);
        shell->surface = VK_NULL_HANDLE;
    }

    vulkan_context_destroy(&shell->vulkan);

    if (shell->window != nullptr)
    {
        SDL_DestroyWindow(shell->window);
        shell->window = nullptr;
    }

    if (shell->sdl_ready)
    {
        SDL_Quit();
        shell->sdl_ready = false;
    }
}

// ---------------------------------------------------------------------------
// Per frame
// ---------------------------------------------------------------------------

bool app_shell_process_event(AppShell* shell, const SDL_Event* event)
{
    (void)shell;
    return debug_ui_process_event(event);
}

bool app_shell_begin_frame(AppShell* shell)
{
    shell->frame_started = false;
    shell->acquire_tried = false;

    if (SDL_GetWindowFlags(shell->window) & SDL_WINDOW_MINIMIZED)
    {
        SDL_Delay(10);
        return false;
    }

    // Window resized since the swapchain was built?
    uint32_t width = 0;
    uint32_t height = 0;
    if (window_size_in_pixels(shell->window, &width, &height) &&
        (width != shell->swapchain.extent.width || height != shell->swapchain.extent.height))
    {
        if (!recreate_swapchain(shell))
        {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Swapchain recreation failed");
        }
    }

    debug_ui_begin_frame();
    shell->frame_started = true;
    return true;
}

bool app_shell_acquire(AppShell* shell)
{
    if (!shell->frame_started)
    {
        return false;
    }
    if (!shell->acquire_tried)
    {
        shell->acquire_tried = true;
        shell->acquire_result = renderer_begin_frame(&shell->renderer, &shell->vulkan, &shell->swapchain);
    }
    return shell->acquire_result == RenderFrameResult::ok;
}

bool app_shell_present(AppShell* shell, ClearColor clear_color)
{
    if (!shell->frame_started)
    {
        return true;
    }

    // Acquire BEFORE marking the frame finished: app_shell_acquire() refuses to
    // run outside a frame. (Getting this order wrong leaves an acquired frame
    // whose fence was reset but never submitted: the next frame waits forever.)
    RenderFrameResult result = RenderFrameResult::error;
    bool acquired = app_shell_acquire(shell);
    shell->frame_started = false;

    if (acquired)
    {
        renderer_begin_rendering(&shell->renderer, &shell->swapchain, clear_color);
        debug_ui_render(renderer_command_buffer(&shell->renderer));
        result = renderer_end_frame(&shell->renderer, &shell->vulkan, &shell->swapchain);
    }
    else
    {
        debug_ui_end_frame_without_render();
        result = shell->acquire_result;
    }

    if (result == RenderFrameResult::swapchain_needs_recreate)
    {
        return recreate_swapchain(shell);
    }
    return result == RenderFrameResult::ok;
}

bool app_shell_set_vsync(AppShell* shell, bool vsync)
{
    if (shell->swapchain.vsync == vsync)
    {
        return true;
    }
    shell->swapchain.vsync = vsync;
    return recreate_swapchain(shell);
}

double app_shell_display_refresh_hz(const AppShell* shell)
{
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(shell->window));
    if (mode == nullptr)
    {
        return 0.0;
    }
    // The exact ratio when the driver gives one (59.94 = 60000/1001); else the rounded float.
    if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0)
    {
        return static_cast<double>(mode->refresh_rate_numerator) / static_cast<double>(mode->refresh_rate_denominator);
    }
    return static_cast<double>(mode->refresh_rate);
}

bool app_shell_vsync_forced(const AppShell* shell)
{
    return !shell->swapchain.vsync && shell->swapchain.present_mode == VK_PRESENT_MODE_FIFO_KHR;
}
