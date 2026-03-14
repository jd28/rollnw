#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/propset_gff_importer.hpp>
#include <nw/profiles/nwn1/propset_gff_policy.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <benchmark/benchmark.h>

namespace nwk = nw::kernel;

namespace {

nw::smalls::Script* ensure_propset_bench_module()
{
    static nw::smalls::Script* script = nullptr;
    if (script) {
        return script;
    }

    constexpr const char* k_source = R"(
        [[intrinsic("get_propset")]] fn get_propset(obj: object): $T;

        [[propset]]
        type BenchStats {
            str: int;
            dex: int;
            con: int;
        };

        fn touch(target: Creature): int {
            var s = get_propset!(BenchStats)(target);
            if (s.str == 0) {
                s.str = 10;
                s.dex = 12;
                s.con = 14;
            }
            return s.str;
        }

        fn read_loop(target: Creature, iters: int): int {
            var s = get_propset!(BenchStats)(target);
            var sum = 0;
            var i = 0;
            for (i < iters) {
                sum = sum + s.str + s.dex + s.con;
                i = i + 1;
            }
            return sum;
        }

        fn write_loop(target: Creature, iters: int): int {
            var s = get_propset!(BenchStats)(target);
            var i = 0;
            for (i < iters) {
                s.str = s.str + 1;
                s.dex = s.dex + 1;
                i = i + 1;
            }
            return s.str;
        }
    )";

    script = nw::kernel::runtime().load_module_from_source("bench.propset", k_source);
    return script;
}

nw::smalls::Value as_creature_value(nw::Creature* creature)
{
    auto& rt = nw::kernel::runtime();
    auto v = nw::smalls::Value::make_object(creature->handle());
    v.type_id = rt.object_subtype_for_tag(creature->handle().type);
    return v;
}

} // namespace

static void BM_propset_alloc_free(benchmark::State& state)
{
    auto& rt = nwk::runtime();
    auto* script = ensure_propset_bench_module();
    if (!script || script->errors() != 0) {
        state.SkipWithError("failed to load bench.propset module");
        return;
    }

    for (auto _ : state) {
        auto* creature = nwk::objects().make<nw::Creature>();
        if (!creature) {
            state.SkipWithError("failed to allocate creature");
            return;
        }

        nw::Vector<nw::smalls::Value> args;
        args.push_back(as_creature_value(creature));
        auto res = rt.execute_script(script, "touch", args);
        benchmark::DoNotOptimize(res);
        nwk::objects().destroy(creature->handle());
    }
}
BENCHMARK(BM_propset_alloc_free);

static void BM_propset_script_field_access(benchmark::State& state)
{
    auto& rt = nwk::runtime();
    auto* script = ensure_propset_bench_module();
    if (!script || script->errors() != 0) {
        state.SkipWithError("failed to load bench.propset module");
        return;
    }

    auto* creature = nwk::objects().make<nw::Creature>();
    if (!creature) {
        state.SkipWithError("failed to allocate creature");
        return;
    }

    nw::Vector<nw::smalls::Value> args;
    constexpr int k_loop_iters = 1000;
    args.push_back(as_creature_value(creature));
    args.push_back(nw::smalls::Value::make_int(k_loop_iters));

    for (auto _ : state) {
        auto res = rt.execute_script(script, "read_loop", args);
        benchmark::DoNotOptimize(res);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(k_loop_iters) * 3);

    nwk::objects().destroy(creature->handle());
}
BENCHMARK(BM_propset_script_field_access);

static void BM_propset_field_write(benchmark::State& state)
{
    auto& rt = nwk::runtime();
    auto* script = ensure_propset_bench_module();
    if (!script || script->errors() != 0) {
        state.SkipWithError("failed to load bench.propset module");
        return;
    }

    auto* creature = nwk::objects().make<nw::Creature>();
    if (!creature) {
        state.SkipWithError("failed to allocate creature");
        return;
    }

    nw::Vector<nw::smalls::Value> args;
    constexpr int k_loop_iters = 1000;
    args.push_back(as_creature_value(creature));
    args.push_back(nw::smalls::Value::make_int(k_loop_iters));

    for (auto _ : state) {
        auto res = rt.execute_script(script, "write_loop", args);
        benchmark::DoNotOptimize(res);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(k_loop_iters) * 2);

    nwk::objects().destroy(creature->handle());
}
BENCHMARK(BM_propset_field_write);

// ---------------------------------------------------------------------------
// BM_propset_gff_import: GFF → propset import overhead (Phase 6 preview)
// ---------------------------------------------------------------------------

namespace {

struct PropsetImportBenchState {
    nwn1::PropsetGffPolicyRegistry registry;
    nwn1::PropsetGffImporter importer;
    nw::ResourceData gff_data;

    PropsetImportBenchState()
        : registry(nwn1::make_nwn1_propset_policy_registry())
        , importer(&nw::kernel::runtime(), &registry)
        , gff_data(nw::ResourceData::from_file("test_data/user/development/nw_chicken.utc"))
    {
    }
};

PropsetImportBenchState* ensure_propset_import_state()
{
    static PropsetImportBenchState* state = nullptr;
    if (state) { return state; }

    // Trigger compilation of propset types by loading a script that imports them.
    auto* script = nw::kernel::runtime().load_module_from_source(
        "bench.propset_import_init",
        "import core.creature as Cre;");
    if (!script || script->errors() != 0) { return nullptr; }

    auto rd = nw::ResourceData::from_file("test_data/user/development/nw_chicken.utc");
    nw::Gff probe(rd.copy());
    if (!probe.valid()) { return nullptr; }

    state = new PropsetImportBenchState();
    return state;
}

} // anonymous namespace

static void BM_propset_gff_import(benchmark::State& state)
{
    auto& rt = nwk::runtime();
    auto* s = ensure_propset_import_state();
    if (!s || !s->gff_data.bytes.size()) {
        state.SkipWithError("failed to set up propset import bench state");
        return;
    }

    for (auto _ : state) {
        auto* cre = nwk::objects().make<nw::Creature>();
        if (!cre) {
            state.SkipWithError("failed to allocate creature");
            return;
        }

        nw::ResourceData rd = s->gff_data.copy();
        nw::Gff gff(std::move(rd));
        if (gff.valid()) {
            nw::deserialize(cre, gff.toplevel(), nw::SerializationProfile::blueprint);
            rt.init_object_propsets(cre->handle());
            s->importer.import_creature(cre, gff.toplevel(),
                nw::SerializationProfile::blueprint);
        }

        benchmark::DoNotOptimize(cre);
        nwk::objects().destroy(cre->handle());
    }
}
BENCHMARK(BM_propset_gff_import);
