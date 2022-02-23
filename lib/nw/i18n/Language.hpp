#pragma once

#include "../util/string.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace nw {

/**
 * @brief Constants and related properties for supported languages
 * @note Might not be identical to what the game uses... yet.
 * Short codes taken from https://en.wikipedia.org/wiki/List_of_ISO_639-1_codes.
 * Encodings are probably right.
 */
struct Language {
    /// Language IDs
    enum ID : uint32_t {
        invalid = std::numeric_limits<uint32_t>::max(),
        // These might have changed with NWN:EE??
        english = 0,
        french = 1,
        german = 2,
        italian = 3,
        spanish = 4,
        polish = 5,
        korean = 128,              ///< Unsupported in EE?
        chinese_traditional = 129, ///< Unsupported in EE?
        chinese_simplified = 130,  ///< Unsupported in EE?
        japanese = 131             ///< Unsupported in EE?
    };
    /// @}

private:
    struct Properties {
        ID id;
        std::string_view lang_short;
        std::string_view lang_long;
        std::string_view encoding;
        bool has_feminine;
    };

    static const std::array<Properties, 10> language_table;
    static std::string default_encoding_;

public:
    /// Get default encoding, should maybe just use locale?  I dunno..
    /// Need some way to deal with CExoStrings.. default is cp-1251
    static const std::string& default_encoding();

    /// Gets the encoding for a particular language
    static std::string_view encoding(ID lang);

    /// Converts string (short or long form) to ID
    static Language::ID from_string(std::string_view lang);

    /// Determines if language has feminine translations
    static bool has_feminine(ID lang);

    /// Set default encoding, should maybe just use locale?  I dunno..
    static void set_default_encoding(std::string_view encoding);

    /// Converts language to string form
    static std::string_view to_string(ID lang, bool long_name = false);
};

} // namespace nw
