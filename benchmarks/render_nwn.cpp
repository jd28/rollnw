#include <nw/log.hpp>
#include <nw/render/nwn/render_asset_cache.hpp>

#include <benchmark/benchmark.h>

#include <string>
#include <vector>

namespace {

struct RenderAssetCacheBenchmarkData {
    explicit RenderAssetCacheBenchmarkData(int texture_count)
        : cache{nullptr}
    {
        names.reserve(static_cast<size_t>(texture_count));
        resrefs.reserve(static_cast<size_t>(texture_count));
        for (int i = 0; i < texture_count; ++i) {
            const std::string suffix = std::to_string(i);
            std::string name{"__rcache_"};
            name.append(16u - name.size() - suffix.size(), '0');
            name += suffix;
            names.push_back(std::move(name));
            resrefs.emplace_back(names.back());
        }

        const auto previous_verbosity = loguru::g_stderr_verbosity;
        loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
        for (nw::Resref resref : resrefs) {
            cache.get_or_load_texture(resref, false, {});
        }
        loguru::g_stderr_verbosity = previous_verbosity;
    }

    std::vector<std::string> names;
    std::vector<nw::Resref> resrefs;
    nw::render::nwn::RenderAssetCache cache;
};

void run_render_asset_cache_hot_hits(benchmark::State& state, bool convert_string_boundary)
{
    const int texture_count = static_cast<int>(state.range(0));
    if (texture_count <= 0) {
        state.SkipWithError("texture count must be positive");
        return;
    }

    RenderAssetCacheBenchmarkData data{texture_count};
    if (data.cache.stats().texture_count != static_cast<size_t>(texture_count)) {
        state.SkipWithError("failed to seed render asset cache");
        return;
    }

    if (convert_string_boundary) {
        for (auto _ : state) {
            for (const auto& name : data.names) {
                auto texture = data.cache.get_or_load_texture(nw::Resref{name}, false, {});
                benchmark::DoNotOptimize(texture);
            }
        }
    } else {
        for (auto _ : state) {
            for (nw::Resref resref : data.resrefs) {
                auto texture = data.cache.get_or_load_texture(resref, false, {});
                benchmark::DoNotOptimize(texture);
            }
        }
    }

    state.SetItemsProcessed(state.iterations() * texture_count);
}

static void BM_render_asset_cache_hot_texture_hits_resref(benchmark::State& state)
{
    run_render_asset_cache_hot_hits(state, false);
}

static void BM_render_asset_cache_hot_texture_hits_string_boundary(benchmark::State& state)
{
    run_render_asset_cache_hot_hits(state, true);
}

BENCHMARK(BM_render_asset_cache_hot_texture_hits_resref)->Arg(1)->Arg(3)->Arg(85)->Arg(137);
BENCHMARK(BM_render_asset_cache_hot_texture_hits_string_boundary)->Arg(1)->Arg(3)->Arg(85)->Arg(137);

} // namespace
