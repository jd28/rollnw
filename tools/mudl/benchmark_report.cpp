#include "benchmark_report.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <system_error>

namespace mudl {

nlohmann::json benchmark_path_metadata_json(std::string_view path)
{
    if (path.empty()) {
        return nullptr;
    }

    std::filesystem::path raw_path{std::string(path)};
    std::error_code ec;
    auto absolute_path = std::filesystem::weakly_canonical(raw_path, ec);
    if (ec) {
        ec.clear();
        absolute_path = std::filesystem::absolute(raw_path, ec);
    }

    nlohmann::json result{
        {"path", raw_path.string()},
    };
    if (!ec) {
        result["absolute_path"] = absolute_path.string();
    }
    return result;
}

} // namespace mudl
