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

bool LocString::add(LanguageID language, std::string_view string, bool feminine)
{
    if (language == LanguageID::invalid) { return false; }
    uint32_t l = Language::to_runtime_id(language, feminine);

    for (auto& [lang, str] : strings_) {
        if (lang == l) {
            str = std::string(string);
            return true;
        }
    }

    strings_.emplace_back(l, string);
    // Got to keep this thing sorted for == comparisons, etc.
    std::sort(strings_.begin(), strings_.end(), [](const LocStringPair& a, const LocStringPair& b) {
        return a.first < b.first;
    });

    return true;
}

bool LocString::contains(LanguageID language, bool feminine) const
{
    if (language == LanguageID::invalid) { return false; }
    uint32_t l = Language::to_runtime_id(language, feminine);

    for (const auto& [lang, str] : strings_) {
        if (lang == l) {
            return true;
        }
    }
    return false;
}

std::string LocString::get(LanguageID language, bool feminine) const
{
    if (language == LanguageID::invalid) { return {}; }
    uint32_t l = Language::to_runtime_id(language, feminine);

    for (const auto& [lang, str] : strings_) {
        if (lang == l) {
            return str;
        }
    }
    return {};
}

void LocString::remove(LanguageID language, bool feminine)
{
    uint32_t needle = Language::to_runtime_id(language, feminine);
    auto it = std::remove_if(begin(), end(), [needle](const LocStringPair& value) {
        return value.first == needle;
    });
    strings_.erase(it, std::end(strings_));
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

    uint32_t lang = std::numeric_limits<uint32_t>::max();
    std::string s;
    for (const auto& str : strings) {
        str.at("lang").get_to(lang);
        str.at("string").get_to(s);
        auto base_lang = Language::to_base_id(lang);
        loc.add(base_lang.first, s, base_lang.second);
    }
}

void to_json(nlohmann::json& j, const LocString& loc)
{
    j = nlohmann::json::object();
    j["strref"] = loc.strref();
    auto& arr = j["strings"] = nlohmann::json::array();
    for (const auto& [lang, string] : loc) {
        arr.push_back({{"lang", lang}, {"string", string}});
    }
}

} // namespace nw
