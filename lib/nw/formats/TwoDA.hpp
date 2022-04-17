#pragma once

#include "../log.hpp"
#include "../util/ByteArray.hpp"
#include "../util/string.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nw {

namespace detail {

// Not a variant, not for public consumption
struct StringVariant {
    StringVariant(std::string string)
        : str{std::move(string)}
        , view{str}
    {
    }
    StringVariant(std::string_view string)
        : view{string}
    {
    }

    std::string str;
    std::string_view view;
};

} // namespace detail

struct TwoDA {
    TwoDA() = default;

    /// Constructs TwoDA object from a file
    explicit TwoDA(const std::filesystem::path& filename);

    /// Constructs TwoDA object from an array of bytes
    explicit TwoDA(ByteArray bytes);

    /// Finds the index of a column, or -1
    int column_index(std::string_view column) const;

    /// Get the number of columns
    size_t columns() const noexcept;

    /// Gets an element
    template <typename T>
    std::optional<T> get(size_t row, size_t col);

    /// Gets an element
    template <typename T>
    std::optional<T> get(size_t row, std::string_view col);

    /// Gets an element
    template <typename T>
    bool get_to(size_t row, size_t col, T& out);

    /// Gets an element
    template <typename T>
    bool get_to(size_t row, std::string_view, T& out);

    /// Pads the 2da with ``count`` rows.
    void pad(size_t count);

    /// Number of rows
    size_t rows() const noexcept;

    /// Sets an element
    template <typename T>
    void set(size_t row, size_t col, const T& value);

    /// Sets an element
    template <typename T>
    void set(size_t row, std::string_view col, const T& value);

    /// Is the 2da parsed without error
    bool is_valid() const noexcept;

private:
    friend std::ostream& operator<<(std::ostream& out, const TwoDA& tda);

    ByteArray bytes_;
    bool is_loaded_ = false;

    std::string default_;
    std::vector<int> widths_;
    std::vector<std::string> columns_;
    std::vector<detail::StringVariant> rows_;

    bool parse();
};

template <typename T>
std::optional<T> TwoDA::get(size_t row, size_t col)
{
    T temp;
    return get_to(row, col, temp)
        ? std::optional<T>{std::move(temp)}
        : std::optional<T>{};
}

template <typename T>
std::optional<T> TwoDA::get(size_t row, std::string_view col)
{
    int ci = column_index(col);
    if (ci < 0) {
        LOG_F(WARNING, "unknown column: {}", col);
        return std::optional<T>{};
    }

    return get<T>(row, static_cast<size_t>(ci));
}

template <typename T>
bool TwoDA::get_to(size_t row, size_t col, T& out)
{
    static_assert(
        std::is_same_v<T, std::string> || std::is_same_v<T, float> || std::is_convertible_v<T, int32_t> || std::is_same_v<T, std::string_view>,
        "TwoDA only supports float, std::string, std::string_view, or anything convertable to int32_t");

    size_t idx = row * columns_.size() + col;
    if (idx >= rows_.size()) {
        LOG_F(ERROR, "Out of Bounds row {}, col {}", row, col);
        return false;
    }

    std::string_view res = rows_[idx].view;

    if (res == "****") return false;

    if constexpr (std::is_same_v<T, float> || std::is_convertible_v<T, int>) {
        auto opt = string::from<T>(res);
        if (!opt) return false;
        out = *opt;
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        out = res;
    } else {
        out = std::string(res);
    }

    return true;
}

template <typename T>
bool TwoDA::get_to(size_t row, std::string_view col, T& out)
{
    int ci = column_index(col);
    if (ci == -1) {
        LOG_F(WARNING, "unknown column: {}", col);
        return false;
    }
    return get_to<T>(row, column_index(col), out);
}

template <typename T>
void TwoDA::set(size_t row, size_t col, const T& value)
{
    size_t idx = row * columns_.size() + col;
    if (idx >= rows_.size()) return;

    int quote = 0;
    if constexpr (std::is_same_v<T, float> || std::is_convertible_v<T, int>) {
        rows_[idx] = std::to_string(value);
    } else {
        rows_[idx] = value;
        if (rows_[idx].view.find(' ') != std::string_view::npos) {
            quote = 2;
        }
    }
    widths_[col] = std::max(widths_[col], value.size() + quote);
}

template <typename T>
void TwoDA::set(size_t row, std::string_view col, const T& value)
{
    set<T>(row, column_index(col), value);
}

/// Overload for ``operator<<``
std::ostream& operator<<(std::ostream& out, const TwoDA& tda);

} // namespace nw
