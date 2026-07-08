#pragma once

#include <nw/gfx/gfx.hpp>

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace nw::render {
struct RenderContext;
class ShaderProvider;
} // namespace nw::render

namespace nw {
struct Encounter;
struct Trigger;
} // namespace nw

namespace nw::render::viewer {

struct PreviewScene;
enum class DebugShapeCategory : uint8_t;

struct DebugShapeOptions {
    bool enabled = true;
    bool triggers = true;
    bool encounters = true;
};

class SceneDebugRenderer {
public:
    explicit SceneDebugRenderer(nw::gfx::Context* ctx) noexcept;
    ~SceneDebugRenderer();

    [[nodiscard]] bool initialize(nw::render::ShaderProvider& shader_provider);

    void render_debug_grid(nw::gfx::CommandList* cmd, const PreviewScene& scene, const nw::render::RenderContext& ctx,
        float spacing, float major_interval, float minor_width, float major_width, float opacity, float z_offset);
    void render_debug_shapes(nw::gfx::CommandList* cmd, const PreviewScene& scene, const nw::render::RenderContext& ctx,
        DebugShapeOptions options = {});

private:
    nw::gfx::Context* ctx_ = nullptr;
    nw::gfx::Handle<nw::gfx::Pipeline> debug_grid_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> debug_shape_pipeline_;
    nw::gfx::Handle<nw::gfx::Buffer> debug_grid_vertices_;
    nw::gfx::Handle<nw::gfx::Buffer> debug_grid_indices_;
    nw::gfx::Handle<nw::gfx::Buffer> debug_shape_vertices_;
    nw::gfx::Handle<nw::gfx::Buffer> debug_shape_indices_;
    size_t debug_grid_vertex_capacity_ = 0;
    size_t debug_grid_index_capacity_ = 0;
    size_t debug_shape_vertex_capacity_ = 0;
    size_t debug_shape_index_capacity_ = 0;
};

void append_debug_triangle(PreviewScene& scene, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
    const glm::vec4& color);
void append_debug_segment(PreviewScene& scene, const glm::vec3& a, const glm::vec3& b, const glm::vec4& color,
    float width);
void append_debug_shape_range(PreviewScene& scene, DebugShapeCategory category, size_t first_index);
bool append_trigger_debug_geometry(PreviewScene& scene, const nw::Trigger& trigger);
bool append_encounter_debug_geometry(PreviewScene& scene, const nw::Encounter& encounter);

} // namespace nw::render::viewer
