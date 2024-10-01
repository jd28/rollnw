#include "Resref.hpp"

#include "../kernel/Kernel.hpp"
#include "../log.hpp"

#include <nlohmann/json.hpp>

#include <ostream>

namespace nw {

Resref::Resref()
{
    data_.fill(0);
}

Resref::Resref(const char* string) noexcept
    : Resref(StringView(string))
{
}

Resref::Resref(StringView string) noexcept
{
    data_.fill(0);

    if (string.length() > nw::kernel::config().max_resref_length()) {
        LOG_F(ERROR, "invalid resref: '{}', resref must be less than {} characters",
            string, nw::kernel::config().max_resref_length());
        return;
    }
    memcpy(data_.data(), string.data(), std::min(nw::kernel::config().max_resref_length(), string.size()));
    std::transform(data_.begin(), data_.end(), data_.begin(), [](char c) {
        return static_cast<char>(::tolower(c));
    });
}

const Resref::Storage& Resref::data() const noexcept { return data_; }

bool Resref::empty() const noexcept { return !data_[0]; }

Resref::size_type Resref::length() const noexcept
{
    size_type i = 0;
    for (; i < nw::kernel::config().max_resref_length(); ++i) {
        if (!data_[i]) { break; }
    }
    return i;
}

String Resref::string() const
{
    return String(view());
}

StringView Resref::view() const noexcept { return StringView(data_.data(), length()); }

std::ostream& operator<<(std::ostream& out, const Resref& resref)
{
    out << resref.view();
    return out;
}

void from_json(const nlohmann::json& j, Resref& r)
{
    if (j.is_string()) {
        r = Resref{j.get<String>()};
    }
}

void to_json(nlohmann::json& j, const Resref& r)
{
    j = r.view();
}

} // namespace nw
