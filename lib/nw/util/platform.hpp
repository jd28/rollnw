#pragma once

#include <filesystem>

namespace nw {

/// Gets user's documents path
std::filesystem::path documents_path();

/// Gets user's home path
std::filesystem::path home_path();

/// Creates randomly named folder in tmp.  Analguous to POSIX ``mkdtemp``.
std::filesystem::path create_unique_tmp_path();

/// Copies and deletes a file to a new location, overwrites existing
bool move_file_safely(const std::filesystem::path& from, const std::filesystem::path& to);

} // namespace nw
