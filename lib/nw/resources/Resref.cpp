#include "Resref.hpp"

#include "../log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ostream>

namespace nw {

Resref::Resref(Storage data) noexcept
    : data_{data}
{
    std::transform(data_.begin(), data_.end(), data_.begin(), ::tolower);
}

Resref::Resref(const char* string) noexcept
    : Resref(std::string_view(string))
{
}

Resref::Resref(std::string_view string) noexcept
    : data_{}
{
    DCHECK_F(string.size() <= 16, "Resrefs are limited to 16 characters.");
    memcpy(data_.data(), string.data(), std::min(size_type(16), string.size()));
    std::transform(data_.begin(), data_.end(), data_.begin(), ::tolower);
}

bool Resref::empty() const noexcept { return !data_[0]; }

Resref::size_type Resref::length() const noexcept
{
    size_type i = 0;
    for (; i < max_size; ++i) {
        if (!data_[i]) { break; }
    }
    return i;
}

std::string Resref::string() const
{
    return std::string(view());
}

std::string_view Resref::view() const noexcept { return std::string_view(data_.data(), length()); }

bool operator==(const Resref& lhs, const Resref& rhs) noexcept { return lhs.view() == rhs.view(); }
bool operator!=(const Resref& lhs, const Resref& rhs) noexcept { return !(lhs == rhs); }
bool operator<(const Resref& lhs, const Resref& rhs) noexcept { return lhs.view() < rhs.view(); }

std::ostream& operator<<(std::ostream& out, const Resref& resref)
{
    out << resref.view();
    return out;
}

void from_json(const nlohmann::json& j, Resref& r)
{
    if (j.is_string()) {
        r = Resref{j.get<std::string>()};
    }
}

void to_json(nlohmann::json& j, const Resref& r)
{
    j = r.view();
}

} // namespace nw
