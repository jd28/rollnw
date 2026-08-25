#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/render/model_asset.hpp>
#include <nw/render/viewer/area_render_scene.hpp>
#include <nw/render/viewer/preview_scene.hpp>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
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
                ? viewer::AreaRenderRecordKind::creature
                : viewer::AreaRenderRecordKind::tile;
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

} // namespace
