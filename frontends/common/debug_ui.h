#pragma once

// Dear ImGui setup for a frontend's development/debug UI.
// Thin wrapper over the official imgui SDL3 + Vulkan backends (deps/imgui/backends).

#include <cstdint>
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

// Textures shown with ImGui (e.g. a core's video). The id is ImGui's ImTextureID
// (a VkDescriptorSet underneath), kept as uint64_t so this header needs no imgui.h.
// The view must be in SHADER_READ_ONLY_OPTIMAL layout whenever ImGui draws it.
uint64_t debug_ui_add_texture(VkImageView view);
void     debug_ui_remove_texture(uint64_t texture_id);

// ImGui::Image() with nearest-neighbour filtering (sharp pixels) instead of ImGui's
// default linear. Call inside an ImGui window.
void debug_ui_image_nearest(uint64_t texture_id, float width, float height, float uv_max_x, float uv_max_y);
