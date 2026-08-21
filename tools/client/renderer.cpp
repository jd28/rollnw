#include "renderer.hpp"

bool ClientRenderer::initialize(SDL_Window* window)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (!nwgfx_.initialize(window)) {
        return false;
    }
    if (!nwgfx_.supports_rml_render_interface()) {
        nwgfx_.shutdown();
        return false;
    }
    backend_ = Backend::nwgfx;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "rollnw client renderer backend: nw::gfx");
    return true;
#endif

    (void)window;
    return false;
}

void ClientRenderer::bootstrap_swapchain(uint32_t& width, uint32_t& height)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.bootstrap_swapchain(width, height);
    }
#else
    (void)width;
    (void)height;
#endif
}

void ClientRenderer::on_resize(uint32_t width, uint32_t height, Rml::Context* context)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.on_resize(width, height, context);
    }
#else
    (void)width;
    (void)height;
    (void)context;
#endif
}

bool ClientRenderer::ensure_swapchain(SDL_Window* window, uint32_t& width, uint32_t& height, Rml::Context* context)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.ensure_swapchain(window, width, height, context);
    }
#endif

    (void)window;
    (void)width;
    (void)height;
    (void)context;
    return true;
}

bool ClientRenderer::is_swapchain_valid() const
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.is_swapchain_valid();
    }
#endif

    return false;
}

Rml::RenderInterface* ClientRenderer::render_interface()
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.render_interface();
    }
#endif

    return nullptr;
}

void ClientRenderer::set_rml_generated_textures(
    const std::vector<nw::toolset::RmlGeneratedTexture>* textures) noexcept
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.set_rml_generated_textures(textures);
    }
#else
    (void)textures;
#endif
}

void ClientRenderer::begin_frame()
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.begin_frame();
    }
#endif
}

bool ClientRenderer::render_area_viewport(const std::filesystem::path& project_dir,
    uint64_t module_generation,
    std::string_view area_resource,
    ClientViewportRect viewport,
    int32_t dt_ms)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.render_area_viewport(
            project_dir, module_generation, area_resource, viewport, dt_ms);
    }
#endif

    (void)project_dir;
    (void)module_generation;
    (void)area_resource;
    (void)viewport;
    (void)dt_ms;
    return false;
}

bool ClientRenderer::render_preview_viewport(const std::filesystem::path& project_dir,
    uint64_t module_generation,
    std::string_view resource_path,
    ClientViewportRect viewport,
    int32_t dt_ms)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.render_preview_viewport(
            project_dir, module_generation, resource_path, viewport, dt_ms);
    }
#endif

    (void)project_dir;
    (void)module_generation;
    (void)resource_path;
    (void)viewport;
    (void)dt_ms;
    return false;
}

bool ClientRenderer::prepare_preview_object(const std::filesystem::path& project_dir,
    uint64_t module_generation,
    std::string_view resource_path)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.prepare_preview_object(
            project_dir, module_generation, resource_path);
    }
#endif

    (void)project_dir;
    (void)module_generation;
    (void)resource_path;
    return false;
}

void ClientRenderer::clear_viewer_viewport()
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.clear_viewer_viewport();
    }
#endif
}

bool ClientRenderer::drag_viewer_viewport(ClientViewportDragMode mode,
    float delta_x,
    float delta_y,
    ClientViewportRect viewport)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.drag_viewer_viewport(mode, delta_x, delta_y, viewport);
    }
#endif

    (void)mode;
    (void)delta_x;
    (void)delta_y;
    (void)viewport;
    return false;
}

bool ClientRenderer::select_viewer_area_object(
    float pixel_x,
    float pixel_y,
    ClientViewportRect viewport,
    ClientAreaSelectionTarget target)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.select_viewer_area_object(pixel_x, pixel_y, viewport, target);
    }
#endif

    (void)pixel_x;
    (void)pixel_y;
    (void)viewport;
    (void)target;
    return false;
}

bool ClientRenderer::set_viewer_area_object_selection(nw::ObjectHandle object) noexcept
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.set_viewer_area_object_selection(object);
    }
#endif

    (void)object;
    return false;
}

bool ClientRenderer::focus_viewer_area_object_selection() noexcept
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.focus_viewer_area_object_selection();
    }
#endif

    return false;
}

std::optional<glm::vec3> ClientRenderer::viewer_area_surface_point(
    float pixel_x, float pixel_y, ClientViewportRect viewport)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.viewer_area_surface_point(pixel_x, pixel_y, viewport);
    }
#endif

    (void)pixel_x;
    (void)pixel_y;
    (void)viewport;
    return std::nullopt;
}

bool ClientRenderer::preview_viewer_area_object_spatial(const nw::ObjectSpatialState& spatial)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.preview_viewer_area_object_spatial(spatial);
    }
#endif

    (void)spatial;
    return false;
}

bool ClientRenderer::append_viewer_area_object_previews(
    std::span<const nw::ObjectHandle> objects, float opacity)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.append_viewer_area_object_previews(objects, opacity);
    }
#endif

    return false;
}

bool ClientRenderer::sync_viewer_area_object_spatial(nw::ObjectHandle object)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.sync_viewer_area_object_spatial(object);
    }
#endif

    (void)object;
    return false;
}

bool ClientRenderer::rebuild_live_viewer_area(
    nw::ObjectHandle area, nw::ObjectHandle selected_object)
{
    switch (backend_) {
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    case Backend::nwgfx:
        return nwgfx_.rebuild_live_viewer_area(area, selected_object);
#endif
    case Backend::none:
        break;
    }
    return false;
}

bool ClientRenderer::rebuild_live_viewer_object(nw::ObjectHandle object)
{
    switch (backend_) {
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    case Backend::nwgfx:
        return nwgfx_.rebuild_live_viewer_object(object);
#endif
    case Backend::none:
        break;
    }
    return false;
}

bool ClientRenderer::refresh_live_viewer_object_visual(nw::ObjectHandle object)
{
    switch (backend_) {
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    case Backend::nwgfx:
        return nwgfx_.refresh_live_viewer_object_visual(object);
#endif
    case Backend::none:
        break;
    }
    return false;
}

bool ClientRenderer::clear_viewer_area_object_selection() noexcept
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.clear_viewer_area_object_selection();
    }
#endif

    return false;
}

bool ClientRenderer::zoom_viewer_viewport(float wheel_delta, ClientViewportRect viewport)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.zoom_viewer_viewport(wheel_delta, viewport);
    }
#endif

    (void)wheel_delta;
    (void)viewport;
    return false;
}

bool ClientRenderer::viewer_viewport_camera_command(ClientViewportCameraCommand command,
    float scale,
    ClientViewportRect viewport)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.viewer_viewport_camera_command(command, scale, viewport);
    }
#endif

    (void)command;
    (void)scale;
    (void)viewport;
    return false;
}

void ClientRenderer::set_area_viewer_options(const ClientAreaViewerOptions& options)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.set_area_viewer_options(options);
    }
#else
    (void)options;
#endif
}

ClientAreaViewerOptions ClientRenderer::area_viewer_options() const noexcept
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.area_viewer_options();
    }
#endif

    return {};
}

void ClientRenderer::set_viewer_area_lights_enabled(bool enabled)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.set_viewer_area_lights_enabled(enabled);
    }
#else
    (void)enabled;
#endif
}

bool ClientRenderer::viewer_area_lights_enabled() const noexcept
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.viewer_area_lights_enabled();
    }
#endif

    return true;
}

const nw::render::viewer::ViewerFrameStats* ClientRenderer::last_viewer_frame_stats() const noexcept
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.last_viewer_frame_stats();
    }
#endif

    return nullptr;
}

nw::ObjectHandle ClientRenderer::active_viewer_object() const noexcept
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.active_viewer_object();
    }
#endif
    return nw::ObjectHandle{};
}

nw::ObjectHandle ClientRenderer::area_viewer_object() const noexcept
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.area_viewer_object();
    }
#endif
    return nw::ObjectHandle{};
}

const ClientGpuFrameStats* ClientRenderer::last_gpu_frame_stats() const noexcept
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.last_gpu_frame_stats();
    }
#endif

    return nullptr;
}

ClientGpuTimerScope ClientRenderer::begin_gpu_timer(const char* label)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        return nwgfx_.begin_gpu_timer(label);
    }
#else
    (void)label;
#endif

    return {};
}

void ClientRenderer::end_gpu_timer(ClientGpuTimerScope scope)
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.end_gpu_timer(scope);
    }
#else
    (void)scope;
#endif
}

void ClientRenderer::wait_idle()
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.wait_idle();
    }
#endif
}

void ClientRenderer::end_frame()
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.end_frame();
    }
#endif
}

void ClientRenderer::shutdown()
{
#if defined(ROLLNW_CLIENT_USE_NWGFX_BACKEND)
    if (backend_ == Backend::nwgfx) {
        nwgfx_.shutdown();
    }
#endif

    backend_ = Backend::none;
}
