#pragma once

#include "viewport_rect.hpp"

#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectHandle.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace nw::gfx {
struct CommandList;
struct Context;
}

namespace nw::render::viewer {
struct ViewerFrameStats;
}

namespace nw::toolset {
enum class PreviewActorLocomotion : uint8_t;
struct PreviewCameraState;
struct PreviewDoorVisualState;
struct PreviewNavigationDebugView;
}

class ClientViewerViewport {
public:
    explicit ClientViewerViewport(nw::gfx::Context* context);
    ~ClientViewerViewport();

    ClientViewerViewport(const ClientViewerViewport&) = delete;
    ClientViewerViewport& operator=(const ClientViewerViewport&) = delete;

    [[nodiscard]] bool render(nw::gfx::CommandList* command_list,
        const std::filesystem::path& project_dir,
        uint64_t module_generation,
        std::string_view area_resource,
        ClientViewportRect viewport,
        int32_t dt_ms);
    [[nodiscard]] bool render_preview(nw::gfx::CommandList* command_list,
        const std::filesystem::path& project_dir,
        uint64_t module_generation,
        std::string_view resource_path,
        ClientViewportRect viewport,
        int32_t dt_ms);
    [[nodiscard]] bool prepare_preview(const std::filesystem::path& project_dir,
        uint64_t module_generation,
        std::string_view resource_path);
    void clear();
    void shutdown();
    bool drag(ClientViewportDragMode mode, float delta_x, float delta_y, ClientViewportRect viewport);
    bool select_area_object(
        float pixel_x,
        float pixel_y,
        ClientViewportRect viewport,
        ClientAreaSelectionTarget target);
    bool set_area_object_selection(nw::ObjectHandle object) noexcept;
    bool focus_area_object_selection() noexcept;
    [[nodiscard]] std::optional<ClientViewportRay> viewport_ray(
        float pixel_x, float pixel_y, ClientViewportRect viewport);
    [[nodiscard]] std::optional<glm::vec3> area_surface_point(
        float pixel_x, float pixel_y, ClientViewportRect viewport);
    [[nodiscard]] std::optional<ClientAreaDoorHit> area_door_hit(
        float pixel_x,
        float pixel_y,
        ClientViewportRect viewport,
        std::span<const nw::ObjectHandle> doors);
    bool preview_area_object_spatial(const nw::ObjectSpatialState& spatial);
    bool append_area_object_previews(
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
    bool end_toolset_preview_visuals() noexcept;
    [[nodiscard]] std::optional<glm::vec3> area_camera_focus() const noexcept;
    bool sync_area_object_spatial(nw::ObjectHandle object);
    bool rebuild_live_area(nw::ObjectHandle area, nw::ObjectHandle selected_object);
    bool rebuild_live_object(nw::ObjectHandle object);
    bool refresh_live_object_visual(nw::ObjectHandle object);
    bool clear_area_object_selection() noexcept;
    bool zoom(float wheel_delta, ClientViewportRect viewport);
    bool camera_command(ClientViewportCameraCommand command, float scale, ClientViewportRect viewport);
    void set_area_options(const ClientAreaViewerOptions& options) noexcept;
    [[nodiscard]] ClientAreaViewerOptions area_options() const noexcept;
    void set_area_lights_enabled(bool enabled) noexcept;
    [[nodiscard]] bool area_lights_enabled() const noexcept;
    [[nodiscard]] const nw::render::viewer::ViewerFrameStats* last_frame_stats() const noexcept;
    [[nodiscard]] nw::ObjectHandle active_object() const noexcept;
    [[nodiscard]] nw::ObjectHandle area_object() const noexcept;
    [[nodiscard]] bool matches_area_resource(std::string_view area_resource) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
