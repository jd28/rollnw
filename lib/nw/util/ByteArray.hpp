#pragma once

#include <cstdint>
#include <filesystem>
#include <ios>
#include <string>
#include <vector>

namespace nw {

/**
 * @brief nw::ByteArray provides an array of raw bytes.
 *
 */
class ByteArray {
public:
    /// @name Types
    /// @{
    using Base = std::vector<std::byte>;
    using iterator = Base::iterator;
    using const_iterator = Base::const_iterator;
    using size_type = Base::size_type;
    /// @}

    /// @name Constructors
    /// @{
    ByteArray() = default;
    ByteArray(const std::byte* buffer, size_t len);
    ByteArray(ByteArray&&) = default;
    ByteArray(const ByteArray&) = default;
    /// @}

    /// @name Operators
    /// @{
    ByteArray& operator=(ByteArray&&) = default;
    ByteArray& operator=(const ByteArray&) = default;
    bool operator==(const ByteArray& other) const { return array_ == other.array_; }
    std::byte& operator[](size_type pos) { return array_[pos]; }
    const std::byte& operator[](size_type pos) const { return array_[pos]; }
    /// @}

    /// @name Functions
    /// @{

    /**
     * @brief Appends bytes to the array
     *
     * @param buffer Pointer to the data to append.
     * @param len Length of ``buffer`` in bytes.
     */
    void append(const void* buffer, size_t len);

    /**
     * @brief Clears the data in the array
     *
     */
    void clear() { array_.clear(); }

    /**
     * @brief Returns pointer to the underlying array
     *
     * @return Pointer to the underlying element storage
     */
    std::byte* data() noexcept { return array_.data(); }

    /**
     * @copydoc ByteArray::data()
     */
    const std::byte* data() const noexcept { return array_.data(); }

    /**
     * @brief Appends one element to the array
     *
     * @param byte Element to append to the array.
     */
    void push_back(std::byte byte) { array_.push_back(byte); }

    /**
     * @brief Read bytes at offset
     *
     * @param buffer Buffer into which bytes are read.
     * @param offset Offset into the underlying array of bytes.
     * @param size Number of bytes to read.
     * @return ``true`` if successfully read, ``false`` if not
     */
    bool read_at(size_t offset, void* buffer, size_t size) const;

    /**
     * @brief Increases the capacity of the array by ``count`` elements
     *
     * @param count The amount of elements to increase capacity by.
     */
    void reserve(size_type count) { array_.reserve(count); }

    /**
     * @brief Resizes array to contain ``count`` elements.
     *
     * @param count New size of the array.
     *
     * @note If the current size is greater than ``count``, the container is reduced to its first ``count`` elements.
     *       If the current size is less than ``count``,
     *       1. additional default-inserted elements are appended
     */
    void resize(size_type count) { array_.resize(count); }

    /**
     * @brief Returns the number of elements in the container
     *
     * @return size_type The number of elements in the container.
     */
    size_type size() const noexcept { return array_.size(); }

    /**
     * @brief Constructs string view of the array
     *
     * @return std::string_view The array as a string view.
     */
    std::string_view string_view() const;

    /// @name Static Functions
    /// @{
    static ByteArray from_file(const std::filesystem::path& path);
    /// @}

private:
    std::vector<std::byte> array_;
};

} // namespace nw
