#include "smalls_property_tree.hpp"

#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/render/model_asset.hpp>
#include <nw/render/viewer/area_render_scene.hpp>
#include <nw/render/viewer/preview_scene.hpp>
#include <nw/render/viewer/scene_debug.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/smalls/runtime.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace {

using namespace std::literals;

namespace viewer = nw::render::viewer;

constexpr uint8_t kRenderEnabled = viewer::AreaRenderScene::RecordFlag::render_enabled;

nw::render::Bounds test_bounds(float x, float y = 0.0f)
{
    return {
        .min = {x, y, 0.0f},
        .max = {x + 1.0f, y + 1.0f, 1.0f},
    };
}

struct LiveObjects {
    ~LiveObjects()
    {
        for (const auto handle : handles) {
            if (nw::kernel::objects().valid(handle)) {
                nw::kernel::objects().destroy(handle);
            }
        }
    }

    template <typename T>
    nw::ObjectHandle make()
    {
        auto* object = nw::kernel::objects().make<T>();
        EXPECT_NE(object, nullptr);
        if (!object) {
            return nw::ObjectHandle{};
        }
        handles.push_back(object->handle());
        return object->handle();
    }

    std::vector<nw::ObjectHandle> handles;
};

struct TestGfxRuntime {
    nw::gfx::Core* core = nullptr;
    nw::gfx::Context* context = nullptr;
    bool owns_sdl_video = false;

    ~TestGfxRuntime()
    {
        if (context) {
            nw::gfx::wait_idle(context);
            nw::gfx::destroy_context(context);
        }
        if (core) {
            nw::gfx::destroy_core(core);
        }
        if (owns_sdl_video) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    bool initialize()
    {
        if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0u) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                return false;
            }
            owns_sdl_video = true;
        }

        nw::gfx::CoreConfig core_config{};
        core_config.app_name = "rollnw_test";
        core_config.enable_validation = false;
        core = nw::gfx::create_core(core_config);
        if (!core) {
            return false;
        }

        nw::gfx::ContextDesc context_desc{};
        context_desc.width = 64;
        context_desc.height = 64;
        context = nw::gfx::create_context(core, context_desc);
        return context != nullptr;
    }
};

std::unique_ptr<nw::render::RenderModel> make_selection_model(
    nw::gfx::Context* context,
    const std::array<glm::vec3, 3>& positions,
    nw::render::Bounds bounds)
{
    std::array<nw::render::Vertex, 3> vertices;
    for (size_t i = 0; i < positions.size(); ++i) {
        vertices[i].position = positions[i];
    }
    constexpr std::array<uint16_t, 3> indices{0u, 1u, 2u};

    auto model = std::make_unique<nw::render::RenderModel>();
    model->materials.push_back(nw::render::Material{});
    nw::render::Primitive primitive;
    primitive.vertex_count = static_cast<uint32_t>(vertices.size());
    primitive.index_count = static_cast<uint32_t>(indices.size());
    primitive.index_stride = sizeof(uint16_t);
    primitive.bounds = bounds;
    primitive.vertices = nw::gfx::create_buffer(context, nw::gfx::BufferDesc{
                                                             .size = sizeof(vertices),
                                                             .usage = nw::gfx::BufferUsage::Vertex,
                                                             .cpu_visible = true,
                                                         });
    primitive.indices = nw::gfx::create_buffer(context, nw::gfx::BufferDesc{
                                                            .size = sizeof(indices),
                                                            .usage = nw::gfx::BufferUsage::Index,
                                                            .cpu_visible = true,
                                                        });
    auto* vertex_data = nw::gfx::map_buffer(primitive.vertices);
    auto* index_data = nw::gfx::map_buffer(primitive.indices);
    if (!vertex_data || !index_data) {
        if (vertex_data) {
            nw::gfx::unmap_buffer(primitive.vertices);
        }
        if (index_data) {
            nw::gfx::unmap_buffer(primitive.indices);
        }
        if (primitive.vertices.valid()) {
            nw::gfx::destroy_buffer(primitive.vertices);
        }
        if (primitive.indices.valid()) {
            nw::gfx::destroy_buffer(primitive.indices);
        }
        return nullptr;
    }
    std::memcpy(vertex_data, vertices.data(), sizeof(vertices));
    std::memcpy(index_data, indices.data(), sizeof(indices));
    nw::gfx::unmap_buffer(primitive.vertices);
    nw::gfx::unmap_buffer(primitive.indices);

    model->primitives.push_back(primitive);
    model->bounds = bounds;
    return model;
}

void destroy_selection_model_buffers(viewer::PreviewScene& scene)
{
    for (auto& model : scene.static_models) {
        if (!model) {
            continue;
        }
        for (auto& primitive : model->primitives) {
            if (primitive.vertices.valid()) {
                nw::gfx::destroy_buffer(primitive.vertices);
                primitive.vertices = {};
            }
            if (primitive.indices.valid()) {
                nw::gfx::destroy_buffer(primitive.indices);
                primitive.indices = {};
            }
        }
    }
}

TEST(RenderViewerAreaSelection, TracesNearestRaisedAndSlopedSurfacesInBatches)
{
    const std::array triangles{
        viewer::AreaSurfaceTriangle{
            .v0 = {0.0f, 0.0f, 2.0f},
            .v1 = {2.0f, 0.0f, 2.0f},
            .v2 = {0.0f, 2.0f, 2.0f},
        },
        viewer::AreaSurfaceTriangle{
            .v0 = {0.0f, 0.0f, 4.0f},
            .v1 = {2.0f, 0.0f, 4.0f},
            .v2 = {0.0f, 2.0f, 4.0f},
        },
        viewer::AreaSurfaceTriangle{
            .v0 = {10.0f, 0.0f, 1.0f},
            .v1 = {12.0f, 0.0f, 1.0f},
            .v2 = {10.0f, 2.0f, 3.0f},
        },
    };
    const std::array ranges{
        viewer::AreaSurfaceRange{
            .bounds = {.min = {0.0f, 0.0f, 2.0f}, .max = {2.0f, 2.0f, 4.0f}},
            .first_triangle = 0,
            .triangle_count = 2,
        },
        viewer::AreaSurfaceRange{
            .bounds = {.min = {10.0f, 0.0f, 1.0f}, .max = {12.0f, 2.0f, 3.0f}},
            .first_triangle = 2,
            .triangle_count = 1,
        },
    };
    const std::array rays{
        viewer::ViewerRay{
            .origin = {0.5f, 0.5f, 10.0f},
            .direction = {0.0f, 0.0f, -2.0f},
        },
        viewer::ViewerRay{
            .origin = {10.5f, 0.5f, 10.0f},
            .direction = {0.0f, 0.0f, -1.0f},
        },
    };
    std::array<viewer::AreaSurfaceHit, 2> hits;
    viewer::trace_area_surfaces(rays, ranges, triangles, hits);

    ASSERT_EQ(hits[0].status, viewer::AreaSurfaceHitStatus::hit);
    EXPECT_EQ(hits[0].range_index, 0u);
    EXPECT_NEAR(hits[0].position.z, 4.0f, 1.0e-5f);
    EXPECT_NEAR(hits[0].distance, 6.0f, 1.0e-5f);
    EXPECT_EQ(hits[0].normal, glm::vec3(0.0f, 0.0f, 1.0f));

    ASSERT_EQ(hits[1].status, viewer::AreaSurfaceHitStatus::hit);
    EXPECT_EQ(hits[1].range_index, 1u);
    EXPECT_NEAR(hits[1].position.z, 1.5f, 1.0e-5f);
    EXPECT_GT(hits[1].normal.z, 0.0f);
}

TEST(RenderViewerAreaSelection, ReportsSurfaceMissesAndInvalidProtocols)
{
    const std::array triangles{
        viewer::AreaSurfaceTriangle{
            .v0 = {0.0f, 0.0f, 2.0f},
            .v1 = {2.0f, 0.0f, 2.0f},
            .v2 = {0.0f, 2.0f, 2.0f},
        },
    };
    std::array ranges{
        viewer::AreaSurfaceRange{
            .bounds = {.min = {0.0f, 0.0f, 2.0f}, .max = {2.0f, 2.0f, 2.0f}},
            .first_triangle = 0,
            .triangle_count = 1,
        },
    };

    viewer::ViewerRay ray{
        .origin = {5.0f, 5.0f, 10.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    EXPECT_EQ(viewer::trace_area_surface(ray, ranges, triangles).status,
        viewer::AreaSurfaceHitStatus::miss);

    ray.direction = {};
    EXPECT_EQ(viewer::trace_area_surface(ray, ranges, triangles).status,
        viewer::AreaSurfaceHitStatus::invalid_input);

    ray.direction = {0.0f, 0.0f, -1.0f};
    ranges[0].triangle_count = 2;
    EXPECT_EQ(viewer::trace_area_surface(ray, ranges, triangles).status,
        viewer::AreaSurfaceHitStatus::invalid_input);
}

TEST(RenderViewerAreaSelection, UpdatesAllSceneRootsForOneSpatialRow)
{
    LiveObjects live;
    const auto creature = live.make<nw::Creature>();
    viewer::PreviewScene scene;
    for (int i = 0; i < 2; ++i) {
        auto model = std::make_unique<nw::render::RenderModel>();
        model->bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        };
        scene.add(std::move(model));
        scene.static_area_model_info.back().object = creature;
    }

    const nw::ObjectSpatialState spatial{
        .owner = creature,
        .position = {5.0f, 6.0f, 7.0f},
        .orientation = {1.0f, 0.0f, 0.0f},
        .scale = {2.0f, 2.0f, 2.0f},
    };
    const std::array rows{spatial};
    const auto stats = viewer::update_area_object_spatial_states(scene, rows);
    EXPECT_EQ(stats.input_count, 1u);
    EXPECT_EQ(stats.rejected_input_count, 0u);
    EXPECT_EQ(stats.render_model_root_count, 2u);

    for (size_t i = 0; i < scene.static_models.size(); ++i) {
        const auto* instance = scene.static_model_instance(i);
        ASSERT_NE(instance, nullptr);
        EXPECT_EQ(glm::vec3(instance->root_transform[3]), spatial.position);
        EXPECT_EQ(instance->current_bounds.min, glm::vec3(3.0f, 4.0f, 5.0f));
        EXPECT_EQ(instance->current_bounds.max, glm::vec3(7.0f, 8.0f, 9.0f));
    }

    // The first update builds the retained lookup. A later topology change
    // must invalidate it so the newly added root participates immediately.
    auto added_model = std::make_unique<nw::render::RenderModel>();
    added_model->bounds = {
        .min = {-1.0f, -1.0f, -1.0f},
        .max = {1.0f, 1.0f, 1.0f},
    };
    scene.add(std::move(added_model));
    scene.static_area_model_info.back().object = creature;
    const auto added_stats = viewer::update_area_object_spatial_states(scene, rows);
    EXPECT_EQ(added_stats.render_model_root_count, 3u);

    auto invalid_spatial = spatial;
    invalid_spatial.scale.x = 0.0f;
    const std::array invalid_rows{invalid_spatial};
    const auto invalid_stats = viewer::update_area_object_spatial_states(scene, invalid_rows);
    EXPECT_EQ(invalid_stats.rejected_input_count, 1u);
    EXPECT_EQ(invalid_stats.render_model_root_count, 0u);
}

TEST(RenderViewerAreaSelection, UpdatesWaypointMarkerNativePositiveYToObjectHeading)
{
    LiveObjects live;
    const auto waypoint = live.make<nw::Waypoint>();
    viewer::PreviewScene scene;
    auto model = std::make_unique<nw::render::RenderModel>();
    model->bounds = {
        .min = {-1.0f, -1.0f, 0.0f},
        .max = {1.0f, 1.0f, 2.0f},
    };
    scene.add(std::move(model));
    scene.static_area_model_info.back().object = waypoint;

    nw::ObjectSpatialState spatial{
        .owner = waypoint,
        .position = {5.0f, 6.0f, 7.0f},
        .orientation = {1.0f, 0.0f, 0.0f},
        .scale = {1.0f, 1.0f, 1.0f},
    };
    std::array rows{spatial};
    auto stats = viewer::update_area_object_spatial_states(scene, rows);
    ASSERT_EQ(stats.render_model_root_count, 1u);
    const auto* instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    glm::vec3 forward = instance->root_transform * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};
    EXPECT_NEAR(forward.x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(forward.y, 0.0f, 1.0e-5f);

    spatial.orientation = {0.0f, 1.0f, 0.0f};
    rows[0] = spatial;
    stats = viewer::update_area_object_spatial_states(scene, rows);
    ASSERT_EQ(stats.render_model_root_count, 1u);
    instance = scene.static_model_instance(0);
    ASSERT_NE(instance, nullptr);
    forward = instance->root_transform * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};
    EXPECT_NEAR(forward.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(forward.y, 1.0f, 1.0e-5f);
}

TEST(RenderViewerAreaSelection, RejectsBoundsOnlyHitsAndSelectsNearestTriangle)
{
    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    LiveObjects live;
    const std::array objects{live.make<nw::Creature>(), live.make<nw::Creature>()};
    viewer::PreviewScene scene;
    auto near_model = make_selection_model(
        gfx.context,
        {{{1.5f, 0.7f, 0.7f}, {1.5f, 0.9f, 0.7f}, {1.5f, 0.7f, 0.9f}}},
        {.min = {1.0f, 0.0f, 0.0f}, .max = {2.0f, 1.0f, 1.0f}});
    auto far_model = make_selection_model(
        gfx.context,
        {{{3.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 0.0f}, {3.0f, 0.0f, 1.0f}}},
        {.min = {3.0f, 0.0f, 0.0f}, .max = {4.0f, 1.0f, 1.0f}});
    ASSERT_TRUE(near_model);
    ASSERT_TRUE(far_model);

    scene.add(std::move(near_model));
    scene.static_area_model_info.back().kind = viewer::AreaRenderRecordKind::creature;
    scene.static_area_model_info.back().object = objects[0];
    scene.add(std::move(far_model));
    scene.static_area_model_info.back().kind = viewer::AreaRenderRecordKind::creature;
    scene.static_area_model_info.back().object = objects[1];

    viewer::AreaRenderScene records;
    records.rebuild(scene);
    const std::array rays{
        viewer::ViewerRay{
            .origin = {0.0f, 0.25f, 0.25f},
            .direction = {2.0f, 0.0f, 0.0f},
        },
        viewer::ViewerRay{
            .origin = {0.0f, 0.75f, 0.75f},
            .direction = {1.0f, 0.0f, 0.0f},
        },
    };
    std::array<viewer::AreaObjectSelection, 2> selections;
    viewer::select_area_objects(rays, records, scene, selections);

    ASSERT_EQ(selections[0].status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(selections[0].record_index, 1u);
    EXPECT_EQ(selections[0].object, objects[1]);
    EXPECT_NEAR(selections[0].distance, 3.0f, 1.0e-5f);

    ASSERT_EQ(selections[1].status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(selections[1].record_index, 0u);
    EXPECT_EQ(selections[1].object, objects[0]);
    EXPECT_NEAR(selections[1].distance, 1.5f, 1.0e-5f);

    std::array<viewer::AreaObjectSelection, 1> mismatched_selection;
    viewer::select_area_objects(
        std::span<const viewer::ViewerRay>{}, records, scene, mismatched_selection);
    EXPECT_EQ(mismatched_selection[0].status, viewer::AreaObjectSelectionStatus::invalid_input);

    auto invalid_ray = rays[0];
    invalid_ray.direction = {};
    EXPECT_EQ(viewer::select_area_object(invalid_ray, records, scene).status,
        viewer::AreaObjectSelectionStatus::invalid_input);
    EXPECT_EQ(viewer::select_area_object(
                  rays[0],
                  records,
                  scene,
                  {.target = static_cast<viewer::AreaObjectSelectionTarget>(255)})
                  .status,
        viewer::AreaObjectSelectionStatus::invalid_input);

    nw::kernel::objects().destroy(objects[1]);
    EXPECT_EQ(viewer::select_area_object(rays[0], records, scene).status,
        viewer::AreaObjectSelectionStatus::miss);

    auto* near_instance = scene.static_model_instance(0);
    ASSERT_NE(near_instance, nullptr);
    near_instance->visible = false;
    records.rebuild(scene);
    EXPECT_EQ(viewer::select_area_object(rays[1], records, scene).status,
        viewer::AreaObjectSelectionStatus::miss);
    destroy_selection_model_buffers(scene);
}

TEST(RenderViewerAreaSelection, SeparatesObjectAndTileSelectionTargets)
{
    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    LiveObjects live;
    const auto object = live.make<nw::Creature>();
    viewer::PreviewScene scene;
    auto tile_model = make_selection_model(
        gfx.context,
        {{{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}}},
        {.min = {1.0f, 0.0f, 0.0f}, .max = {2.0f, 1.0f, 1.0f}});
    auto object_model = make_selection_model(
        gfx.context,
        {{{3.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 0.0f}, {3.0f, 0.0f, 1.0f}}},
        {.min = {3.0f, 0.0f, 0.0f}, .max = {4.0f, 1.0f, 1.0f}});
    ASSERT_TRUE(tile_model);
    ASSERT_TRUE(object_model);

    constexpr float tile_elevation = 6.0f;
    scene.add(std::move(tile_model));
    scene.static_area_model_info.back().kind = viewer::AreaRenderRecordKind::tile;
    scene.static_area_model_info.back().tile_x = 4;
    scene.static_area_model_info.back().tile_y = 7;
    auto* tile_instance = scene.static_model_instance(0);
    ASSERT_NE(tile_instance, nullptr);
    tile_instance->root_transform = glm::translate(
        glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, tile_elevation});
    tile_instance->current_bounds.min.z += tile_elevation;
    tile_instance->current_bounds.max.z += tile_elevation;
    scene.add(std::move(object_model));
    scene.static_area_model_info.back().kind = viewer::AreaRenderRecordKind::creature;
    scene.static_area_model_info.back().object = object;

    viewer::AreaRenderScene records;
    records.rebuild(scene);
    const viewer::ViewerRay object_ray{
        .origin = {0.0f, 0.25f, 0.25f},
        .direction = {1.0f, 0.0f, 0.0f},
    };
    const viewer::ViewerRay tile_ray{
        .origin = {0.0f, 0.25f, tile_elevation + 0.25f},
        .direction = {1.0f, 0.0f, 0.0f},
    };
    const auto object_hit = viewer::select_area_object(object_ray, records, scene);
    ASSERT_EQ(object_hit.status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(object_hit.record_index, 1u);
    EXPECT_EQ(object_hit.kind, viewer::AreaRenderRecordKind::creature);
    EXPECT_EQ(object_hit.object, object);
    EXPECT_EQ(object_hit.source, viewer::AreaObjectSelectionSource::area_record);
    EXPECT_NEAR(object_hit.position.x, 3.0f, 1.0e-5f);
    EXPECT_NEAR(object_hit.position.y, 0.25f, 1.0e-5f);
    EXPECT_NEAR(object_hit.position.z, 0.25f, 1.0e-5f);
    EXPECT_NEAR(object_hit.distance, 3.0f, 1.0e-5f);

    const auto tile_hit = viewer::select_area_object(
        tile_ray,
        records,
        scene,
        {.target = viewer::AreaObjectSelectionTarget::tile});
    ASSERT_EQ(tile_hit.status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(tile_hit.record_index, 0u);
    EXPECT_EQ(tile_hit.kind, viewer::AreaRenderRecordKind::tile);
    EXPECT_EQ(tile_hit.tile_x, 4);
    EXPECT_EQ(tile_hit.tile_y, 7);
    EXPECT_EQ(tile_hit.object.type, nw::ObjectType::invalid);
    EXPECT_EQ(tile_hit.source, viewer::AreaObjectSelectionSource::area_record);
    EXPECT_NEAR(tile_hit.position.x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(tile_hit.position.y, 0.25f, 1.0e-5f);
    EXPECT_NEAR(tile_hit.position.z, tile_elevation + 0.25f, 1.0e-5f);
    EXPECT_NEAR(tile_hit.distance, 1.0f, 1.0e-5f);
    const auto tile_bounds = viewer::area_tile_selection_bounds(tile_hit, records);
    ASSERT_TRUE(tile_bounds);
    EXPECT_EQ(tile_bounds->min, glm::vec3(40.0f, 70.0f, tile_elevation));
    EXPECT_EQ(tile_bounds->max, glm::vec3(50.0f, 80.0f, tile_elevation + 1.0f));

    const auto raised_tile_hit = viewer::select_area_object(
        viewer::ViewerRay{
            .origin = {0.0f, 0.1f, tile_elevation + 0.75f},
            .direction = {1.0f, 0.0f, 0.0f},
        },
        records,
        scene,
        {.target = viewer::AreaObjectSelectionTarget::tile});
    ASSERT_EQ(raised_tile_hit.status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_NEAR(raised_tile_hit.position.z, tile_elevation + 0.75f, 1.0e-5f);
    const auto raised_tile_bounds = viewer::area_tile_selection_bounds(raised_tile_hit, records);
    ASSERT_TRUE(raised_tile_bounds);
    EXPECT_EQ(raised_tile_bounds->min, tile_bounds->min);
    EXPECT_EQ(raised_tile_bounds->max, tile_bounds->max);
    EXPECT_FALSE(viewer::area_tile_selection_bounds(object_hit, records));

    auto equal_depth_object_model = make_selection_model(
        gfx.context,
        {{{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}}},
        {.min = {1.0f, 0.0f, 0.0f}, .max = {2.0f, 1.0f, 1.0f}});
    ASSERT_TRUE(equal_depth_object_model);
    scene.add(std::move(equal_depth_object_model));
    scene.static_area_model_info.back().kind = viewer::AreaRenderRecordKind::creature;
    scene.static_area_model_info.back().object = object;
    records.rebuild(scene);

    const auto equal_depth_hit = viewer::select_area_object(object_ray, records, scene);
    ASSERT_EQ(equal_depth_hit.status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(equal_depth_hit.record_index, 2u);
    EXPECT_EQ(equal_depth_hit.kind, viewer::AreaRenderRecordKind::creature);
    EXPECT_EQ(equal_depth_hit.object, object);
    EXPECT_NEAR(equal_depth_hit.distance, 1.0f, 1.0e-5f);

    const auto equal_depth_tile_hit = viewer::select_area_object(
        tile_ray,
        records,
        scene,
        {.target = viewer::AreaObjectSelectionTarget::tile});
    ASSERT_EQ(equal_depth_tile_hit.status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(equal_depth_tile_hit.record_index, 0u);
    EXPECT_EQ(equal_depth_tile_hit.kind, viewer::AreaRenderRecordKind::tile);
    EXPECT_EQ(equal_depth_tile_hit.object.type, nw::ObjectType::invalid);
    destroy_selection_model_buffers(scene);
}

TEST(RenderViewerAreaSelection, SelectsTriggerAndEncounterFootprints)
{
    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    LiveObjects live;
    const auto trigger_handle = live.make<nw::Trigger>();
    const auto encounter_handle = live.make<nw::Encounter>();
    auto* trigger = nw::kernel::objects().get<nw::Trigger>(trigger_handle);
    auto* encounter = nw::kernel::objects().get<nw::Encounter>(encounter_handle);
    ASSERT_NE(trigger, nullptr);
    ASSERT_NE(encounter, nullptr);

    const std::array trigger_points{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{2.0f, 0.0f, 0.0f},
        glm::vec3{2.0f, 2.0f, 0.0f},
        glm::vec3{0.0f, 2.0f, 0.0f},
    };
    const std::array encounter_points{
        glm::vec3{3.0f, 0.0f, 0.0f},
        glm::vec3{5.0f, 0.0f, 0.0f},
        glm::vec3{5.0f, 2.0f, 0.0f},
        glm::vec3{3.0f, 2.0f, 0.0f},
    };
    auto& components = nw::kernel::objects().components();
    ASSERT_TRUE(components.set_geometry(trigger_handle, trigger_points));
    ASSERT_TRUE(components.set_geometry(encounter_handle, encounter_points));

    viewer::PreviewScene scene;
    auto tile_model = make_selection_model(
        gfx.context,
        {{{0.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}, {0.0f, 6.0f, 0.0f}}},
        {.min = {0.0f, 0.0f, 0.0f}, .max = {6.0f, 6.0f, 0.0f}});
    ASSERT_TRUE(tile_model);
    scene.add(std::move(tile_model));
    scene.static_area_model_info.back().kind = viewer::AreaRenderRecordKind::tile;
    scene.static_area_model_info.back().tile_x = 0;
    scene.static_area_model_info.back().tile_y = 0;
    ASSERT_TRUE(viewer::append_trigger_debug_geometry(scene, *trigger));
    ASSERT_TRUE(viewer::append_encounter_debug_geometry(scene, *encounter));
    ASSERT_EQ(scene.debug_shape_selection_ranges.size(), 2u);
    viewer::AreaRenderScene records;
    records.rebuild(scene);

    const auto trigger_hit = viewer::select_area_object(
        {
            .origin = {1.0f, 1.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
        },
        records,
        scene);
    ASSERT_EQ(trigger_hit.status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(trigger_hit.source, viewer::AreaObjectSelectionSource::debug_shape);
    EXPECT_EQ(trigger_hit.record_index, 0u);
    EXPECT_EQ(trigger_hit.object, trigger_handle);
    EXPECT_NEAR(trigger_hit.distance, 4.92f, 1.0e-5f);

    const auto encounter_hit = viewer::select_area_object(
        {
            .origin = {4.0f, 1.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
        },
        records,
        scene);
    ASSERT_EQ(encounter_hit.status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(encounter_hit.source, viewer::AreaObjectSelectionSource::debug_shape);
    EXPECT_EQ(encounter_hit.record_index, 1u);
    EXPECT_EQ(encounter_hit.object, encounter_handle);
    EXPECT_NEAR(encounter_hit.distance, 4.88f, 1.0e-5f);

    const auto disabled_trigger = viewer::select_area_object(
        {
            .origin = {1.0f, 1.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
        },
        records,
        scene,
        {.triggers_enabled = false, .encounters_enabled = true});
    EXPECT_EQ(disabled_trigger.status, viewer::AreaObjectSelectionStatus::miss);

    const auto underlying_tile = viewer::select_area_object(
        {
            .origin = {1.0f, 1.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
        },
        records,
        scene,
        {.target = viewer::AreaObjectSelectionTarget::tile});
    ASSERT_EQ(underlying_tile.status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(underlying_tile.source, viewer::AreaObjectSelectionSource::area_record);
    EXPECT_EQ(underlying_tile.kind, viewer::AreaRenderRecordKind::tile);
    EXPECT_EQ(underlying_tile.object.type, nw::ObjectType::invalid);
    EXPECT_NEAR(underlying_tile.distance, 5.0f, 1.0e-5f);
    destroy_selection_model_buffers(scene);
}

TEST(RenderViewerAreaSelection, SelectsNonPlanarEncounterFootprintWithoutSpawnMarkers)
{
    LiveObjects live;
    const auto encounter_handle = live.make<nw::Encounter>();
    auto* encounter = nw::kernel::objects().get<nw::Encounter>(encounter_handle);
    ASSERT_NE(encounter, nullptr);

    const std::array footprint_points{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{2.0f, 0.0f, 1.0f},
        glm::vec3{2.0f, 2.0f, 2.0f},
        glm::vec3{0.0f, 2.0f, 1.0f},
    };
    const std::array spawn_points{
        nw::ObjectSpawnPoint{.position = {20.0f, 30.0f, 1.0f}, .orientation = 0.0f},
    };
    ASSERT_TRUE(nw::kernel::objects().components().set_geometry(
        encounter_handle, footprint_points));
    ASSERT_TRUE(nw::kernel::objects().components().set_spawn_points(encounter_handle, spawn_points));

    viewer::PreviewScene scene;
    ASSERT_TRUE(viewer::append_encounter_debug_geometry(scene, *encounter));
    ASSERT_EQ(scene.debug_shape_selection_ranges.size(), 1u);
    ASSERT_EQ(scene.debug_shape_ranges.size(), 2u);
    EXPECT_EQ(scene.debug_shape_selection_ranges[0].point_count, footprint_points.size());
    EXPECT_LT(scene.debug_shape_selection_ranges[0].bounds.max.x, spawn_points[0].position.x);
    EXPECT_LT(scene.debug_shape_selection_ranges[0].bounds.max.y, spawn_points[0].position.y);
    viewer::AreaRenderScene records;
    records.rebuild(scene);

    const auto footprint_hit = viewer::select_area_object(
        {
            .origin = {1.0f, 1.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
        },
        records,
        scene);
    ASSERT_EQ(footprint_hit.status, viewer::AreaObjectSelectionStatus::hit);
    EXPECT_EQ(footprint_hit.source, viewer::AreaObjectSelectionSource::debug_shape);
    EXPECT_EQ(footprint_hit.object, encounter_handle);
    EXPECT_NEAR(footprint_hit.distance, 3.88f, 1.0e-5f);

    const auto spawn_marker_miss = viewer::select_area_object(
        {
            .origin = {20.0f, 30.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
        },
        records,
        scene);
    EXPECT_EQ(spawn_marker_miss.status, viewer::AreaObjectSelectionStatus::miss);
}

TEST(RenderViewerAreaSelection, ReducesRepeatedObjectRecordsToOneBound)
{
    LiveObjects live;
    const auto selected_object = live.make<nw::Creature>();
    const auto other_object = live.make<nw::Creature>();
    const std::array objects{selected_object, other_object, selected_object, selected_object};
    std::array bounds{
        test_bounds(4.0f, 2.0f),
        test_bounds(-20.0f, -20.0f),
        nw::render::Bounds{.min = {2.0f, 1.0f, -1.0f}, .max = {7.0f, 5.0f, 3.0f}},
        test_bounds(100.0f, 100.0f),
    };
    const std::array<uint8_t, 4> flags{kRenderEnabled, kRenderEnabled, kRenderEnabled, 0};
    const std::array kinds{
        viewer::AreaRenderRecordKind::creature,
        viewer::AreaRenderRecordKind::creature,
        viewer::AreaRenderRecordKind::creature,
        viewer::AreaRenderRecordKind::creature,
    };

    auto result = viewer::collect_area_object_bounds(selected_object, bounds, flags, kinds, objects);
    ASSERT_EQ(result.status, viewer::AreaObjectBoundsStatus::found);
    EXPECT_EQ(result.record_count, 2u);
    EXPECT_EQ(result.bounds.min, glm::vec3(2.0f, 1.0f, -1.0f));
    EXPECT_EQ(result.bounds.max, glm::vec3(7.0f, 5.0f, 3.0f));

    bounds[2].min.x = std::numeric_limits<float>::quiet_NaN();
    result = viewer::collect_area_object_bounds(selected_object, bounds, flags, kinds, objects);
    ASSERT_EQ(result.status, viewer::AreaObjectBoundsStatus::found);
    EXPECT_EQ(result.record_count, 1u);
    EXPECT_EQ(result.bounds.min, glm::vec3(4.0f, 2.0f, 0.0f));

    EXPECT_EQ(viewer::collect_area_object_bounds(
                  selected_object, bounds, std::span<const uint8_t>{}, kinds, objects)
                  .status,
        viewer::AreaObjectBoundsStatus::invalid_input);
    EXPECT_EQ(viewer::collect_area_object_bounds(
                  nw::ObjectHandle{}, bounds, flags, kinds, objects)
                  .status,
        viewer::AreaObjectBoundsStatus::invalid_input);
}

struct AreaSelectionCandidate {
    nw::ObjectHandle object;
    viewer::AreaRenderRecordKind kind = viewer::AreaRenderRecordKind::unknown;
};

TEST(RenderViewerAreaSelection, AreaFixtureObjectsProduceDistinctWorkbenchSnapshots)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/module_as_dir", false);
    ASSERT_NE(module, nullptr);

    const nw::Resref area_resref{"test_area"sv};
    if (nw::kernel::resman().demand({area_resref, nw::ResourceType::are}).bytes.size() == 0u) {
        GTEST_SKIP() << "test_area fixture unavailable";
    }

    auto* area = nw::kernel::objects().make_area(area_resref);
    ASSERT_NE(area, nullptr);
    ASSERT_TRUE(area->instantiate());
    const auto area_handle = area->handle();

    std::vector<AreaSelectionCandidate> candidates;
    const auto append_first = [&candidates](const auto& objects, viewer::AreaRenderRecordKind kind) {
        if (!objects.empty() && objects.front()) {
            candidates.push_back({objects.front()->handle(), kind});
        }
    };
    append_first(area->creatures, viewer::AreaRenderRecordKind::creature);
    append_first(area->doors, viewer::AreaRenderRecordKind::door);
    append_first(area->items, viewer::AreaRenderRecordKind::item);
    append_first(area->placeables, viewer::AreaRenderRecordKind::placeable);
    if (candidates.size() < 2) {
        area->clear();
        nw::kernel::objects().destroy(area_handle);
        GTEST_SKIP() << "test_area fixture has fewer than two selectable object types";
    }

    const std::array objects{candidates[0].object, candidates[1].object};
    nw::toolset::PropertyTreeExpansionState expansion;
    std::array<nw::toolset::PropertyTreeSnapshot, 2> snapshots;

    for (size_t index = 0; index < snapshots.size(); ++index) {
        nw::toolset::build_property_rows(
            nw::kernel::runtime(), objects[index], expansion, {}, snapshots[index]);
        EXPECT_EQ(snapshots[index].status, nw::toolset::PropertyTreeStatus::ready);
        EXPECT_GT(snapshots[index].persistent_propset_count, 0u);
        EXPECT_FALSE(snapshots[index].rows.empty());
    }

    EXPECT_NE(snapshots[0].object, snapshots[1].object);
    EXPECT_NE(snapshots[0].rows.front().root_propset_type, snapshots[1].rows.front().root_propset_type);

    const auto first_object = objects[0];
    const auto second_object = objects[1];
    area->clear();
    nw::kernel::objects().destroy(area_handle);
    EXPECT_FALSE(nw::kernel::objects().valid(first_object));
    EXPECT_FALSE(nw::kernel::objects().valid(second_object));
    EXPECT_FALSE(nw::kernel::objects().valid(area_handle));
}

} // namespace
