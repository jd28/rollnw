#pragma once

#include <string>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>


constexpr glm::vec3 VEC3_ZERO{ 0.0f, 0.0f, 0.0f };
constexpr glm::vec3 VEC3_ONES{ 1.0f, 1.0f, 1.0f };

constexpr glm::vec3 VEC3_X{ 1.0f, 0.0f, 0.0f };
constexpr glm::vec3 VEC3_Y{ 0.0f, 1.0f, 0.0f };
constexpr glm::vec3 VEC3_Z{ 0.0f, 0.0f, 1.0f };

constexpr glm::quat QUAT_IDENTITY{ 1.0f, 0.0f, 0.0f, 0.0f };

constexpr glm::mat4 MAT4_IDENTITY{ 1.0f };


struct Transform {
	glm::vec3 translation = VEC3_ZERO;
	glm::quat rotation = QUAT_IDENTITY;
	glm::vec3 scale = VEC3_ONES;

	Transform();
	Transform(glm::vec3 translation_, glm::quat rotation_, glm::vec3 scale_);
};

std::string to_string(const Transform& transform);

glm::vec3 transform_point(glm::mat4 matrix, glm::vec3 point);
glm::vec3 transform_vector(glm::mat4 matrix, glm::vec3 vector);

glm::vec3 transform_point(const Transform& transform, glm::vec3 point);
glm::vec3 transform_vector(const Transform& transform, glm::vec3 vector);

glm::vec3 project_point(glm::mat4 matrix, glm::vec3 point);

Transform mat4_to_transform(glm::mat4 matrix);
glm::mat4 transform_to_mat4(const Transform& transform);

Transform transform_inverse(const Transform& transform);
Transform transform_inverse_nonuniform(const Transform& transform);

Transform operator*(const Transform& transform1, const Transform& transform2);

struct RigidTransform {
	glm::vec3 translation = VEC3_ZERO;
	glm::quat rotation = QUAT_IDENTITY;
};

RigidTransform mat4_to_rigid_transform(glm::mat4 matrix);
glm::mat4 rigid_transform_to_mat4(const RigidTransform& rigid_transform);

Transform rigid_transform_to_transform(const RigidTransform& rigid_transform);
RigidTransform transform_to_rigid_transform(const Transform& transform);

glm::vec3 quat_axis_x(glm::quat q);
glm::vec3 quat_axis_y(glm::quat q);
glm::vec3 quat_axis_z(glm::quat q);

glm::vec3 quat_transform_vector(glm::quat q, glm::vec3 vector);
glm::vec3 quat_transform_point(glm::quat q, glm::vec3 point);

glm::vec3 quat_to_euler_angles(glm::quat q);
glm::quat euler_angles_to_quat(glm::vec3 pitch_yaw_roll);
glm::quat from_to_rotation_to_quat(glm::vec3 va, glm::vec3 vb);
glm::quat look_rotation_to_quat(glm::vec3 forward, glm::vec3 up);

struct Ray {
	glm::vec3 position{ VEC3_ZERO };
	glm::vec3 direction{ VEC3_Z };

	Ray();
	Ray(glm::vec3 position_, glm::vec3 direction_);
};

