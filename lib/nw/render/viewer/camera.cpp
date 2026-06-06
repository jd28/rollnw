#include "camera.hpp"

namespace nw::render::viewer {

namespace {

constexpr float kMinOrthoHalfHeight = 2.0f;
constexpr float kMaxOrthoHalfHeight = 2000.0f;

}

Camera::Camera()
{
    update_vectors();
    sync_position_from_orbit();
}

void Camera::update(float /*dt*/)
{
    if (!free_camera_) {
        sync_position_from_orbit();
    }
}

void Camera::set_position(const glm::vec3& pos)
{
    position_ = pos;
    if (free_camera_ && !top_down_view_) {
        target_ = position_ + front_;
    }
}

void Camera::set_target(const glm::vec3& target)
{
    target_ = target;
    if (!free_camera_) {
        sync_position_from_orbit();
    } else if (!top_down_view_) {
        auto direction = glm::normalize(target_ - position_);
        free_yaw_ = glm::degrees(std::atan2(direction.x, direction.y));
        free_pitch_ = glm::degrees(std::asin(direction.z));
        update_vectors();
    }
}

void Camera::set_aspect_ratio(float aspect)
{
    aspect_ratio_ = aspect;
}

void Camera::set_fov(float fov_degrees)
{
    fov_ = fov_degrees;
}

void Camera::set_near_far(float near_plane, float far_plane)
{
    near_plane_ = near_plane;
    far_plane_ = far_plane;
}

void Camera::set_projection_mode(Camera::ProjectionMode mode)
{
    projection_mode_ = mode;
}

void Camera::set_free_view(const glm::vec3& position, const glm::vec3& target, Camera::ProjectionMode mode)
{
    position_ = position;
    target_ = target;
    projection_mode_ = mode;
    free_camera_ = true;
    top_down_view_ = false;

    auto direction = glm::normalize(target_ - position_);
    free_yaw_ = glm::degrees(std::atan2(direction.x, direction.y));
    free_pitch_ = glm::degrees(std::asin(direction.z));
    const glm::vec3 orbit_delta = position_ - target_;
    orbit_radius_ = std::max(glm::length(orbit_delta), 0.25f);
    orbit_yaw_ = glm::degrees(std::atan2(orbit_delta.y, orbit_delta.x));
    orbit_pitch_ = glm::degrees(std::asin(std::clamp(orbit_delta.z / orbit_radius_, -1.0f, 1.0f)));
    update_vectors();
}

void Camera::set_orbit_view(const glm::vec3& target, float radius, float yaw_degrees, float pitch_degrees,
    Camera::ProjectionMode mode)
{
    target_ = target;
    projection_mode_ = mode;
    free_camera_ = false;
    top_down_view_ = false;
    orbit_radius_ = std::max(radius, 0.25f);
    orbit_yaw_ = yaw_degrees;
    orbit_pitch_ = glm::clamp(pitch_degrees, -85.0f, 85.0f);
    sync_position_from_orbit();
}

void Camera::set_orbit_angle(float angle_degrees)
{
    free_camera_ = false;
    orbit_yaw_ = angle_degrees;
    sync_position_from_orbit();
}

void Camera::orbit(float delta_yaw_degrees, float delta_pitch_degrees)
{
    free_camera_ = false;
    orbit_yaw_ += delta_yaw_degrees;
    orbit_pitch_ = glm::clamp(orbit_pitch_ + delta_pitch_degrees, -85.0f, 85.0f);
    sync_position_from_orbit();
}

void Camera::pan(float delta_right, float delta_up)
{
    auto forward = get_forward();
    auto right = glm::normalize(glm::cross(forward, up_));
    auto camera_up = glm::normalize(glm::cross(right, forward));
    auto delta = right * delta_right + camera_up * delta_up;
    target_ += delta;
    position_ += delta;
}

void Camera::zoom(float zoom_factor)
{
    if (projection_mode_ == Camera::ProjectionMode::orthographic) {
        ortho_half_height_ = glm::clamp(ortho_half_height_ * zoom_factor, kMinOrthoHalfHeight, kMaxOrthoHalfHeight);
        return;
    }

    if (free_camera_) {
        move_forward((zoom_factor < 1.0f ? 1.0f : -1.0f) * 4.0f, false);
    } else {
        orbit_radius_ = glm::clamp(orbit_radius_ * zoom_factor, 0.25f, 500.0f);
        sync_position_from_orbit();
    }
}

void Camera::set_area_overview(const nw::render::Bounds& bounds)
{
    free_camera_ = true;
    top_down_view_ = true;
    projection_mode_ = Camera::ProjectionMode::orthographic;
    auto center = bounds.center();
    const glm::vec3 extents = bounds.max - bounds.min;
    const float planar = std::max(std::max(extents.x, extents.y), 20.0f);
    ortho_half_height_ = glm::clamp(planar * 0.5f, kMinOrthoHalfHeight, kMaxOrthoHalfHeight);
    position_ = glm::vec3{center.x, center.y, bounds.max.z + planar * 0.45f};
    target_ = center;
    free_yaw_ = 0.0f;
    free_pitch_ = -89.0f;
    update_vectors();
}

void Camera::set_area_overview_xy(const glm::vec2& center, const glm::vec2& size, float base_z)
{
    free_camera_ = true;
    top_down_view_ = true;
    projection_mode_ = Camera::ProjectionMode::orthographic;
    const float half_height = std::max(size.y * 0.5f, (size.x * 0.5f) / std::max(aspect_ratio_, 0.1f));
    const float distance = std::max(size.x, size.y) * 0.75f;
    ortho_half_height_ = glm::clamp(half_height * 1.05f, kMinOrthoHalfHeight, kMaxOrthoHalfHeight);
    position_ = glm::vec3{center.x, center.y, base_z + distance * 1.35f};
    target_ = glm::vec3{center.x, center.y, base_z};
    free_yaw_ = 0.0f;
    free_pitch_ = -89.0f;
    update_vectors();
}

void Camera::set_area_navigation_target(const glm::vec3& target)
{
    target_ = target;
    leave_orthographic_overview();
    top_down_view_ = false;
    auto direction = glm::normalize(target_ - position_);
    free_yaw_ = glm::degrees(std::atan2(direction.x, direction.y));
    free_pitch_ = glm::degrees(std::asin(direction.z));
    update_vectors();
}

void Camera::move_forward(float amount, bool planar)
{
    if (!free_camera_) {
        zoom(amount < 0.0f ? 1.12f : (1.0f / 1.12f));
        return;
    }

    leave_orthographic_overview();
    top_down_view_ = false;

    glm::vec3 direction = front_;
    if (planar) {
        direction.z = 0.0f;
        if (glm::dot(direction, direction) > 1.0e-8f) {
            direction = glm::normalize(direction);
        }
    }
    position_ += direction * amount;
    target_ = position_ + front_;
}

void Camera::move_right(float amount)
{
    if (!free_camera_) {
        pan(amount, 0.0f);
        return;
    }
    leave_orthographic_overview();
    top_down_view_ = false;
    position_ += right_ * amount;
    target_ = position_ + front_;
}

void Camera::move_up(float amount)
{
    if (!free_camera_) {
        pan(0.0f, amount);
        return;
    }
    leave_orthographic_overview();
    top_down_view_ = false;
    position_ += up_ * amount;
    position_.z = std::max(1.0f, position_.z);
    target_ = position_ + front_;
}

void Camera::yaw(float delta_degrees)
{
    if (!free_camera_) {
        orbit(delta_degrees, 0.0f);
        return;
    }
    leave_orthographic_overview();
    top_down_view_ = false;
    free_yaw_ += delta_degrees;
    if (free_yaw_ < 0.0f) free_yaw_ += 360.0f;
    if (free_yaw_ >= 360.0f) free_yaw_ -= 360.0f;
    update_vectors();
    target_ = position_ + front_;
}

void Camera::pitch(float delta_degrees)
{
    if (!free_camera_) {
        orbit(0.0f, delta_degrees);
        return;
    }
    leave_orthographic_overview();
    top_down_view_ = false;
    free_pitch_ = glm::clamp(free_pitch_ + delta_degrees, -89.0f, 89.0f);
    update_vectors();
    target_ = position_ + front_;
}

glm::mat4 Camera::get_view_matrix() const
{
    const auto forward = glm::normalize(target_ - position_);
    const auto view_up = std::abs(glm::dot(forward, up_)) > 0.99f
        ? glm::vec3{0.0f, 1.0f, 0.0f}
        : up_;
    return glm::lookAt(position_, target_, view_up);
}

glm::mat4 Camera::get_projection_matrix() const
{
    if (projection_mode_ == Camera::ProjectionMode::orthographic) {
        const float half_height = std::max(ortho_half_height_, kMinOrthoHalfHeight);
        const float half_width = half_height * std::max(aspect_ratio_, 0.1f);
        auto proj = glm::orthoRH_ZO(-half_width, half_width, -half_height, half_height, near_plane_, far_plane_);
        proj[1][1] *= -1.0f;
        return proj;
    }

    // perspectiveRH_ZO: right-handed (matches NWN coord system), depth [0,1] (Vulkan range).
    // Flip Y because Vulkan clip-space Y points down, opposite to glm's default.
    auto proj = glm::perspectiveRH_ZO(glm::radians(fov_), aspect_ratio_, near_plane_, far_plane_);
    proj[1][1] *= -1.0f;
    return proj;
}

glm::vec3 Camera::get_forward() const
{
    const auto delta = target_ - position_;
    if (glm::dot(delta, delta) <= 1.0e-8f) {
        return front_;
    }
    return glm::normalize(delta);
}

float Camera::pan_units_per_pixel(float viewport_height_pixels) const
{
    if (projection_mode_ == Camera::ProjectionMode::orthographic) {
        const float pixels = std::max(viewport_height_pixels, 1.0f);
        return (ortho_half_height_ * 2.0f) / pixels;
    }
    return std::max(0.01f, glm::length(position_ - target_) * 0.0015f);
}

void Camera::leave_orthographic_overview()
{
    if (projection_mode_ == Camera::ProjectionMode::orthographic && top_down_view_) {
        projection_mode_ = Camera::ProjectionMode::perspective;
    }
}

void Camera::fit_to_bounds(const nw::render::Bounds& bounds)
{
    free_camera_ = false;
    projection_mode_ = Camera::ProjectionMode::perspective;
    float radius = bounds.radius();
    orbit_radius_ = radius * 2.5f; // Distance for 60° FOV
    target_ = bounds.center();
    orbit_yaw_ = 90.0f;
    orbit_pitch_ = 20.0f;

    // Ensure minimum distance
    if (orbit_radius_ < 1.0f) orbit_radius_ = 1.0f;
    sync_position_from_orbit();
}

void Camera::update_vectors()
{
    free_pitch_ = glm::clamp(free_pitch_, -89.0f, 89.0f);
    glm::vec3 front;
    front.x = std::cos(glm::radians(free_pitch_)) * std::sin(glm::radians(free_yaw_));
    front.y = std::cos(glm::radians(free_pitch_)) * std::cos(glm::radians(free_yaw_));
    front.z = std::sin(glm::radians(free_pitch_));
    front_ = glm::normalize(front);
    right_ = glm::normalize(glm::cross(front_, glm::vec3(0.0f, 0.0f, 1.0f)));
}

void Camera::sync_position_from_orbit()
{
    float yaw_rad = glm::radians(orbit_yaw_);
    float pitch_rad = glm::radians(orbit_pitch_);
    float cos_pitch = cosf(pitch_rad);

    position_.x = target_.x + orbit_radius_ * cos_pitch * cosf(yaw_rad);
    position_.y = target_.y + orbit_radius_ * cos_pitch * sinf(yaw_rad);
    position_.z = target_.z + orbit_radius_ * sinf(pitch_rad);
}

} // namespace nw::render::viewer
