#include "StaticTwoDA.hpp"

#include "TwoDATokenizer.hpp"

using namespace std::literals;

namespace nw {

StaticTwoDA::StaticTwoDA(const std::filesystem::path& filename)
    : StaticTwoDA(ResourceData::from_file(filename))
{
}

StaticTwoDA::StaticTwoDA(ResourceData data)
    : data_{std::move(data)}
{
    is_loaded_ = parse();
}

StaticTwoDA::StaticTwoDA(std::string_view str)
{
    data_.name = Resource("<source>"sv, ResourceType::twoda);
    data_.bytes.append(str.data(), str.size());
    is_loaded_ = parse();
}

size_t StaticTwoDA::column_index(const StringView column) const
{
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (string::icmp(columns_[i], column))
            return i;
    }
    return npos;
}

size_t StaticTwoDA::columns() const noexcept
{
    return columns_.size();
}

StringView StaticTwoDA::get_internal(size_t row, size_t col) const
{
    size_t idx = row * columns_.size() + col;
    CHECK_F(idx < rows_.size(), "[2da] {}: out of bounds row {}, col {}", data_.name.resref.view(), row, col);
    return rows_[idx];
}

bool StaticTwoDA::parse()
{
    if (data_.bytes.size() == 0) {
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
    }

    while (is_newline(tknz.next()))
        ;

    tk = tknz.next(); // Drop row number

    size_t cur = 0, row = 0;
    for (; !tk.empty(); tk = tknz.next()) {
        if (!is_newline(tk)) {
            rows_.push_back(tk);
            ++cur;
        } else {
            ++row;

            int pad = 0, drop = 0;
            while (rows_.size() < row * columns_.size()) {
                rows_.emplace_back(StringView("****"));
                ++pad;
            }

            while (rows_.size() > row * columns_.size()) {
                rows_.pop_back();
                ++drop;
            }

            if (drop) {
                LOG_F(WARNING, "[2da] {}:{} - Incorrect column count dropped {} columns, line {}",
                    data_.name.resref.view(), row, drop, tknz.line - 1);
            } else if (pad) {
                LOG_F(WARNING, "[2da] {}:{} - Incorrect column count padded {} columns, line {}",
                    data_.name.resref.view(), row, pad, tknz.line - 1);
            }

            while (is_newline(tk))
                tk = tknz.next();
            // Row number will get dropped at start of next iteration
        }
    }

    return true;
}

TwoDARowView StaticTwoDA::row(size_t row) const noexcept
{
    if (row >= rows()) {
        return {};
    }
    auto start = row * columns_.size();
    return {{&rows_[start], columns_.size()}, this, row};
}

size_t StaticTwoDA::rows() const noexcept
{
    return columns_.empty() ? 0 : rows_.size() / columns_.size();
}

bool StaticTwoDA::is_valid() const noexcept
{
    return is_loaded_;
}

} // namespace nw
