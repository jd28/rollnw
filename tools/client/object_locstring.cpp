#include "object_locstring.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/runtime.hpp>

namespace nw::toolset {
namespace {

void set_diagnostic(std::string* diagnostic, std::string_view value)
{
    if (diagnostic) { diagnostic->assign(value); }
}

std::optional<TextRef> read_propset_text_ref(smalls::Runtime& runtime,
    const ObjectLocStringTarget& target, std::string* diagnostic)
{
    const auto* definition = runtime.get_struct_def(target.propset_type);
    if (!definition || !definition->is_propset
        || target.field_index >= definition->field_count) {
        set_diagnostic(diagnostic, "Localized-string propset target is invalid");
        return std::nullopt;
    }
    const auto& field = definition->fields[target.field_index];
    if (field.is_object_component_array
        || field.type_id != runtime.type_id("core.types.TextRef", false)) {
        set_diagnostic(diagnostic, "Localized-string propset field is not a TextRef");
        return std::nullopt;
    }
    const auto propset = runtime.find_propset_ref(
        target.propset_type, target.object);
    if (propset.storage != smalls::ValueStorage::propset) {
        set_diagnostic(diagnostic, "Localized-string propset is unavailable");
        return std::nullopt;
    }
    auto value = runtime.read_struct_value_field(
        propset, definition, target.field_index);
    auto* reference = static_cast<TextRef*>(runtime.get_value_data_ptr(value));
    if (!reference) {
        set_diagnostic(diagnostic, "Localized-string TextRef is unavailable");
        return std::nullopt;
    }
    return *reference;
}

} // namespace

std::optional<LocString> read_object_locstring(smalls::Runtime& runtime,
    const ObjectLocStringTarget& target, std::string* diagnostic)
{
    if (diagnostic) { diagnostic->clear(); }
    auto* base = kernel::objects().get_object_base(target.object);
    if (!base) {
        set_diagnostic(diagnostic, "Localized-string target is no longer live");
        return std::nullopt;
    }

    switch (target.storage) {
    case ObjectLocStringStorage::object_name:
        return base->name;
    case ObjectLocStringStorage::area_name: {
        auto* area = base->as_area();
        if (area) { return area->name; }
        break;
    }
    case ObjectLocStringStorage::module_name: {
        auto* module = base->as_module();
        if (module) { return module->name; }
        break;
    }
    case ObjectLocStringStorage::module_description: {
        auto* module = base->as_module();
        if (module) { return module->description; }
        break;
    }
    case ObjectLocStringStorage::propset_text_ref: {
        const auto reference = read_propset_text_ref(runtime, target, diagnostic);
        if (!reference) { return std::nullopt; }
        return kernel::strings().to_locstring(*reference);
    }
    case ObjectLocStringStorage::none:
        break;
    }

    set_diagnostic(diagnostic, "Localized-string storage does not match its object");
    return std::nullopt;
}

bool write_object_locstring(smalls::Runtime& runtime,
    const ObjectLocStringTarget& target, const LocString& replacement,
    TextRef replacement_ref, std::string* diagnostic)
{
    if (diagnostic) { diagnostic->clear(); }
    auto* base = kernel::objects().get_object_base(target.object);
    if (!base) {
        set_diagnostic(diagnostic, "Localized-string target is no longer live");
        return false;
    }

    LocString* direct = nullptr;
    switch (target.storage) {
    case ObjectLocStringStorage::object_name:
        direct = &base->name;
        break;
    case ObjectLocStringStorage::area_name: {
        auto* area = base->as_area();
        direct = area ? &area->name : nullptr;
        break;
    }
    case ObjectLocStringStorage::module_name: {
        auto* module = base->as_module();
        direct = module ? &module->name : nullptr;
        break;
    }
    case ObjectLocStringStorage::module_description: {
        auto* module = base->as_module();
        direct = module ? &module->description : nullptr;
        break;
    }
    case ObjectLocStringStorage::propset_text_ref: {
        const auto current = read_propset_text_ref(runtime, target, diagnostic);
        const auto* definition = runtime.get_struct_def(target.propset_type);
        if (!current || !definition || !replacement_ref.valid()
            || kernel::strings().to_locstring(replacement_ref) != replacement) {
            if (diagnostic && diagnostic->empty()) {
                *diagnostic = "Localized-string replacement TextRef is invalid";
            }
            return false;
        }
        const auto propset = runtime.find_propset_ref(
            target.propset_type, target.object);
        if (!runtime.write_struct_value_field(propset, definition,
                target.field_index,
                smalls::detail::make_value(&runtime, replacement_ref))) {
            set_diagnostic(diagnostic, "Localized-string TextRef replacement failed");
            return false;
        }
        return true;
    }
    case ObjectLocStringStorage::none:
        break;
    }

    if (!direct || replacement_ref.valid()) {
        set_diagnostic(diagnostic, "Localized-string direct target is invalid");
        return false;
    }
    *direct = replacement;
    return true;
}

} // namespace nw::toolset
