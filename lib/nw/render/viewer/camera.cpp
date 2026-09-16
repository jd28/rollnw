#include "camera.hpp"

#include <algorithm>
#include <cmath>

namespace nw::render::viewer {

namespace {

constexpr float kMinOrthoHalfHeight = 2.0f;
constexpr float kMaxOrthoHalfHeight = 2000.0f;
constexpr float kBoundsFitDistanceScale = 2.5f;
constexpr float kMinBoundsFitDistance = 1.0f;
constexpr float kBoundsFitPitchDegrees = 20.0f;

bool finite_ordered_bounds(const nw::render::Bounds& bounds) noexcept
{
    return std::isfinite(bounds.min.x) && std::isfinite(bounds.min.y) && std::isfinite(bounds.min.z)
        && std::isfinite(bounds.max.x) && std::isfinite(bounds.max.y) && std::isfinite(bounds.max.z)
        && bounds.min.x <= bounds.max.x
        && bounds.min.y <= bounds.max.y
        && bounds.min.z <= bounds.max.z;
}

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
    const auto forward = get_forward();
    auto right = glm::cross(forward, up_);
    const float right_length_squared = glm::dot(right, right);
    if (!std::isfinite(right_length_squared)) {
        return;
    }
    if (right_length_squared <= 1.0e-8f) {
        const auto heading = planar_forward();
        right = {heading.y, -heading.x, 0.0f};
    } else {
        right /= std::sqrt(right_length_squared);
    }
    const auto camera_up = glm::normalize(glm::cross(right, forward));
    const auto delta = right * delta_right + camera_up * delta_up;
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

void Camera::set_area_gameplay_view(const nw::render::Bounds& bounds, float fov_degrees)
{
    const glm::vec3 extents = bounds.max - bounds.min;
    const glm::vec3 center = bounds.center();
    const float planar_extent = std::max(std::max(extents.x, extents.y), 20.0f);
    const float eye_height = std::max(4.0f, std::min(8.0f, std::max(extents.z * 0.15f, 4.0f)));
    const float eye_z = std::max(bounds.min.z + eye_height, center.z + 2.0f);

    const glm::vec3 position{
        bounds.min.x + extents.x * 0.18f,
        bounds.min.y + extents.y * 0.18f,
        eye_z,
    };

    glm::vec2 direction{
        center.x - position.x,
        center.y - position.y,
    };
    if (glm::dot(direction, direction) < 1.0e-5f) {
        direction = {1.0f, 0.25f};
    }
    direction = glm::normalize(direction);

    const float look_distance = std::min(std::max(planar_extent * 0.22f, 35.0f), 90.0f);
    const glm::vec3 target{
        position.x + direction.x * look_distance,
        position.y + direction.y * look_distance,
        eye_z - 0.5f,
    };

    if (fov_degrees > 0.0f) {
        set_fov(fov_degrees);
    }
    set_free_view(position, target, Camera::ProjectionMode::perspective);
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

bool Camera::focus_on(const nw::render::Bounds& bounds) noexcept
{
    if (!finite_ordered_bounds(bounds)) {
        return false;
    }

    const glm::vec3 target = bounds.center();
    if (free_camera_ && top_down_view_) {
        const glm::vec3 delta = target - target_;
        target_ = target;
        position_ += delta;
        const glm::vec3 extents = bounds.max - bounds.min;
        const float half_height = std::max(
            extents.y * 0.5f,
            (extents.x * 0.5f) / std::max(aspect_ratio_, 0.1f));
        ortho_half_height_ = glm::clamp(
            half_height * 1.05f,
            kMinOrthoHalfHeight,
            kMaxOrthoHalfHeight);
        return true;
    }

    glm::vec2 approach{
        position_.x - target.x,
        position_.y - target.y,
    };
    if (glm::dot(approach, approach) <= 1.0e-8f) {
        approach = {-front_.x, -front_.y};
    }
    if (glm::dot(approach, approach) <= 1.0e-8f) {
        approach = {0.0f, 1.0f};
    }
    approach = glm::normalize(approach);

    target_ = target;
    orbit_radius_ = std::max(bounds.radius() * kBoundsFitDistanceScale, kMinBoundsFitDistance);
    orbit_yaw_ = glm::degrees(std::atan2(approach.y, approach.x));
    orbit_pitch_ = kBoundsFitPitchDegrees;
    if (free_camera_) {
        const float pitch = glm::radians(orbit_pitch_);
        const float horizontal_distance = orbit_radius_ * std::cos(pitch);
        position_ = target_ + glm::vec3{
                        approach.x * horizontal_distance,
                        approach.y * horizontal_distance,
                        orbit_radius_ * std::sin(pitch),
                    };
        const glm::vec3 direction = glm::normalize(target_ - position_);
        free_yaw_ = glm::degrees(std::atan2(direction.x, direction.y));
        free_pitch_ = glm::degrees(std::asin(direction.z));
        update_vectors();
        return true;
    }

    sync_position_from_orbit();
    return true;
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

bool Camera::translate_planar(
    float forward_amount, float right_amount) noexcept
{
    if (!std::isfinite(forward_amount) || !std::isfinite(right_amount)) {
        return false;
    }

    const glm::vec3 forward = planar_forward();
    const glm::vec3 right{forward.y, -forward.x, 0.0f};
    const glm::vec3 delta
        = forward * forward_amount + right * right_amount;
    const glm::vec3 position = position_ + delta;
    const glm::vec3 target = target_ + delta;
    if (!std::isfinite(position.x) || !std::isfinite(position.y)
        || !std::isfinite(position.z) || !std::isfinite(target.x)
        || !std::isfinite(target.y) || !std::isfinite(target.z)) {
        return false;
    }

    position_ = position;
    target_ = target;
    return true;
}

bool Camera::orbit_around_target(float delta_yaw_degrees,
    float delta_pitch_degrees,
    float min_pitch_degrees,
    float max_pitch_degrees) noexcept
{
    const glm::vec3 offset = position_ - target_;
    const float radius = glm::length(offset);
    if (!std::isfinite(delta_yaw_degrees)
        || !std::isfinite(delta_pitch_degrees)
        || !std::isfinite(min_pitch_degrees)
        || !std::isfinite(max_pitch_degrees)
        || min_pitch_degrees < -90.0f
        || max_pitch_degrees > 90.0f
        || min_pitch_degrees > max_pitch_degrees
        || !std::isfinite(radius) || radius <= 1.0e-4f) {
        return false;
    }

    const float planar_length_squared
        = offset.x * offset.x + offset.y * offset.y;
    if (!std::isfinite(planar_length_squared)) {
        return false;
    }

    float yaw = 0.0f;
    if (planar_length_squared > 1.0e-8f) {
        yaw = std::atan2(offset.y, offset.x);
    } else {
        const glm::vec3 heading = planar_forward();
        yaw = std::atan2(-heading.y, -heading.x);
    }
    yaw += glm::radians(std::remainder(delta_yaw_degrees, 360.0f));

    const float pitch_degrees = glm::degrees(std::asin(
        std::clamp(offset.z / radius, -1.0f, 1.0f)));
    const float next_pitch_degrees = delta_pitch_degrees == 0.0f
        ? pitch_degrees
        : std::clamp(pitch_degrees + delta_pitch_degrees,
              min_pitch_degrees, max_pitch_degrees);
    const float pitch = glm::radians(next_pitch_degrees);
    const float planar_radius = radius * std::cos(pitch);
    position_ = target_ + glm::vec3{
                    planar_radius * std::cos(yaw),
                    planar_radius * std::sin(yaw),
                    radius * std::sin(pitch),
                };
    orbit_radius_ = radius;
    orbit_yaw_ = glm::degrees(yaw);
    orbit_pitch_ = next_pitch_degrees;

    if (!free_camera_) {
        return true;
    }

    if (next_pitch_degrees < 89.999f) {
        leave_orthographic_overview();
        top_down_view_ = false;
    }
    free_yaw_ = std::remainder(-90.0f - orbit_yaw_, 360.0f);
    free_pitch_ = -next_pitch_degrees;
    update_vectors();
    return true;
}

glm::mat4 Camera::get_view_matrix() const
{
    const auto forward = glm::normalize(target_ - position_);
    const auto view_up = std::abs(glm::dot(forward, up_)) > 0.99f
        ? planar_forward()
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

glm::vec3 Camera::planar_forward() const noexcept
{
    glm::vec3 result = get_forward();
    result.z = 0.0f;
    float length_squared = glm::dot(result, result);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8f) {
        result = free_camera_
            ? glm::vec3{front_.x, front_.y, 0.0f}
            : glm::vec3{-std::cos(glm::radians(orbit_yaw_)),
                  -std::sin(glm::radians(orbit_yaw_)), 0.0f};
        length_squared = glm::dot(result, result);
    }
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return result / std::sqrt(length_squared);
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
    orbit_radius_ = radius * kBoundsFitDistanceScale;
    target_ = bounds.center();
    orbit_yaw_ = 90.0f;
    orbit_pitch_ = kBoundsFitPitchDegrees;

    // Ensure minimum distance
    if (orbit_radius_ < kMinBoundsFitDistance) orbit_radius_ = kMinBoundsFitDistance;
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
