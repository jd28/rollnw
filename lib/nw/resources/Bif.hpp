#pragma once

#include <nowide/fstream.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace nw {

class ByteArray;
struct Key;

struct BifElement {
    uint32_t id;
    uint32_t offset;
    uint32_t size;
    uint32_t type;
};

struct Bif {
    friend struct Key;

    /// @name Constructors / Destructors
    /// @{
    Bif(Key* key, std::filesystem::path path);
    Bif(const Bif&) = delete;
    Bif(Bif&& other) = default;
    /// @}

    ByteArray demand(size_t index);

    /// @name Operators
    /// @{
    Bif& operator=(const Bif&) = delete;
    Bif& operator=(Bif&& other) = default;
    /// @}

private:
    Key* key = nullptr;
    std::filesystem::path path_;
    nowide::ifstream file_;
    std::streamsize fsize_;
    std::vector<BifElement> elements;
    bool is_loaded_;

    bool load();
};

} // namespace nw
