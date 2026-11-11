// piccolo: basic libretro frontend built on host-libretro.
//
// Current scope: open a 1920x1080 SDL3 window, render with Vulkan, and show
// one floating Dear ImGui window. No cores, no host, no audio yet.
// Window/Vulkan/ImGui setup is shared with other frontends (frontends/common/app_shell).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"

#include "app_shell.h"
#include "vk_swapchain.h"

static const ClearColor CLEAR_COLOR = { 0.08f, 0.08f, 0.10f, 1.0f };

struct App
{
    AppShell shell = {};
    bool     running = false;
    bool     show_imgui_demo = false;
};

static void app_handle_events(App* app)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        app_shell_process_event(&app->shell, &event);

        if (event.type == SDL_EVENT_QUIT)
        {
            app->running = false;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(app->shell.window))
        {
            app->running = false;
        }
    }
}

static void app_draw_debug_ui(App* app)
{
    const ImGuiIO& io = ImGui::GetIO();
    const VkPhysicalDeviceProperties* gpu = &app->shell.vulkan.physical_device_properties;
    const VulkanSwapchain* swapchain = &app->shell.swapchain;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("piccolo");

    ImGui::Text("GPU: %s", gpu->deviceName);
    ImGui::Text("Vulkan %u.%u.%u", VK_API_VERSION_MAJOR(gpu->apiVersion), VK_API_VERSION_MINOR(gpu->apiVersion),
                VK_API_VERSION_PATCH(gpu->apiVersion));
    ImGui::Text("Swapchain: %ux%u, %u images, %s", swapchain->extent.width, swapchain->extent.height,
                swapchain->image_count, vk_present_mode_to_string(swapchain->present_mode));
    ImGui::Text("Validation: %s", app->shell.vulkan.validation_enabled ? "on" : "off");
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

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    App app = {};

    AppShellConfig config = {};
    config.name       = "piccolo";
    config.identifier = "org.multitap.piccolo";

    if (!app_shell_create(&app.shell, &config))
    {
        app_shell_destroy(&app.shell);
        return 1;
    }

    app.running = true;
    while (app.running)
    {
        app_handle_events(&app);

        if (app_shell_begin_frame(&app.shell))
        {
            app_draw_debug_ui(&app);
            if (!app_shell_present(&app.shell, CLEAR_COLOR))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal rendering error, exiting");
                break;
            }
        }
    }

    app_shell_destroy(&app.shell);
    return 0;
}
