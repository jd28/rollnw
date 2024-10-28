#pragma once

#include "../log.hpp"
#include "../resources/ResourceData.hpp"
#include "../util/string.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace nw {

namespace detail {

// Not a variant, not for public consumption
struct StringVariant {
    StringVariant(const char* string)
        : str{string}
        , view{str}
    {
    }

    StringVariant(String string)
        : str{std::move(string)}
        , view{str}
    {
    }

    StringVariant(StringView string)
        : view{string}
    {
    }

    StringVariant(const StringVariant&) = default;
    StringVariant(StringVariant&& other)
    {
        if (other.str.size()) {
            str = std::move(other.str);
            view = str;
        } else {
            view = other.view;
        }
    }
    StringVariant& operator=(const StringVariant&) = default;
    StringVariant& operator=(StringVariant&& other)
    {
        if (this != &other) {
            if (other.str.size()) {
                str = std::move(other.str);
                view = str;
            } else {
                view = other.view;
            }
        }
        return *this;
    }

    String str;
    StringView view;
};

} // namespace detail

struct TwoDA final {
    using RowType = std::vector<detail::StringVariant>;
    static constexpr size_t npos = std::numeric_limits<size_t>::max();

    TwoDA() = default;
    TwoDA(const TwoDA&) = delete;
    TwoDA(TwoDA&&) = default;
    virtual ~TwoDA() = default;

    TwoDA& operator=(const TwoDA&) = delete;
    TwoDA& operator=(TwoDA&&) = default;

    /// Constructs TwoDA object from a file
    explicit TwoDA(const std::filesystem::path& filename);

    /// Constructs TwoDA object from an array of bytes
    explicit TwoDA(ResourceData data);

    /// Constructs TwoDA object from a string
    explicit TwoDA(std::string_view data);

    /// Appends new column with default `****` values
    bool add_column(StringView name);

    /// Finds the index of a column, or -1
    size_t column_index(StringView column) const;

    /// Gets list of column names
    const Vector<String>& column_names() const noexcept { return columns_; };

    /// Get the number of columns
    size_t columns() const noexcept;

    /// Gets an element
    template <typename T>
    std::optional<T> get(size_t row, size_t col) const;

    /// Gets an element
    template <typename T>
    std::optional<T> get(size_t row, StringView col) const;

    /// Gets an element
    template <typename T>
    bool get_to(size_t row, size_t col, T& out) const;

    /// Gets an element
    template <typename T>
    bool get_to(size_t row, StringView col, T& out) const;

    /// Gets raw twoda value
    StringView get_raw(size_t row, size_t col) const;

    /// Is the 2da parsed without error
    bool is_valid() const noexcept;

    /// Pads the 2da with ``count`` rows.
    void pad(size_t count);

    /// Number of rows
    size_t rows() const noexcept;

    /// Sets an element
    template <typename T>
    void set(size_t row, size_t col, const T& value);

    /// Sets an element
    template <typename T>
    void set(size_t row, StringView col, const T& value);

    friend std::ostream& operator<<(std::ostream& out, const TwoDA& tda);

private:
    ResourceData data_;
    Vector<size_t> widths_;
    bool is_loaded_ = true;
    std::vector<RowType> rows_;
    String default_;
    Vector<String> columns_;

    bool parse();
};

template <typename T>
std::optional<T> TwoDA::get(size_t row, size_t col) const
{
    T temp;
    return get_to(row, col, temp)
        ? std::optional<T>{std::move(temp)}
        : std::optional<T>{};
}

template <typename T>
std::optional<T> TwoDA::get(size_t row, StringView col) const
{
    size_t ci = column_index(col);
    if (ci == npos) {
        LOG_F(WARNING, "unknown column: {}", col);
        return std::optional<T>{};
    }

    return get<T>(row, ci);
}

template <typename T>
bool TwoDA::get_to(size_t row, size_t col, T& out) const
{
    static_assert(
        std::is_same_v<T, String> || std::is_same_v<T, float> || std::is_convertible_v<T, int32_t> || std::is_same_v<T, StringView>,
        "TwoDA only supports float, String, StringView, or anything convertible to int32_t");

    StringView res = get_raw(row, col);
    if (res == "****") return false;

    if constexpr (std::is_same_v<T, float> || std::is_convertible_v<T, int>) {
        auto opt = string::from<T>(res);
        if (!opt) return false;
        out = *opt;
    } else if constexpr (std::is_same_v<T, StringView>) {
        out = res;
    } else {
        out = String(res);
    }

    return true;
}

template <typename T>
bool TwoDA::get_to(size_t row, StringView col, T& out) const
{
    size_t ci = column_index(col);
    if (ci == npos) {
        LOG_F(WARNING, "unknown column: {}", col);
        return false;
    }
    return get_to<T>(row, ci, out);
}

template <typename T>
void TwoDA::set(size_t row, size_t col, const T& value)
{
    int quote = 0;
    if constexpr (std::is_floating_point_v<T> || std::is_integral_v<T>) {
        rows_[row][col] = std::to_string(value);
    } else {
        rows_[row][col] = value;
        if (rows_[row][col].view.find(' ') != StringView::npos) {
            quote = 2;
        }
    }
    widths_[col] = std::max(widths_[col], rows_[row][col].view.size() + quote);
}

template <typename T>
void TwoDA::set(size_t row, StringView col, const T& value)
{
    set<T>(row, column_index(col), value);
}

/// Overload for ``operator<<``
std::ostream& operator<<(std::ostream& out, const TwoDA& tda);

} // namespace nw
