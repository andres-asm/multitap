#pragma once

// Everything a frontend needs before it draws anything: SDL, the window, Vulkan
// (instance, device, surface, swapchain, renderer) and the Dear ImGui debug UI.
// Every frontend (piccolo, the host-libretro test frontend) uses this instead of
// its own copy.
//
// Each frame:
//
//   if (app_shell_begin_frame(&shell))        // false = minimized, skip drawing
//   {
//       ... build ImGui UI ...
//       if (app_shell_acquire(&shell))       // a swapchain image is ours, recording started
//       {
//           ... optional uploads into renderer_command_buffer(&shell.renderer) ...
//       }
//       if (!app_shell_present(&shell, clear)) { fatal error }
//   }

#include <cstdint>

#include <vulkan/vulkan.h>

#include "renderer.h"
#include "vk_context.h"
#include "vk_swapchain.h"

struct SDL_Window;
union SDL_Event;

struct AppShellConfig
{
    const char* name           = "frontend";   // window title, Vulkan application name
    const char* version        = "0.1.0";
    const char* identifier     = "org.multitap.frontend";
    int32_t     window_width   = 1920;          // clamped to the screen's usable area
    int32_t     window_height  = 1080;
};

// Zero/null-initialized, so app_shell_destroy() is safe at any point during startup.
struct AppShell
{
    SDL_Window*     window         = nullptr;
    VulkanContext   vulkan         = {};
    VkSurfaceKHR    surface        = VK_NULL_HANDLE;
    VulkanSwapchain swapchain      = {};
    Renderer        renderer       = {};
    bool            sdl_ready      = false;
    bool            debug_ui_ready = false;

    // Per-frame state between begin_frame / acquire / present.
    bool              frame_started  = false;
    bool              acquire_tried  = false;
    RenderFrameResult acquire_result = RenderFrameResult::error;
};

bool app_shell_create(AppShell* shell, const AppShellConfig* config);
void app_shell_destroy(AppShell* shell);

// Forward every SDL event (feeds ImGui). Returns true if ImGui used it.
bool app_shell_process_event(AppShell* shell, const SDL_Event* event);

// Recreates the swapchain if the window was resized and starts the ImGui frame.
// Returns false when the window is minimized (nothing to draw; it sleeps briefly).
bool app_shell_begin_frame(AppShell* shell);

// Acquires the next swapchain image and starts command recording. Returns true
// if recording started; callers may then record uploads (outside the rendering pass).
bool app_shell_acquire(AppShell* shell);

// Draws the ImGui UI and presents (acquiring first if the caller didn't).
// Returns false only on a fatal Vulkan error.
bool app_shell_present(AppShell* shell, ClearColor clear_color);
