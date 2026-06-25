#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/nwn/model_renderer.hpp>

#include <benchmark/benchmark.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace {

namespace nwn = nw::render::nwn;

struct DummyBuffer {
    uint8_t value = 0;
};

nw::gfx::Handle<nw::gfx::Buffer> make_valid_buffer_handle()
{
    static nw::gfx::Pool<nw::gfx::Buffer, DummyBuffer> pool;
    return pool.insert(DummyBuffer{});
}

std::unique_ptr<nwn::ModelInstance> make_collect_benchmark_model(int mesh_count)
{
    auto model = std::make_unique<nwn::ModelInstance>();
    auto root = std::make_unique<nwn::Node>();
    root->has_transform_ = true;
    root->position_ = glm::vec3{0.0f};
    auto* root_ptr = root.get();
    model->nodes_.push_back(std::move(root));

    const auto vertex_buffer = make_valid_buffer_handle();
    const auto index_buffer = make_valid_buffer_handle();
    model->nodes_.reserve(static_cast<size_t>(mesh_count) + 1u);
    root_ptr->children_.reserve(static_cast<size_t>(mesh_count));

    for (int i = 0; i < mesh_count; ++i) {
        auto mesh = std::make_unique<nwn::Mesh>();
        mesh->parent_ = root_ptr;
        mesh->owner_ = model.get();
        mesh->has_transform_ = true;
        mesh->position_ = glm::vec3{
            static_cast<float>(i % 16) * 0.75f,
            static_cast<float>(i / 16) * 0.75f,
            static_cast<float>(i % 5) * 0.1f,
        };
        mesh->rotation_ = glm::angleAxis(static_cast<float>(i % 7) * 0.07f, glm::vec3{0.0f, 0.0f, 1.0f});
        mesh->scale_ = glm::vec3{
            1.0f + static_cast<float>(i % 3) * 0.15f,
            1.0f + static_cast<float>(i % 5) * 0.08f,
            1.0f + static_cast<float>(i % 7) * 0.04f,
        };
        mesh->vertices = vertex_buffer;
        mesh->indices = index_buffer;
        mesh->index_count = 36;
        mesh->owns_gpu_buffers = false;
        mesh->local_bounds = nw::render::Bounds{
            .min = {-0.5f, -0.5f, -0.5f},
            .max = {0.5f, 0.5f, 0.5f},
        };

        auto* mesh_ptr = mesh.get();
        root_ptr->children_.push_back(mesh_ptr);
        model->nodes_.push_back(std::move(mesh));
    }

    return model;
}

struct PreparedDrawBenchmarkData {
    std::vector<std::unique_ptr<nwn::Mesh>> meshes;
    std::vector<nwn::PreparedDrawItem> draws;
    std::vector<const nwn::PreparedDrawItem*> draw_pointers;
};

PreparedDrawBenchmarkData make_prepared_draw_benchmark_data(int draw_count, int group_size)
{
    PreparedDrawBenchmarkData data;
    data.meshes.reserve(static_cast<size_t>(draw_count));
    data.draws.reserve(static_cast<size_t>(draw_count));
    data.draw_pointers.reserve(static_cast<size_t>(draw_count));

    const auto vertex_buffer = make_valid_buffer_handle();
    const auto index_buffer = make_valid_buffer_handle();
    const int safe_group_size = std::max(1, group_size);

    for (int i = 0; i < draw_count; ++i) {
        const int group = i / safe_group_size;
        auto mesh = std::make_unique<nwn::Mesh>();
        mesh->vertices = vertex_buffer;
        mesh->indices = index_buffer;
        mesh->index_count = 36u + static_cast<uint32_t>(group % 4) * 6u;
        mesh->owns_gpu_buffers = false;
        mesh->material_mode = nw::render::MaterialMode::opaque;

        nwn::PreparedDrawItem draw{
            .mesh = mesh.get(),
            .world = glm::translate(glm::mat4{1.0f}, glm::vec3{
                static_cast<float>(i % 32),
                static_cast<float>(i / 32),
                0.0f,
            }),
            .normal_matrix = glm::mat4{1.0f},
            .model_root = glm::mat4{1.0f},
            .sort_position = glm::vec3{
                static_cast<float>(i % 32),
                static_cast<float>(i / 32),
                0.0f,
            },
            .static_area_geometry = nwn::PreparedDrawGeometry{
                .vertices = vertex_buffer,
                .indices = index_buffer,
                .index_count = mesh->index_count,
                .first_index = static_cast<uint32_t>(group) * 48u,
                .vertex_offset = group * 12,
            },
            .order = static_cast<size_t>(i),
            .static_material_index = static_cast<uint32_t>(i),
        };

        data.meshes.push_back(std::move(mesh));
        data.draws.push_back(draw);
    }

    for (const auto& draw : data.draws) {
        data.draw_pointers.push_back(&draw);
    }
    return data;
}

void run_collect_benchmark(benchmark::State& state, bool alternate_roots, bool mutate_one_node)
{
    const int mesh_count = static_cast<int>(state.range(0));
    auto model = make_collect_benchmark_model(mesh_count);
    std::vector<nwn::PreparedDrawItem> draws;
    draws.reserve(static_cast<size_t>(mesh_count));

    const glm::mat4 root_a = glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, 1.25f, 0.8f});
    const glm::mat4 root_b = glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, 5.0f, 0.5f})
        * glm::scale(glm::mat4{1.0f}, glm::vec3{0.8f, 1.4f, 1.1f});
    size_t mutation_index = 1;
    uint64_t iteration = 0;

    for (auto _ : state) {
        draws.clear();
        if (mutate_one_node && model->nodes_.size() > 1) {
            auto* node = model->nodes_[mutation_index].get();
            node->position_.z += 0.001f;
            mutation_index += 17;
            if (mutation_index >= model->nodes_.size()) {
                mutation_index = 1 + mutation_index % (model->nodes_.size() - 1u);
            }
        }

        const glm::mat4& root = alternate_roots && (iteration % 2 == 1) ? root_b : root_a;
        nwn::collect_prepared_draws(draws, *model, root);
        benchmark::DoNotOptimize(draws.data());
        benchmark::DoNotOptimize(draws.size());
        benchmark::ClobberMemory();
        ++iteration;
    }

    state.SetItemsProcessed(state.iterations() * mesh_count);
}

void run_prepared_indirect_benchmark(benchmark::State& state, int mode)
{
    const int draw_count = static_cast<int>(state.range(0));
    const int group_size = static_cast<int>(state.range(1));
    auto data = make_prepared_draw_benchmark_data(draw_count, group_size);
    std::vector<nw::gfx::IndexedIndirectDrawCommand> command_storage;
    command_storage.reserve(data.draw_pointers.size());
    nwn::PreparedIndirectDrawCommands commands;

    for (auto _ : state) {
        bool ok = false;
        switch (mode) {
        case 0:
            ok = nwn::build_prepared_indirect_draw_commands(
                command_storage,
                commands,
                std::span<const nwn::PreparedDrawItem* const>{data.draw_pointers.data(), data.draw_pointers.size()},
                nw::render::MaterialMode::opaque);
            break;
        case 1:
            ok = nwn::build_prepared_static_material_indirect_draw_commands(
                command_storage,
                commands,
                std::span<const nwn::PreparedDrawItem* const>{data.draw_pointers.data(), data.draw_pointers.size()},
                nw::render::MaterialMode::opaque);
            break;
        default:
            ok = nwn::build_prepared_shadow_indirect_draw_commands(
                command_storage,
                commands,
                std::span<const nwn::PreparedDrawItem* const>{data.draw_pointers.data(), data.draw_pointers.size()});
            break;
        }
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(commands.command_count);
        benchmark::DoNotOptimize(commands.instance_count);
        benchmark::DoNotOptimize(command_storage.data());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * draw_count);
}

void run_render_prepared_overload_benchmark(benchmark::State& state, bool pointer_span)
{
    const int draw_count = static_cast<int>(state.range(0));
    auto data = make_prepared_draw_benchmark_data(draw_count, 4);
    nwn::ModelRenderContext render_ctx{};
    nw::render::RenderContext ctx{};
    nwn::PreparedDrawScratch scratch;
    scratch.reserve(data.draw_pointers.size());

    for (auto _ : state) {
        scratch.begin_frame();
        if (pointer_span) {
            nwn::render_prepared_model_draws(
                render_ctx,
                nullptr,
                std::span<const nwn::PreparedDrawItem* const>{data.draw_pointers.data(), data.draw_pointers.size()},
                ctx,
                scratch,
                nw::render::RenderPassSelection::all);
        } else {
            nwn::render_prepared_model_draws(
                render_ctx,
                nullptr,
                std::span<const nwn::PreparedDrawItem>{data.draws.data(), data.draws.size()},
                ctx,
                scratch,
                nw::render::RenderPassSelection::all);
        }
        benchmark::DoNotOptimize(scratch.transparent_draws.data());
        benchmark::DoNotOptimize(scratch.transparent_draws.size());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * draw_count);
}

static void BM_nwn_collect_prepared_draws_steady(benchmark::State& state)
{
    run_collect_benchmark(state, false, false);
}

static void BM_nwn_collect_prepared_draws_alternating_roots(benchmark::State& state)
{
    run_collect_benchmark(state, true, false);
}

static void BM_nwn_collect_prepared_draws_one_node_dirty(benchmark::State& state)
{
    run_collect_benchmark(state, false, true);
}

static void BM_nwn_prepared_indirect_draw_commands(benchmark::State& state)
{
    run_prepared_indirect_benchmark(state, 0);
}

static void BM_nwn_prepared_static_material_indirect_draw_commands(benchmark::State& state)
{
    run_prepared_indirect_benchmark(state, 1);
}

static void BM_nwn_prepared_shadow_indirect_draw_commands(benchmark::State& state)
{
    run_prepared_indirect_benchmark(state, 2);
}

static void BM_nwn_render_prepared_value_span_overload_null_context(benchmark::State& state)
{
    run_render_prepared_overload_benchmark(state, false);
}

static void BM_nwn_render_prepared_pointer_span_overload_null_context(benchmark::State& state)
{
    run_render_prepared_overload_benchmark(state, true);
}

BENCHMARK(BM_nwn_collect_prepared_draws_steady)->Arg(128)->Arg(512);
BENCHMARK(BM_nwn_collect_prepared_draws_alternating_roots)->Arg(128)->Arg(512);
BENCHMARK(BM_nwn_collect_prepared_draws_one_node_dirty)->Arg(128)->Arg(512);
BENCHMARK(BM_nwn_prepared_indirect_draw_commands)
    ->Args({512, 1})
    ->Args({512, 4})
    ->Args({2048, 4});
BENCHMARK(BM_nwn_prepared_static_material_indirect_draw_commands)
    ->Args({512, 1})
    ->Args({512, 4})
    ->Args({2048, 4});
BENCHMARK(BM_nwn_prepared_shadow_indirect_draw_commands)
    ->Args({512, 1})
    ->Args({512, 4})
    ->Args({2048, 4});
BENCHMARK(BM_nwn_render_prepared_value_span_overload_null_context)->Arg(512)->Arg(2048);
BENCHMARK(BM_nwn_render_prepared_pointer_span_overload_null_context)->Arg(512)->Arg(2048);

} // namespace
