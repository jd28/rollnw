#include "Strings.hpp"

#include "../i18n/Language.hpp"
#include "../log.hpp"
#include "../util/profile.hpp"
#include "../util/templates.hpp"
#include "Config.hpp"
#include "Kernel.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace nw::kernel {

const std::type_index Strings::type_index{typeid(Strings)};

Strings::Strings(MemoryResource* memory)
    : Service(memory)
    , string_pool_{8192, 128, allocator()}
{
}

String Strings::get(uint32_t strref, bool feminine) const
{
    if (strref == 0xFFFFFFFF) { return ""; }
    String result;
    if (Tlk::custom_flag & strref) {
        strref ^= Tlk::custom_flag;
        result = feminine ? customf_.get(strref) : custom_.get(strref);
    } else {
        result = feminine ? dialogf_.get(strref) : dialog_.get(strref);
    }
    return !result.empty() ? result : fmt::format("Bad Strref ({})", strref);
}

String Strings::get(const LocString& locstring, bool feminine) const
{
    if (locstring.contains(global_lang_, feminine)) {
        return locstring.get(global_lang_, feminine);
    } else {
        return get(locstring.strref(), feminine);
    }
}

String Strings::get(TextRef ref, bool feminine) const
{
    return get(ref, global_lang_, feminine);
}

String Strings::get(TextRef ref, LanguageID language, bool feminine) const
{
    if (language == LanguageID::invalid) { return ""; }

    const TextEntry* entry = get_text_entry(ref);
    if (!entry) { return ""; }

    if (entry->overrides.contains(language, feminine)) {
        return entry->overrides.get(language, feminine);
    }

    if (entry->text.contains(language, feminine)) {
        return entry->text.get(language, feminine);
    }

    return get(entry->text.strref(), feminine);
}

TextRef Strings::make_text_ref(uint32_t strref)
{
    text_entries_.push_back(TextEntry{LocString{strref}, LocString{}});
    return TextRef{static_cast<uint32_t>(text_entries_.size())};
}

TextRef Strings::make_text_ref(const LocString& locstring)
{
    TextRef ref = make_text_ref(locstring.strref());
    TextEntry* entry = get_text_entry(ref);
    if (entry) {
        entry->text = locstring;
    }
    return ref;
}

TextRef Strings::clone_text_ref(TextRef ref)
{
    const TextEntry* entry = get_text_entry(ref);
    if (!entry) { return {}; }

    text_entries_.push_back(*entry);
    return TextRef{static_cast<uint32_t>(text_entries_.size())};
}

bool Strings::set_text(TextRef ref, LanguageID language, StringView str, bool feminine)
{
    TextEntry* entry = get_text_entry(ref);
    if (!entry) { return false; }
    return entry->text.add(language, str, feminine);
}

bool Strings::set_text_override(TextRef ref, LanguageID language, StringView str, bool feminine)
{
    TextEntry* entry = get_text_entry(ref);
    if (!entry) { return false; }
    return entry->overrides.add(language, str, feminine);
}

void Strings::clear_text_override(TextRef ref, LanguageID language, bool feminine)
{
    TextEntry* entry = get_text_entry(ref);
    if (!entry) { return; }
    entry->overrides.remove(language, feminine);
}

LocString Strings::to_locstring(TextRef ref, bool include_overrides) const
{
    const TextEntry* entry = get_text_entry(ref);
    if (!entry) { return LocString{}; }

    LocString result = entry->text;
    if (include_overrides) {
        for (const auto& [lang, string] : entry->overrides) {
            auto base_lang = Language::to_base_id(lang);
            result.add(base_lang.first, string, base_lang.second);
        }
    }
    return result;
}

Strings::TextEntry* Strings::get_text_entry(TextRef ref)
{
    if (!ref.valid() || ref.value > text_entries_.size()) { return nullptr; }
    return &text_entries_[ref.value - 1];
}

const Strings::TextEntry* Strings::get_text_entry(TextRef ref) const
{
    if (!ref.valid() || ref.value > text_entries_.size()) { return nullptr; }
    return &text_entries_[ref.value - 1];
}

InternedString Strings::get_interned(StringView str) const
{
    auto it = interned_.find(str);
    if (it != std::end(interned_)) {
        return InternedString{&*it};
    }
    return {};
}

void Strings::initialize(ServiceInitTime time)
{
    NW_PROFILE_SCOPE_N("strings.initialize");

    if (time != ServiceInitTime::kernel_start && time != ServiceInitTime::module_pre_load) { return; }
    auto lang = Language::to_string(global_language());
    if (config().version() == GameVersion::vEE) {
        auto path = config().install_path() / "lang" / lang / "data" / "dialog.tlk";
        auto tlk_path = path.string();
        NW_PROFILE_TEXT(tlk_path.data(), tlk_path.size());
        {
            NW_PROFILE_SCOPE_N("strings.initialize.load_dialog_tlk");
            load_dialog_tlk(path);
        }
    }
}

InternedString Strings::intern(StringView str)
{
    if (str.empty()) {
        LOG_F(ERROR, "strings: attempting to intern empty string");
        return {};
    }
    return InternedString(&*interned_.insert(String(str)).first);
}

InternedString Strings::intern(uint32_t strref)
{
    return intern(get(strref));
}

void Strings::load_custom_tlk(const std::filesystem::path& path)
{
    custom_.load_from(path);
    if (custom_.language_id() != global_lang_) {
        LOG_F(WARNING, "tlk language does not match global language: {} != {}",
            to_underlying(custom_.language_id()), to_underlying(global_lang_));
    }

    if (Language::has_feminine(custom_.language_id())) {
        auto fem = path.parent_path() / (path.stem().string() + "f.tlk");
        LOG_F(INFO, "Strings checking for feminine tlk: '{}'", fem);
        if (fs::exists(fem)) {
            customf_.load_from(fem);
        }
    }
}

void Strings::load_dialog_tlk(const std::filesystem::path& path)
{
    dialog_.load_from(path);
    if (dialog_.language_id() != global_lang_) {
        LOG_F(WARNING, "tlk language does not match global language: {} != {}",
            to_underlying(dialog_.language_id()), to_underlying(global_lang_));
    }

    if (Language::has_feminine(dialog_.language_id())) {
        auto fem = path.parent_path() / (path.stem().string() + "f.tlk");
        LOG_F(INFO, "Strings checking for feminine tlk: '{}'", fem);
        if (fs::exists(fem)) {
            dialogf_.load_from(fem);
        }
    }
}

nw::MemoryPool* Strings::pool() noexcept
{
    return &string_pool_;
}

LanguageID Strings::global_language() const noexcept
{
    return global_lang_;
}

void Strings::set_global_language(LanguageID language) noexcept
{
    global_lang_ = language;
}

nlohmann::json Strings::stats() const
{
    nlohmann::json j;
    j["strings service"] = {
        {"total_interned_strings", interned_.size()},
    };
    return j;
}

void Strings::unload_custom_tlk()
{
    custom_.reset(global_lang_);
    customf_.reset(global_lang_);
}

} // namespace nw::kernel
