#include "Language.hpp"

#include "../util/templates.hpp"

namespace nw {

const std::array<Language::Properties, 10> Language::language_table{
    Language::Properties{LanguageID::english, "en", "English", "CP1252", false},
    Language::Properties{LanguageID::french, "fr", "French", "CP1252", true},
    Language::Properties{LanguageID::german, "de", "German", "CP1252", true},
    Language::Properties{LanguageID::italian, "it", "Italian", "CP1252", true},
    Language::Properties{LanguageID::spanish, "es", "Spanish", "CP1252", true},
    Language::Properties{LanguageID::polish, "pl", "Polish", "CP1250", true},
    // No clue if EE even supports the following.
    Language::Properties{LanguageID::korean, "ko", "Korean", "CP949", true},
    Language::Properties{LanguageID::chinese_simplified, "zh-cn", "Chinese Simplified", "CP936", true},
    Language::Properties{LanguageID::chinese_traditional, "zh-tw", "Chinese Traditional", "CP950", true},
    Language::Properties{LanguageID::japanese, "ja", "Japanese", "CP932", true},
};

std::string_view Language::encoding(LanguageID lang)
{
    for (const auto& info : language_table) {
        if (info.id == lang) { return info.encoding; }
    }
    return {};
}

LanguageID Language::from_string(std::string_view lang)
{
    for (const auto& info : language_table) {
        if (string::icmp(info.lang_long, lang) || string::icmp(info.lang_short, lang)) {
            return info.id;
        }
    }
    return LanguageID::invalid;
}

bool Language::has_feminine(LanguageID lang)
{
    for (const auto& info : language_table) {
        if (info.id == lang) { return info.has_feminine; }
    }
    return false;
}

std::pair<LanguageID, bool> Language::to_base_id(uint32_t lang)
{
    bool fem = !!(lang % 2);
    if (fem) { --lang; }
    auto id = static_cast<LanguageID>(lang /= 2);
    for (const auto& info : language_table) {
        if (info.id == id) {
            return {id, fem};
        }
    }
    return {LanguageID::invalid, false};
}

uint32_t Language::to_runtime_id(LanguageID lang, bool feminine)
{
    if (lang == LanguageID::invalid) { return 0xFFFFFFFF; }
    uint32_t l = to_underlying(lang) * 2;
    if (feminine) { ++l; }
    return l;
}

std::string_view Language::to_string(LanguageID lang, bool long_name)
{
    for (const auto& info : language_table) {
        if (info.id == lang) {
            return long_name ? info.lang_long : info.lang_short;
        }
    }
    return {};
}

} // namespace nw
