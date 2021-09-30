#include "Language.hpp"

namespace nw {

const std::vector<Language::Properties> Language::language_table{
    {Language::english, u8"en", u8"English", u8"CP1252", false},
    {Language::french, u8"fr", u8"French", u8"CP1252", true},
    {Language::german, u8"de", u8"German", u8"CP1252", true},
    {Language::italian, u8"it", u8"Italian", u8"CP1252", true},
    {Language::spanish, u8"es", u8"Spanish", u8"CP1252", true},
    {Language::polish, u8"pl", u8"Polish", u8"CP1250", true},
    // No clue if EE even supports the following.
    {Language::korean, u8"ko", u8"Korean", u8"CP949", true},
    {Language::chinese_simplified, u8"zh-cn", u8"Chinese Simplified", u8"CP936", true},
    {Language::chinese_traditional, u8"zh-tw", u8"Chinese Traditional", u8"CP950", true},
    {Language::japanese, u8"ja", u8"Japanese", u8"CP932", true},
};

std::string Language::default_encoding_ = "CP1252";

const std::string& Language::default_encoding() { return Language::default_encoding_; }

const char* Language::encoding(Language::ID lang)
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
