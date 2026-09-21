#include "area_edit_benchmark.hpp"

#include "app_runtime.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/render/area_tile_grid.hpp>
#include <nw/render/viewer/preview_scene.hpp>
#include <nw/render/viewer/session.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mudl {
namespace {

using Clock = std::chrono::steady_clock;
using json = nlohmann::json;

enum class AreaEditBenchmarkKind : uint8_t {
    orientation,
    model_swap,
};

struct AreaEditBenchmarkSample {
    double edit_refresh_ms = 0.0;
    double edit_frame_submit_ms = 0.0;
    double edit_gpu_wait_ms = 0.0;
    double edit_presented_ms = 0.0;
    double edit_render_cpu_ms = 0.0;
    double edit_area_prepare_ms = 0.0;
    double edit_forward_plus_prepare_ms = 0.0;
    double edit_opaque_ms = 0.0;
    std::optional<double> edit_render_gpu_ms;
    uint64_t edit_draw_calls = 0u;
    uint64_t edit_draw_instances = 0u;
    bool edit_uses_cached_draw_lists = false;
    nw::render::viewer::AreaTileRefreshStats edit_refresh_stats;
    nw::render::viewer::AreaTileRecordRefreshStats
        edit_record_refresh_stats;
    double restore_refresh_ms = 0.0;
    double restore_frame_submit_ms = 0.0;
    double restore_gpu_wait_ms = 0.0;
    double restore_presented_ms = 0.0;
    uint64_t restore_draw_calls = 0u;
    uint64_t restore_draw_instances = 0u;
    nw::render::viewer::AreaTileRefreshStats restore_refresh_stats;
    nw::render::viewer::AreaTileRecordRefreshStats
        restore_record_refresh_stats;
};

struct AreaEditBenchmarkScenario {
    std::string name;
    uint32_t requested_tile_count = 0u;
    std::vector<uint32_t> tile_indices;
    std::optional<AreaEditBenchmarkSample> cold_sample;
    std::vector<AreaEditBenchmarkSample> samples;
    std::string diagnostic;
    bool available = false;
};

double elapsed_ms(Clock::time_point begin, Clock::time_point end) noexcept
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

double gpu_frame_ms(
    const nw::render::viewer::ViewerFrameStats& stats) noexcept
{
    return static_cast<double>(stats.gpu_shadow_seconds
               + stats.gpu_opaque_seconds
               + stats.gpu_water_seconds
               + stats.gpu_transparent_seconds
               + stats.gpu_particles_seconds
               + stats.gpu_debug_seconds
               + stats.gpu_forward_plus_cull_seconds)
        * 1000.0;
}

std::vector<uint32_t> distributed_tile_indices(
    size_t tile_count, uint32_t requested_count)
{
    const size_t count = std::min<size_t>(
        tile_count, requested_count);
    std::vector<uint32_t> result;
    result.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const size_t tile_index = index * tile_count / count;
        result.push_back(static_cast<uint32_t>(tile_index));
    }
    return result;
}

json timing_summary(std::vector<double> values)
{
    if (values.empty()) {
        return nullptr;
    }
    std::sort(values.begin(), values.end());
    long double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    const size_t median_index = values.size() / 2u;
    const double median = values.size() % 2u == 0u
        ? (values[median_index - 1u] + values[median_index]) * 0.5
        : values[median_index];
    const size_t p95_index = std::min<size_t>(values.size() - 1u,
        static_cast<size_t>(
            std::ceil(static_cast<double>(values.size()) * 0.95))
            - 1u);
    return {
        {"avg", static_cast<double>(sum / values.size())},
        {"min", values.front()},
        {"median", median},
        {"p95", values[p95_index]},
        {"max", values.back()},
    };
}

template <typename Getter>
json sample_timing_summary(
    std::span<const AreaEditBenchmarkSample> samples, Getter getter)
{
    std::vector<double> values;
    values.reserve(samples.size());
    for (const auto& sample : samples) {
        values.push_back(getter(sample));
    }
    return timing_summary(std::move(values));
}

json optional_gpu_timing_summary(
    std::span<const AreaEditBenchmarkSample> samples)
{
    std::vector<double> values;
    values.reserve(samples.size());
    for (const auto& sample : samples) {
        if (sample.edit_render_gpu_ms) {
            values.push_back(*sample.edit_render_gpu_ms);
        }
    }
    return {
        {"available_samples", values.size()},
        {"timing_ms", timing_summary(std::move(values))},
    };
}

json refresh_stats_json(
    const nw::render::viewer::AreaTileRefreshStats& stats)
{
    return {
        {"changed_tiles", stats.changed_tile_count},
        {"retained_models", stats.retained_model_count},
        {"replaced_models", stats.replaced_model_count},
        {"model_prepare_ms", stats.model_prepare_seconds * 1000.0f},
        {"instance_refresh_ms", stats.instance_refresh_seconds * 1000.0f},
        {"light_refresh_ms", stats.light_refresh_seconds * 1000.0f},
        {"scene_summary_ms", stats.scene_summary_seconds * 1000.0f},
        {"record_refresh_ms", stats.record_refresh_seconds * 1000.0f},
    };
}

json record_refresh_stats_json(
    const nw::render::viewer::AreaTileRecordRefreshStats& stats)
{
    return {
        {"variable_draw_counts", stats.variable_draw_counts},
        {"prepare_ms", stats.prepare_seconds * 1000.0f},
        {"draw_splice_ms", stats.draw_splice_seconds * 1000.0f},
        {"surface_protocol_ms", stats.surface_protocol_seconds * 1000.0f},
        {"summary_ms", stats.summary_seconds * 1000.0f},
    };
}

json sample_json(const AreaEditBenchmarkSample& sample)
{
    return {
        {"edit_refresh_ms", sample.edit_refresh_ms},
        {"edit_frame_submit_ms", sample.edit_frame_submit_ms},
        {"edit_gpu_wait_ms", sample.edit_gpu_wait_ms},
        {"edit_presented_ms", sample.edit_presented_ms},
        {"edit_render_cpu_ms", sample.edit_render_cpu_ms},
        {"edit_area_prepare_ms", sample.edit_area_prepare_ms},
        {"edit_forward_plus_prepare_ms", sample.edit_forward_plus_prepare_ms},
        {"edit_opaque_ms", sample.edit_opaque_ms},
        {"edit_render_gpu_ms", sample.edit_render_gpu_ms ? json(*sample.edit_render_gpu_ms) : json(nullptr)},
        {"edit_draw_calls", sample.edit_draw_calls},
        {"edit_draw_instances", sample.edit_draw_instances},
        {"edit_uses_cached_draw_lists", sample.edit_uses_cached_draw_lists},
        {"edit_refresh_phases", refresh_stats_json(sample.edit_refresh_stats)},
        {"edit_record_refresh_phases", record_refresh_stats_json(sample.edit_record_refresh_stats)},
        {"restore_refresh_ms", sample.restore_refresh_ms},
        {"restore_frame_submit_ms", sample.restore_frame_submit_ms},
        {"restore_gpu_wait_ms", sample.restore_gpu_wait_ms},
        {"restore_presented_ms", sample.restore_presented_ms},
        {"restore_draw_calls", sample.restore_draw_calls},
        {"restore_draw_instances", sample.restore_draw_instances},
        {"restore_refresh_phases", refresh_stats_json(sample.restore_refresh_stats)},
        {"restore_record_refresh_phases", record_refresh_stats_json(sample.restore_record_refresh_stats)},
    };
}

json scenario_json(const AreaEditBenchmarkScenario& scenario)
{
    json samples = json::array();
    for (const auto& sample : scenario.samples) {
        samples.push_back(sample_json(sample));
    }
    const auto rows = std::span<const AreaEditBenchmarkSample>{
        scenario.samples.data(), scenario.samples.size()};
    return {
        {"name", scenario.name},
        {"available", scenario.available},
        {"diagnostic", scenario.diagnostic},
        {"requested_tile_count", scenario.requested_tile_count},
        {"tile_count", scenario.tile_indices.size()},
        {"tile_indices", scenario.tile_indices},
        {"cold_sample", scenario.cold_sample ? sample_json(*scenario.cold_sample) : json(nullptr)},
        {"summary_ms", {
                           {"edit_refresh", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_refresh_ms; })},
                           {"edit_frame_submit", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_frame_submit_ms; })},
                           {"edit_gpu_wait", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_gpu_wait_ms; })},
                           {"edit_presented", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_presented_ms; })},
                           {"edit_render_cpu", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_render_cpu_ms; })},
                           {"edit_area_prepare", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_area_prepare_ms; })},
                           {"edit_forward_plus_prepare", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_forward_plus_prepare_ms; })},
                           {"edit_opaque", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_opaque_ms; })},
                           {"edit_render_gpu", optional_gpu_timing_summary(rows)},
                           {"edit_model_prepare", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_refresh_stats.model_prepare_seconds * 1000.0f; })},
                           {"edit_instance_refresh", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_refresh_stats.instance_refresh_seconds * 1000.0f; })},
                           {"edit_light_refresh", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_refresh_stats.light_refresh_seconds * 1000.0f; })},
                           {"edit_scene_summary", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_refresh_stats.scene_summary_seconds * 1000.0f; })},
                           {"edit_record_refresh", sample_timing_summary(rows, [](const auto& sample) { return sample.edit_refresh_stats.record_refresh_seconds * 1000.0f; })},
                           {"restore_refresh", sample_timing_summary(rows, [](const auto& sample) { return sample.restore_refresh_ms; })},
                           {"restore_frame_submit", sample_timing_summary(rows, [](const auto& sample) { return sample.restore_frame_submit_ms; })},
                           {"restore_gpu_wait", sample_timing_summary(rows, [](const auto& sample) { return sample.restore_gpu_wait_ms; })},
                           {"restore_presented", sample_timing_summary(rows, [](const auto& sample) { return sample.restore_presented_ms; })},
                       }},
        {"samples", std::move(samples)},
    };
}

bool render_presented_frame(
    AppState& state,
    nw::render::viewer::ViewerSession& session,
    const nw::render::viewer::ViewerViewport& viewport,
    double& submit_ms,
    double& wait_ms,
    nw::render::viewer::ViewerFrameStats& stats,
    std::string& diagnostic)
{
    const auto submit_begin = Clock::now();
    auto* command_list = nw::gfx::begin_frame(state.gfx_context);
    if (!command_list) {
        diagnostic = "begin_frame failed";
        return false;
    }
    session.tick(0);
    session.render(command_list, viewport);
    stats = session.last_frame_stats();
    nw::gfx::end_frame(state.gfx_context);
    const auto submit_end = Clock::now();
    nw::gfx::wait_idle(state.gfx_context);
    const auto wait_end = Clock::now();
    submit_ms = elapsed_ms(submit_begin, submit_end);
    wait_ms = elapsed_ms(submit_end, wait_end);
    return true;
}

bool collect_completed_gpu_frame(
    AppState& state,
    nw::render::viewer::ViewerSession& session,
    const nw::render::viewer::ViewerViewport& viewport,
    nw::render::viewer::ViewerFrameStats& stats,
    std::string& diagnostic)
{
    double submit_ms = 0.0;
    double wait_ms = 0.0;
    return render_presented_frame(state, session, viewport,
        submit_ms, wait_ms, stats, diagnostic);
}

bool build_edited_tiles(
    const nw::Area& area,
    std::span<const uint32_t> tile_indices,
    AreaEditBenchmarkKind kind,
    std::vector<nw::AreaTile>& original_tiles,
    std::vector<nw::AreaTile>& edited_tiles,
    std::string& diagnostic)
{
    if (!area.tileset || tile_indices.empty()) {
        diagnostic = "area tileset or edit batch is unavailable";
        return false;
    }
    original_tiles.clear();
    edited_tiles.clear();
    original_tiles.reserve(tile_indices.size());
    edited_tiles.reserve(tile_indices.size());
    for (const uint32_t tile_index : tile_indices) {
        if (tile_index >= area.tiles.size()) {
            diagnostic = "tile index is outside the area";
            return false;
        }
        const auto& original = area.tiles[tile_index];
        if (original.id < 0
            || static_cast<size_t>(original.id)
                >= area.tileset->tiles.size()) {
            diagnostic = "area tile id is outside the tileset";
            return false;
        }
        auto edited = original;
        if (kind == AreaEditBenchmarkKind::orientation) {
            edited.orientation = (original.orientation + 1) % 4;
        } else {
            const auto& original_model = area.tileset
                                             ->tiles[static_cast<size_t>(original.id)]
                                             .model;
            auto replacement = area.tileset->tiles.end();
            for (auto candidate = area.tileset->tiles.begin();
                candidate != area.tileset->tiles.end(); ++candidate) {
                if (candidate->model != original_model) {
                    replacement = candidate;
                    break;
                }
            }
            if (replacement == area.tileset->tiles.end()) {
                diagnostic = "tileset has no alternate tile model";
                return false;
            }
            edited.id = static_cast<int32_t>(
                std::distance(area.tileset->tiles.begin(), replacement));
        }
        original_tiles.push_back(original);
        edited_tiles.push_back(edited);
    }
    return true;
}

void assign_tiles(nw::Area& area,
    std::span<const uint32_t> tile_indices,
    std::span<const nw::AreaTile> tiles)
{
    for (size_t index = 0; index < tile_indices.size(); ++index) {
        area.tiles[tile_indices[index]] = tiles[index];
    }
}

bool run_edit_iteration(
    AppState& state,
    nw::render::viewer::ViewerSession& session,
    nw::Area& area,
    const nw::render::viewer::ViewerViewport& viewport,
    std::span<const uint32_t> tile_indices,
    std::span<const nw::AreaTile> original_tiles,
    std::span<const nw::AreaTile> edited_tiles,
    std::vector<std::shared_ptr<nw::render::RenderModel>>& retained_models,
    AreaEditBenchmarkSample& sample,
    std::string& diagnostic)
{
    const auto edit_begin = Clock::now();
    assign_tiles(area, tile_indices, edited_tiles);
    const auto refresh_begin = Clock::now();
    const auto refreshed = session.refresh_live_area_tiles(
        area.handle(), tile_indices);
    const auto refresh_end = Clock::now();
    if (!refreshed.ok()) {
        diagnostic = refreshed.diagnostic;
        assign_tiles(area, tile_indices, original_tiles);
        (void)session.refresh_live_area_tiles(
            area.handle(), tile_indices);
        return false;
    }
    sample.edit_refresh_stats
        = session.scene()->last_area_tile_refresh_stats;
    sample.edit_record_refresh_stats
        = session.scene()->area_render_scene->last_tile_record_refresh_stats();
    retained_models.clear();
    retained_models.reserve(tile_indices.size());
    for (const uint32_t tile_index : tile_indices) {
        const auto* scene = session.scene();
        if (!scene
            || tile_index >= scene->area_tile_model_indices.size()) {
            diagnostic = "edited tile model row is unavailable";
            assign_tiles(area, tile_indices, original_tiles);
            (void)session.refresh_live_area_tiles(
                area.handle(), tile_indices);
            return false;
        }
        const uint32_t model_index
            = scene->area_tile_model_indices[tile_index];
        if (model_index >= scene->static_models.size()
            || !scene->static_models[model_index]) {
            diagnostic = "edited tile model is unavailable";
            assign_tiles(area, tile_indices, original_tiles);
            (void)session.refresh_live_area_tiles(
                area.handle(), tile_indices);
            return false;
        }
        const auto& model = scene->static_models[model_index];
        if (std::find(retained_models.begin(), retained_models.end(), model)
            == retained_models.end()) {
            retained_models.push_back(model);
        }
    }
    nw::render::viewer::ViewerFrameStats edit_frame_stats;
    if (!render_presented_frame(state, session, viewport,
            sample.edit_frame_submit_ms, sample.edit_gpu_wait_ms,
            edit_frame_stats, diagnostic)) {
        assign_tiles(area, tile_indices, original_tiles);
        (void)session.refresh_live_area_tiles(
            area.handle(), tile_indices);
        return false;
    }
    const auto edit_end = Clock::now();
    sample.edit_refresh_ms = elapsed_ms(refresh_begin, refresh_end);
    sample.edit_presented_ms = elapsed_ms(edit_begin, edit_end);
    sample.edit_render_cpu_ms
        = static_cast<double>(edit_frame_stats.total_render_seconds)
        * 1000.0;
    sample.edit_area_prepare_ms
        = static_cast<double>(edit_frame_stats.area_prepare_seconds)
        * 1000.0;
    sample.edit_forward_plus_prepare_ms
        = static_cast<double>(edit_frame_stats.forward_plus_prepare_seconds)
        * 1000.0;
    sample.edit_opaque_ms
        = static_cast<double>(edit_frame_stats.opaque_seconds)
        * 1000.0;
    sample.edit_draw_calls
        = edit_frame_stats.total_command_stats.draw_count;
    sample.edit_draw_instances
        = edit_frame_stats.total_command_stats.draw_instance_count;
    sample.edit_uses_cached_draw_lists
        = edit_frame_stats.area_frame_uses_cached_draw_lists;

    nw::render::viewer::ViewerFrameStats completed_edit_stats;
    if (!collect_completed_gpu_frame(state, session, viewport,
            completed_edit_stats, diagnostic)) {
        assign_tiles(area, tile_indices, original_tiles);
        (void)session.refresh_live_area_tiles(
            area.handle(), tile_indices);
        return false;
    }
    if (completed_edit_stats.gpu_timer_count > 0u) {
        sample.edit_render_gpu_ms
            = gpu_frame_ms(completed_edit_stats);
    }

    const auto restore_begin = Clock::now();
    assign_tiles(area, tile_indices, original_tiles);
    const auto restore_refresh_begin = Clock::now();
    const auto restored = session.refresh_live_area_tiles(
        area.handle(), tile_indices);
    const auto restore_refresh_end = Clock::now();
    if (!restored.ok()) {
        diagnostic = restored.diagnostic;
        return false;
    }
    sample.restore_refresh_stats
        = session.scene()->last_area_tile_refresh_stats;
    sample.restore_record_refresh_stats
        = session.scene()->area_render_scene->last_tile_record_refresh_stats();
    nw::render::viewer::ViewerFrameStats restore_frame_stats;
    if (!render_presented_frame(state, session, viewport,
            sample.restore_frame_submit_ms,
            sample.restore_gpu_wait_ms, restore_frame_stats,
            diagnostic)) {
        return false;
    }
    const auto restore_end = Clock::now();
    sample.restore_refresh_ms
        = elapsed_ms(restore_refresh_begin, restore_refresh_end);
    sample.restore_presented_ms
        = elapsed_ms(restore_begin, restore_end);
    sample.restore_draw_calls
        = restore_frame_stats.total_command_stats.draw_count;
    sample.restore_draw_instances
        = restore_frame_stats.total_command_stats.draw_instance_count;
    return true;
}

bool build_preview_rows(
    const nw::Area& area,
    uint32_t requested_count,
    std::vector<nw::render::viewer::AreaTilePreviewRow>& first,
    std::vector<nw::render::viewer::AreaTilePreviewRow>& second,
    std::string& diagnostic)
{
    if (!area.tileset || requested_count == 0u
        || area.tiles.size() < static_cast<size_t>(requested_count) * 2u) {
        diagnostic = "area cannot provide two disjoint preview batches";
        return false;
    }
    const auto preview_tile = std::find_if(area.tileset->tiles.begin(),
        area.tileset->tiles.end(), [](const auto& tile) {
            return !tile.model.empty();
        });
    if (preview_tile == area.tileset->tiles.end()) {
        diagnostic = "tileset has no preview model";
        return false;
    }
    const int32_t preview_tile_id = static_cast<int32_t>(
        std::distance(area.tileset->tiles.begin(), preview_tile));
    first.clear();
    second.clear();
    first.reserve(requested_count);
    second.reserve(requested_count);
    for (uint32_t index = 0u; index < requested_count; ++index) {
        first.push_back({
            .tile_index = index,
            .tile_id = preview_tile_id,
        });
        second.push_back({
            .tile_index = static_cast<uint32_t>(
                area.tiles.size() - requested_count + index),
            .tile_id = preview_tile_id,
        });
    }
    return true;
}

bool run_preview_iteration(
    AppState& state,
    nw::render::viewer::ViewerSession& session,
    const nw::render::viewer::ViewerViewport& viewport,
    std::span<const nw::render::viewer::AreaTilePreviewRow> first,
    std::span<const nw::render::viewer::AreaTilePreviewRow> second,
    nw::render::viewer::AreaTilePreviewLease& lease,
    AreaEditBenchmarkSample& sample,
    std::string& diagnostic)
{
    const auto edit_begin = Clock::now();
    const auto refresh_begin = Clock::now();
    const auto updated = session.update_area_tile_previews(second, lease);
    const auto refresh_end = Clock::now();
    if (!updated.ok()) {
        diagnostic = updated.diagnostic;
        return false;
    }
    nw::render::viewer::ViewerFrameStats edit_frame_stats;
    if (!render_presented_frame(state, session, viewport,
            sample.edit_frame_submit_ms, sample.edit_gpu_wait_ms,
            edit_frame_stats, diagnostic)) {
        return false;
    }
    const auto edit_end = Clock::now();
    sample.edit_refresh_ms = elapsed_ms(refresh_begin, refresh_end);
    sample.edit_presented_ms = elapsed_ms(edit_begin, edit_end);
    sample.edit_render_cpu_ms
        = static_cast<double>(edit_frame_stats.total_render_seconds)
        * 1000.0;
    sample.edit_area_prepare_ms
        = static_cast<double>(edit_frame_stats.area_prepare_seconds)
        * 1000.0;
    sample.edit_forward_plus_prepare_ms
        = static_cast<double>(edit_frame_stats.forward_plus_prepare_seconds)
        * 1000.0;
    sample.edit_opaque_ms
        = static_cast<double>(edit_frame_stats.opaque_seconds)
        * 1000.0;
    sample.edit_draw_calls
        = edit_frame_stats.total_command_stats.draw_count;
    sample.edit_draw_instances
        = edit_frame_stats.total_command_stats.draw_instance_count;
    sample.edit_uses_cached_draw_lists
        = edit_frame_stats.area_frame_uses_cached_draw_lists;

    nw::render::viewer::ViewerFrameStats completed_edit_stats;
    if (!collect_completed_gpu_frame(state, session, viewport,
            completed_edit_stats, diagnostic)) {
        return false;
    }
    if (completed_edit_stats.gpu_timer_count > 0u) {
        sample.edit_render_gpu_ms = gpu_frame_ms(completed_edit_stats);
    }

    const auto restore_begin = Clock::now();
    const auto restore_refresh_begin = Clock::now();
    const auto restored = session.update_area_tile_previews(first, lease);
    const auto restore_refresh_end = Clock::now();
    if (!restored.ok()) {
        diagnostic = restored.diagnostic;
        return false;
    }
    nw::render::viewer::ViewerFrameStats restore_frame_stats;
    if (!render_presented_frame(state, session, viewport,
            sample.restore_frame_submit_ms,
            sample.restore_gpu_wait_ms, restore_frame_stats,
            diagnostic)) {
        return false;
    }
    const auto restore_end = Clock::now();
    sample.restore_refresh_ms
        = elapsed_ms(restore_refresh_begin, restore_refresh_end);
    sample.restore_presented_ms
        = elapsed_ms(restore_begin, restore_end);
    sample.restore_draw_calls
        = restore_frame_stats.total_command_stats.draw_count;
    sample.restore_draw_instances
        = restore_frame_stats.total_command_stats.draw_instance_count;
    return true;
}

AreaEditBenchmarkScenario run_preview_scenario(
    AppState& state,
    nw::render::viewer::ViewerSession& session,
    const nw::Area& area,
    const nw::render::viewer::ViewerViewport& viewport,
    uint32_t tile_count,
    int samples,
    int warmup_samples)
{
    AreaEditBenchmarkScenario result;
    result.name = "preview_hover";
    result.requested_tile_count = tile_count;
    std::vector<nw::render::viewer::AreaTilePreviewRow> first;
    std::vector<nw::render::viewer::AreaTilePreviewRow> second;
    if (!build_preview_rows(
            area, tile_count, first, second, result.diagnostic)) {
        return result;
    }
    result.tile_indices.reserve(first.size());
    for (const auto& row : first) {
        result.tile_indices.push_back(row.tile_index);
    }

    nw::render::viewer::AreaTilePreviewLease lease;
    const auto initialized = session.update_area_tile_previews(first, lease);
    if (!initialized.ok()) {
        result.diagnostic = initialized.diagnostic;
        return result;
    }
    const auto finish = [&]() {
        const auto restored = session.restore_area_tile_previews(lease);
        if (!restored.ok() && result.diagnostic.empty()) {
            result.diagnostic = restored.diagnostic;
        }
        return restored.ok();
    };

    AreaEditBenchmarkSample cold_sample;
    if (!run_preview_iteration(state, session, viewport,
            first, second, lease, cold_sample, result.diagnostic)) {
        (void)finish();
        return result;
    }
    result.cold_sample = cold_sample;
    for (int warmup = 0; warmup < warmup_samples; ++warmup) {
        AreaEditBenchmarkSample ignored;
        if (!run_preview_iteration(state, session, viewport,
                first, second, lease, ignored, result.diagnostic)) {
            (void)finish();
            return result;
        }
    }
    result.samples.reserve(static_cast<size_t>(samples));
    for (int index = 0; index < samples; ++index) {
        AreaEditBenchmarkSample sample;
        if (!run_preview_iteration(state, session, viewport,
                first, second, lease, sample, result.diagnostic)) {
            (void)finish();
            return result;
        }
        result.samples.push_back(sample);
    }
    result.available = finish();
    return result;
}

AreaEditBenchmarkScenario run_scenario(
    AppState& state,
    nw::render::viewer::ViewerSession& session,
    nw::Area& area,
    const nw::render::viewer::ViewerViewport& viewport,
    AreaEditBenchmarkKind kind,
    uint32_t tile_count,
    int samples,
    int warmup_samples)
{
    AreaEditBenchmarkScenario result;
    result.name = kind == AreaEditBenchmarkKind::orientation
        ? "orientation"
        : "model_swap";
    result.requested_tile_count = tile_count;
    result.tile_indices = distributed_tile_indices(
        area.tiles.size(), tile_count);

    std::vector<nw::AreaTile> original_tiles;
    std::vector<nw::AreaTile> edited_tiles;
    if (!build_edited_tiles(area, result.tile_indices, kind,
            original_tiles, edited_tiles, result.diagnostic)) {
        return result;
    }

    AreaEditBenchmarkSample cold_sample;
    std::vector<std::shared_ptr<nw::render::RenderModel>> retained_models;
    if (!run_edit_iteration(state, session, area, viewport,
            result.tile_indices, original_tiles, edited_tiles,
            retained_models, cold_sample, result.diagnostic)) {
        return result;
    }
    result.cold_sample = cold_sample;

    for (int warmup = 0; warmup < warmup_samples; ++warmup) {
        AreaEditBenchmarkSample ignored;
        if (!run_edit_iteration(state, session, area, viewport,
                result.tile_indices, original_tiles, edited_tiles,
                retained_models, ignored, result.diagnostic)) {
            return result;
        }
    }
    result.samples.reserve(static_cast<size_t>(samples));
    for (int index = 0; index < samples; ++index) {
        AreaEditBenchmarkSample sample;
        if (!run_edit_iteration(state, session, area, viewport,
                result.tile_indices, original_tiles, edited_tiles,
                retained_models, sample, result.diagnostic)) {
            return result;
        }
        result.samples.push_back(sample);
    }
    result.available = true;
    return result;
}

} // namespace

int run_area_edit_benchmark_command(
    AppState& state,
    std::string_view area_resref,
    int samples,
    int warmup_samples,
    const std::filesystem::path& output_path)
{
    if (!state.preview_resources || !state.gfx_context) {
        LOG_F(ERROR,
            "Area edit benchmark requires preview resources and a graphics context");
        return 1;
    }
    samples = std::max(samples, 1);
    warmup_samples = std::max(warmup_samples, 0);
    const nw::render::viewer::ViewerViewport viewport{
        .x = 0,
        .y = 0,
        .width = static_cast<uint32_t>(
            std::max(state.window_width, 1)),
        .height = static_cast<uint32_t>(
            std::max(state.window_height, 1)),
    };

    nw::render::viewer::ViewerSession session{
        *state.preview_resources, state.debug_renderer.get()};
    session.set_playing(false);
    session.set_area_day_night_autoplay(false);
    if (!session.load_area(area_resref)
        || !session.fit_to_scene(viewport)) {
        LOG_F(ERROR, "Failed to load area edit benchmark '{}'",
            area_resref);
        return 1;
    }
    auto* scene = session.scene();
    auto* area = scene
        ? nw::kernel::objects().get<nw::Area>(scene->root_object)
        : nullptr;
    if (!scene || !scene->area_render_scene || !area
        || area->tiles.empty()
        || area->tiles.size()
            > std::numeric_limits<uint32_t>::max()) {
        LOG_F(ERROR,
            "Area edit benchmark scene is missing valid tile data");
        return 1;
    }

    nw::render::AreaTileGridDebugGeometry tile_grid;
    if (!nw::render::build_area_tile_grid_debug_geometry(
            *area, tile_grid)
        || !session.set_tile_grid_debug_geometry(
            tile_grid.vertices, tile_grid.indices)) {
        LOG_F(ERROR,
            "Area edit benchmark could not build the persistent tile grid");
        return 1;
    }

    std::string initial_frame_failure;
    double initial_submit_ms = 0.0;
    double initial_wait_ms = 0.0;
    nw::render::viewer::ViewerFrameStats initial_frame_stats;
    if (!render_presented_frame(state, session, viewport,
            initial_submit_ms, initial_wait_ms,
            initial_frame_stats, initial_frame_failure)) {
        LOG_F(ERROR, "Area edit benchmark initial frame failed: {}",
            initial_frame_failure);
        return 1;
    }
    if (initial_frame_stats.tile_grid_vertex_count
            != tile_grid.vertices.size()
        || initial_frame_stats.tile_grid_index_count
            != tile_grid.indices.size()
        || initial_frame_stats.tile_grid_command_stats.draw_count != 1u) {
        LOG_F(ERROR,
            "Area edit benchmark tile grid was not submitted: vertices={} indices={} draws={}",
            initial_frame_stats.tile_grid_vertex_count,
            initial_frame_stats.tile_grid_index_count,
            initial_frame_stats.tile_grid_command_stats.draw_count);
        return 1;
    }
    std::vector<uint32_t> batch_sizes{1u};
    if (area->tiles.size() > 1u) {
        batch_sizes.push_back(static_cast<uint32_t>(
            std::min<size_t>(64u, area->tiles.size())));
    }
    json scenarios = json::array();
    bool all_available = true;
    for (const auto kind : {
             AreaEditBenchmarkKind::orientation,
             AreaEditBenchmarkKind::model_swap}) {
        for (const uint32_t batch_size : batch_sizes) {
            auto scenario = run_scenario(state, session, *area,
                viewport, kind, batch_size,
                samples, warmup_samples);
            all_available = all_available && scenario.available;
            scenarios.push_back(scenario_json(scenario));
        }
    }
    std::vector<uint32_t> preview_batch_sizes;
    if (area->tiles.size() >= 2u) {
        preview_batch_sizes.push_back(1u);
        const uint32_t large_preview_batch = static_cast<uint32_t>(
            std::min<size_t>(64u, area->tiles.size() / 2u));
        if (large_preview_batch > 1u) {
            preview_batch_sizes.push_back(large_preview_batch);
        }
    }
    for (const uint32_t batch_size : preview_batch_sizes) {
        auto scenario = run_preview_scenario(state, session, *area,
            viewport, batch_size, samples, warmup_samples);
        all_available = all_available && scenario.available;
        scenarios.push_back(scenario_json(scenario));
    }

    const auto& cache_stats = scene->area_render_scene->stats();
    const json report{
        {"area", std::string(area_resref)},
        {"samples", samples},
        {"warmup_samples", warmup_samples},
        {"viewport", {
                         {"width", viewport.width},
                         {"height", viewport.height},
                     }},
        {"scene", {
                      {"area_width", area->width},
                      {"area_height", area->height},
                      {"tiles", area->tiles.size()},
                      {"tile_grid_vertices", tile_grid.vertices.size()},
                      {"tile_grid_indices", tile_grid.indices.size()},
                      {"models", scene->static_models.size()},
                      {"particle_systems", scene->particles.size()},
                      {"records", cache_stats.record_count},
                      {"prepared_draws", cache_stats.prepared_draw_count},
                      {"surface_geometries", cache_stats.surface_range_count},
                      {"surface_instances", cache_stats.surface_instance_count},
                      {"surface_triangles", cache_stats.surface_triangle_count},
                      {"surface_bytes", cache_stats.surface_bytes},
                      {"local_lights", cache_stats.local_light_count},
                  }},
        {"measurement_scope", {
                                  {"edit_refresh", "in-memory AreaTile rows through ViewerSession live scene refresh"},
                                  {"presented", "tile mutation through submitted frame and graphics queue idle"},
                                  {"restore", "exact original AreaTile rows through submitted restore frame and graphics queue idle"},
                                  {"preview_hover", "retained transient preview rows moved between two disjoint tile batches"},
                                  {"tile_grid", "persistent editor grid batch enabled for every presented frame"},
                                  {"project_writes", false},
                                  {"client_command_history", false},
                                  {"rmlui_publication", false},
                              }},
        {"all_scenarios_available", all_available},
        {"scenarios", std::move(scenarios)},
    };

    if (!output_path.empty()) {
        std::ofstream output{output_path};
        if (!output) {
            LOG_F(ERROR, "Failed to open area edit benchmark output '{}'",
                output_path.string());
            return 1;
        }
        output << report.dump(2) << '\n';
        if (!output) {
            LOG_F(ERROR, "Failed to write area edit benchmark output '{}'",
                output_path.string());
            return 1;
        }
    }
    std::cout << report.dump(2) << '\n'
              << std::flush;
    return all_available ? 0 : 1;
}

} // namespace mudl
