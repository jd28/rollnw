#pragma once

#include "../util/string.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace nw {

/// Language IDs
enum struct LanguageID : uint32_t {
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

/**
 * @brief Constants and related properties for supported languages
 * @note Might not be identical to what the game uses... yet.
 * Short codes taken from https://en.wikipedia.org/wiki/List_of_ISO_639-1_codes.
 * Encodings are probably right.
 */
struct Language {
private:
    struct Properties {
        LanguageID id;
        std::string_view lang_short;
        std::string_view lang_long;
        std::string_view encoding;
        bool has_feminine;
    };

    static const std::array<Properties, 10> language_table;

public:
    /// Gets the encoding for a particular language
    static std::string_view encoding(LanguageID lang);

    /// Converts string (short or long form) to ID
    static LanguageID from_string(std::string_view lang);

    /// Determines if language has feminine translations
    static bool has_feminine(LanguageID lang);

    /// Convert runtime language identifier to base language and bool indicating
    /// masc/fem.
    static std::pair<LanguageID, bool> to_base_id(uint32_t lang);

    /// Convert language ID to runtime identifier.
    static uint32_t to_runtime_id(LanguageID lang, bool feminine = false);

    /// Converts language to string form
    static std::string_view to_string(LanguageID lang, bool long_name = false);
};

} // namespace nw
