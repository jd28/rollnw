#pragma once

#include "../config.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace nw {

struct ByteArray;
struct Key;

struct BifElement {
    uint32_t id;
    uint32_t offset;
    uint32_t size;
    uint32_t type;
};

/// Bif is used only by ``nw::Key``, it has no independant use.
struct Bif {
    friend struct Key;

    Bif(Key* key, std::filesystem::path path);

    // LCOV_EXCL_START
    Bif(const Bif&) = delete;
    Bif(Bif&& other) = default;

    Bif& operator=(const Bif&) = delete;
    Bif& operator=(Bif&& other) = default;
    // LCOV_EXCL_STOP

    ByteArray demand(size_t index) const;

private:
    Key* key_ = nullptr;
    std::filesystem::path path_;
    mutable std::ifstream file_;
    std::streamsize fsize_;
    Vector<BifElement> elements;
    bool is_loaded_;

    bool load();
};

} // namespace nw
