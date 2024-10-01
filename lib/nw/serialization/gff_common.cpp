#include "gff_common.hpp"

#include "../log.hpp"

namespace nw {

GffLabel::GffLabel()
{
    data_.fill(0);
}

GffLabel::GffLabel(Storage data) noexcept
    : data_{data}
{
}

GffLabel::GffLabel(const char* string) noexcept
    : GffLabel(StringView(string))
{
}

GffLabel::GffLabel(StringView string) noexcept
    : data_{}
{
    DCHECK_F(string.size() <= 16, "GffLabels are limited to 16 characters.");
    memcpy(data_.data(), string.data(), std::min(size_type(16), string.size()));
}

bool GffLabel::empty() const noexcept { return !data_[0]; }

GffLabel::size_type GffLabel::length() const noexcept
{
    size_type i = 0;
    for (; i < max_size; ++i) {
        if (!data_[i]) { break; }
    }
    return i;
}

String GffLabel::string() const
{
    return String(view());
}

StringView GffLabel::view() const noexcept { return StringView(data_.data(), length()); }

} // namespace nw
