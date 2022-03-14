#include "Strings.hpp"

#include "../i18n/Language.hpp"
#include "../log.hpp"

namespace fs = std::filesystem;

namespace nw::kernel {

std::string Strings::get(uint32_t strref, bool feminine) const
{
    if (Tlk::custom_flag & strref) {
        strref ^= Tlk::custom_flag;
        return feminine ? customf_.get(strref) : custom_.get(strref);
    } else {
        return feminine ? dialogf_.get(strref) : dialog_.get(strref);
    }
}

std::string Strings::get(const LocString& locstring, bool feminine) const
{
    if (locstring.contains(global_lang_, feminine)) {
        return locstring.get(global_lang_, feminine);
    } else {
        return get(locstring.strref(), feminine);
    }
}

InternedString Strings::intern(std::string_view str)
{
    return InternedString(&*interned_.insert(std::string(str)).first);
}

void Strings::load_custom_tlk(const std::filesystem::path& path)
{
    custom_ = Tlk{path};
    if (custom_.language_id() != global_lang_) {
        LOG_F(WARNING, "tlk language does not match global language: {} != {}",
            custom_.language_id(), global_lang_);
    }

    if (Language::has_feminine(custom_.language_id())) {
        auto fem = path.parent_path() / (path.stem().string() + "f.tlk");
        LOG_F(INFO, "Strings checking for feminine tlk: '{}'", fem);
        if (fs::exists(fem)) {
            customf_ = Tlk{fem};
        }
    }
}

void Strings::load_dialog_tlk(const std::filesystem::path& path)
{
    dialog_ = Tlk{path};
    if (dialog_.language_id() != global_lang_) {
        LOG_F(WARNING, "tlk language does not match global language: {} != {}",
            dialog_.language_id(), global_lang_);
    }

    if (Language::has_feminine(dialog_.language_id())) {
        auto fem = path.parent_path() / (path.stem().string() + "f.tlk");
        LOG_F(INFO, "Strings checking for feminine tlk: '{}'", fem);
        if (fs::exists(fem)) {
            dialogf_ = Tlk{fem};
        }
    }
}

LanguageID Strings::global_language() const noexcept
{
    return global_lang_;
}

void Strings::set_global_language(LanguageID language) noexcept
{
    global_lang_ = language;
}

} // namespace nw::kernel
