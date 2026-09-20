#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/render/model_asset.hpp>
#include <nw/render/model_instance_animation.hpp>
#include <nw/render/viewer/area_render_scene.hpp>
#include <nw/render/viewer/preview_scene.hpp>
#include <nw/render/viewer/scene_lights.hpp>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <vector>

namespace {

namespace viewer = nw::render::viewer;

void destroy_selection_benchmark_buffers(nw::render::Primitive& primitive)
{
    if (primitive.vertices.valid()) {
        nw::gfx::destroy_buffer(primitive.vertices);
        primitive.vertices = {};
    }
    if (primitive.indices.valid()) {
        nw::gfx::destroy_buffer(primitive.indices);
        primitive.indices = {};
    }
}

struct SelectionBenchmarkGfxRuntime {
    nw::gfx::Core* core = nullptr;
    nw::gfx::Context* context = nullptr;
    bool owns_sdl_video = false;

    ~SelectionBenchmarkGfxRuntime()
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
        core_config.app_name = "rollnw_benchmark";
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

std::unique_ptr<nw::render::RenderModel> make_selection_benchmark_model(
    nw::gfx::Context* context, float x, float y, bool with_geometry)
{
    auto model = std::make_unique<nw::render::RenderModel>();
    model->bounds = {
        .min = {x, y, 0.0f},
        .max = {x + 1.0f, y + 1.0f, 1.0f},
    };
    if (!with_geometry) {
        return model;
    }

    std::array<nw::render::Vertex, 3> vertices;
    vertices[0].position = {x, y, 0.0f};
    vertices[1].position = {x, y + 1.0f, 0.0f};
    vertices[2].position = {x, y, 1.0f};
    constexpr std::array<uint16_t, 3> indices{0u, 1u, 2u};

    model->materials.push_back(nw::render::Material{});
    nw::render::Primitive primitive;
    primitive.vertex_count = static_cast<uint32_t>(vertices.size());
    primitive.index_count = static_cast<uint32_t>(indices.size());
    primitive.index_stride = sizeof(uint16_t);
    primitive.bounds = model->bounds;
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
    if (!primitive.vertices.valid() || !primitive.indices.valid()) {
        destroy_selection_benchmark_buffers(primitive);
        return nullptr;
    }

    auto* vertex_data = nw::gfx::map_buffer(primitive.vertices);
    if (!vertex_data) {
        destroy_selection_benchmark_buffers(primitive);
        return nullptr;
    }
    auto* index_data = nw::gfx::map_buffer(primitive.indices);
    if (!index_data) {
        nw::gfx::unmap_buffer(primitive.vertices);
        destroy_selection_benchmark_buffers(primitive);
        return nullptr;
    }
    std::memcpy(vertex_data, vertices.data(), sizeof(vertices));
    std::memcpy(index_data, indices.data(), sizeof(indices));
    nw::gfx::unmap_buffer(primitive.vertices);
    nw::gfx::unmap_buffer(primitive.indices);
    model->primitives.push_back(primitive);
    return model;
}

std::shared_ptr<nw::render::RenderModel> make_area_dynamic_record_benchmark_model(
    uint32_t primitive_count)
{
    auto model = std::make_shared<nw::render::RenderModel>();
    model->bounds = {
        .min = {0.0f, 0.0f, 0.0f},
        .max = {10.0f, 10.0f, 1.0f},
    };
    model->materials.push_back(nw::render::Material{});
    nw::render::Node main_light_node;
    main_light_node.local_transform[3]
        = glm::vec4{5.0f, 5.0f, 2.0f, 1.0f};
    main_light_node.world_transform = main_light_node.local_transform;
    model->nodes.push_back(main_light_node);
    nw::render::Node source_light_node;
    source_light_node.local_transform[3]
        = glm::vec4{7.5f, 7.5f, 2.0f, 1.0f};
    source_light_node.world_transform
        = source_light_node.local_transform;
    model->nodes.push_back(source_light_node);
    model->lights.push_back({
        .node = 0u,
        .color = {1.0f, 1.0f, 1.0f},
        .radius = 8.0f,
        .external_color_slot = 0u,
        .main_contribution = true,
    });
    model->lights.push_back({
        .node = 1u,
        .color = {1.0f, 1.0f, 1.0f},
        .radius = 6.0f,
        .external_color_slot = 2u,
    });
    for (uint32_t primitive_index = 0;
        primitive_index < primitive_count;
        ++primitive_index) {
        nw::render::Primitive primitive;
        primitive.vertex_count = 78u;
        primitive.index_count = 78u;
        primitive.index_stride = sizeof(uint16_t);
        primitive.bounds = model->bounds;
        // CPU-only fixture: retain prepared-draw volume without requiring GPU
        // surface buffers. Tile-record refresh benchmarks the flat metadata
        // and light passes; surface-cache copying has a separate exact-volume
        // benchmark below.
        primitive.skinned = true;
        model->primitives.push_back(primitive);
    }
    return model;
}

void destroy_selection_benchmark_buffers(viewer::PreviewScene& scene)
{
    for (auto& model : scene.static_models) {
        if (!model) {
            continue;
        }
        for (auto& primitive : model->primitives) {
            destroy_selection_benchmark_buffers(primitive);
        }
    }
}

struct AreaObjectSelectionBenchmarkData {
    AreaObjectSelectionBenchmarkData(int64_t record_count, int64_t selectable_record_count)
    {
        if (!gfx.initialize()) {
            return;
        }

        auto* creature = nw::kernel::objects().make<nw::Creature>();
        if (!creature) {
            return;
        }
        object = creature->handle();

        const int64_t safe_record_count = std::max<int64_t>(record_count, 0);
        const size_t count = static_cast<size_t>(safe_record_count);
        const size_t selectable_count = static_cast<size_t>(
            std::clamp<int64_t>(selectable_record_count, 0, safe_record_count));
        const size_t selectable_begin = count - selectable_count;

        for (size_t index = 0; index < count; ++index) {
            const bool selectable = index >= selectable_begin;
            const float x = selectable
                ? 1.0f + static_cast<float>(index - selectable_begin) * 2.0f
                : 0.0f;
            const float y = selectable ? 0.0f : 2.0f;
            auto model = make_selection_benchmark_model(gfx.context, x, y, selectable);
            if (!model) {
                return;
            }
            scene.add(std::move(model));
            auto& source = scene.static_area_model_info.back();
            source.kind = selectable
                ? nw::ObjectType::creature
                : nw::ObjectType::tile;
            source.object = selectable ? object : nw::ObjectHandle{};
            if (!selectable) {
                source.tile_x = static_cast<int16_t>(index);
                source.tile_y = 0;
            }
        }
        records.rebuild(scene);
        initialized = records.stats().record_count == count
            && records.stats().selectable_object_record_count == selectable_count;
    }

    ~AreaObjectSelectionBenchmarkData()
    {
        destroy_selection_benchmark_buffers(scene);
        if (nw::kernel::objects().valid(object)) {
            nw::kernel::objects().destroy(object);
        }
    }

    SelectionBenchmarkGfxRuntime gfx;
    nw::ObjectHandle object;
    viewer::PreviewScene scene;
    viewer::AreaRenderScene records;
    bool initialized = false;
};

void BM_area_object_selection(benchmark::State& state, bool hit)
{
    AreaObjectSelectionBenchmarkData data{state.range(0), state.range(1)};
    if (!data.initialized) {
        state.SkipWithError("failed to create area selection benchmark scene");
        return;
    }

    const viewer::ViewerRay ray{
        .origin = {0.0f, hit ? 0.25f : 4.0f, 0.25f},
        .direction = {1.0f, 0.0f, 0.0f},
    };
    const auto expected = viewer::select_area_object(ray, data.records, data.scene);
    const auto expected_status = hit
        ? viewer::AreaObjectSelectionStatus::hit
        : viewer::AreaObjectSelectionStatus::miss;
    if (expected.status != expected_status) {
        state.SkipWithError("area selection benchmark fixture produced the wrong result");
        return;
    }

    for (auto _ : state) {
        auto selected = viewer::select_area_object(ray, data.records, data.scene);
        benchmark::DoNotOptimize(selected);
    }

    state.counters["records"] = static_cast<double>(data.records.stats().record_count);
    state.counters["selectable_records"] = static_cast<double>(state.range(1));
    state.counters["object_handle_bytes"] = static_cast<double>(data.records.stats().object_handle_bytes);
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK_CAPTURE(BM_area_object_selection, hit, true)
    ->Args({5, 1})
    ->Args({256, 80})
    ->Args({573, 61});
BENCHMARK_CAPTURE(BM_area_object_selection, miss, false)
    ->Args({5, 1})
    ->Args({256, 80})
    ->Args({573, 61});

struct AreaSurfaceInstanceBenchmarkData {
    static constexpr uint32_t width = 32u;
    static constexpr uint32_t height = 32u;
    static constexpr uint32_t triangles_per_geometry = 413u;

    AreaSurfaceInstanceBenchmarkData()
    {
        const viewer::AreaSurfaceTriangle triangle{
            .v0 = {0.0f, 0.0f, 0.0f},
            .v1 = {10.0f, 0.0f, 0.0f},
            .v2 = {0.0f, 10.0f, 0.0f},
        };
        triangles.assign(triangles_per_geometry, triangle);
        geometries.push_back({
            .bounds = {
                .min = {0.0f, 0.0f, 0.0f},
                .max = {10.0f, 10.0f, 0.0f},
            },
            .first_triangle = 0u,
            .triangle_count = triangles_per_geometry,
        });

        const uint32_t tile_count = width * height;
        instances.resize(tile_count);
        initial_updates.reserve(tile_count);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const uint32_t model_index = y * width + x;
                const glm::vec3 translation{
                    static_cast<float>(x) * 10.0f,
                    static_cast<float>(y) * 10.0f,
                    0.0f,
                };
                glm::mat4 root{1.0f};
                root[3] = glm::vec4{translation, 1.0f};
                initial_updates.push_back({
                    .bounds = {
                        .min = translation,
                        .max = translation
                            + glm::vec3{10.0f, 10.0f, 1.0f},
                    },
                    .root = root,
                    .model_index = model_index,
                    .geometry_index = 0u,
                });
            }
        }
        initialized = viewer::update_area_surface_instances(
            initial_updates, 1u, instances);
    }

    std::vector<viewer::AreaSurfaceRange> geometries;
    std::vector<viewer::AreaSurfaceTriangle> triangles;
    std::vector<viewer::AreaSurfaceInstance> instances;
    std::vector<viewer::AreaSurfaceInstanceUpdate> initial_updates;
    bool initialized = false;
};

void BM_area_surface_instance_update_32x32_413(
    benchmark::State& state)
{
    AreaSurfaceInstanceBenchmarkData data;
    const uint32_t changed_count
        = static_cast<uint32_t>(state.range(0));
    if (!data.initialized
        || changed_count > data.initial_updates.size()) {
        state.SkipWithError(
            "failed to create shared surface-instance benchmark data");
        return;
    }
    std::vector<viewer::AreaSurfaceInstanceUpdate> updates(
        data.initial_updates.begin(),
        data.initial_updates.begin() + changed_count);
    bool raised = true;
    for (auto _ : state) {
        const float z = raised ? 1.0f : 0.0f;
        raised = !raised;
        for (auto& update : updates) {
            update.root[3].z = z;
            update.bounds.min.z = z;
            update.bounds.max.z = z + 1.0f;
        }
        const bool updated = viewer::update_area_surface_instances(
            updates, 1u, data.instances);
        benchmark::DoNotOptimize(data.instances.data());
        if (!updated) {
            state.SkipWithError(
                "shared surface-instance update rejected a valid batch");
            break;
        }
    }
    state.counters["changed_tiles"]
        = static_cast<double>(changed_count);
    state.counters["shared_triangles"]
        = static_cast<double>(data.triangles.size());
    state.SetItemsProcessed(state.iterations() * changed_count);
}

BENCHMARK(BM_area_surface_instance_update_32x32_413)
    ->Arg(1)
    ->Arg(64)
    ->Unit(benchmark::kMicrosecond);

void BM_area_surface_instance_trace_32x32_413(
    benchmark::State& state, bool hit)
{
    AreaSurfaceInstanceBenchmarkData data;
    if (!data.initialized) {
        state.SkipWithError(
            "failed to create shared surface-instance benchmark data");
        return;
    }
    const viewer::ViewerRay ray{
        .origin = hit
            ? glm::vec3{162.5f, 162.5f, 100.0f}
            : glm::vec3{-5.0f, -5.0f, 100.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    std::array<viewer::AreaSurfaceHit, 1> hits;
    const auto rays = std::span<const viewer::ViewerRay>{&ray, 1u};
    const auto expected = hit
        ? viewer::AreaSurfaceHitStatus::hit
        : viewer::AreaSurfaceHitStatus::miss;
    viewer::trace_area_surface_instances(
        rays, data.geometries, data.triangles,
        data.instances, hits);
    if (hits[0].status != expected) {
        state.SkipWithError(
            "shared surface-instance trace fixture produced the wrong result");
        return;
    }

    for (auto _ : state) {
        viewer::trace_area_surface_instances(
            rays, data.geometries, data.triangles,
            data.instances, hits);
        benchmark::DoNotOptimize(hits);
    }
    state.counters["instances"]
        = static_cast<double>(data.instances.size());
    state.counters["shared_triangles"]
        = static_cast<double>(data.triangles.size());
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_CAPTURE(BM_area_surface_instance_trace_32x32_413,
    hit, true)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_area_surface_instance_trace_32x32_413,
    miss, false)
    ->Unit(benchmark::kMicrosecond);

struct AreaObjectSpatialUpdateBenchmarkData {
    explicit AreaObjectSpatialUpdateBenchmarkData(int64_t model_count)
    {
        auto* creature = nw::kernel::objects().make<nw::Creature>();
        if (!creature) {
            return;
        }
        spatial.owner = creature->handle();

        const size_t count = static_cast<size_t>(std::max<int64_t>(model_count, 0));
        for (size_t index = 0; index < count; ++index) {
            auto model = std::make_unique<nw::render::RenderModel>();
            model->bounds = {
                .min = {-1.0f, -1.0f, -1.0f},
                .max = {1.0f, 1.0f, 1.0f},
            };
            scene.add(std::move(model));
            if (index + 1 == count) {
                scene.static_area_model_info.back().object = spatial.owner;
            }
        }
    }

    ~AreaObjectSpatialUpdateBenchmarkData()
    {
        if (nw::kernel::objects().valid(spatial.owner)) {
            nw::kernel::objects().destroy(spatial.owner);
        }
    }

    viewer::PreviewScene scene;
    nw::ObjectSpatialState spatial;
};

void BM_area_object_spatial_update(benchmark::State& state)
{
    AreaObjectSpatialUpdateBenchmarkData data{state.range(0)};
    if (!nw::kernel::objects().valid(data.spatial.owner)) {
        state.SkipWithError("failed to create spatial update benchmark object");
        return;
    }
    data.scene.rebuild_runtime_update_indices();

    for (auto _ : state) {
        data.spatial.position.x += 0.001f;
        const std::array rows{data.spatial};
        auto stats = viewer::update_area_object_spatial_states(data.scene, rows);
        benchmark::DoNotOptimize(stats.render_model_root_count);
    }

    state.counters["models"] = static_cast<double>(data.scene.static_models.size());
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_area_object_spatial_update)
    ->Arg(16)
    ->Arg(256)
    ->Arg(573);

struct AreaDynamicRecordBenchmarkData {
    explicit AreaDynamicRecordBenchmarkData(bool include_lights = false)
    {
        constexpr int32_t width = 32;
        constexpr int32_t height = 32;
        constexpr uint32_t primitive_count = 16;
        tile_model = make_area_dynamic_record_benchmark_model(
            primitive_count);
        if (!tile_model) {
            return;
        }

        scene.is_area = true;
        scene.area_width = width;
        scene.area_height = height;
        for (int32_t y = 0; y < height; ++y) {
            for (int32_t x = 0; x < width; ++x) {
                scene.add(tile_model);
                const uint32_t model_index
                    = static_cast<uint32_t>(scene.static_models.size() - 1u);
                auto* instance = scene.static_model_instance(model_index);
                if (!instance) {
                    return;
                }
                const glm::vec3 translation{
                    static_cast<float>(x) * 10.0f,
                    static_cast<float>(y) * 10.0f,
                    0.0f,
                };
                instance->root_transform[3]
                    = glm::vec4{translation, 1.0f};
                nw::render::publish_render_model_static_node_world_transforms(
                    *instance, *tile_model);
                instance->current_bounds = {
                    .min = tile_model->bounds.min + translation,
                    .max = tile_model->bounds.max + translation,
                };
                instance->scene_animation_enabled = false;
                scene.static_area_model_info[model_index] = {
                    .kind = nw::ObjectType::tile,
                    .tile_x = static_cast<int16_t>(x),
                    .tile_y = static_cast<int16_t>(y),
                    .static_candidate = true,
                };
            }
        }
        if (include_lights) {
            scene.local_lights.reserve(
                static_cast<size_t>(width) * height * 2u);
            scene.render_local_lights.reserve(
                static_cast<size_t>(width) * height * 2u);
            for (int32_t y = 0; y < height; ++y) {
                for (int32_t x = 0; x < width; ++x) {
                    const uint32_t model_index = static_cast<uint32_t>(
                        static_cast<size_t>(y) * width + x);
                    const glm::vec3 center{
                        static_cast<float>(x) * 10.0f + 5.0f,
                        static_cast<float>(y) * 10.0f + 5.0f,
                        2.0f,
                    };
                    scene.local_lights.push_back({
                        .position = center,
                        .radius = 8.0f,
                        .source = viewer::SceneLocalLightSource::tile_model,
                        .model_index = model_index,
                    });
                    scene.render_local_lights.push_back({
                        .position = center,
                        .radius = 8.0f,
                    });
                    scene.local_lights.push_back({
                        .position = center
                            + glm::vec3{2.5f, 2.5f, 0.0f},
                        .radius = 6.0f,
                        .source = viewer::SceneLocalLightSource::tile_model,
                        .model_index = model_index,
                    });
                    scene.render_local_lights.push_back({
                        .position = center + glm::vec3{2.5f, 2.5f, 0.0f},
                        .radius = 6.0f,
                    });
                }
            }
        }
        records.rebuild(scene);

        auto dynamic_model = std::make_unique<nw::render::RenderModel>();
        dynamic_model->bounds = {
            .min = {-1.0f, -1.0f, 0.0f},
            .max = {1.0f, 1.0f, 2.0f},
        };
        scene.add(std::move(dynamic_model));
        dynamic_model_index = static_cast<uint32_t>(
            scene.static_models.size() - 1u);
        scene.static_area_model_info[dynamic_model_index].object = nw::ObjectHandle{
            .id = static_cast<nw::ObjectID>(1u),
            .type = nw::ObjectType::tile,
            .version = 0u,
        };
        initialized = records.rebuild_dynamic_records(scene, false)
            && records.stats().record_count == scene.static_models.size();
    }

    viewer::PreviewScene scene;
    viewer::AreaRenderScene records;
    std::shared_ptr<nw::render::RenderModel> tile_model;
    uint32_t dynamic_model_index = nw::render::kInvalidModelInstanceIndex;
    bool initialized = false;
};

void BM_area_dynamic_record_rebuild_32x32(benchmark::State& state)
{
    AreaDynamicRecordBenchmarkData data;
    if (!data.initialized) {
        state.SkipWithError(
            "failed to create 32x32 dynamic-record benchmark scene");
        return;
    }
    auto* instance
        = data.scene.static_model_instance(data.dynamic_model_index);
    if (!instance) {
        state.SkipWithError("dynamic-record benchmark model is unavailable");
        return;
    }

    for (auto _ : state) {
        instance->root_transform[3].x += 0.001f;
        instance->current_bounds.min.x += 0.001f;
        instance->current_bounds.max.x += 0.001f;
        const bool rebuilt
            = data.records.rebuild_dynamic_records(data.scene, false);
        if (!rebuilt) {
            state.SkipWithError("dynamic-record rebuild rejected a valid scene");
            break;
        }
    }

    state.counters["records"]
        = static_cast<double>(data.records.stats().record_count);
    state.counters["static_primitives"] = 32.0 * 32.0 * 16.0;
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_area_dynamic_record_rebuild_32x32)
    ->Unit(benchmark::kMicrosecond);

void BM_area_tile_preview_record_refresh_32x32(benchmark::State& state)
{
    AreaDynamicRecordBenchmarkData data;
    if (!data.initialized) {
        state.SkipWithError(
            "failed to create 32x32 tile-preview benchmark scene");
        return;
    }
    auto* instance
        = data.scene.static_model_instance(data.dynamic_model_index);
    if (!instance) {
        state.SkipWithError("tile-preview benchmark model is unavailable");
        return;
    }
    const std::array model_indices{data.dynamic_model_index};
    const uint64_t static_cache_generation
        = data.records.static_cache_generation();

    for (auto _ : state) {
        instance->root_transform[3].x += 0.001f;
        instance->current_bounds.min.x += 0.001f;
        instance->current_bounds.max.x += 0.001f;
        const bool refreshed = data.records.refresh_tile_preview_records(
            data.scene, model_indices);
        data.records.refresh_runtime_records(data.scene);
        if (!refreshed
            || data.records.static_cache_generation()
                != static_cache_generation) {
            state.SkipWithError(
                "tile-preview refresh rebuilt or rejected the static cache");
            break;
        }
    }

    state.counters["records"]
        = static_cast<double>(data.records.stats().record_count);
    state.counters["preview_records"] = 1.0;
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_area_tile_preview_record_refresh_32x32)
    ->Unit(benchmark::kMicrosecond);

void BM_area_light_index_rebuild_32x32_2048(benchmark::State& state)
{
    AreaDynamicRecordBenchmarkData data{true};
    if (!data.initialized) {
        state.SkipWithError(
            "failed to create 32x32 light-index benchmark scene");
        return;
    }

    for (auto _ : state) {
        const bool rebuilt
            = data.records.rebuild_dynamic_records(data.scene, true);
        if (!rebuilt) {
            state.SkipWithError("light-index rebuild rejected a valid scene");
            break;
        }
    }

    state.counters["lights"]
        = static_cast<double>(data.scene.render_local_lights.size());
    state.counters["records"]
        = static_cast<double>(data.records.stats().record_count);
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_area_light_index_rebuild_32x32_2048)
    ->Unit(benchmark::kMicrosecond);

void BM_area_tile_record_rebuild_32x32_no_lights(
    benchmark::State& state)
{
    AreaDynamicRecordBenchmarkData data;
    if (!data.initialized) {
        state.SkipWithError(
            "failed to create no-light tile-record benchmark scene");
        return;
    }
    const size_t changed_count
        = static_cast<size_t>(state.range(0));
    std::vector<uint32_t> changed_model_indices(changed_count);
    for (uint32_t index = 0; index < changed_count; ++index) {
        changed_model_indices[index] = index;
    }

    for (auto _ : state) {
        const bool rebuilt = data.records.rebuild_tile_records(
            data.scene, changed_model_indices);
        if (!rebuilt) {
            state.SkipWithError(
                "no-light tile-record rebuild rejected a valid scene");
            break;
        }
    }

    state.counters["changed_tiles"]
        = static_cast<double>(changed_count);
    state.counters["records"]
        = static_cast<double>(data.records.stats().record_count);
    state.counters["static_primitives"] = 32.0 * 32.0 * 16.0;
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_area_tile_record_rebuild_32x32_no_lights)
    ->Arg(1)
    ->Arg(64)
    ->Unit(benchmark::kMicrosecond);

void BM_area_tile_record_rebuild_32x32_2048(
    benchmark::State& state)
{
    AreaDynamicRecordBenchmarkData data{true};
    if (!data.initialized) {
        state.SkipWithError(
            "failed to create 32x32 tile-record benchmark scene");
        return;
    }
    const size_t changed_count
        = static_cast<size_t>(state.range(0));
    std::vector<uint32_t> changed_model_indices(changed_count);
    for (uint32_t index = 0; index < changed_count; ++index) {
        changed_model_indices[index] = index;
    }

    for (auto _ : state) {
        const bool rebuilt = data.records.rebuild_tile_records(
            data.scene, changed_model_indices);
        if (!rebuilt) {
            state.SkipWithError(
                "tile-record rebuild rejected a valid scene");
            break;
        }
    }

    state.counters["changed_tiles"]
        = static_cast<double>(changed_count);
    state.counters["lights"]
        = static_cast<double>(data.scene.render_local_lights.size());
    state.counters["records"]
        = static_cast<double>(data.records.stats().record_count);
    state.counters["static_primitives"] = 32.0 * 32.0 * 16.0;
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_area_tile_record_rebuild_32x32_2048)
    ->Arg(1)
    ->Arg(64)
    ->Unit(benchmark::kMicrosecond);

void BM_area_tile_record_row_count_fallback_32x32_2048(
    benchmark::State& state)
{
    AreaDynamicRecordBenchmarkData data{true};
    if (!data.initialized) {
        state.SkipWithError(
            "failed to create tile-record fallback benchmark scene");
        return;
    }
    const size_t changed_count
        = static_cast<size_t>(state.range(0));
    std::vector<uint32_t> changed_model_indices(changed_count);
    for (uint32_t index = 0; index < changed_count; ++index) {
        changed_model_indices[index] = index;
    }
    auto alternate_model
        = make_area_dynamic_record_benchmark_model(17u);
    if (!alternate_model) {
        state.SkipWithError(
            "failed to create alternate tile-record benchmark model");
        return;
    }

    bool use_alternate = true;
    for (auto _ : state) {
        const auto& model
            = use_alternate ? alternate_model : data.tile_model;
        use_alternate = !use_alternate;
        for (const uint32_t model_index :
            changed_model_indices) {
            data.scene.static_models[model_index] = model;
        }
        const bool rebuilt = data.records.rebuild_tile_records(
            data.scene, changed_model_indices);
        if (!rebuilt) {
            state.SkipWithError(
                "tile-record row-count fallback rejected a valid scene");
            break;
        }
    }

    state.counters["changed_tiles"]
        = static_cast<double>(changed_count);
    state.counters["lights"]
        = static_cast<double>(data.scene.render_local_lights.size());
    state.counters["records"]
        = static_cast<double>(data.records.stats().record_count);
    state.counters["static_primitives"] = 32.0 * 32.0 * 16.0;
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_area_tile_record_row_count_fallback_32x32_2048)
    ->Arg(1)
    ->Arg(64)
    ->Unit(benchmark::kMicrosecond);

void BM_area_tile_record_stable_light_refresh_32x32_2048(
    benchmark::State& state)
{
    AreaDynamicRecordBenchmarkData data{true};
    if (!data.initialized) {
        state.SkipWithError(
            "failed to create stable-light tile-record benchmark scene");
        return;
    }
    const size_t changed_count
        = static_cast<size_t>(state.range(0));
    std::vector<uint32_t> changed_model_indices(changed_count);
    std::vector<uint32_t> changed_light_indices(changed_count * 2u);
    for (uint32_t index = 0; index < changed_count; ++index) {
        changed_model_indices[index] = index;
        changed_light_indices[index * 2u] = index * 2u;
        changed_light_indices[index * 2u + 1u] = index * 2u + 1u;
    }

    bool move_positive = true;
    for (auto _ : state) {
        const float delta = move_positive ? 0.125f : -0.125f;
        move_positive = !move_positive;
        for (const uint32_t light_index : changed_light_indices) {
            data.scene.local_lights[light_index].position.x += delta;
            data.scene.render_local_lights[light_index].position.x += delta;
        }
        const bool rebuilt
            = data.records.rebuild_tile_records_with_stable_light_rows(
                data.scene, changed_model_indices, {},
                changed_light_indices);
        if (!rebuilt) {
            state.SkipWithError(
                "stable-light tile-record refresh rejected a valid scene");
            break;
        }
    }

    state.counters["changed_lights"]
        = static_cast<double>(changed_light_indices.size());
    state.counters["changed_tiles"]
        = static_cast<double>(changed_count);
    state.counters["lights"]
        = static_cast<double>(data.scene.render_local_lights.size());
    state.counters["records"]
        = static_cast<double>(data.records.stats().record_count);
    state.counters["static_primitives"] = 32.0 * 32.0 * 16.0;
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_area_tile_record_stable_light_refresh_32x32_2048)
    ->Arg(1)
    ->Arg(64)
    ->Unit(benchmark::kMicrosecond);

void BM_area_tile_light_row_refresh_32x32_2048(
    benchmark::State& state)
{
    AreaDynamicRecordBenchmarkData data{true};
    if (!data.initialized) {
        state.SkipWithError(
            "failed to create tile-light row benchmark scene");
        return;
    }
    const size_t changed_count
        = static_cast<size_t>(state.range(0));
    std::vector<uint32_t> changed_model_indices(changed_count);
    std::vector<nw::AreaTile> changed_tiles(changed_count);
    for (uint32_t index = 0; index < changed_count; ++index) {
        changed_model_indices[index] = index;
        changed_tiles[index].mainlight1 = 4;
        changed_tiles[index].srclight1 = 8;
    }

    std::vector<uint32_t> changed_light_indices;
    bool alternate = false;
    for (auto _ : state) {
        alternate = !alternate;
        for (auto& tile : changed_tiles) {
            tile.mainlight1 = alternate ? 4 : 12;
        }
        const auto refreshed = viewer::refresh_scene_tile_model_lights(
            data.scene, changed_model_indices, changed_tiles,
            changed_light_indices);
        benchmark::DoNotOptimize(changed_light_indices.data());
        if (refreshed
                != viewer::SceneTileLightRefreshStatus::stable_rows
            || changed_light_indices.size() != changed_count * 2u) {
            state.SkipWithError(
                "tile-light row refresh reindexed stable rows");
            break;
        }
    }

    state.counters["changed_lights"]
        = static_cast<double>(changed_count * 2u);
    state.counters["changed_tiles"]
        = static_cast<double>(changed_count);
    state.counters["lights"]
        = static_cast<double>(data.scene.local_lights.size());
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_area_tile_light_row_refresh_32x32_2048)
    ->Arg(1)
    ->Arg(64)
    ->Unit(benchmark::kMicrosecond);

void BM_area_tile_commit_cache_refresh_32x32_2048_instances(
    benchmark::State& state)
{
    AreaDynamicRecordBenchmarkData scene_data{true};
    const uint32_t changed_count
        = static_cast<uint32_t>(state.range(0));
    if (!scene_data.initialized) {
        state.SkipWithError(
            "failed to create tile-commit cache benchmark scene");
        return;
    }
    std::vector<nw::AreaTile> changed_tiles(changed_count);
    std::vector<uint32_t> changed_model_indices(changed_count);
    std::iota(changed_model_indices.begin(),
        changed_model_indices.end(), 0u);
    for (auto& tile : changed_tiles) {
        tile.mainlight1 = 4;
        tile.srclight1 = 8;
    }

    std::vector<uint32_t> changed_light_indices;
    bool move_positive = true;
    for (auto _ : state) {
        const float delta = move_positive ? 0.125f : -0.125f;
        move_positive = !move_positive;
        for (const uint32_t model_index : changed_model_indices) {
            auto* instance
                = scene_data.scene.static_model_instance(model_index);
            instance->root_transform[3].z += delta;
            instance->current_bounds.min.z += delta;
            instance->current_bounds.max.z += delta;
            nw::render::publish_render_model_static_node_world_transforms(
                *instance, *scene_data.tile_model);
        }
        for (auto& tile : changed_tiles) {
            tile.mainlight1 = move_positive ? 4 : 12;
        }
        const auto light_refresh
            = viewer::refresh_scene_tile_model_lights(
                scene_data.scene,
                changed_model_indices,
                changed_tiles, changed_light_indices);
        const bool records_refreshed
            = light_refresh
                == viewer::SceneTileLightRefreshStatus::stable_rows
            && scene_data.records
                   .rebuild_tile_records_with_stable_light_rows(
                       scene_data.scene,
                       changed_model_indices,
                       {},
                       changed_light_indices);
        benchmark::DoNotOptimize(
            scene_data.records.surface_instances().data());
        if (!records_refreshed
            || scene_data.records.stats().surface_instance_count != 1024u) {
            state.SkipWithError(
                "tile-commit cache refresh rejected a valid batch");
            break;
        }
    }

    state.counters["changed_lights"]
        = static_cast<double>(changed_count * 2u);
    state.counters["changed_tiles"]
        = static_cast<double>(changed_count);
    state.counters["lights"] = 2048.0;
    state.counters["records"] = 1025.0;
    state.counters["surface_instances"] = 1024.0;
    state.counters["surface_triangles"]
        = static_cast<double>(
            scene_data.records.stats().surface_triangle_count);
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_area_tile_commit_cache_refresh_32x32_2048_instances)
    ->Arg(1)
    ->Arg(64)
    ->Unit(benchmark::kMicrosecond);

} // namespace
