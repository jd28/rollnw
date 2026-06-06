#include "session.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/log.hpp>

#include <algorithm>
#include <cmath>

namespace nw::render::viewer {

namespace {

bool load_default_scene_animation(PreviewScene& scene)
{
    if (scene.models.empty()) {
        return false;
    }

    bool loaded_animation = false;
    auto try_animation = [&](std::string_view animation) {
        if (!loaded_animation && !animation.empty()) {
            loaded_animation = scene.load_animation(animation);
        }
    };

    if (const auto* mdl = scene.models.front()->mdl_) {
        try_animation(preferred_model_animation_name(*mdl, PreferredModelAnimationContext::hold));
    }
    try_animation("default");
    try_animation("on");
    try_animation("cast01");
    try_animation("impact");
    try_animation("pause1");
    try_animation("cpause1");
    return loaded_animation;
}

size_t live_particle_count(const PreviewScene& scene)
{
    size_t result = 0;
    for (const auto& scene_particles : scene.particles) {
        result += scene_particles.system.particles.core.position.size();
    }
    return result;
}

void bootstrap_scene_playback(PreviewScene& scene)
{
    const bool loaded_animation = load_default_scene_animation(scene);
    if (loaded_animation) {
        scene.update(33);
    } else {
        scene.rebuild_particles();
    }

    if (!scene.particles.empty()) {
        const int32_t particle_prime_ms = compute_particle_prime_ms(scene, loaded_animation);
        scene.update(particle_prime_ms);
        LOG_F(INFO, "Viewer session: primed {} particle systems ({} live particles, {} ms)",
            scene.particles.size(),
            live_particle_count(scene),
            particle_prime_ms);
    }
}

} // namespace

ViewerSession::ViewerSession(Renderer& renderer)
    : renderer_(&renderer)
{
}

ViewerSession::~ViewerSession() = default;

bool ViewerSession::load_model(std::string_view resref)
{
    if (!renderer_) {
        return false;
    }

    auto scene = load_preview_scene(*renderer_, resref);
    if (!scene) {
        LOG_F(ERROR, "Viewer session: failed to load model '{}'", resref);
        return false;
    }

    if (!set_scene(std::move(scene), ViewerSceneKind::model, std::string{resref})) {
        return false;
    }

    if (scene_) {
        bootstrap_scene_playback(*scene_);
    }

    return true;
}

bool ViewerSession::load_area(std::string_view resref)
{
    if (!renderer_) {
        return false;
    }

    auto scene = load_area_scene(*renderer_, resref);
    if (!scene) {
        LOG_F(ERROR, "Viewer session: failed to load area '{}'", resref);
        return false;
    }
    return set_scene(std::move(scene), ViewerSceneKind::area, std::string{resref});
}

bool ViewerSession::load_object_file(const std::filesystem::path& path)
{
    if (!renderer_) {
        return false;
    }

    auto scene = load_preview_scene(*renderer_, path.string());
    if (!scene) {
        LOG_F(ERROR, "Viewer session: failed to load object file '{}'", path.string());
        return false;
    }
    if (!set_scene(std::move(scene), ViewerSceneKind::object_file, path.string())) {
        return false;
    }
    if (scene_) {
        bootstrap_scene_playback(*scene_);
    }
    return true;
}

void ViewerSession::clear()
{
    scene_.reset();
    scene_kind_ = ViewerSceneKind::none;
    loaded_source_.clear();
}

void ViewerSession::tick(int32_t dt_ms)
{
    if (dt_ms < 0) {
        dt_ms = 0;
    }

    camera_.update(static_cast<float>(dt_ms) * 0.001f);
    if (playing_ && scene_) {
        scene_->update(dt_ms);
    }
}

void ViewerSession::render(nw::gfx::CommandList* command_list, ViewerViewport viewport)
{
    if (!renderer_ || !scene_ || !command_list || !viewport.valid()) {
        return;
    }

    update_viewport(viewport);
    const auto render_context = make_render_context();

    nw::gfx::cmd_begin_render(command_list, {}, nw::gfx::RenderLoadOp::load);
    nw::gfx::cmd_set_viewport(command_list,
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height),
        0.0f,
        1.0f);
    nw::gfx::cmd_set_scissor(command_list, viewport.x, viewport.y, viewport.width, viewport.height);

    for (const auto& model : scene_->models) {
        if (model) {
            renderer_->render_model(command_list, *model, render_context);
        }
    }
    for (const auto& model : scene_->static_models) {
        if (model) {
            renderer_->render_static_model(command_list, *model, render_context);
        }
    }
    renderer_->render_particles(command_list, *scene_, render_context);

    nw::gfx::cmd_end_render(command_list);
}

bool ViewerSession::select_animation(std::string_view animation)
{
    return scene_ && scene_->load_animation(animation);
}

bool ViewerSession::fit_to_scene(ViewerViewport viewport)
{
    if (!scene_ || !viewport.valid()) {
        return false;
    }
    viewport_ = viewport;
    fit_loaded_scene();
    return true;
}

void ViewerSession::update_viewport(ViewerViewport viewport)
{
    if (!scene_ || !viewport.valid()) {
        return;
    }

    viewport_ = viewport;
    const float aspect = static_cast<float>(std::max(1u, viewport.width))
        / static_cast<float>(std::max(1u, viewport.height));
    const auto bounds = scene_->current_bounds();
    const float radius = std::max(bounds.radius(), 1.0f);
    camera_.set_aspect_ratio(aspect);
    camera_.set_near_far(0.1f, std::max(1000.0f, radius * 8.0f));
}

bool ViewerSession::set_scene(std::unique_ptr<PreviewScene> scene, ViewerSceneKind kind, std::string source)
{
    if (!scene) {
        return false;
    }

    scene_ = std::move(scene);
    scene_kind_ = kind;
    loaded_source_ = std::move(source);
    fit_loaded_scene();
    return true;
}

void ViewerSession::fit_loaded_scene()
{
    if (!scene_) {
        return;
    }

    if (viewport_.valid()) {
        update_viewport(viewport_);
    }
    const auto bounds = scene_->current_bounds();
    camera_.fit_to_bounds(bounds);
    if (scene_kind_ == ViewerSceneKind::area) {
        camera_.set_free_view(camera_.get_position(), camera_.get_target(), Camera::ProjectionMode::perspective);
    }
}

nw::render::RenderContext ViewerSession::make_render_context() const
{
    nw::render::RenderContext result{};
    result.view = camera_.get_view_matrix();
    result.projection = camera_.get_projection_matrix();
    result.camera_position = camera_.get_position();
    result.camera_target = camera_.get_target();
    result.camera_near_plane = camera_.near_plane();
    result.camera_far_plane = camera_.far_plane();
    result.orthographic_camera = camera_.is_orthographic();

    if (scene_kind_ == ViewerSceneKind::area) {
        result.lighting_space = nw::render::LightingSpace::world_space;
        result.lighting.ambient = glm::vec3{0.18f, 0.18f, 0.20f};
        result.lighting.key_direction = glm::normalize(glm::vec3{-0.35f, 0.45f, -0.82f});
        result.lighting.key_color = glm::vec3{1.0f, 0.95f, 0.88f};
        result.lighting.key_intensity = 1.2f;
        result.lighting.fill_direction = glm::normalize(glm::vec3{0.75f, -0.2f, -0.45f});
        result.lighting.fill_color = glm::vec3{0.55f, 0.68f, 0.9f};
        result.lighting.fill_intensity = 0.35f;
        result.lighting.rim_direction = glm::normalize(glm::vec3{0.2f, -0.85f, -0.3f});
        result.lighting.rim_color = glm::vec3{0.85f, 0.95f, 1.0f};
        result.lighting.rim_intensity = 0.22f;
    } else {
        result.lighting = studio_preview_lighting();
        result.lighting_space = studio_preview_lighting_space();
    }
    return result;
}

} // namespace nw::render::viewer
