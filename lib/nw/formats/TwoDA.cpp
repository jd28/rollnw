#include "TwoDA.hpp"

#include "../log.hpp"
#include "../util/string.hpp"
#include "TwoDATokenizer.hpp"

#include <algorithm>
#include <charconv>
#include <iostream>

using namespace std::literals;

namespace nw {

// == TwoDA ===================================================================
// ============================================================================

TwoDA::TwoDA(const std::filesystem::path& filename)
    : TwoDA(ResourceData::from_file(filename))
{
}

TwoDA::TwoDA(ResourceData data)
    : data_{std::move(data)}
{
    is_loaded_ = parse();
}

TwoDA::TwoDA(std::string_view str)
{
    data_.name = Resource("<source>"sv, ResourceType::twoda);
    data_.bytes.append(str.data(), str.size());
    is_loaded_ = parse();
}

bool TwoDA::add_column(StringView name)
{
    if (column_index(name) != npos) {
        return false;
    }
    columns_.push_back(String{name});
    widths_.push_back(std::max(size_t(4), name.size()));
    for (auto& row : rows_) {
        row.push_back(StringView("****"));
    }
    return true;
}

size_t TwoDA::column_index(const StringView column) const
{
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (string::icmp(columns_[i], column))
            return i;
    }
    return npos;
}

size_t TwoDA::columns() const noexcept
{
    return columns_.size();
}

StringView TwoDA::get_raw(size_t row, size_t col) const
{
    CHECK_F(row < rows_.size(), "[2da] {}: out of bounds row {}, col {}", data_.name.resref.view(), row, col);
    CHECK_F(col < columns_.size(), "[2da] {}: out of bounds row {}, col {}", data_.name.resref.view(), row, col);
    return rows_[row][col].view;
}

void TwoDA::pad(size_t count)
{
    RowType p;
    for (size_t i = 0; i < columns(); ++i) {
        p.push_back(StringView("****"));
    }

    for (size_t i = 0; i < count; ++i) {
        rows_.push_back(p);
    }
}

size_t TwoDA::rows() const noexcept
{
    return rows_.size();
}

bool TwoDA::is_valid() const noexcept
{
    return is_loaded_;
}

inline bool needs_quote(StringView str)
{
    return str.find(' ') != StringView::npos;
}

bool TwoDA::parse()
{
    if (data_.bytes.size() == 0) {
        LOG_F(INFO, "No data");
        return false;
    }

    TwoDATokenizer tknz{data_.bytes.string_view()};
    StringView tk;

    if (tknz.next() != "2DA" || tknz.next() != "V2.0") {
        LOG_F(ERROR, "[2da] {}: invalid header", data_.name.resref.view());
        return false;
    }

    while (is_newline(tk = tknz.next()))
        ; // Drop new lines

    if (tk.empty()) {
        LOG_F(ERROR, "[2da] {}: No columns specified", data_.name.resref.view());
        return false;
    } else if (tk == "DEFAULT:") {
        tk = tknz.next();
        default_ = String(tk);
        while (is_newline(tk = tknz.next())) // Drop new lines
            ;
    } // If we make it here first column is in tk.

    // Columns
    for (; !is_newline(tk) && !tk.empty(); tk = tknz.next()) {
        columns_.emplace_back(tk);
        widths_.push_back(tk.size());
    }

    while (is_newline(tknz.next()))
        ;

    tk = tknz.next(); // Drop row number

    RowType next;
    next.reserve(columns());
    size_t cur = 0, row = 0;
    for (; !tk.empty(); tk = tknz.next()) {
        if (!is_newline(tk)) {
            next.push_back(tk);
            bool quote = needs_quote(tk);
            size_t& width = widths_[cur % columns_.size()];
            width = std::max(width, tk.size() + (quote ? 2 : 0));
            ++cur;
        } else {
            ++row;

            int pad = 0, drop = 0;
            while (next.size() < columns_.size()) {
                next.emplace_back(StringView("****"));
                ++pad;
            }

            while (next.size() > columns_.size()) {
                next.pop_back();
                ++drop;
            }

            if (drop) {
                LOG_F(WARNING, "[2da] {}:{} - Incorrect column count dropped {} columns, line {}",
                    data_.name.resref.view(), row, drop, tknz.line - 1);
            } else if (pad) {
                LOG_F(WARNING, "[2da] {}:{} - Incorrect column count padded {} columns, line {}",
                    data_.name.resref.view(), row, pad, tknz.line - 1);
            }

            rows_.push_back(next);
            next.clear();
            while (is_newline(tk))
                tk = tknz.next();
            // Row number will get dropped at start of next iteration
        }
    }

    return true;
}

std::ostream& operator<<(std::ostream& out, const nw::TwoDA& tda)
{
    if (!tda.is_valid()) {
        LOG_F(ERROR, "Attempting to output invalid TwoDA");
        return out;
    }

    const int pad = 4;
    String sep{32, ' '};

    out << "2DA V2.0" << std::endl;
    if (tda.default_.size()) {
        out << "DEFAULT: ";
        bool quote = needs_quote(tda.default_);
        if (quote) out << '"';
        out << tda.default_;
        if (quote) out << '"';
    }
    out << std::endl;

    char buffer[24] = {0};
    auto res = std::to_chars(buffer, buffer + 24, tda.rows() - 1);
    size_t max_size = res.ptr - buffer;
    sep.resize(pad + max_size, ' ');
    out << sep;

    for (size_t i = 0; i < tda.columns_.size(); ++i) {
        out << tda.columns_[i];
        if (i + 1 < tda.columns_.size()) {
            sep.resize(size_t(pad + tda.widths_[i] - int(tda.columns_[i].size())), ' ');
            out << sep;
        }
    }
    out << std::endl;

    size_t cur = 0;

    size_t rows = tda.rows();
    for (size_t i = 0; i < rows; ++i) {
        char buffer[24] = {0};
        auto res = std::to_chars(buffer, buffer + 24, i);
        size_t cur_size = res.ptr - buffer;
        sep.resize(pad + max_size - cur_size, ' ');
        out << buffer << sep;

        for (size_t j = 0; j < tda.columns(); ++j) {
            bool quote = needs_quote(tda.rows_[i][j].view);
            if (quote) out << '"';
            out << tda.rows_[i][j].view;
            if (quote) out << '"';
            if (j + 1 < tda.columns()) {
                size_t p = size_t(tda.widths_[j] - int(tda.rows_[i][j].view.size()) + pad + (quote ? -2 : 0));
                sep.resize(p, ' ');
                out << sep;
            }
        }

        out << std::endl;
        ++cur;
    }

    return out;
}

} // namespace nw
