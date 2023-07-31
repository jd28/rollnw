find_package(Threads)

pybind11_add_module(rollnw NO_EXTRAS
    wrapper.cpp
    opaque_types.cpp

    glm/glm_util.cpp
    glm/wrap_vmath.cpp

    wrapper_formats.cpp
    wrapper_i18n.cpp
    wrapper_kernel.cpp
    wrapper_model.cpp
    wrapper_nwn1.cpp
    wrapper_objects_components.cpp
    wrapper_objects.cpp
    wrapper_resources.cpp
    wrapper_rules.cpp
    wrapper_script.cpp
    wrapper_serialization.cpp
    wrapper_util.cpp
)

if(MSVC)
target_compile_options(rollnw PRIVATE /bigobj)
endif()

target_link_libraries(rollnw PRIVATE
    nw
    nw-profiles
    pybind11::module
)

target_include_directories(rollnw PRIVATE
    ../external/rollnw/profiles
    ../external/pybind11_json/include
    ../external/pybind11/include
)
