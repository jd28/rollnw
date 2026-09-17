#include "loading_view.hpp"
#include "renderer.hpp"
#include "toolset_backend.hpp"

#include <RmlUi/Core.h>
#include <nw/kernel/Kernel.hpp>

namespace nw::toolset {
namespace {

std::pair<int, int> query_window_pixels(SDL_Window* window)
{
    int pixel_w = 0;
    int pixel_h = 0;
    SDL_GetWindowSizeInPixels(window, &pixel_w, &pixel_h);
    if (pixel_w <= 0 || pixel_h <= 0) {
        SDL_GetWindowSize(window, &pixel_w, &pixel_h);
    }
    return {pixel_w, pixel_h};
}

struct ProjectLoadPresentation {
    SDL_Window* window = nullptr;
    Rml::Context* context = nullptr;
    Rml::Context* palette_context = nullptr;
    ClientRenderer* renderer = nullptr;
    ProjectLoadRequest* load = nullptr;
    Rml::ElementDocument* overlay = nullptr;
    nw::kernel::ModuleLoadProgressStage stage
        = nw::kernel::ModuleLoadProgressStage::reset_services;
    Uint64 last_present_ms = 0;
    bool has_stage = false;
};

void present_project_load_progress(
    void* user_data, nw::kernel::ModuleLoadProgressStage stage)
{
    auto* presentation = static_cast<ProjectLoadPresentation*>(user_data);
    if (!presentation || !presentation->window || !presentation->context
        || !presentation->palette_context || !presentation->renderer
        || !presentation->load
        || !presentation->load->active()) {
        return;
    }

    constexpr Uint64 k_min_present_interval_ms = 32;
    const Uint64 now = SDL_GetTicks();
    const bool stage_changed = !presentation->has_stage
        || presentation->stage != stage;
    if (!stage_changed
        && now - presentation->last_present_ms < k_min_present_interval_ms) {
        return;
    }
    presentation->stage = stage;
    presentation->has_stage = true;
    presentation->last_present_ms = now;

    presentation->load->stage
        = project_load_stage_message(stage);
    sync_loading_overlay(presentation->overlay, *presentation->load);
    SDL_PumpEvents();

    const auto window_flags = SDL_GetWindowFlags(presentation->window);
    const auto pixels = query_window_pixels(presentation->window);
    if ((window_flags & SDL_WINDOW_MINIMIZED)
        || pixels.first <= 0 || pixels.second <= 0) {
        return;
    }

    uint32_t width = static_cast<uint32_t>(pixels.first);
    uint32_t height = static_cast<uint32_t>(pixels.second);
    if (!presentation->renderer->ensure_swapchain(
            presentation->window, width, height, presentation->context)) {
        return;
    }
    presentation->palette_context->SetDimensions(
        Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));
    presentation->palette_context->Update();
    presentation->renderer->begin_frame();
    presentation->context->Render();
    presentation->palette_context->Render();
    presentation->renderer->end_frame();
}

} // namespace

std::optional<CommandResult> poll_loading_project(LoadingViewState& state,
    SDL_Window* window, Rml::Context* context, Rml::Context* command_context,
    Rml::ElementDocument* overlay, ClientRenderer& renderer, ToolsetBackend& backend)
{
    if (!state.project_load.active() || !state.project_load.presented) { return std::nullopt; }
    ProjectLoadPresentation presentation{
        .window = window,
        .context = context,
        .palette_context = command_context,
        .renderer = &renderer,
        .load = &state.project_load,
        .overlay = overlay,
    };
    return backend.open_project(state.project_load.path,
        {.callback = present_project_load_progress, .user_data = &presentation});
}

} // namespace nw::toolset
