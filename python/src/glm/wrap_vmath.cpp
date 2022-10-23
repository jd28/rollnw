// #define GLM_FORCE_DEPTH_ZERO_TO_ONE


#include "wrap_vmath.h"

#include <pybind11/stl.h>
namespace py = pybind11;

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include "glm_util.h"


void wrap_Vector3(py::module_& m) {
	py::class_<glm::vec3> t(m, "Vector3");

	t.def(py::init([]() {
		return glm::vec3(0.0f, 0.0f, 0.0f);
	}));
	t.def(py::init<float, float, float>());

	t.def_readwrite("x", &glm::vec3::x);
	t.def_readwrite("y", &glm::vec3::y);
	t.def_readwrite("z", &glm::vec3::z);
	
	t.def("length", [](glm::vec3& self) {
		return glm::length(self);
	});
	t.def("length_squared", [](glm::vec3& self) {
		return self.x * self.x + self.y * self.y + self.z * self.z;
	});
	t.def("dot", [](glm::vec3& self, glm::vec3& other) {
		return glm::dot(self, other);
	});
	t.def("cross", [](glm::vec3& self, glm::vec3& other) {
		return glm::cross(self, other);
	});
	t.def("normalize_self", [](glm::vec3& self) {
		self = glm::normalize(self);
	});
	t.def("normalize", [](glm::vec3& self) {
		glm::vec3 res = glm::normalize(self);
		return res;
	});
	
	t.def("__eq__", [](glm::vec3& self, glm::vec3& other) {
		return self == other;
	});
	t.def("__add__", [](glm::vec3& self, glm::vec3& other) {
		return self + other;
	});
	t.def("__iadd__", [](glm::vec3& self, glm::vec3& other) {
		self += other;
		return self;
	});
	t.def("__mul__", [](glm::vec3& self, float n) {
		return self * n;
	});
	t.def("__imul__", [](glm::vec3& self, float n) {
		self *= n;
		return self;
	});
	t.def("__rmul__", [](glm::vec3& self, float n) {
		return self * n;
	});
	t.def("__sub__", [](glm::vec3& self, glm::vec3& other) {
		return self - other;
	});
	t.def("__isub__", [](glm::vec3& self, glm::vec3& other) {
		self -= other;
		return self;
	});
	t.def("__neg__", [](glm::vec3& self) {
		return -self;
	});

	t.def("__copy__", [](glm::vec3 self) {
		return self;
	});
	t.def("copy", [](glm::vec3 self) {
		return self;
	});
	t.def("__repr__", [](glm::vec3& self) {
		return glm::to_string(self);
	});
}


void wrap_Quaternion(py::module_& m) {
	py::class_<glm::quat> t(m, "Quaternion");

	t.def(py::init([]() {
		return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	}));
	t.def(py::init<float, float, float, float>());

	t.def_readwrite("w", &glm::quat::w);
	t.def_readwrite("x", &glm::quat::x);
	t.def_readwrite("y", &glm::quat::y);
	t.def_readwrite("z", &glm::quat::z);

	t.def("length", [](glm::quat& self) {
		return glm::length(self);
	});
	t.def("length_squared", [](glm::quat& self) {
		return self.w * self.w + self.x * self.x + self.y * self.y + self.z * self.z;
	});

	t.def("normalize_self", [](glm::quat& self) {
		self = glm::normalize(self);
	});
	t.def("normalize", [](glm::quat& self) {
		return glm::normalize(self);
	});

	t.def("slerp", [](glm::quat& self, glm::quat& other, float alpha) {
		return glm::slerp(self, other, alpha);
	});
	t.def("conjugate", [](glm::quat& self) {
		return glm::conjugate(self);
	});
	t.def("inverse", [](glm::quat& self) {
		return glm::inverse(self);
	});
	t.def("angle_axis", [](glm::quat& self) {
		float angle = glm::angle(self);
		glm::vec3 axis = glm::axis(self);
		return std::make_pair(angle, axis);
	});
	t.def("transform_point", [](glm::quat& self, glm::vec3 point) {
		return quat_transform_point(self, point);
	});
	t.def("transform_vector", [](glm::quat& self, glm::vec3 vector) {
		return quat_transform_vector(self, vector);
	});
	t.def("to_matrix3", [](glm::quat& self) {
		glm::mat3 matrix3 = glm::mat3_cast(self);
		return std::vector<glm::vec3>{ matrix3[0], matrix3[1], matrix3[2] };
	});
	t.def("to_matrix4", [](glm::quat& self) {
		return glm::mat4_cast(self);
	});
	t.def_static("from_angle_axis", [](float angle, glm::vec3 axis) {
		return glm::angleAxis(angle, axis);
	});
	t.def_static("from_matrix3", [](std::vector<glm::vec3>& matrix3) {
		glm::mat3 matrix3_(matrix3[0], matrix3[1], matrix3[2]);
		return glm::quat_cast(matrix3_);
	});
	t.def("euler_angles", &quat_to_euler_angles);
	t.def_static("from_euler_angles", &euler_angles_to_quat);
	t.def_static("from_from_to_rotation", &from_to_rotation_to_quat);
	t.def_static("from_look_rotation", &look_rotation_to_quat);

	t.def("__mul__", [](glm::quat& self, glm::quat& other) {
		return self * other;
	});
	t.def("__imul__", [](glm::quat& self, glm::quat& other) {
		self *= other;
		return self;
	});

	t.def("__copy__", [](glm::quat self) {
		return self;
	});
	t.def("copy", [](glm::quat self) {
		return self;
	});
	t.def("__repr__", [](glm::quat& self) {
		return glm::to_string(self);
	});
}


void wrap_Matrix4(py::module_& m_) {
	py::class_<glm::mat4> t(m_, "Matrix4");

	t.def(py::init([]() {
		return glm::mat4(1.0f);
	}));

	t.def(py::init([](
			float m00, float m01, float m02, float m03,
			float m10, float m11, float m12, float m13,
			float m20, float m21, float m22, float m23,
			float m30, float m31, float m32, float m33) {
		return glm::mat4(
			m00, m01, m02, m03,
			m10, m11, m12, m13,
			m20, m21, m22, m23,
			m30, m31, m32, m33
		);
	}));

	t.def_property("m00",
		[](glm::mat4& m) { return m[0][0]; },
		[](glm::mat4& m, float f) { m[0][0] = f; });
	t.def_property("m01",
		[](glm::mat4& m) { return m[0][1]; },
		[](glm::mat4& m, float f) { m[0][1] = f; });
	t.def_property("m02",
		[](glm::mat4& m) { return m[0][2]; },
		[](glm::mat4& m, float f) { m[0][2] = f; });
	t.def_property("m03",
		[](glm::mat4& m) { return m[0][3]; },
		[](glm::mat4& m, float f) { m[0][3] = f; });

	t.def_property("m10",
		[](glm::mat4& m) { return m[1][0]; },
		[](glm::mat4& m, float f) { m[1][0] = f; });
	t.def_property("m11",
		[](glm::mat4& m) { return m[1][1]; },
		[](glm::mat4& m, float f) { m[1][1] = f; });
	t.def_property("m12",
		[](glm::mat4& m) { return m[1][2]; },
		[](glm::mat4& m, float f) { m[1][2] = f; });
	t.def_property("m13",
		[](glm::mat4& m) { return m[1][3]; },
		[](glm::mat4& m, float f) { m[1][3] = f; });

	t.def_property("m20",
		[](glm::mat4& m) { return m[2][0]; },
		[](glm::mat4& m, float f) { m[2][0] = f; });
	t.def_property("m21",
		[](glm::mat4& m) { return m[2][1]; },
		[](glm::mat4& m, float f) { m[2][1] = f; });
	t.def_property("m22",
		[](glm::mat4& m) { return m[2][2]; },
		[](glm::mat4& m, float f) { m[2][2] = f; });
	t.def_property("m23",
		[](glm::mat4& m) { return m[2][3]; },
		[](glm::mat4& m, float f) { m[2][3] = f; });

	t.def_property("m30",
		[](glm::mat4& m) { return m[3][0]; },
		[](glm::mat4& m, float f) { m[3][0] = f; });
	t.def_property("m31",
		[](glm::mat4& m) { return m[3][1]; },
		[](glm::mat4& m, float f) { m[3][1] = f; });
	t.def_property("m32",
		[](glm::mat4& m) { return m[3][2]; },
		[](glm::mat4& m, float f) { m[3][2] = f; });
	t.def_property("m33",
		[](glm::mat4& m) { return m[3][3]; },
		[](glm::mat4& m, float f) { m[3][3] = f; });

	t.def("transform_point", [](glm::mat4& self, glm::vec3 point) {
		return transform_point(self, point);
	});
	t.def("transform_vector", [](glm::mat4& self, glm::vec3 vector) {
		return transform_vector(self, vector);
	});
	t.def("project_point", [](glm::mat4& self, glm::vec3 point) {
		return project_point(self, point);
	});
	t.def("inverse", [](glm::mat4& self) {
		return glm::inverse(self);
	});
	t.def("transpose", [](glm::mat4& self) {
		return glm::transpose(self);
	});
	t.def("to_transform", [](glm::mat4& self) {
		return mat4_to_transform(self);
	});
	t.def("__mul__", [](glm::mat4& self, glm::mat4& other) {
		return self * other;
	});
	t.def("__add__", [](glm::mat4& self, glm::mat4& other) {
		return self + other;
	});
	t.def("__iadd__", [](glm::mat4& self, glm::mat4& other) {
		self += other;
	});
	t.def("__sub__", [](glm::mat4& self, glm::mat4& other) {
		return self - other;
	});
	t.def("__isub__", [](glm::mat4& self, glm::mat4& other) {
		self -= other;
	});
	t.def("mul_scalar", [](glm::mat4& self, float s) {
		return self * s;
	});

	t.def_static("from_orthographic", [](float left, float right, float bottom, float top, float z_near, float z_far) {
		return glm::ortho(left, right, bottom, top, z_near, z_far);
	});
	t.def_static("from_perspective", [](float fov, float aspect, float z_near, float z_far) {
		return glm::perspective(fov, aspect, z_near, z_far);
	});

	t.def("__copy__", [](glm::mat4 self) {
		return self;
	});
	t.def("copy", [](glm::mat4 self) {
		return self;
	});
	t.def("__repr__", [](glm::mat4& self) {
		return glm::to_string(self);
	});
}

void wrap_Transform(py::module_& m) {
	py::class_<Transform> t(m, "Transform");

	t.def(py::init<>());
	t.def(py::init<glm::vec3, glm::quat, glm::vec3>());

	t.def_readwrite("translation", &Transform::translation);
	t.def_readwrite("rotation", &Transform::rotation);
	t.def_readwrite("scale", &Transform::scale);
	
	t.def("transform_point", [](Transform& self, glm::vec3 point) {
		return transform_point(self, point);
	});
	t.def("transform_vector", [](Transform& self, glm::vec3 vector) {
		return transform_vector(self, vector);
	});
	t.def("inverse", [](Transform& self) {
		return transform_inverse(self);
	});
	t.def("to_matrix4", [](Transform& self) {
		return transform_to_mat4(self);
	});

	t.def("__mul__", [](Transform& self, Transform& other) {
		return self * other;
	});

	t.def("__copy__", [](Transform self) {
		return self;
	});
	t.def("copy", [](Transform self) {
		return self;
	});
	t.def("__repr__", [](Transform& self) {
		return to_string(self);
	});
}

void wrap_Vector2(py::module_& m) {
	py::class_<glm::vec2> t(m, "Vector2");

	t.def(py::init([]() {
		return glm::vec2(0.0f, 0.0f);
	}));
	t.def(py::init<float, float>());

	t.def_readwrite("x", &glm::vec2::x);
	t.def_readwrite("y", &glm::vec2::y);

	t.def("length", [](glm::vec2& self) {
		return glm::length(self);
	});
	t.def("length_squared", [](glm::vec2& self) {
		return self.x * self.x + self.y * self.y;
	});
	t.def("dot", [](glm::vec2& self, glm::vec2& other) {
		return glm::dot(self, other);
	});
	t.def("normalize_self", [](glm::vec2& self) {
		self = glm::normalize(self);
	});
	t.def("normalize", [](glm::vec2& self) {
		glm::vec2 res = glm::normalize(self);
		return res;
	});

	t.def("__eq__", [](glm::vec2& self, glm::vec2& other) {
		return self == other;
	});
	t.def("__add__", [](glm::vec2& self, glm::vec2& other) {
		return self + other;
	});
	t.def("__iadd__", [](glm::vec2& self, glm::vec2& other) {
		self += other;
		return self;
	});
	t.def("__mul__", [](glm::vec2& self, float n) {
		return self * n;
	});
	t.def("__imul__", [](glm::vec2& self, float n) {
		self *= n;
		return self;
	});
	t.def("__rmul__", [](glm::vec2& self, float n) {
		return self * n;
	});
	t.def("__sub__", [](glm::vec2& self, glm::vec2& other) {
		return self - other;
	});
	t.def("__isub__", [](glm::vec2& self, glm::vec2& other) {
		self -= other;
		return self;
	});
	t.def("__neg__", [](glm::vec2& self) {
		return -self;
	});

	t.def("__copy__", [](glm::vec2 self) {
		return self;
	});
	t.def("copy", [](glm::vec2 self) {
		return self;
	});
	t.def("__repr__", [](glm::vec2& self) {
		return glm::to_string(self);
	});
}

void wrap_Vector4(py::module_& m) {
	py::class_<glm::vec4> t(m, "Vector4");

	t.def(py::init([]() {
		return glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
	}));
	t.def(py::init<float, float, float, float>());

	t.def_readwrite("x", &glm::vec4::x);
	t.def_readwrite("y", &glm::vec4::y);
	t.def_readwrite("z", &glm::vec4::z);
	t.def_readwrite("w", &glm::vec4::w);

	t.def("length", [](glm::vec4& self) {
		return glm::length(self);
	});
	t.def("length_squared", [](glm::vec4& self) {
		return self.x * self.x + self.y * self.y + self.z * self.z + self.w * self.w;
	});
	t.def("dot", [](glm::vec4& self, glm::vec4& other) {
		return glm::dot(self, other);
	});
	t.def("normalize_self", [](glm::vec4& self) {
		self = glm::normalize(self);
	});
	t.def("normalize", [](glm::vec4& self) {
		glm::vec4 res = glm::normalize(self);
		return res;
	});

	t.def("__eq__", [](glm::vec4& self, glm::vec4& other) {
		return self == other;
	});
	t.def("__add__", [](glm::vec4& self, glm::vec4& other) {
		return self + other;
	});
	t.def("__iadd__", [](glm::vec4& self, glm::vec4& other) {
		self += other;
		return self;
	});
	t.def("__mul__", [](glm::vec4& self, float n) {
		return self * n;
	});
	t.def("__imul__", [](glm::vec4& self, float n) {
		self *= n;
		return self;
	});
	t.def("__rmul__", [](glm::vec4& self, float n) {
		return self * n;
	});
	t.def("__sub__", [](glm::vec4& self, glm::vec4& other) {
		return self - other;
	});
	t.def("__isub__", [](glm::vec4& self, glm::vec4& other) {
		self -= other;
		return self;
	});
	t.def("__neg__", [](glm::vec4& self) {
		return -self;
	});

	t.def("__copy__", [](glm::vec4 self) {
		return self;
	});
	t.def("copy", [](glm::vec4 self) {
		return self;
	});
	t.def("__repr__", [](glm::vec4& self) {
		return glm::to_string(self);
	});
}

void wrap_Ray(py::module_& m) {
	py::class_<Ray> t(m, "Ray");

	t.def(py::init<>());
	t.def(py::init<glm::vec3, glm::vec3>());

	t.def_readwrite("position", &Ray::position);
	t.def_readwrite("direction", &Ray::direction);
	t.def("__copy__", [](Ray self) {
		return self;
	});
	t.def("copy", [](Ray self) {
		return self;
	});
	t.def("__repr__", [](Ray& self) {
		return "Ray(" + glm::to_string(self.position) + ", " + glm::to_string(self.direction) + ")";
	});
}

void wrap_vmath(pybind11::module_& m)
{
	wrap_Vector3(m);
	wrap_Quaternion(m);
	wrap_Matrix4(m);
	wrap_Transform(m);
	wrap_Vector2(m);
	wrap_Vector4(m);
	wrap_Ray(m);
}
