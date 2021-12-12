#include "LocString.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

LocString::LocString(uint32_t strref)
    : strref_{strref}
{
}

bool LocString::operator==(const LocString& other) const
{
    return strref_ == other.strref_ && strings_ == other.strings_;
}

LocString::iterator LocString::begin() { return strings_.begin(); }
LocString::iterator LocString::end() { return strings_.end(); }
LocString::const_iterator LocString::begin() const { return strings_.begin(); }
LocString::const_iterator LocString::end() const { return strings_.end(); }

void LocString::add(uint32_t language, std::string string, bool feminine, bool force_language)
{
    if (!force_language) {
        language *= 2;
        if (feminine) ++language;
    }

    for (auto& [lang, str] : strings_) {
        if (lang == language) {
            str = std::move(string);
            return;
        }
    }

    strings_.emplace_back(language, std::move(string));
    // Got to keep this thing sorted for == comparisons, etc.
    std::sort(strings_.begin(), strings_.end(), [](const LocStringPair& a, const LocStringPair& b) {
        return a.first < b.first;
    });
}

bool LocString::contains(uint32_t language, bool feminine) const
{
    language *= 2;
    if (feminine) ++language;
    for (const auto& [lang, str] : strings_) {
        if (lang == language) {
            return true;
        }
    }
    return false;
}

std::string LocString::get(uint32_t language, bool feminine) const
{
    language *= 2;
    if (feminine) ++language;

    for (const auto& [lang, str] : strings_) {
        if (lang == language) {
            return str;
        }
    }
    return {};
}

size_t LocString::size() const
{
    return strings_.size();
}

uint32_t LocString::strref() const
{
    return strref_;
}

void from_json(const nlohmann::json& j, LocString& loc)
{
    loc = LocString(j.at("strref").get<uint32_t>());
    auto strings = j.at("strings");

    uint32_t lang = ~0;
    std::string s;
    for (const auto& str : strings) {
        str.at("lang").get_to(lang);
        str.at("string").get_to(s);
        loc.add(lang, s, false, true);
    }
}

void to_json(nlohmann::json& j, const LocString& loc)
{
    j = nlohmann::json::object();
    j["strref"] = loc.strref();
    if (loc.size()) {
        auto& arr = j["strings"] = nlohmann::json::array();
        for (const auto& [lang, string] : loc) {
            arr.push_back({{"lang", lang}, {"string", string}});
        }
    }
}

} // namespace nw
