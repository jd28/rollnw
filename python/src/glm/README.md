# vmath
Simple python 3D vector3 math library wrapping some types from GLM library using pybind11.

## Description
Both pure python version and C++ version provided.

Not a direct glm wrapping. There are some Object-Oriented enhencements to the types.

Should be useful for other pybind11 bindings that use glm types, the conversion will be automatic (if done right).

Types:

+ Vector3 - glm::vec3
+ Quaternion - glm::quat
+ Matrix4 - glm::mat4
+ Transform - translation-rotation-scale
+ Vector2 - glm::vec2
+ Vector4 - glm::vec4
+ Ray - position-direction

Main source files comes from:

+ toy/wrap/wrap_vmath.h
+ toy/wrap/wrap_vmath.cpp
+ toy/glm_util.h
+ toy/glm_util.cpp

## Documentation
See vmath.py for interfaces.

See src/wrap_vmath.cpp for pybind11 binding code.

## Usage
Main classes:
* Vector3: 3D Vector3.
* Matrix4: 4x4 column major matrix4, M1 * M2 means first apply M2, then apply M1.
* Quaternion: Quaternion for rotation.
* Transform: Transform represented as Translation-Rotation-Scale.

```python
>>> from vmath import Vector3, Matrix4, Quaternion, Transform
>>> v1 = Vector3(1.0, 2.0, 3.0)
>>> v2 = Vector3(4.0, 5.0, 6.0)
>>> v1 + v2
Vector3(5.0000, 7.0000, 9.0000)

>>> q = Quaternion.from_euler_angles(Vector3(0.0, 0.8, 0.0))
>>> q
Quaternion(0.9211, 0.0000, 0.3894, 0.0000)

>>> xf = Transform(v1, q, Vector3(1.0, 1.0, 1.0))
>>> m = xf.to_matrix4()
>>> print(m)
Matrix4<0.6967, 0.0000, 0.7174, 1.0000
       0.0000, 1.0000, 0.0000, 2.0000
       -0.7174, 0.0000, 0.6967, 3.0000
       0.0000, 0.0000, 0.0000, 1.0000>

>>> m.decompose()
Transform(Vector3(1.0000, 2.0000, 3.0000), Quaternion(0.9211, 0.0000, 0.3894, 0.0000), Vector3(1.0000, 1.0000, 1.0000))

>>> m.transform_vector3(v1)
Vector3(2.8488, 2.0000, 1.3728)
>>> m.transform_point(v1)
Vector3(3.8488, 4.0000, 4.3728)
```

Euler angles should be stored in Vector3(pitch, yaw, roll), order yaw-pitch-roll.
Therefore the corresponding rotation matrix4 is M(yaw) * M(pitch) * M(roll).


## Automatic conversion
One may want to use pybind11 binded glm types in multiple different extension modules, or embedded modules.

The conversion should just works thanks to internal pybind11 mechanisms, but with some carvets. Some notes:

+ Don't mix Debug and Release extension modules. The types won't be recongnised as the same by pybind11.

+ If one want more control, just drop the cpp files in the target project and compile _vmath as builtin (embedded) as vmath.


## Build
Checkout pip_install_debug.bat file. One need to provide GLM path to build the extension.

## Note
The interface of the types between the pure python module and extension module is somewhat different.

## License
MIT License.
