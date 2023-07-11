#include "TwoDA.hpp"

#include "../log.hpp"
#include "../util/string.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>

using namespace std::literals;

namespace nw {

namespace detail {

struct TwoDATokenizer {
    explicit TwoDATokenizer(std::string_view tda)
        : buffer{tda}
    {
    }

    std::string_view next()
    {
        std::string_view result;
        while (pos < buffer.size()) {
            switch (buffer[pos]) {
            default:
                start = pos++;
                while (pos < buffer.size()) {
                    if (buffer[pos] == ' '
                        || buffer[pos] == '\t'
                        || buffer[pos] == '\r'
                        || buffer[pos] == '\n') {
                        end = pos;
                        break;
                    }
                    ++pos;
                }
                if (pos == buffer.size()) end = buffer.size();
                result = std::string_view(&buffer[start], end - start);

                break;
            case '"':
                start = ++pos;
                while (pos < buffer.size()) {
                    if (buffer[pos] == '"' && buffer[pos - 1] != '\\') {
                        end = pos;
                        break;
                    }
                    ++pos;
                }
                if (pos == buffer.size() || buffer[pos] != '"')
                    throw std::runtime_error("Unterminated quote.");
                ++pos;

                result = std::string_view(&buffer[start], end - start);
                break;
            case '\r':
            case '\n':
                start = pos;
                if (buffer[pos] == '\r') ++pos;
                if (pos < buffer.size() && buffer[pos] == '\n') ++pos;
                result = std::string_view(&buffer[start], pos - start);
                ++line;
                break;
            case ' ':
            case '\t':
                ++pos;
                break;
            }
            if (result.size()) {
                break;
            }
        }
        return result;
    }

    size_t pos = 0, start = 0, end = 0, line = 0;
    std::string_view buffer;
};

} // namespace detail

TwoDA::TwoDA(const std::filesystem::path& filename)
    : TwoDA(ResourceData::from_file(filename))
{
}

TwoDA::TwoDA(ResourceData data)
    : data_{std::move(data)}
{
    is_loaded_ = parse();
}

size_t TwoDA::column_index(const std::string_view column) const
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

void TwoDA::pad(size_t count)
{
    size_t pad = count * columns_.size();
    for (size_t i = 0; i < pad; ++i) {
        rows_.emplace_back(std::string_view("****"));
    }
}

inline bool is_newline(std::string_view token)
{
    return !token.empty() && (token[0] == '\r' || token[0] == '\n');
}

inline bool needs_quote(std::string_view str)
{
    return str.find(' ') != std::string_view::npos;
}

bool TwoDA::parse()
{
    if (data_.bytes.size() == 0) {
        return false;
    }
    detail::TwoDATokenizer tknz{data_.bytes.string_view()};
    std::string_view tk;

    if (tknz.next() != "2DA" || tknz.next() != "V2.0") {
        LOG_F(ERROR, "Invalid 2DA Header");
        return false;
    }

    while (is_newline(tk = tknz.next()))
        ; // Drop new lines

    if (tk.empty()) {
        LOG_F(ERROR, "Invalid 2DA: No columns specified");
        return false;
    } else if (tk == "DEFAULT:") {
        tk = tknz.next();
        default_ = std::string(tk);
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

    size_t cur = 0, row = 0;
    for (; !tk.empty(); tk = tknz.next()) {
        if (!is_newline(tk)) {
            rows_.push_back(tk);
            bool quote = needs_quote(tk);
            size_t& width = widths_[cur % columns_.size()];
            width = std::max(width, tk.size() + (quote ? 2 : 0));
            ++cur;
        } else {
            ++row;

            int pad = 0, drop = 0;
            while (rows_.size() < row * columns_.size()) {
                rows_.emplace_back(std::string_view("****"));
                ++pad;
            }

            while (rows_.size() > row * columns_.size()) {
                rows_.pop_back();
                ++drop;
            }

            if (drop) {
                LOG_F(WARNING, "Row: {} - Incorrect column count dropped {} columns, line {}", row, drop, tknz.line - 1);
            } else if (pad) {
                LOG_F(WARNING, "Row: {} - Incorrect column count padded {} columns, line {}", row, pad, tknz.line - 1);
            }

            while (is_newline(tk))
                tk = tknz.next();
            // Row number will get dropped at start of next iteration
        }
    }

    return true;
}

TwoDARowView TwoDA::row(size_t row) const noexcept
{
    if (row >= rows()) {
        return {};
    }
    auto start = row * columns_.size();
    return {{&rows_[start], columns_.size()}, this, row};
}

size_t TwoDA::rows() const noexcept
{
    return columns_.empty() ? 0 : rows_.size() / columns_.size();
}

bool TwoDA::is_valid() const noexcept
{
    return is_loaded_;
}

std::ostream& operator<<(std::ostream& out, const nw::TwoDA& tda)
{
    if (!tda.is_valid()) {
        LOG_F(ERROR, "Attempting to output invalid TwoDA");
        return out;
    }

    const int pad = 4;
    std::string sep{32, ' '};

    out << "2DA V1.0" << std::endl;
    if (tda.default_.size()) {
        out << "DEFAULT: ";
        bool quote = needs_quote(tda.default_);
        if (quote) out << '"';
        out << tda.default_;
        if (quote) out << '"';
    }
    out << std::endl;

    std::string temp = std::to_string(tda.rows() - 1);
    size_t num_size = temp.size();
    sep.resize(pad + num_size, ' ');
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
    // Gonna do nested for loops for clarity.
    for (size_t i = 0; i < rows; ++i) {
        temp = std::to_string(cur);
        sep.resize(pad + num_size - temp.size(), ' ');
        out << temp << sep;

        size_t start = i * tda.columns(), stop = (i + 1) * tda.columns();
        for (size_t j = start; j < stop; ++j) {
            bool quote = needs_quote(tda.rows_[j].view);
            if (quote) out << '"';
            out << tda.rows_[j].view;
            if (quote) out << '"';
            if (j + 1 < stop) {
                size_t p = size_t(tda.widths_[j % tda.columns()] - int(tda.rows_[j].view.size()) + pad + (quote ? -2 : 0));
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
