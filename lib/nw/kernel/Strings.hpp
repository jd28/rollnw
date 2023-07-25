#pragma once

#include "../legacy/LocString.hpp"
#include "../legacy/Tlk.hpp"
#include "../log.hpp"
#include "../util/InternedString.hpp"
#include "Kernel.hpp"

#include <absl/container/node_hash_set.h>

#include <filesystem>
#include <string_view>

namespace nw::kernel {

struct Strings : public Service {
    Strings() = default;
    virtual ~Strings() = default;

    /// Initializes strings system
    virtual void initialize() override;

    /// Initializes strings system
    virtual void clear() override
    {
        unload_custom_tlk();
    }

    /// Gets string by LocString
    /// @note if Tlk strref, use that; if not look in localized strings
    std::string get(const LocString& locstring, bool feminine = false) const;

    /// Gets string by Tlk strref
    std::string get(uint32_t strref, bool feminine = false) const;

    /// Gets interned string
    /// @note Return will not be valid if there is no interned string
    InternedString get_interned(std::string_view str) const;

    /// Interns a string
    /// @note Multiple calls to `intern` with the same string will and must return
    /// the same exact underlying string, such that equality can be determined
    /// by a comparison of pointers.
    InternedString intern(std::string_view str);

    /// Interns a string by strref
    /// @note Multiple calls to `intern` with the same string will and must return
    /// the same exact underlying string, such that equality can be determined
    /// by a comparison of pointers.
    InternedString intern(uint32_t strref);

    /// Loads a modules custom Tlk and feminine version if available
    void load_custom_tlk(const std::filesystem::path& path);

    /// Loads a dialog Tlk and feminine version if available
    void load_dialog_tlk(const std::filesystem::path& path);

    /// Gets the language ID that is considered 'default'
    /// @note This determines the character encoding of strings as they are stored
    /// in game resources, TLK, GFF, etc.  In EE the only encoding that isn't CP1252
    /// is Polish, so generally safe to not worry too much.
    LanguageID global_language() const noexcept;

    /// Sets the language ID that is considered 'default'
    void set_global_language(LanguageID language) noexcept;

    /// Unloads a modules custom Tlk and feminine version if available
    void unload_custom_tlk();

private:
    Tlk dialog_;
    Tlk dialogf_;
    Tlk custom_;
    Tlk customf_;

    // Node hash set for pointer stability
    absl::node_hash_set<std::string> interned_;

    LanguageID global_lang_ = LanguageID::english;
};

inline Strings& strings()
{
    auto res = services().strings.get();
    if (!res) {
        LOG_F(FATAL, "kernel: unable to load strings service");
    }
    return *res;
}

} // namespace nw::kernel
