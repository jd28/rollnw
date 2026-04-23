#include <nw/render/particle_compile.hpp>
#include <nw/render/particle_system.hpp>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

using namespace nw::render;

constexpr float kParticleBenchmarkDt = 1.0f / 60.0f;
constexpr float kParticleSeedDt = 1.0e-6f;

struct ParticleBenchmarkTemplate {
    CompiledParticleEffect effect;
    ParticleSystemInstance system;
    uint32_t live_particles = 0;
};

ParticleBenchmarkTemplate make_benchmark_template(CompiledParticleEffect effect, ParticleSystemInstance system, uint32_t live_particles)
{
    ParticleBenchmarkTemplate result{
        .effect = std::move(effect),
        .system = std::move(system),
        .live_particles = live_particles,
    };
    result.system.effect = &result.effect;
    return result;
}

ParticleEmitterDef make_benchmark_emitter(uint32_t max_particles)
{
    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::event_burst;
    emitter.emission.rate = static_cast<float>(max_particles);
    emitter.initial.lifetime = {4.0f, 4.0f};
    emitter.initial.speed = {2.0f, 2.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.initial.color = {1.0f, 1.0f, 1.0f, 1.0f};
    emitter.max_particles = max_particles;
    return emitter;
}

void seed_burst_particles(ParticleSystemInstance& system, uint16_t emitter_id)
{
    trigger_particle_emitter(system, emitter_id, 1);
    tick_particle_system(system, kParticleSeedDt);

    auto& core = system.particles.core;
    for (size_t i = 0; i < core.age.size(); ++i) {
        if (core.emitter_id[i] != emitter_id) { continue; }
        core.age[i] = 0.0f;
        core.normalized_age[i] = 0.0f;
    }
}

ParticleBenchmarkTemplate make_single_emitter_template(uint32_t live_particles)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    auto emitter = make_benchmark_emitter(live_particles);
    emitter.over_life.size_x.keys = {{0.0f, 1.0f}, {1.0f, 1.5f}};
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    seed_burst_particles(system, 0);

    return make_benchmark_template(std::move(compiled), std::move(system), live_particles);
}

ParticleBenchmarkTemplate make_mixed_emitters_template(uint32_t live_particles)
{
    const uint32_t emitter_count = 5;
    const uint32_t base_share = live_particles / emitter_count;
    const uint32_t remainder = live_particles % emitter_count;

    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});
    ParticleMaterialDef mesh_material;
    mesh_material.mesh = "PLC_Chunk_W01";
    effect.materials.push_back(mesh_material);

    auto basic = make_benchmark_emitter(base_share + remainder);
    basic.render.material = 0;
    basic.over_life.size_x.keys = {{0.0f, 1.0f}, {1.0f, 1.5f}};
    effect.emitters.push_back(basic);

    auto gravity = make_benchmark_emitter(base_share);
    gravity.render.material = 0;
    gravity.targeting.mode = ParticleTargetingMode::point_gravity;
    gravity.targeting.gravity = 4.0f;
    effect.emitters.push_back(gravity);

    auto bezier = make_benchmark_emitter(base_share);
    bezier.render.material = 0;
    bezier.targeting.mode = ParticleTargetingMode::point_bezier;
    bezier.targeting.transition_factor = 1.0f;
    effect.emitters.push_back(bezier);

    auto chain = make_benchmark_emitter(base_share);
    chain.render.material = 0;
    chain.render.mode = ParticleRenderMode::linked_chain;
    chain.targeting.mode = ParticleTargetingMode::point_bezier;
    chain.targeting.transition_factor = 1.0f;
    effect.emitters.push_back(chain);

    auto mesh = make_benchmark_emitter(base_share);
    mesh.render.material = 1;
    effect.emitters.push_back(mesh);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[1].target_point = {0.0f, 0.0f, 4.0f};
    system.emitters[2].target_point = {0.0f, 0.0f, 4.0f};
    system.emitters[3].target_point = {0.0f, 0.0f, 4.0f};

    for (uint16_t emitter_id = 0; emitter_id < emitter_count; ++emitter_id) {
        seed_burst_particles(system, emitter_id);
    }

    return make_benchmark_template(std::move(compiled), std::move(system), live_particles);
}

ParticleBenchmarkTemplate make_high_churn_template(uint32_t live_particles)
{
    auto result = make_single_emitter_template(live_particles);
    auto& core = result.system.particles.core;
    for (size_t i = 0; i < core.age.size(); ++i) {
        if ((i & 1u) == 0u) {
            core.age[i] = std::max(0.0f, core.lifetime[i] - 0.5f * kParticleBenchmarkDt);
        } else {
            core.age[i] = 0.25f * core.lifetime[i];
        }
        core.normalized_age[i] = core.lifetime[i] > 0.0f ? core.age[i] / core.lifetime[i] : 0.0f;
    }
    return result;
}

ParticleBenchmarkTemplate make_burst_spawn_template(uint32_t live_particles)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});
    effect.emitters.push_back(make_benchmark_emitter(live_particles));

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    return make_benchmark_template(std::move(compiled), std::move(system), live_particles);
}

void run_particle_tick_benchmark(benchmark::State& state, const ParticleBenchmarkTemplate& fixture)
{
    ParticleSystemInstance system = fixture.system;
    for (auto _ : state) {
        state.PauseTiming();
        system = fixture.system;
        state.ResumeTiming();

        tick_particle_system(system, kParticleBenchmarkDt);
        benchmark::DoNotOptimize(system.particles.core.age.data());
        benchmark::DoNotOptimize(system.particles.core.position.data());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * fixture.live_particles);
    state.counters["live_particles"] = static_cast<double>(fixture.live_particles);
}

void run_particle_render_packet_benchmark(benchmark::State& state, const ParticleBenchmarkTemplate& fixture)
{
    ParticleSystemInstance system = fixture.system;
    for (auto _ : state) {
        state.PauseTiming();
        system = fixture.system;
        state.ResumeTiming();

        const auto packets = build_particle_render_packets(system);
        benchmark::DoNotOptimize(packets.data());
        benchmark::DoNotOptimize(packets.size());
        benchmark::DoNotOptimize(system.particles.core.position.data());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * fixture.live_particles);
    state.counters["live_particles"] = static_cast<double>(fixture.live_particles);
}

void run_particle_burst_spawn_benchmark(benchmark::State& state, const ParticleBenchmarkTemplate& fixture)
{
    ParticleSystemInstance system = fixture.system;
    for (auto _ : state) {
        state.PauseTiming();
        system = fixture.system;
        trigger_particle_emitter(system, 0, 1);
        state.ResumeTiming();

        tick_particle_system(system, kParticleBenchmarkDt);
        benchmark::DoNotOptimize(system.particles.core.position.data());
        benchmark::DoNotOptimize(system.particles.core.age.size());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * fixture.live_particles);
    state.counters["spawned_particles"] = static_cast<double>(fixture.live_particles);
}

static void BM_particles_tick_single_emitter(benchmark::State& state)
{
    const auto fixture = make_single_emitter_template(static_cast<uint32_t>(state.range(0)));
    run_particle_tick_benchmark(state, fixture);
}
BENCHMARK(BM_particles_tick_single_emitter)->Arg(1024)->Arg(5120)->Arg(10240);

static void BM_particles_tick_mixed_emitters(benchmark::State& state)
{
    const auto fixture = make_mixed_emitters_template(static_cast<uint32_t>(state.range(0)));
    run_particle_tick_benchmark(state, fixture);
}
BENCHMARK(BM_particles_tick_mixed_emitters)->Arg(1024)->Arg(5120)->Arg(10240);

static void BM_particles_tick_high_churn(benchmark::State& state)
{
    const auto fixture = make_high_churn_template(static_cast<uint32_t>(state.range(0)));
    run_particle_tick_benchmark(state, fixture);
}
BENCHMARK(BM_particles_tick_high_churn)->Arg(1024)->Arg(5120)->Arg(10240);

static void BM_particles_render_packets_single_emitter(benchmark::State& state)
{
    const auto fixture = make_single_emitter_template(static_cast<uint32_t>(state.range(0)));
    run_particle_render_packet_benchmark(state, fixture);
}
BENCHMARK(BM_particles_render_packets_single_emitter)->Arg(1024)->Arg(5120)->Arg(10240);

static void BM_particles_render_packets_mixed_emitters(benchmark::State& state)
{
    const auto fixture = make_mixed_emitters_template(static_cast<uint32_t>(state.range(0)));
    run_particle_render_packet_benchmark(state, fixture);
}
BENCHMARK(BM_particles_render_packets_mixed_emitters)->Arg(1024)->Arg(5120)->Arg(10240);

static void BM_particles_burst_spawn(benchmark::State& state)
{
    const auto fixture = make_burst_spawn_template(static_cast<uint32_t>(state.range(0)));
    run_particle_burst_spawn_benchmark(state, fixture);
}
BENCHMARK(BM_particles_burst_spawn)->Arg(1024)->Arg(5120)->Arg(10240);

} // namespace
