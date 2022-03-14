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

    virtual std::string get(const LocString& locstring, bool feminine = false) const;
    virtual std::string get(uint32_t strref, bool feminine = false) const;
    virtual InternedString intern(std::string_view str);
    virtual void load_custom_tlk(const std::filesystem::path& path);
    virtual void load_dialog_tlk(const std::filesystem::path& path);
    LanguageID global_language() const noexcept;
    void set_global_language(LanguageID language) noexcept;

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
    explicit InternedString(const std::string* str)
        : string_{str}
    {
    }

    int compare(std::string_view other) const noexcept { return view().compare(other); }
    std::string_view view() const noexcept { return *string_; }
    operator std::string_view() const noexcept { return view(); }
    bool operator==(InternedString rhs) const noexcept { return string_ == rhs.string_; }
    bool operator==(std::string_view rhs) const noexcept { return compare(rhs) == 0; }
    bool operator<(std::string_view rhs) const noexcept { return compare(rhs) < 0; }
    bool operator>(std::string_view rhs) const noexcept { return compare(rhs) > 0; }

private:
    const std::string* string_ = nullptr;
};

} // namespace nw::kernel
