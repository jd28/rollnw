#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <ios>
#include <span>
#include <string>
#include <vector>

namespace nw {

struct ByteArray {
    using Base = std::vector<uint8_t>;
    using iterator = Base::iterator;
    using const_iterator = Base::const_iterator;
    using size_type = Base::size_type;

    ByteArray() = default;
    ByteArray(const uint8_t* buffer, size_t len);
    ByteArray(ByteArray&&) = default;
    ByteArray(const ByteArray&) = default;

    ByteArray& operator=(ByteArray&&) = default;
    ByteArray& operator=(const ByteArray&) = default;
    bool operator==(const ByteArray& other) const { return array_ == other.array_; }
    uint8_t& operator[](size_type pos) { return array_[pos]; }
    const uint8_t& operator[](size_type pos) const { return array_[pos]; }

    /// Appends bytes to the array
    void append(const void* buffer, size_t len);

    /// @brief Clears the data in the array
    void clear() { array_.clear(); }

    /// Returns pointer to the underlying array
    uint8_t* data() noexcept { return array_.data(); }

    /// Returns pointer to the underlying array
    const uint8_t* data() const noexcept { return array_.data(); }

    /// Appends one element to the array
    void push_back(uint8_t byte) { array_.push_back(byte); }

    /// Reads ``size`` bytes at ``offset`` into an arbitrary ``buffer``
    bool read_at(size_t offset, void* buffer, size_t size) const;

    /// Increases the capacity of the array by ``count`` elements
    void reserve(size_type count) { array_.reserve(count); }

    /// Resizes array to contain ``count`` elements.  If greater, than current size, null padded.
    void resize(size_type count) { array_.resize(count); }

    /// Returns the number of bytes
    size_type size() const noexcept { return array_.size(); }

    /// Construct std::span
    std::span<uint8_t> span() { return {data(), size()}; }

    /// Construct std::span
    std::span<const uint8_t> span() const { return {data(), size()}; }

    /// Constructs string view of the array
    std::string_view string_view() const;

    /// Write contents to file
    bool write_to(const std::filesystem::path& path) const;

    /// Load a file into memory
    static ByteArray from_file(const std::filesystem::path& path);

private:
    std::vector<uint8_t> array_;
};

void from_json(const nlohmann::json& json, ByteArray& ba);
void to_json(nlohmann::json& json, const ByteArray& ba);

} // namespace nw
