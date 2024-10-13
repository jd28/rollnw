#include "Strings.hpp"

#include "../i18n/Language.hpp"
#include "../log.hpp"
#include "../util/templates.hpp"
#include "Config.hpp"
#include "Kernel.hpp"

namespace fs = std::filesystem;

namespace nw::kernel {

const std::type_index Strings::type_index{typeid(Strings)};

Strings::Strings(MemoryResource* memory)
    : Service(memory)
{
}

String Strings::get(uint32_t strref, bool feminine) const
{
    if (strref == 0xFFFFFFFF) { return {}; }
    if (Tlk::custom_flag & strref) {
        strref ^= Tlk::custom_flag;
        return feminine ? customf_.get(strref) : custom_.get(strref);
    } else {
        return feminine ? dialogf_.get(strref) : dialog_.get(strref);
    }
}

String Strings::get(const LocString& locstring, bool feminine) const
{
    if (locstring.contains(global_lang_, feminine)) {
        return locstring.get(global_lang_, feminine);
    } else {
        return get(locstring.strref(), feminine);
    }
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
    if (time != ServiceInitTime::kernel_start && time != ServiceInitTime::module_pre_load) { return; }
    auto lang = Language::to_string(global_language());
    if (config().version() == GameVersion::vEE) {
        auto path = config().install_path() / "lang" / lang / "data" / "dialog.tlk";
        load_dialog_tlk(path);
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
    custom_ = Tlk{path};
    if (custom_.language_id() != global_lang_) {
        LOG_F(WARNING, "tlk language does not match global language: {} != {}",
            to_underlying(custom_.language_id()), to_underlying(global_lang_));
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
            to_underlying(dialog_.language_id()), to_underlying(global_lang_));
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

void Strings::unload_custom_tlk()
{
    custom_ = Tlk{global_lang_};
    customf_ = Tlk{global_lang_};
}

} // namespace nw::kernel
