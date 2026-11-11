#pragma once

// Dear ImGui setup for piccolo's development/debug UI.
// Thin wrapper over the official imgui SDL3 + Vulkan backends (deps/imgui/backends).

#include <vulkan/vulkan.h>

struct SDL_Window;
union SDL_Event;
struct VulkanContext;
struct VulkanSwapchain;

bool debug_ui_init(SDL_Window* window, const VulkanContext* context, const VulkanSwapchain* swapchain);
void debug_ui_shutdown();

// Forward every SDL event here. Returns true if imgui used it.
bool debug_ui_process_event(const SDL_Event* event);

// Call once per frame before any ImGui:: widget calls.
void debug_ui_begin_frame();

// Finishes the imgui frame and records its draw commands. Must be called inside
// the active rendering pass (between renderer_begin_frame and renderer_end_frame).
void debug_ui_render(VkCommandBuffer command_buffer);

// Finishes the imgui frame without drawing (e.g. the frame was skipped).
void debug_ui_end_frame_without_render();

// Call after the swapchain was recreated (image count may have changed).
void debug_ui_on_swapchain_recreated(const VulkanSwapchain* swapchain);
