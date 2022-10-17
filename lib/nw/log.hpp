#pragma once

#define LOGURU_USE_FMTLIB 1
#define LOGURU_REDEFINE_ASSERT 1
#include "loguru/loguru.hpp"

// Needed to format std::filesystem::path
#include <fmt/ostream.h>

#include <filesystem>

/// rollNW namespace
namespace nw {

/// Initialize logger
void init_logger(int argc, char* argv[]);

} // namespace nw

template <>
struct fmt::formatter<std::filesystem::path> : formatter<string_view> {
    template <typename FormatContext>
    auto format(const std::filesystem::path& p, FormatContext& ctx) const
    {
        return formatter<string_view>::format(p.string(), ctx);
    }
};
