#pragma once

#include "../log.hpp"
#include "../resources/ResourceData.hpp"
#include "../util/string.hpp"

#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>

namespace nw {

struct TwoDARowView;

// == TwoDA ===================================================================

struct StaticTwoDA final {
    static constexpr size_t npos = std::numeric_limits<size_t>::max();

    StaticTwoDA() = default;
    StaticTwoDA(const StaticTwoDA&) = delete;
    StaticTwoDA(StaticTwoDA&&) = default;

    StaticTwoDA& operator=(const StaticTwoDA&) = delete;
    StaticTwoDA& operator=(StaticTwoDA&&) = default;

    /// Constructs TwoDA object from a file
    explicit StaticTwoDA(const std::filesystem::path& filename);

    /// Constructs TwoDA object from an array of bytes
    explicit StaticTwoDA(ResourceData data);

    /// Constructs TwoDA object from string
    explicit StaticTwoDA(std::string_view str);

    /// Finds the index of a column, or -1
    size_t column_index(StringView column) const;

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

    /// Gets entire row as
    TwoDARowView row(size_t row) const noexcept;

    /// Number of rows
    size_t rows() const noexcept;

    /// Is the 2da parsed without error
    bool is_valid() const noexcept;

private:
    ResourceData data_;
    bool is_loaded_ = false;
    Vector<StringView> rows_;
    String default_;
    Vector<String> columns_;

    StringView get_internal(size_t row, size_t col) const;
    bool parse();
};

template <typename T>
std::optional<T> StaticTwoDA::get(size_t row, size_t col) const
{
    T temp;
    return get_to(row, col, temp)
        ? std::optional<T>{std::move(temp)}
        : std::optional<T>{};
}

template <typename T>
std::optional<T> StaticTwoDA::get(size_t row, StringView col) const
{
    size_t ci = column_index(col);
    if (ci == npos) {
        LOG_F(WARNING, "unknown column: {}", col);
        return std::optional<T>{};
    }

    return get<T>(row, ci);
}

template <typename T>
bool StaticTwoDA::get_to(size_t row, size_t col, T& out) const
{
    static_assert(
        std::is_same_v<T, String> || std::is_same_v<T, float> || std::is_convertible_v<T, int32_t> || std::is_same_v<T, StringView>,
        "TwoDA only supports float, String, StringView, or anything convertible to int32_t");

    StringView res = get_internal(row, col);
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
bool StaticTwoDA::get_to(size_t row, StringView col, T& out) const
{
    size_t ci = column_index(col);
    if (ci == npos) {
        LOG_F(WARNING, "unknown column: {}", col);
        return false;
    }
    return get_to<T>(row, ci, out);
}

// == TwoDARowView ============================================================

/**
 * @brief A view of a row in a TwoDA file
 * @note This behaves as ``StringView``, i.e. constant view only
 */
struct TwoDARowView {
    std::span<const StringView> data;
    const StaticTwoDA* parent = nullptr;
    size_t row_number = StaticTwoDA::npos;

    /// Gets an element
    template <typename T>
    std::optional<T> get(size_t col) const;

    /// Gets an element
    template <typename T>
    std::optional<T> get(StringView col) const;

    /// Gets an element
    template <typename T>
    bool get_to(size_t col, T& out) const;

    /// Gets an element
    template <typename T>
    bool get_to(StringView col, T& out) const;

    /// Gets the number of columns
    size_t size() const noexcept { return data.size(); }
};

template <typename T>
std::optional<T> TwoDARowView::get(size_t col) const
{
    if (!parent || col >= data.size()) {
        return {};
    }
    return parent->get<T>(row_number, col);
}

template <typename T>
std::optional<T> TwoDARowView::get(StringView col) const
{
    if (!parent) {
        return {};
    }
    return parent->get<T>(row_number, col);
}

template <typename T>
bool TwoDARowView::get_to(size_t col, T& out) const
{
    if (!parent || col >= data.size()) {
        return false;
    }
    return parent->get_to(row_number, col, out);
}

template <typename T>
bool TwoDARowView::get_to(StringView col, T& out) const
{
    if (!parent) {
        return false;
    }
    return parent->get_to(row_number, col, out);
}

}
