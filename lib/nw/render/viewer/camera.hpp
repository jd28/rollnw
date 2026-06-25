#pragma once

#include <nw/render/model.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace nw::render::viewer {

// Simple orbit camera
class Camera {
public:
    enum class ProjectionMode {
        perspective,
        orthographic,
    };

    Camera();

    void update(float dt);

    // Setters
    void set_position(const glm::vec3& pos);
    void set_target(const glm::vec3& target);
    void set_aspect_ratio(float aspect);
    void set_fov(float fov_degrees);
    void set_near_far(float near_plane, float far_plane);
    void set_projection_mode(ProjectionMode mode);
    void set_free_view(const glm::vec3& position, const glm::vec3& target,
        ProjectionMode mode = ProjectionMode::perspective);
    void set_orbit_view(const glm::vec3& target, float radius, float yaw_degrees, float pitch_degrees,
        ProjectionMode mode = ProjectionMode::perspective);
    void set_orbit_angle(float angle_degrees); // For turntable
    void orbit(float delta_yaw_degrees, float delta_pitch_degrees);
    void pan(float delta_right, float delta_up);
    void zoom(float zoom_factor);
    void set_area_overview(const nw::render::Bounds& bounds);
    void set_area_gameplay_view(const nw::render::Bounds& bounds, float fov_degrees = 65.0f);
    void set_area_overview_xy(const glm::vec2& center, const glm::vec2& size, float base_z = 0.0f);
    void set_area_navigation_target(const glm::vec3& target);
    void move_forward(float amount, bool planar = false);
    void move_right(float amount);
    void move_up(float amount);
    void yaw(float delta_degrees);
    void pitch(float delta_degrees);

    // Getters
    glm::mat4 get_view_matrix() const;
    glm::mat4 get_projection_matrix() const;
    glm::vec3 get_position() const { return position_; }
    glm::vec3 get_target() const { return target_; }
    glm::vec3 get_forward() const;
    float near_plane() const { return near_plane_; }
    float far_plane() const { return far_plane_; }
    float fov_degrees() const { return fov_; }
    ProjectionMode projection_mode() const { return projection_mode_; }
    bool is_orthographic() const { return projection_mode_ == ProjectionMode::orthographic; }
    float pan_units_per_pixel(float viewport_height_pixels) const;

    // Auto-fit to model bounds
    void fit_to_bounds(const nw::render::Bounds& bounds);

private:
    void leave_orthographic_overview();
    void update_vectors();
    void sync_position_from_orbit();

    bool free_camera_ = false;
    bool top_down_view_ = false;

    glm::vec3 position_{0.0f, -5.0f, 2.0f};
    glm::vec3 target_{0.0f, 0.0f, 0.0f};
    glm::vec3 front_{0.0f, 1.0f, 0.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};
    glm::vec3 up_{0.0f, 0.0f, 1.0f};

    float aspect_ratio_ = 16.0f / 9.0f;
    float fov_ = 60.0f;
    float near_plane_ = 0.1f;
    float far_plane_ = 1000.0f;
    ProjectionMode projection_mode_ = ProjectionMode::perspective;
    float ortho_half_height_ = 10.0f;

    // Orbit parameters
    float orbit_radius_ = 5.0f;
    float orbit_yaw_ = 90.0f;
    float orbit_pitch_ = 20.0f;
    float free_yaw_ = 0.0f;
    float free_pitch_ = -89.0f;
};

} // namespace nw::render::viewer
