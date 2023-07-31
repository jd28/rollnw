#define _USE_MATH_DEFINES
#include <cmath>

#include "glm_util.h"

#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/mat3x3.hpp>

std::string to_string(const Transform& transform)
{
    std::string s1 = glm::to_string(transform.translation);
    std::string s2 = glm::to_string(transform.rotation);
    std::string s3 = glm::to_string(transform.scale);
    return s1 + " " + s2 + " " + s3;
}

glm::vec3 transform_point(glm::mat4 matrix, glm::vec3 point)
{
    glm::vec4 v = matrix * glm::vec4(point, 1.0f);
    return glm::vec3(v);
}

glm::vec3 transform_vector(glm::mat4 matrix, glm::vec3 vector)
{
    glm::vec4 v = matrix * glm::vec4(vector, 0.0f);
    return glm::vec3(v);
}

glm::vec3 transform_point(const Transform& transform, glm::vec3 point)
{
    return transform_point(transform_to_mat4(transform), point);
}

glm::vec3 transform_vector(const Transform& transform, glm::vec3 vector)
{
    return transform_vector(transform_to_mat4(transform), vector);
}

glm::vec3 project_point(glm::mat4 matrix, glm::vec3 point)
{
    glm::vec4 v = matrix * glm::vec4(point, 1.0f);
    glm::vec3 projected(v);
    return projected / v.w;
}

Transform mat4_to_transform(glm::mat4 matrix)
{
    glm::vec3 axis_x = matrix[0];
    glm::vec3 axis_y = matrix[1];
    glm::vec3 axis_z = matrix[2];
    glm::vec3 translation = matrix[3];
    glm::vec3 scale(glm::length(axis_x), glm::length(axis_y), glm::length(axis_z));
    axis_x = glm::normalize(axis_x);
    axis_y = glm::normalize(axis_y);
    axis_z = glm::normalize(axis_z);
    glm::mat3 rotation_matrix(axis_x, axis_y, axis_z);
    glm::quat rotation = glm::quat_cast(rotation_matrix);
    return Transform{
        translation,
        rotation,
        scale};
}

glm::mat4 transform_to_mat4(const Transform& transform)
{
    glm::mat4 scale_mat = glm::scale(glm::mat4(1.0f), transform.scale);
    glm::mat4 rotation_mat = glm::mat4_cast(transform.rotation);
    glm::mat4 translation_mat = glm::translate(glm::mat4(1.0f), transform.translation);
    return translation_mat * rotation_mat * scale_mat;
}

Transform transform_inverse(const Transform& transform)
{
    // uniform scale only!
    glm::vec3 new_scale = 1.0f / transform.scale;
    glm::quat new_rotation = glm::inverse(transform.rotation);
    // (RS)_inv * (-t) == S_inv * R_inv * (-t)
    glm::mat3 rs_inv = glm::mat3(glm::scale(glm::mat4(1.0f), new_scale)) * glm::mat3_cast(new_rotation);
    glm::vec3 new_translation = rs_inv * (-transform.translation);

    return Transform{
        new_translation,
        new_rotation,
        new_scale};
}

Transform transform_inverse_nonuniform(const Transform& transform)
{
    glm::mat4 mat4_inversed = glm::inverse(transform_to_mat4(transform));
    return mat4_to_transform(mat4_inversed);
}

Transform operator*(const Transform& transform1, const Transform& transform2)
{
    // TODO no need to convert to mat4
    return mat4_to_transform(transform_to_mat4(transform1) * transform_to_mat4(transform2));
}

RigidTransform mat4_to_rigid_transform(glm::mat4 matrix)
{
    Transform transform = mat4_to_transform(matrix);
    return RigidTransform{
        transform.translation,
        transform.rotation};
}

glm::mat4 rigid_transform_to_mat4(const RigidTransform& rigid_transform)
{
    glm::mat4 rotation_mat = glm::mat4_cast(rigid_transform.rotation);
    glm::mat4 translation_mat = glm::translate(glm::mat4(1.0f), rigid_transform.translation);
    return translation_mat * rotation_mat;
}

Transform rigid_transform_to_transform(const RigidTransform& rigid_transform)
{
    return Transform{
        rigid_transform.translation,
        rigid_transform.rotation,
        VEC3_ONES,
    };
}

RigidTransform transform_to_rigid_transform(const Transform& transform)
{
    return RigidTransform{
        transform.translation,
        transform.rotation};
}

glm::vec3 quat_axis_x(glm::quat q)
{
    return glm::mat3_cast(q)[0];
}

glm::vec3 quat_axis_y(glm::quat q)
{
    return glm::mat3_cast(q)[1];
}

glm::vec3 quat_axis_z(glm::quat q)
{
    return glm::mat3_cast(q)[2];
}

glm::vec3 quat_transform_vector(glm::quat q, glm::vec3 vector)
{
    return glm::mat3_cast(q) * vector;
}

glm::vec3 quat_transform_point(glm::quat q, glm::vec3 point)
{
    return glm::mat3_cast(q) * point;
}

glm::vec3 quat_to_euler_angles(glm::quat q)
{
    // (pitch, yaw, roll), euler order y-x-z
    // swap ZYX <->YXZ from Wikipedia Conversion_between_quaternions_and_Euler_angles
    float w = q.w;
    float x = q.z;
    float y = q.x;
    float z = q.y;
    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float sinr_cosp = 2.0f * (w * x + y * z);
    float cosr_cosp = 1.0f - 2.0f * (xx + yy);
    float roll = std::atan2(sinr_cosp, cosr_cosp);
    float sinp = 2.0f * (w * y - z * x);
    float pitch;
    if (std::abs(sinp) >= 1.0f) {
        pitch = std::copysign(static_cast<float>(M_PI) / 2.0f, sinp);
    } else {
        pitch = std::asin(sinp);
    }
    float siny_cosp = 2.0f * (w * z + x * y);
    float cosy_cosp = 1.0f - 2.0f * (yy + zz);
    float yaw = std::atan2(siny_cosp, cosy_cosp);
    return glm::vec3(pitch, yaw, roll);
}

glm::quat euler_angles_to_quat(glm::vec3 pitch_yaw_roll)
{
    ;
    // (pitch, yaw, roll), euler order y-x-z
    float hpitch = pitch_yaw_roll.x * 0.5f;
    float hyaw = pitch_yaw_roll.y * 0.5f;
    float hroll = pitch_yaw_roll.z * 0.5f;
    float cy = std::cos(hyaw);
    float sy = std::sin(hyaw);
    float cp = std::cos(hpitch);
    float sp = std::sin(hpitch);
    float cr = std::cos(hroll);
    float sr = std::sin(hroll);
    // swap ZYX < ->YXZ from Wikipedia Conversion_between_quaternions_and_Euler_angles
    float w = cy * cp * cr + sy * sp * sr;
    float x = cy * cp * sr - sy * sp * cr;
    float y = sy * cp * sr + cy * sp * cr;
    float z = sy * cp * cr - cy * sp * sr;

    // same as glm::quat_cast(glm::eulerAngleYXZ(pitch_yaw_roll.y, pitch_yaw_roll.x, pitch_yaw_roll.z));
    return glm::quat(w, y, z, x);
}

glm::quat from_to_rotation_to_quat(glm::vec3 va, glm::vec3 vb)
{
    return glm::quat(va, vb);
}

glm::quat look_rotation_to_quat(glm::vec3 forward, glm::vec3 up)
{
    return glm::quatLookAt(forward, up);
}

Transform::Transform()
{
}

Transform::Transform(glm::vec3 translation_, glm::quat rotation_, glm::vec3 scale_)
    : translation(translation_)
    , rotation(rotation_)
    , scale(scale_)
{
}

Ray::Ray()
{
}

Ray::Ray(glm::vec3 position_, glm::vec3 direction_)
    : position(position_)
    , direction(direction_)
{
}
