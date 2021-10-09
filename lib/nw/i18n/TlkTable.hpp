#pragma once

#include "Tlk.hpp"

#include <filesystem>

namespace nw {

struct TlkTable {
    static const uint32_t custom_id;

    explicit TlkTable(const std::filesystem::path& dialog, const std::filesystem::path& custom = {});

    std::string_view get(uint32_t strref, bool feminine = false) const;
    bool is_valid() const noexcept;

private:
    Tlk dialog_;
    Tlk dialogf_;
    Tlk custom_;
    Tlk customf_;

    bool is_valid_ = false;
};

} // namespace nw
