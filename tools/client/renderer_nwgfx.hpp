#pragma once

#include "rml_nwgfx_renderer.hpp"
#include "viewer_viewport.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace nw::gfx {
struct Context;
struct Core;
}

namespace nw::render::viewer {
struct AreaTilePreviewRow;
struct ViewerFrameStats;
}

namespace nw::toolset {
class ObjectDocument;
enum class PreviewActorLocomotion : uint8_t;
struct PreviewCameraState;
struct PreviewDoorVisualState;
struct PreviewNavigationDebugView;
struct AreaTileCrosserEdge;
}

class ClientRendererNwgfx {
public:
    ~ClientRendererNwgfx();

    bool initialize(SDL_Window* window);
    void bootstrap_swapchain(uint32_t& width, uint32_t& height);
    void on_resize(uint32_t width, uint32_t height, Rml::Context* context);
    bool ensure_swapchain(SDL_Window* window, uint32_t& width, uint32_t& height, Rml::Context* context);

    void shutdown();

    bool supports_rml_render_interface() const { return rml_ready_; }
    bool is_swapchain_valid() const;
    Rml::RenderInterface* render_interface();
    void set_rml_generated_textures(
        const std::vector<nw::toolset::RmlGeneratedTexture>* item_icons,
        const std::vector<nw::toolset::RmlGeneratedTexture>* area_tiles) noexcept;
    void begin_frame();
    [[nodiscard]] bool render_area_viewport(const std::filesystem::path& project_dir,
        uint64_t module_generation,
        std::string_view area_resource,
        nw::toolset::ObjectDocument& document,
        ClientViewportRect viewport,
        int32_t dt_ms);
    [[nodiscard]] bool render_preview_viewport(const std::filesystem::path& project_dir,
        uint64_t module_generation,
        std::string_view resource_path,
        nw::toolset::ObjectDocument& document,
        ClientViewportRect viewport,
        int32_t dt_ms);
    [[nodiscard]] bool prepare_preview_object(const std::filesystem::path& project_dir,
        uint64_t module_generation,
        std::string_view resource_path,
        nw::toolset::ObjectDocument& document);
    void clear_viewer_viewport();
    bool drag_viewer_viewport(ClientViewportDragMode mode,
        float delta_x,
        float delta_y,
        ClientViewportRect viewport);
    bool select_viewer_area_object(
        float pixel_x,
        float pixel_y,
        ClientViewportRect viewport,
        ClientAreaSelectionTarget target);
    [[nodiscard]] std::optional<ClientAreaTileHit> viewer_area_tile_hit(
        float pixel_x,
        float pixel_y,
        ClientViewportRect viewport);
    bool set_viewer_area_object_selection(nw::ObjectHandle object) noexcept;
    [[nodiscard]] uint32_t active_viewer_area_debug_subindex(
        nw::ObjectHandle object) const noexcept;
    bool focus_viewer_area_object_selection() noexcept;
    [[nodiscard]] std::optional<ClientViewportRay> viewer_viewport_ray(
        float pixel_x, float pixel_y, ClientViewportRect viewport);
    [[nodiscard]] std::optional<glm::vec3> viewer_area_surface_point(
        float pixel_x, float pixel_y, ClientViewportRect viewport);
    [[nodiscard]] std::optional<ClientAreaDoorHit> viewer_area_door_hit(
        float pixel_x,
        float pixel_y,
        ClientViewportRect viewport,
        std::span<const nw::ObjectHandle> doors);
    bool preview_viewer_area_object_spatial(const nw::ObjectSpatialState& spatial);
    bool append_viewer_area_object_previews(
        std::span<const nw::ObjectHandle> objects, float opacity);
    bool begin_toolset_preview_visuals(
        std::span<const nw::ObjectHandle> objects,
        std::span<const nw::toolset::PreviewDoorVisualState> doors,
        const nw::toolset::PreviewCameraState& camera);
    bool update_toolset_preview_visuals(
        std::span<const nw::ObjectSpatialState> spatial_rows,
        std::span<const nw::toolset::PreviewActorLocomotion> locomotion_rows,
        std::span<const nw::toolset::PreviewDoorVisualState> doors,
        const nw::toolset::PreviewCameraState& camera);
    bool update_toolset_preview_navigation_debug(
        const nw::toolset::PreviewNavigationDebugView& view);
    bool update_viewer_area_region_preview(
        std::span<const glm::vec3> points,
        std::optional<glm::vec3> hover,
        bool closing_valid);
    bool update_viewer_area_tile_preview(
        nw::ObjectHandle area,
        std::span<const nw::render::viewer::AreaTilePreviewRow> rows,
        bool paintable = true,
        bool replace_tiles = true,
        std::optional<uint32_t> anchor_tile_index = std::nullopt,
        std::span<const nw::toolset::AreaTileCrosserEdge> crosser_edges = {});
    bool end_toolset_preview_visuals() noexcept;
    [[nodiscard]] std::optional<glm::vec3> viewer_area_camera_focus() const noexcept;
    bool sync_viewer_area_object_spatial(nw::ObjectHandle object);
    bool rebuild_live_viewer_area(nw::ObjectHandle area, nw::ObjectHandle selected_object);
    bool refresh_live_viewer_area_tiles(
        nw::ObjectHandle area, std::span<const uint32_t> tile_indices);
    bool refresh_live_viewer_area_weather(nw::ObjectHandle area);
    bool rebuild_live_viewer_object(nw::ObjectHandle object);
    bool refresh_live_viewer_object_visual(nw::ObjectHandle object);
    bool clear_viewer_area_object_selection() noexcept;
    bool zoom_viewer_viewport(float wheel_delta, ClientViewportRect viewport);
    bool viewer_viewport_camera_command(ClientViewportCameraCommand command,
        float scale,
        ClientViewportRect viewport);
    void set_area_viewer_options(const ClientAreaViewerOptions& options);
    [[nodiscard]] ClientAreaViewerOptions area_viewer_options() const noexcept;
    void set_viewer_area_lights_enabled(bool enabled);
    [[nodiscard]] bool viewer_area_lights_enabled() const noexcept;
    [[nodiscard]] const nw::render::viewer::ViewerFrameStats* last_viewer_frame_stats() const noexcept;
    [[nodiscard]] nw::ObjectHandle active_viewer_object() const noexcept;
    [[nodiscard]] nw::ObjectHandle area_viewer_object() const noexcept;
    [[nodiscard]] bool area_viewer_matches_resource(std::string_view area_resource) const;
    [[nodiscard]] const ClientGpuFrameStats* last_gpu_frame_stats() const noexcept;
    ClientGpuTimerScope begin_gpu_timer(const char* label);
    void end_gpu_timer(ClientGpuTimerScope scope);
    void wait_idle();
    void end_frame();

private:
    SDL_Window* window_ = nullptr;
    nw::gfx::Core* core_ = nullptr;
    nw::gfx::Context* context_ = nullptr;
    RmlNwgfxRenderer renderer_;
    std::unique_ptr<ClientViewerViewport> viewer_viewport_;
    std::vector<nw::gfx::GpuTimerResult> completed_gpu_timer_results_;
    ClientGpuFrameStats last_gpu_frame_stats_;
    bool rml_ready_ = false;
    ClientAreaViewerOptions area_viewer_options_;
};
