#pragma once

#include <filesystem>
#include <string_view>

namespace mudl {

int run_visual_corpus_command(const std::filesystem::path& corpus_path, const std::filesystem::path& output_dir,
    std::string_view module_path, std::string_view user_path, std::string_view pbr_environment_path,
    bool pbr_ibl_enabled, size_t limit, std::string_view filter, const std::filesystem::path& ledger_path = {});

} // namespace mudl
