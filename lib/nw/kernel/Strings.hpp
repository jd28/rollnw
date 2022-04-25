#pragma once

#include "../i18n/LocString.hpp"
#include "../i18n/Tlk.hpp"

#include <absl/container/node_hash_set.h>

#include <filesystem>
#include <string_view>

namespace nw::kernel {

struct InternedString;

struct Strings {
    Strings() = default;
    virtual ~Strings() = default;

    /// Gets string by LocString
    /// @note if Tlk strref, use that; if not look in localized strings
    virtual std::string get(const LocString& locstring, bool feminine = false) const;

    /// Gets string by Tlk strref
    virtual std::string get(uint32_t strref, bool feminine = false) const;

    /// Gets interned string
    /// @note Return will not be valid if there is no interned string
    virtual InternedString get_interned(std::string_view str) const;

    /// Initializes strings system
    virtual void initialize();

    /// Interns a string
    /// @note Multiple calls to `intern` with the same string will and must return
    /// the same exact underlying string, such that equality can be determined
    /// by a comparison of pointers.
    virtual InternedString intern(std::string_view str);

    /// Interns a string by strref
    /// @note Multiple calls to `intern` with the same string will and must return
    /// the same exact underlying string, such that equality can be determined
    /// by a comparison of pointers.
    virtual InternedString intern(uint32_t strref);

    /// Loads a modules custom Tlk and feminine version if available
    virtual void load_custom_tlk(const std::filesystem::path& path);

    /// Loads a dialog Tlk and feminine version if available
    virtual void load_dialog_tlk(const std::filesystem::path& path);

    /// Gets the language ID that is considered 'default'
    /// @note This determines the character encoding of strings as they are stored
    /// in game resources, TLK, GFF, etc.  In EE the only encoding that isn't CP1252
    /// is Polish, so generally safe to not worry too much.
    LanguageID global_language() const noexcept;

    /// Sets the language ID that is considered 'default'
    void set_global_language(LanguageID language) noexcept;

    /// Unloads a modules custom Tlk and feminine version if available
    virtual void unload_custom_tlk();

private:
    Tlk dialog_;
    Tlk dialogf_;
    Tlk custom_;
    Tlk customf_;

    absl::node_hash_set<std::string> interned_;

    LanguageID global_lang_ = LanguageID::english;
    bool is_valid_ = false;
};

struct InternedString {
    InternedString() = default;
    explicit InternedString(const std::string* str)
        : string_{str}
    {
    }

    int compare(std::string_view other) const noexcept { return view().compare(other); }
    std::string_view view() const noexcept { return string_ ? *string_ : std::string_view{}; }
    operator bool() const noexcept { return !!string_; }
    operator std::string_view() const noexcept { return view(); }
    bool operator==(InternedString rhs) const noexcept { return string_ == rhs.string_; }
    bool operator==(std::string_view rhs) const noexcept { return compare(rhs) == 0; }
    bool operator<(std::string_view rhs) const noexcept { return compare(rhs) < 0; }
    bool operator<(const InternedString& rhs) const noexcept { return string_ < rhs.string_; }
    bool operator>(std::string_view rhs) const noexcept { return compare(rhs) > 0; }
    bool operator>(const InternedString& rhs) const noexcept { return string_ > rhs.string_; }

private:
    template <typename H>
    friend H AbslHashValue(H h, const InternedString& res);

    const std::string* string_ = nullptr;
};

template <typename H>
H AbslHashValue(H h, const InternedString& str)
{
    return H::combine(std::move(h), str.view());
}

struct InternedStringHash {
    using is_transparent = void;

    size_t operator()(absl::string_view v) const
    {
        return absl::Hash<absl::string_view>{}(v);
    }

    size_t operator()(const InternedString& v) const
    {
        absl::string_view v2{v.view().data(), v.view().size()};
        return absl::Hash<absl::string_view>{}(v2);
    }
};

struct InternedStringEq {
    using is_transparent = void;

    bool operator()(const InternedString& a, absl::string_view b) const
    {
        return std::equal(a.view().begin(), a.view().end(), b.begin(), b.end());
    }
    bool operator()(absl::string_view b, const InternedString& a) const
    {
        return std::equal(a.view().begin(), a.view().end(), b.begin(), b.end());
    }
    bool operator()(const InternedString& a, const InternedString& b) const
    {
        return a.compare(b) == 0;
    }
    bool operator()(absl::string_view b, absl::string_view a) const
    {
        return std::equal(a.begin(), a.end(), b.begin(), b.end());
    }
};

} // namespace nw::kernel
