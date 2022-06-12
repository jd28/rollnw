#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
    Bif(const Bif&) = delete;
    Bif(Bif&& other) = default;

    ByteArray demand(size_t index) const;

    Bif& operator=(const Bif&) = delete;
    Bif& operator=(Bif&& other) = default;

private:
    Key* key_ = nullptr;
    std::filesystem::path path_;
    mutable std::ifstream file_;
    std::streamsize fsize_;
    std::vector<BifElement> elements;
    bool is_loaded_;

    bool load();
};

} // namespace nw
