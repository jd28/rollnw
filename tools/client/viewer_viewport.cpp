#include "viewer_viewport.hpp"

#include "preview_session.hpp"
#include "viewer_camera_state.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/render/viewer/device.hpp>
#include <nw/resources/assets.hpp>

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace viewer = nw::render::viewer;

std::optional<std::filesystem::path> canonical_project_path(const std::filesystem::path& project_dir)
{
    std::error_code ec;
    auto result = std::filesystem::weakly_canonical(project_dir, ec);
    if (ec || result.empty()) {
        return std::nullopt;
    }
    return result;
}

std::string area_resref_from_resource(std::string_view area_resource)
{
    const auto resource = nw::Resource::from_path(std::filesystem::path{std::string{area_resource}});
    if (!resource.valid()
        || (resource.type != nw::ResourceType::caf
            && resource.type != nw::ResourceType::are)) {
        return {};
    }
    return std::string{resource.resref.view()};
}

std::vector<std::filesystem::path> viewer_shader_roots()
{
    std::vector<std::filesystem::path> result;
    const auto source_shader_root = std::filesystem::path{"lib/nw/render"} / "shaders";
    if (std::filesystem::is_directory(source_shader_root)) {
        result.push_back(source_shader_root);
    }
    if (const char* base_path = SDL_GetBasePath()) {
        const auto shader_root = std::filesystem::path{base_path} / "shaders";
        if (std::filesystem::is_directory(shader_root)) {
            result.push_back(shader_root);
        }
    }
    return result;
}

viewer::ViewerViewport to_viewer_viewport(ClientViewportRect viewport)
{
    return viewer::ViewerViewport{
        viewport.x,
        viewport.y,
        viewport.width,
        viewport.height,
    };
}

void apply_preview_camera(
    viewer::Camera& target,
    const nw::toolset::PreviewCameraState& source)
{
    target.set_orbit_view(
        source.focus,
        source.distance,
        glm::degrees(source.yaw) + 180.0f,
        glm::degrees(-source.pitch));
}

} // namespace

struct ClientViewerViewport::Impl {
    struct PreviewLoadFailure {
        std::string resource;
        std::filesystem::path path;
        std::filesystem::file_time_type modified{};
        bool exists = false;
        bool has_modified = false;
    };

    explicit Impl(nw::gfx::Context* context_)
        : context(context_)
    {
    }

    ~Impl()
    {
        shutdown();
    }

    bool render(nw::gfx::CommandList* command_list,
        const std::filesystem::path& project_dir,
        uint64_t module_generation,
        std::string_view area_resource,
        ClientViewportRect viewport,
        int32_t dt_ms)
    {
        if (!context || !command_list || !viewport.valid()) {
            return false;
        }

        const std::string area_resref = area_resref_from_resource(area_resource);
        if (area_resref.empty()) {
            discard_scene();
            if (failed_area_resource != area_resource) {
                LOG_F(WARNING, "Client area render: not an area resource: {}", area_resource);
                failed_area_resource = std::string{area_resource};
            }
            return false;
        }
        failed_area_resource.clear();

        if (!mount_project(project_dir, module_generation)) {
            return false;
        }
        if (!ensure_runtime()) {
            return false;
        }
        apply_area_reload_request();
        if (!load_area(area_resref, viewport)) {
            return false;
        }
        apply_area_options_to_session();

        draw(command_list, viewport, dt_ms);
        return true;
    }

    bool render_preview(nw::gfx::CommandList* command_list,
        const std::filesystem::path& project_dir,
        uint64_t module_generation,
        std::string_view resource_path,
        ClientViewportRect viewport,
        int32_t dt_ms)
    {
        if (!context || !command_list || !viewport.valid() || resource_path.empty()) {
            return false;
        }

        if (!mount_project(project_dir, module_generation)) {
            return false;
        }
        if (!ensure_runtime()) {
            return false;
        }
        if (!load_preview(resource_path, viewport)) {
            return false;
        }

        draw(command_list, viewport, dt_ms);
        return true;
    }

    bool prepare_preview(const std::filesystem::path& project_dir,
        uint64_t module_generation,
        std::string_view resource_path)
    {
        if (!context || resource_path.empty()) {
            return false;
        }
        if (!mount_project(project_dir, module_generation)) {
            return false;
        }
        if (!ensure_runtime()) {
            return false;
        }

        constexpr ClientViewportRect load_viewport{0, 0, 8, 8};
        return load_preview(resource_path, load_viewport);
    }

    void discard_scene()
    {
        end_toolset_preview_visuals();
        store_loaded_camera();
        if (session) {
            session->clear();
        }
        loaded_area_resref.clear();
        loaded_preview_resource.clear();
    }

    void clear()
    {
        discard_scene();
        clear_load_failures();
    }

    void shutdown()
    {
        if (context) {
            nw::gfx::wait_idle(context);
        }
        clear();
        camera_states.clear();
        session.reset();
        device.reset();
        mounted_project.clear();
        mounted_module_generation = 0;
    }

    bool mount_project(const std::filesystem::path& project_dir, uint64_t module_generation)
    {
        auto& resources = nw::kernel::resman();
        if (project_dir.empty() || module_generation == 0) {
            LOG_F(ERROR, "Client viewer viewport: no project module is mounted");
            clear();
            return false;
        }

        const auto canonical = canonical_project_path(project_dir);
        if (!canonical) {
            LOG_F(ERROR, "Client viewer viewport: failed to resolve project path '{}'", project_dir.string());
            clear();
            return false;
        }

        if (!resources.module_container()) {
            LOG_F(ERROR, "Client viewer viewport: project module is unavailable");
            clear();
            return false;
        }

        if (mounted_project == *canonical && mounted_module_generation == module_generation) {
            return true;
        }

        if (context) {
            nw::gfx::wait_idle(context);
        }
        clear();
        camera_states.clear();
        session.reset();
        device.reset();

        mounted_project = *canonical;
        mounted_module_generation = module_generation;
        LOG_F(INFO, "Client viewer viewport: observing project '{}'", mounted_project.string());
        return true;
    }

    bool ensure_runtime()
    {
        if (!device) {
            device = std::make_unique<viewer::ViewerDevice>(context, nw::kernel::resman());
            viewer::ViewerDeviceOptions options{};
            options.shader_roots = viewer_shader_roots();
            if (!device->initialize(options)) {
                device.reset();
                LOG_F(ERROR, "Client viewer viewport: failed to initialize viewer device");
                return false;
            }
        }

        if (!session) {
            session = device->make_session();
            if (!session) {
                LOG_F(ERROR, "Client viewer viewport: failed to create viewer session");
                return false;
            }
            auto preview_options = session->preview_scene_load_options();
            preview_options.area_object_editing = true;
            session->set_preview_scene_load_options(preview_options);
            apply_area_options_to_session();
        }
        return true;
    }

    bool load_area(const std::string& area_resref, ClientViewportRect viewport)
    {
        if (loaded_area_resref == area_resref && session && session->scene()) {
            update_camera_viewport(viewport);
            return true;
        }

        if (!session) {
            return false;
        }
        if (failed_area_resref == area_resref) {
            return false;
        }

        store_loaded_camera();
        if (!session->load_area(area_resref)) {
            failed_area_resref = area_resref;
            discard_scene();
            LOG_F(ERROR, "Client area render: failed to load area '{}'", area_resref);
            return false;
        }

        loaded_area_resref = area_resref;
        loaded_preview_resource.clear();
        failed_area_resref.clear();
        failed_preview.reset();
        applied_area_time_generation = 0;
        if (!restore_camera(ClientViewerSceneKind::area, area_resref, viewport)) {
            fit_camera_to_scene(viewport);
        }
        return true;
    }

    bool load_preview(std::string_view resource_path, ClientViewportRect viewport)
    {
        if (loaded_preview_resource == resource_path && session && session->scene()) {
            update_camera_viewport(viewport);
            return true;
        }

        if (!session) {
            return false;
        }

        const auto relative_path = std::filesystem::path{std::string{resource_path}};
        const auto preview_path = relative_path.is_absolute() ? relative_path : mounted_project / relative_path;
        if (preview_load_failure_is_current(resource_path, preview_path)) {
            return false;
        }
        if (!std::filesystem::exists(preview_path)) {
            LOG_F(ERROR, "Client preview viewport: resource file does not exist '{}'", preview_path.string());
            record_preview_load_failure(resource_path, preview_path);
            discard_scene();
            return false;
        }

        store_loaded_camera();
        if (!session->load_object_file(preview_path)) {
            record_preview_load_failure(resource_path, preview_path);
            discard_scene();
            LOG_F(ERROR, "Client preview viewport: failed to load '{}'", preview_path.string());
            return false;
        }

        loaded_area_resref.clear();
        loaded_preview_resource = std::string{resource_path};
        failed_area_resref.clear();
        failed_preview.reset();
        if (!restore_camera(ClientViewerSceneKind::preview, resource_path, viewport)) {
            fit_camera_to_scene(viewport);
        }
        return true;
    }

    void clear_load_failures()
    {
        failed_area_resource.clear();
        failed_area_resref.clear();
        failed_preview.reset();
    }

    bool preview_load_failure_is_current(std::string_view resource_path, const std::filesystem::path& preview_path) const
    {
        if (!failed_preview || failed_preview->resource != resource_path || failed_preview->path != preview_path) {
            return false;
        }

        std::error_code ec;
        const bool exists = std::filesystem::exists(preview_path, ec);
        if (ec) {
            return true;
        }
        if (exists != failed_preview->exists) {
            return false;
        }
        if (!exists) {
            return true;
        }

        const auto modified = std::filesystem::last_write_time(preview_path, ec);
        if (ec || !failed_preview->has_modified) {
            return true;
        }
        return modified == failed_preview->modified;
    }

    void record_preview_load_failure(std::string_view resource_path, const std::filesystem::path& preview_path)
    {
        PreviewLoadFailure failure;
        failure.resource = std::string{resource_path};
        failure.path = preview_path;

        std::error_code ec;
        failure.exists = std::filesystem::exists(preview_path, ec);
        if (!ec && failure.exists) {
            failure.modified = std::filesystem::last_write_time(preview_path, ec);
            failure.has_modified = !ec;
        }
        failed_preview = std::move(failure);
    }

    void update_camera_viewport(ClientViewportRect viewport)
    {
        if (!session) {
            return;
        }
        session->update_viewport(to_viewer_viewport(viewport));
    }

    void store_loaded_camera()
    {
        if (!session || !session->scene()) {
            return;
        }
        if (!loaded_area_resref.empty()) {
            (void)camera_states.store(
                ClientViewerSceneKind::area, loaded_area_resref, session->camera());
        } else if (!loaded_preview_resource.empty()) {
            (void)camera_states.store(
                ClientViewerSceneKind::preview, loaded_preview_resource, session->camera());
        }
    }

    bool restore_camera(ClientViewerSceneKind kind,
        std::string_view resource,
        ClientViewportRect viewport)
    {
        if (!session || !camera_states.restore(kind, resource, session->camera())) {
            return false;
        }
        update_camera_viewport(viewport);
        return true;
    }

    void fit_camera_to_scene(ClientViewportRect viewport)
    {
        if (!session) {
            return;
        }
        session->fit_to_scene(to_viewer_viewport(viewport));
    }

    bool set_area_gameplay_view(ClientViewportRect viewport)
    {
        return session && session->set_area_gameplay_view(to_viewer_viewport(viewport));
    }

    bool drag(ClientViewportDragMode mode, float delta_x, float delta_y, ClientViewportRect viewport)
    {
        if (!session || !viewport.valid()) {
            return false;
        }

        update_camera_viewport(viewport);
        auto& camera = session->camera();
        if (mode == ClientViewportDragMode::look) {
            camera.yaw(-delta_x * 0.35f);
            camera.pitch(-delta_y * 0.25f);
        } else {
            const float pan_scale = camera.pan_units_per_pixel(static_cast<float>(viewport.height));
            camera.pan(-delta_x * pan_scale, delta_y * pan_scale);
        }
        return true;
    }

    bool select_area_object(
        float pixel_x,
        float pixel_y,
        ClientViewportRect viewport,
        ClientAreaSelectionTarget target)
    {
        if (!session || !viewport.valid()) {
            return false;
        }

        const auto result = session->select_area_object(
            pixel_x,
            pixel_y,
            to_viewer_viewport(viewport),
            target == ClientAreaSelectionTarget::tile
                ? viewer::AreaObjectSelectionTarget::tile
                : viewer::AreaObjectSelectionTarget::object);
        return result.status != viewer::AreaObjectSelectionStatus::invalid_input;
    }

    bool set_area_object_selection(nw::ObjectHandle object) noexcept
    {
        return session && session->set_area_object_selection(object);
    }

    bool focus_area_object_selection() noexcept
    {
        return session && session->focus_area_object_selection();
    }

    std::optional<ClientViewportRay> viewport_ray(
        float pixel_x, float pixel_y, ClientViewportRect viewport)
    {
        if (!session || !viewport.valid()) return std::nullopt;
        const auto ray = session->viewport_ray(
            pixel_x, pixel_y, to_viewer_viewport(viewport));
        return ray
            ? std::optional<ClientViewportRay>{ClientViewportRay{
                  .origin = ray->origin,
                  .displacement = ray->direction,
              }}
            : std::nullopt;
    }

    std::optional<glm::vec3> area_surface_point(
        float pixel_x, float pixel_y, ClientViewportRect viewport)
    {
        if (!session || !viewport.valid()) {
            return std::nullopt;
        }
        return session->area_surface_point(
            pixel_x, pixel_y, to_viewer_viewport(viewport));
    }

    bool preview_area_object_spatial(const nw::ObjectSpatialState& spatial)
    {
        if (!session) {
            return false;
        }
        const std::array rows{spatial};
        const auto stats = session->update_area_object_spatial_states(rows);
        return stats.render_model_root_count != 0;
    }

    bool append_area_object_previews(
        std::span<const nw::ObjectHandle> objects, float opacity)
    {
        return session && session->append_area_object_previews(objects, opacity).ok();
    }

    bool begin_toolset_preview_visuals(
        std::span<const nw::ObjectHandle> objects,
        const nw::toolset::PreviewCameraState& camera)
    {
        if (!session || objects.empty() || !transient_preview_objects.empty()
            || loaded_area_resref.empty()) {
            return false;
        }

        const auto append = session->append_area_transient_visuals(objects);
        if (!append.ok()) {
            LOG_F(ERROR, "Client preview visuals: {}", append.diagnostic);
            return false;
        }

        transient_saved_camera = session->camera();
        transient_preview_objects.assign(objects.begin(), objects.end());
        transient_animation_inputs.clear();
        transient_animation_inputs.reserve(objects.size());
        for (const auto object : objects) {
            if (object.type == nw::ObjectType::creature) {
                transient_animation_inputs.push_back({
                    .owner = object,
                    .locomotion = viewer::AreaCreatureLocomotion::idle,
                });
            }
        }
        (void)session->update_area_creature_locomotion_animations(
            transient_animation_inputs);
        apply_preview_camera(session->camera(), camera);
        return true;
    }

    bool update_toolset_preview_visuals(
        std::span<const nw::ObjectSpatialState> spatial_rows,
        std::span<const nw::toolset::PreviewActorLocomotion> locomotion_rows,
        const nw::toolset::PreviewCameraState& camera)
    {
        if (!session || transient_preview_objects.empty()
            || spatial_rows.size() != transient_preview_objects.size()
            || locomotion_rows.size() != transient_preview_objects.size()) {
            return false;
        }

        const auto stats = session->update_area_object_spatial_states(spatial_rows);
        if (stats.render_model_root_count == 0) {
            return false;
        }
        for (size_t index = 0; index < transient_animation_inputs.size(); ++index) {
            auto& input = transient_animation_inputs[index];
            if (spatial_rows[index].owner != input.owner) {
                return false;
            }
            switch (locomotion_rows[index]) {
            case nw::toolset::PreviewActorLocomotion::walking_forward:
                input.locomotion = viewer::AreaCreatureLocomotion::walking_forward;
                break;
            case nw::toolset::PreviewActorLocomotion::walking_backward:
                input.locomotion = viewer::AreaCreatureLocomotion::walking_backward;
                break;
            case nw::toolset::PreviewActorLocomotion::strafing_left:
                input.locomotion = viewer::AreaCreatureLocomotion::strafing_left;
                break;
            case nw::toolset::PreviewActorLocomotion::strafing_right:
                input.locomotion = viewer::AreaCreatureLocomotion::strafing_right;
                break;
            case nw::toolset::PreviewActorLocomotion::turning_left:
                input.locomotion = viewer::AreaCreatureLocomotion::turning_left;
                break;
            case nw::toolset::PreviewActorLocomotion::turning_right:
                input.locomotion = viewer::AreaCreatureLocomotion::turning_right;
                break;
            case nw::toolset::PreviewActorLocomotion::idle:
                input.locomotion = viewer::AreaCreatureLocomotion::idle;
                break;
            }
        }
        const auto animation_stats = session->update_area_creature_locomotion_animations(
            transient_animation_inputs);
        if (animation_stats.rejected_input_count != 0) {
            return false;
        }
        apply_preview_camera(session->camera(), camera);
        return true;
    }

    bool end_toolset_preview_visuals() noexcept
    {
        if (transient_preview_objects.empty()) {
            transient_saved_camera.reset();
            return true;
        }

        bool removed = false;
        if (session) {
            const auto result = session->remove_area_transient_visuals(
                transient_preview_objects);
            removed = result.ok();
            if (!removed) {
                LOG_F(ERROR, "Client preview visual teardown: {}", result.diagnostic);
            }
            if (transient_saved_camera) {
                session->camera() = *transient_saved_camera;
            }
        }
        transient_preview_objects.clear();
        transient_animation_inputs.clear();
        transient_saved_camera.reset();
        return removed;
    }

    std::optional<glm::vec3> area_camera_focus() const noexcept
    {
        if (!session || loaded_area_resref.empty()) {
            return std::nullopt;
        }
        return session->camera().get_target();
    }

    bool sync_area_object_spatial(nw::ObjectHandle object)
    {
        const auto* spatial = nw::kernel::objects().components().find_spatial(object);
        return spatial && preview_area_object_spatial(*spatial);
    }

    bool rebuild_live_area(nw::ObjectHandle area, nw::ObjectHandle selected_object)
    {
        return session && session->rebuild_live_area(area, selected_object);
    }

    bool rebuild_live_object(nw::ObjectHandle object)
    {
        return session && session->rebuild_live_object(object);
    }

    bool refresh_live_object_visual(nw::ObjectHandle object)
    {
        return session && session->refresh_live_object_visual(object);
    }

    bool clear_area_object_selection() noexcept
    {
        return session && session->clear_area_object_selection();
    }

    bool zoom(float wheel_delta, ClientViewportRect viewport)
    {
        if (!session || wheel_delta == 0.0f || !viewport.valid()) {
            return false;
        }

        update_camera_viewport(viewport);
        session->camera().move_forward(wheel_delta > 0.0f ? 4.0f : -4.0f, true);
        return true;
    }

    bool camera_command(ClientViewportCameraCommand command, float scale, ClientViewportRect viewport)
    {
        if (!session || !viewport.valid()) {
            return false;
        }

        update_camera_viewport(viewport);
        auto& camera = session->camera();
        const float move_speed = 4.0f * scale;
        const float rotate_speed = 5.0f * scale;
        switch (command) {
        case ClientViewportCameraCommand::move_forward:
            camera.move_forward(move_speed, true);
            break;
        case ClientViewportCameraCommand::move_backward:
            camera.move_forward(-move_speed, true);
            break;
        case ClientViewportCameraCommand::move_left:
            camera.move_right(-move_speed);
            break;
        case ClientViewportCameraCommand::move_right:
            camera.move_right(move_speed);
            break;
        case ClientViewportCameraCommand::move_up:
            camera.move_up(move_speed);
            break;
        case ClientViewportCameraCommand::move_down:
            camera.move_up(-move_speed);
            break;
        case ClientViewportCameraCommand::yaw_left:
            camera.yaw(-rotate_speed);
            break;
        case ClientViewportCameraCommand::yaw_right:
            camera.yaw(rotate_speed);
            break;
        case ClientViewportCameraCommand::pitch_up:
            camera.pitch(rotate_speed);
            break;
        case ClientViewportCameraCommand::pitch_down:
            camera.pitch(-rotate_speed);
            break;
        case ClientViewportCameraCommand::zoom_in:
            camera.zoom(1.0f / (1.0f + 0.12f * scale));
            break;
        case ClientViewportCameraCommand::zoom_out:
            camera.zoom(1.0f + 0.12f * scale);
            break;
        case ClientViewportCameraCommand::fit:
            fit_camera_to_scene(viewport);
            break;
        case ClientViewportCameraCommand::gameplay:
            return set_area_gameplay_view(viewport);
        }
        return true;
    }

    const viewer::ViewerFrameStats* last_frame_stats() const noexcept
    {
        return session ? &session->last_frame_stats() : nullptr;
    }

    nw::ObjectHandle active_object() const noexcept
    {
        return session ? session->active_object() : nw::ObjectHandle{};
    }

    nw::ObjectHandle area_object() const noexcept
    {
        const auto* scene = session ? session->scene() : nullptr;
        return scene && scene->is_area ? scene->root_object : nw::ObjectHandle{};
    }

    void set_area_lights_enabled(bool enabled) noexcept
    {
        area_options.lights_enabled = enabled;
        apply_area_options_to_session();
    }

    bool area_lights_enabled_state() const noexcept
    {
        return area_options.lights_enabled;
    }

    void set_area_options(const ClientAreaViewerOptions& options) noexcept
    {
        area_options = options;
        apply_area_options_to_session();
    }

    ClientAreaViewerOptions area_options_state() const noexcept
    {
        return area_options;
    }

    void apply_area_options_to_session() noexcept
    {
        if (!session) {
            return;
        }

        session->set_area_lights_enabled(area_options.lights_enabled);
        session->set_area_debug_enabled(area_options.debug_enabled);
        session->set_area_triggers_enabled(area_options.triggers_enabled);
        session->set_area_encounters_enabled(area_options.encounters_enabled);
        nw::render::viewer::ForwardPlusRenderPolicy forward_plus_policy = session->forward_plus_policy();
        forward_plus_policy.enabled = area_options.forward_plus_enabled;
        forward_plus_policy.auto_configure_area = area_options.forward_plus_auto_configure_area;
        forward_plus_policy.config = nw::render::viewer::ForwardPlusConfig{
            .tile_size = area_options.forward_plus_tile_size,
            .depth_slices = area_options.forward_plus_depth_slices,
            .max_lights_per_cluster = area_options.forward_plus_max_lights_per_cluster,
        };
        forward_plus_policy.debug_mode = area_options.forward_plus_debug_mode;
        session->set_forward_plus_policy(forward_plus_policy);
        session->set_authored_area_fog_enabled(area_options.fog_enabled);
        session->set_area_shadows_enabled(area_options.shadows_enabled);
        session->set_area_day_night_autoplay(area_options.day_night_autoplay);
        if (applied_area_time_generation != area_options.day_night_time_generation) {
            session->set_area_day_night_elapsed_seconds(area_options.day_night_elapsed_seconds, true);
            applied_area_time_generation = area_options.day_night_time_generation;
        }
    }

    void apply_area_reload_request()
    {
        if (applied_area_reload_generation == area_options.reload_generation) {
            return;
        }

        applied_area_reload_generation = area_options.reload_generation;
        if (!session) {
            return;
        }

        discard_scene();
        clear_load_failures();
        applied_area_time_generation = 0;
    }

    void draw(nw::gfx::CommandList* command_list, ClientViewportRect viewport, int32_t dt_ms)
    {
        if (!session) {
            return;
        }

        session->tick(dt_ms);
        session->render(command_list, to_viewer_viewport(viewport));
    }

    nw::gfx::Context* context = nullptr;
    std::filesystem::path mounted_project;
    uint64_t mounted_module_generation = 0;
    std::unique_ptr<viewer::ViewerDevice> device;
    std::unique_ptr<viewer::ViewerSession> session;
    std::string loaded_area_resref;
    std::string loaded_preview_resource;
    std::string failed_area_resource;
    std::string failed_area_resref;
    std::optional<PreviewLoadFailure> failed_preview;
    ClientViewerCameraStates camera_states;
    std::optional<viewer::Camera> transient_saved_camera;
    std::vector<nw::ObjectHandle> transient_preview_objects;
    std::vector<viewer::AreaCreatureLocomotionAnimationInput> transient_animation_inputs;
    ClientAreaViewerOptions area_options;
    uint64_t applied_area_time_generation = 0;
    uint64_t applied_area_reload_generation = 0;
};

ClientViewerViewport::ClientViewerViewport(nw::gfx::Context* context)
    : impl_(std::make_unique<Impl>(context))
{
}

ClientViewerViewport::~ClientViewerViewport() = default;

bool ClientViewerViewport::render(nw::gfx::CommandList* command_list,
    const std::filesystem::path& project_dir,
    uint64_t module_generation,
    std::string_view area_resource,
    ClientViewportRect viewport,
    int32_t dt_ms)
{
    return impl_
        && impl_->render(
            command_list, project_dir, module_generation, area_resource, viewport, dt_ms);
}

bool ClientViewerViewport::render_preview(nw::gfx::CommandList* command_list,
    const std::filesystem::path& project_dir,
    uint64_t module_generation,
    std::string_view resource_path,
    ClientViewportRect viewport,
    int32_t dt_ms)
{
    return impl_
        && impl_->render_preview(
            command_list, project_dir, module_generation, resource_path, viewport, dt_ms);
}

bool ClientViewerViewport::prepare_preview(const std::filesystem::path& project_dir,
    uint64_t module_generation,
    std::string_view resource_path)
{
    return impl_
        && impl_->prepare_preview(project_dir, module_generation, resource_path);
}

void ClientViewerViewport::clear()
{
    if (impl_) {
        impl_->clear();
    }
}

void ClientViewerViewport::shutdown()
{
    if (impl_) {
        impl_->shutdown();
    }
}

bool ClientViewerViewport::drag(ClientViewportDragMode mode, float delta_x, float delta_y, ClientViewportRect viewport)
{
    return impl_ && impl_->drag(mode, delta_x, delta_y, viewport);
}

bool ClientViewerViewport::select_area_object(
    float pixel_x,
    float pixel_y,
    ClientViewportRect viewport,
    ClientAreaSelectionTarget target)
{
    return impl_ && impl_->select_area_object(pixel_x, pixel_y, viewport, target);
}

bool ClientViewerViewport::set_area_object_selection(nw::ObjectHandle object) noexcept
{
    return impl_ && impl_->set_area_object_selection(object);
}

bool ClientViewerViewport::focus_area_object_selection() noexcept
{
    return impl_ && impl_->focus_area_object_selection();
}

std::optional<ClientViewportRay> ClientViewerViewport::viewport_ray(
    float pixel_x, float pixel_y, ClientViewportRect viewport)
{
    return impl_ ? impl_->viewport_ray(pixel_x, pixel_y, viewport) : std::nullopt;
}

std::optional<glm::vec3> ClientViewerViewport::area_surface_point(
    float pixel_x, float pixel_y, ClientViewportRect viewport)
{
    return impl_ ? impl_->area_surface_point(pixel_x, pixel_y, viewport) : std::nullopt;
}

bool ClientViewerViewport::preview_area_object_spatial(const nw::ObjectSpatialState& spatial)
{
    return impl_ && impl_->preview_area_object_spatial(spatial);
}

bool ClientViewerViewport::append_area_object_previews(
    std::span<const nw::ObjectHandle> objects, float opacity)
{
    return impl_ && impl_->append_area_object_previews(objects, opacity);
}

bool ClientViewerViewport::begin_toolset_preview_visuals(
    std::span<const nw::ObjectHandle> objects,
    const nw::toolset::PreviewCameraState& camera)
{
    return impl_ && impl_->begin_toolset_preview_visuals(objects, camera);
}

bool ClientViewerViewport::update_toolset_preview_visuals(
    std::span<const nw::ObjectSpatialState> spatial_rows,
    std::span<const nw::toolset::PreviewActorLocomotion> locomotion_rows,
    const nw::toolset::PreviewCameraState& camera)
{
    return impl_
        && impl_->update_toolset_preview_visuals(spatial_rows, locomotion_rows, camera);
}

bool ClientViewerViewport::end_toolset_preview_visuals() noexcept
{
    return impl_ && impl_->end_toolset_preview_visuals();
}

std::optional<glm::vec3> ClientViewerViewport::area_camera_focus() const noexcept
{
    return impl_ ? impl_->area_camera_focus() : std::nullopt;
}

bool ClientViewerViewport::sync_area_object_spatial(nw::ObjectHandle object)
{
    return impl_ && impl_->sync_area_object_spatial(object);
}

bool ClientViewerViewport::rebuild_live_area(
    nw::ObjectHandle area, nw::ObjectHandle selected_object)
{
    return impl_ && impl_->rebuild_live_area(area, selected_object);
}

bool ClientViewerViewport::rebuild_live_object(nw::ObjectHandle object)
{
    return impl_ && impl_->rebuild_live_object(object);
}

bool ClientViewerViewport::refresh_live_object_visual(nw::ObjectHandle object)
{
    return impl_ && impl_->refresh_live_object_visual(object);
}

bool ClientViewerViewport::clear_area_object_selection() noexcept
{
    return impl_ && impl_->clear_area_object_selection();
}

bool ClientViewerViewport::zoom(float wheel_delta, ClientViewportRect viewport)
{
    return impl_ && impl_->zoom(wheel_delta, viewport);
}

bool ClientViewerViewport::camera_command(ClientViewportCameraCommand command, float scale, ClientViewportRect viewport)
{
    return impl_ && impl_->camera_command(command, scale, viewport);
}

void ClientViewerViewport::set_area_options(const ClientAreaViewerOptions& options) noexcept
{
    if (impl_) {
        impl_->set_area_options(options);
    }
}

ClientAreaViewerOptions ClientViewerViewport::area_options() const noexcept
{
    return impl_ ? impl_->area_options_state() : ClientAreaViewerOptions{};
}

void ClientViewerViewport::set_area_lights_enabled(bool enabled) noexcept
{
    if (impl_) {
        impl_->set_area_lights_enabled(enabled);
    }
}

bool ClientViewerViewport::area_lights_enabled() const noexcept
{
    return impl_ ? impl_->area_lights_enabled_state() : true;
}

const nw::render::viewer::ViewerFrameStats* ClientViewerViewport::last_frame_stats() const noexcept
{
    return impl_ ? impl_->last_frame_stats() : nullptr;
}

nw::ObjectHandle ClientViewerViewport::active_object() const noexcept
{
    return impl_->active_object();
}

nw::ObjectHandle ClientViewerViewport::area_object() const noexcept
{
    return impl_->area_object();
}
