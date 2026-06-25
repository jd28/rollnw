#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string_view>

namespace mudl {

nlohmann::json benchmark_path_metadata_json(std::string_view path);

} // namespace mudl
