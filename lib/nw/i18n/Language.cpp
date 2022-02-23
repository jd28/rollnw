#include "Language.hpp"

namespace nw {

const std::array<Language::Properties, 10> Language::language_table{
    Language::Properties{Language::english, "en", "English", "CP1252", false},
    Language::Properties{Language::french, "fr", "French", "CP1252", true},
    Language::Properties{Language::german, "de", "German", "CP1252", true},
    Language::Properties{Language::italian, "it", "Italian", "CP1252", true},
    Language::Properties{Language::spanish, "es", "Spanish", "CP1252", true},
    Language::Properties{Language::polish, "pl", "Polish", "CP1250", true},
    // No clue if EE even supports the following.
    Language::Properties{Language::korean, "ko", "Korean", "CP949", true},
    Language::Properties{Language::chinese_simplified, "zh-cn", "Chinese Simplified", "CP936", true},
    Language::Properties{Language::chinese_traditional, "zh-tw", "Chinese Traditional", "CP950", true},
    Language::Properties{Language::japanese, "ja", "Japanese", "CP932", true},
};

std::string Language::default_encoding_ = "CP1252";

const std::string& Language::default_encoding() { return Language::default_encoding_; }

std::string_view Language::encoding(Language::ID lang)
{
    for (const auto& info : language_table) {
        if (info.id == lang) { return info.encoding; }
    }
    return nullptr;
}

Language::ID Language::from_string(std::string_view lang)
{
    for (const auto& info : language_table) {
        if (string::icmp(info.lang_long, lang) || string::icmp(info.lang_short, lang)) {
            return info.id;
        }
    }
    return Language::invalid;
}

bool Language::has_feminine(Language::ID lang)
{
    for (const auto& info : language_table) {
        if (info.id == lang) { return info.has_feminine; }
    }
    return false;
}

std::string_view Language::to_string(Language::ID lang, bool long_name)
{
    for (const auto& info : language_table) {
        if (info.id == lang) {
            return long_name ? info.lang_long : info.lang_short;
        }
    }
    return {};
}

void Language::set_default_encoding(std::string_view encoding)
{
    Language::default_encoding_ = std::string(encoding);
}

} // namespace nw
