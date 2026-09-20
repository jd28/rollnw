#pragma once

#include <filesystem>
#include <string_view>

namespace mudl {

struct AppState;

int run_area_edit_benchmark_command(
    AppState& state,
    std::string_view area_resref,
    int samples,
    int warmup_samples,
    const std::filesystem::path& output_path);

} // namespace mudl
