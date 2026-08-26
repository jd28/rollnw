#include "renderer_nwgfx.hpp"

#include <nw/gfx/gfx.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

bool environment_flag_enabled(const char* name)
{
    const char* value = std::getenv(name);
    return value
        && (std::strcmp(value, "1") == 0
            || std::strcmp(value, "true") == 0
            || std::strcmp(value, "TRUE") == 0
            || std::strcmp(value, "yes") == 0
            || std::strcmp(value, "YES") == 0
            || std::strcmp(value, "on") == 0
            || std::strcmp(value, "ON") == 0);
}

void apply_gpu_timer_result(ClientGpuFrameStats& stats, const nw::gfx::GpuTimerResult& result)
{
    if (!result.available || !result.label) {
        return;
    }

    float* target = nullptr;
    if (std::strcmp(result.label, kClientGpuTimerUi) == 0) {
        target = &stats.ui_seconds;
    } else if (std::strcmp(result.label, kClientGpuTimerViewport) == 0) {
        target = &stats.viewport_seconds;
    } else if (std::strcmp(result.label, kClientGpuTimerOverlay) == 0) {
        target = &stats.overlay_seconds;
    } else if (std::strcmp(result.label, kClientGpuTimerPalette) == 0) {
        target = &stats.palette_seconds;
    }

    if (!target) {
        return;
    }

    *target += result.seconds;
    stats.total_seconds += result.seconds;
    ++stats.timer_count;
}

} // namespace

ClientRendererNwgfx::~ClientRendererNwgfx() = default;

bool ClientRendererNwgfx::initialize(SDL_Window* window)
{
    window_ = window;

    nw::gfx::CoreConfig core_cfg{};
    core_cfg.app_name = "rollnw-client";
    core_cfg.enable_validation = environment_flag_enabled("ROLLNW_CLIENT_VALIDATE")
        || environment_flag_enabled("ROLLNW_GFX_VALIDATE");
    core_ = nw::gfx::create_core(core_cfg);
    if (!core_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "nw::gfx create_core failed");
        return false;
    }

    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);

    nw::gfx::ContextDesc ctx_desc{};
    ctx_desc.window = window_;
    ctx_desc.width = static_cast<uint32_t>(w > 0 ? w : 1280);
    ctx_desc.height = static_cast<uint32_t>(h > 0 ? h : 720);
    context_ = nw::gfx::create_context(core_, ctx_desc);
    if (!context_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "nw::gfx create_context failed");
        nw::gfx::destroy_core(core_);
        core_ = nullptr;
        return false;
    }

    if (!renderer_.initialize(core_, context_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "RmlNwgfxRenderer init failed");
        shutdown();
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
        "nw::gfx context initialized with Client Rml renderer scaffold");
    rml_ready_ = true;
    completed_gpu_timer_results_.reserve(16);

    return true;
}

void ClientRendererNwgfx::bootstrap_swapchain(uint32_t& width, uint32_t& height)
{
    (void)width;
    (void)height;
}

void ClientRendererNwgfx::on_resize(uint32_t width, uint32_t height, Rml::Context* context)
{
    if (rml_ready_) {
        renderer_.on_resize(width, height, context);
    }
}

bool ClientRendererNwgfx::ensure_swapchain(
    SDL_Window* window, uint32_t& width, uint32_t& height, Rml::Context* context)
{
    (void)window;
    if (!rml_ready_) {
        return false;
    }

    width = std::max(1u, width);
    height = std::max(1u, height);
    if (!nw::gfx::resize_context(context_, width, height)) {
        return false;
    }

    if (context) {
        renderer_.on_resize(width, height, context);
    }
    return true;
}

bool ClientRendererNwgfx::is_swapchain_valid() const
{
    return rml_ready_;
}

Rml::RenderInterface* ClientRendererNwgfx::render_interface()
{
    if (!rml_ready_) {
        return nullptr;
    }
    return &renderer_;
}

void ClientRendererNwgfx::set_rml_generated_textures(
    const std::vector<nw::toolset::RmlGeneratedTexture>* textures) noexcept
{
    renderer_.set_generated_textures(textures);
}

void ClientRendererNwgfx::begin_frame()
{
    if (!rml_ready_) {
        return;
    }

    if (!renderer_.begin_frame()) {
        last_gpu_frame_stats_ = {};
        return;
    }

    last_gpu_frame_stats_ = {};
    if (nw::gfx::get_completed_gpu_timer_results(context_, completed_gpu_timer_results_)) {
        for (const auto& result : completed_gpu_timer_results_) {
            apply_gpu_timer_result(last_gpu_frame_stats_, result);
        }
    }
}

bool ClientRendererNwgfx::render_area_viewport(const std::filesystem::path& project_dir,
    uint64_t module_generation,
    std::string_view area_resource,
    ClientViewportRect viewport,
    int32_t dt_ms)
{
    if (!rml_ready_ || !context_ || !viewport.valid()) {
        return false;
    }

    renderer_.finish_render_pass();
    auto* command_list = renderer_.command_list();
    if (!command_list) {
        return false;
    }

    if (!viewer_viewport_) {
        viewer_viewport_ = std::make_unique<ClientViewerViewport>(context_);
    }
    viewer_viewport_->set_area_options(area_viewer_options_);
    return viewer_viewport_->render(
        command_list, project_dir, module_generation, area_resource, viewport, dt_ms);
}

bool ClientRendererNwgfx::render_preview_viewport(const std::filesystem::path& project_dir,
    uint64_t module_generation,
    std::string_view resource_path,
    ClientViewportRect viewport,
    int32_t dt_ms)
{
    if (!rml_ready_ || !context_ || !viewport.valid()) {
        return false;
    }

    renderer_.finish_render_pass();
    auto* command_list = renderer_.command_list();
    if (!command_list) {
        return false;
    }

    if (!viewer_viewport_) {
        viewer_viewport_ = std::make_unique<ClientViewerViewport>(context_);
    }
    viewer_viewport_->set_area_options(area_viewer_options_);
    return viewer_viewport_->render_preview(
        command_list, project_dir, module_generation, resource_path, viewport, dt_ms);
}

bool ClientRendererNwgfx::prepare_preview_object(const std::filesystem::path& project_dir,
    uint64_t module_generation,
    std::string_view resource_path)
{
    if (!rml_ready_ || !context_ || resource_path.empty()) {
        return false;
    }

    renderer_.finish_render_pass();
    if (!renderer_.command_list()) {
        return false;
    }

    if (!viewer_viewport_) {
        viewer_viewport_ = std::make_unique<ClientViewerViewport>(context_);
    }
    viewer_viewport_->set_area_options(area_viewer_options_);
    return viewer_viewport_->prepare_preview(
        project_dir, module_generation, resource_path);
}

void ClientRendererNwgfx::clear_viewer_viewport()
{
    if (viewer_viewport_) {
        viewer_viewport_->clear();
    }
}

bool ClientRendererNwgfx::drag_viewer_viewport(ClientViewportDragMode mode,
    float delta_x,
    float delta_y,
    ClientViewportRect viewport)
{
    return viewer_viewport_ && viewer_viewport_->drag(mode, delta_x, delta_y, viewport);
}

bool ClientRendererNwgfx::select_viewer_area_object(
    float pixel_x,
    float pixel_y,
    ClientViewportRect viewport,
    ClientAreaSelectionTarget target)
{
    return viewer_viewport_
        && viewer_viewport_->select_area_object(pixel_x, pixel_y, viewport, target);
}

bool ClientRendererNwgfx::set_viewer_area_object_selection(nw::ObjectHandle object) noexcept
{
    return viewer_viewport_ && viewer_viewport_->set_area_object_selection(object);
}

bool ClientRendererNwgfx::focus_viewer_area_object_selection() noexcept
{
    return viewer_viewport_ && viewer_viewport_->focus_area_object_selection();
}

std::optional<ClientViewportRay> ClientRendererNwgfx::viewer_viewport_ray(
    float pixel_x, float pixel_y, ClientViewportRect viewport)
{
    return viewer_viewport_
        ? viewer_viewport_->viewport_ray(pixel_x, pixel_y, viewport)
        : std::nullopt;
}

std::optional<glm::vec3> ClientRendererNwgfx::viewer_area_surface_point(
    float pixel_x, float pixel_y, ClientViewportRect viewport)
{
    return viewer_viewport_
        ? viewer_viewport_->area_surface_point(pixel_x, pixel_y, viewport)
        : std::nullopt;
}

bool ClientRendererNwgfx::preview_viewer_area_object_spatial(const nw::ObjectSpatialState& spatial)
{
    return viewer_viewport_ && viewer_viewport_->preview_area_object_spatial(spatial);
}

bool ClientRendererNwgfx::append_viewer_area_object_previews(
    std::span<const nw::ObjectHandle> objects, float opacity)
{
    return viewer_viewport_ && viewer_viewport_->append_area_object_previews(objects, opacity);
}

bool ClientRendererNwgfx::begin_toolset_preview_visuals(
    std::span<const nw::ObjectHandle> objects,
    const nw::toolset::PreviewCameraState& camera)
{
    return viewer_viewport_
        && viewer_viewport_->begin_toolset_preview_visuals(objects, camera);
}

bool ClientRendererNwgfx::update_toolset_preview_visuals(
    std::span<const nw::ObjectSpatialState> spatial_rows,
    std::span<const nw::toolset::PreviewActorLocomotion> locomotion_rows,
    const nw::toolset::PreviewCameraState& camera)
{
    return viewer_viewport_
        && viewer_viewport_->update_toolset_preview_visuals(
            spatial_rows, locomotion_rows, camera);
}

bool ClientRendererNwgfx::update_toolset_preview_navigation_debug(
    const nw::toolset::PreviewNavigationDebugView& view)
{
    return viewer_viewport_
        && viewer_viewport_->update_toolset_preview_navigation_debug(view);
}

bool ClientRendererNwgfx::end_toolset_preview_visuals() noexcept
{
    return viewer_viewport_ && viewer_viewport_->end_toolset_preview_visuals();
}

std::optional<glm::vec3> ClientRendererNwgfx::viewer_area_camera_focus() const noexcept
{
    return viewer_viewport_
        ? viewer_viewport_->area_camera_focus()
        : std::nullopt;
}

bool ClientRendererNwgfx::sync_viewer_area_object_spatial(nw::ObjectHandle object)
{
    return viewer_viewport_ && viewer_viewport_->sync_area_object_spatial(object);
}

bool ClientRendererNwgfx::rebuild_live_viewer_area(
    nw::ObjectHandle area, nw::ObjectHandle selected_object)
{
    return viewer_viewport_ && viewer_viewport_->rebuild_live_area(area, selected_object);
}

bool ClientRendererNwgfx::rebuild_live_viewer_object(nw::ObjectHandle object)
{
    return viewer_viewport_ && viewer_viewport_->rebuild_live_object(object);
}

bool ClientRendererNwgfx::refresh_live_viewer_object_visual(nw::ObjectHandle object)
{
    return viewer_viewport_ && viewer_viewport_->refresh_live_object_visual(object);
}

bool ClientRendererNwgfx::clear_viewer_area_object_selection() noexcept
{
    return viewer_viewport_ && viewer_viewport_->clear_area_object_selection();
}

bool ClientRendererNwgfx::zoom_viewer_viewport(float wheel_delta, ClientViewportRect viewport)
{
    return viewer_viewport_ && viewer_viewport_->zoom(wheel_delta, viewport);
}

bool ClientRendererNwgfx::viewer_viewport_camera_command(ClientViewportCameraCommand command,
    float scale,
    ClientViewportRect viewport)
{
    return viewer_viewport_ && viewer_viewport_->camera_command(command, scale, viewport);
}

void ClientRendererNwgfx::set_area_viewer_options(const ClientAreaViewerOptions& options)
{
    area_viewer_options_ = options;
    if (viewer_viewport_) {
        viewer_viewport_->set_area_options(options);
    }
}

ClientAreaViewerOptions ClientRendererNwgfx::area_viewer_options() const noexcept
{
    return area_viewer_options_;
}

void ClientRendererNwgfx::set_viewer_area_lights_enabled(bool enabled)
{
    area_viewer_options_.lights_enabled = enabled;
    if (viewer_viewport_) {
        viewer_viewport_->set_area_lights_enabled(enabled);
    }
}

bool ClientRendererNwgfx::viewer_area_lights_enabled() const noexcept
{
    return area_viewer_options_.lights_enabled;
}

const nw::render::viewer::ViewerFrameStats* ClientRendererNwgfx::last_viewer_frame_stats() const noexcept
{
    return viewer_viewport_ ? viewer_viewport_->last_frame_stats() : nullptr;
}

nw::ObjectHandle ClientRendererNwgfx::active_viewer_object() const noexcept
{
    return viewer_viewport_ ? viewer_viewport_->active_object() : nw::ObjectHandle{};
}

nw::ObjectHandle ClientRendererNwgfx::area_viewer_object() const noexcept
{
    return viewer_viewport_ ? viewer_viewport_->area_object() : nw::ObjectHandle{};
}

const ClientGpuFrameStats* ClientRendererNwgfx::last_gpu_frame_stats() const noexcept
{
    return &last_gpu_frame_stats_;
}

ClientGpuTimerScope ClientRendererNwgfx::begin_gpu_timer(const char* label)
{
    auto* command_list = renderer_.command_list();
    if (!rml_ready_ || !command_list) {
        return {};
    }

    const auto scope = nw::gfx::cmd_begin_gpu_timer(command_list, label);
    return ClientGpuTimerScope{scope.index};
}

void ClientRendererNwgfx::end_gpu_timer(ClientGpuTimerScope scope)
{
    auto* command_list = renderer_.command_list();
    if (!scope.valid() || !command_list) {
        return;
    }

    nw::gfx::cmd_end_gpu_timer(command_list, nw::gfx::GpuTimerScope{scope.index});
}

void ClientRendererNwgfx::wait_idle()
{
    if (context_) {
        nw::gfx::wait_idle(context_);
    }
}

void ClientRendererNwgfx::end_frame()
{
    if (rml_ready_) {
        if (auto* command_list = renderer_.command_list()) {
            nw::gfx::get_command_stats(command_list, last_gpu_frame_stats_.command_stats);
        }
        renderer_.end_frame();
    }
}

void ClientRendererNwgfx::shutdown()
{
    wait_idle();

    viewer_viewport_.reset();

    if (rml_ready_) {
        renderer_.shutdown();
        rml_ready_ = false;
    }

    if (context_) {
        nw::gfx::destroy_context(context_);
        context_ = nullptr;
    }
    if (core_) {
        nw::gfx::destroy_core(core_);
        core_ = nullptr;
    }
    window_ = nullptr;
}
