#include "../stdlib.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../kernel/Strings.hpp"
#include "../../objects/Module.hpp"
#include "../../objects/ObjectManager.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace nw::smalls {

namespace {

const nw::Module* as_module(nw::ObjectHandle object)
{
    const auto* base = nw::kernel::objects().get_object_base(object);
    return base ? base->as_module() : nullptr;
}

int32_t script_count(size_t value)
{
    return static_cast<int32_t>(std::min(
        value, static_cast<size_t>(std::numeric_limits<int32_t>::max())));
}

int32_t script_int(uint32_t value)
{
    return static_cast<int32_t>(std::min(
        value, static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
}

int32_t get_area_count(nw::ObjectHandle object)
{
    const auto* module = as_module(object);
    if (!module) {
        return 0;
    }
    if (module->areas.is<nw::Vector<nw::Resref>>()) {
        return script_count(module->areas.as<nw::Vector<nw::Resref>>().size());
    }
    return script_count(module->area_count());
}

template <auto Member>
auto get_module_member(nw::ObjectHandle object)
    -> std::remove_cvref_t<decltype(std::declval<nw::Module>().*Member)>
{
    using Result = std::remove_cvref_t<decltype(std::declval<nw::Module>().*Member)>;
    const auto* module = as_module(object);
    return module ? module->*Member : Result{};
}

template <auto Member>
int32_t get_integer_member(nw::ObjectHandle object)
{
    const auto* module = as_module(object);
    if (!module) {
        return 0;
    }
    using Source = std::remove_cvref_t<decltype(module->*Member)>;
    if constexpr (std::is_signed_v<Source>) {
        return static_cast<int32_t>(module->*Member);
    } else {
        return script_int(static_cast<uint32_t>(module->*Member));
    }
}

template <nw::Resref nw::ModuleScripts::* Member>
nw::Resref get_script_member(nw::ObjectHandle object)
{
    const auto* module = as_module(object);
    return module ? module->scripts.*Member : nw::Resref{};
}

ScriptString get_description(nw::ObjectHandle object)
{
    auto& runtime = nw::kernel::runtime();
    const auto* module = as_module(object);
    return ScriptString{runtime.alloc_string(
        module ? nw::kernel::strings().get(module->description) : nw::StringView{})};
}

ScriptString get_name(nw::ObjectHandle object)
{
    auto& runtime = nw::kernel::runtime();
    const auto* module = as_module(object);
    return ScriptString{runtime.alloc_string(
        module ? nw::kernel::strings().get(module->name) : nw::StringView{})};
}

ScriptString get_minimum_game_version(nw::ObjectHandle object)
{
    auto& runtime = nw::kernel::runtime();
    const auto* module = as_module(object);
    return ScriptString{runtime.alloc_string(
        module ? nw::StringView{module->min_game_version} : nw::StringView{})};
}

ScriptString get_custom_tlk(nw::ObjectHandle object)
{
    auto& runtime = nw::kernel::runtime();
    const auto* module = as_module(object);
    return ScriptString{runtime.alloc_string(
        module ? nw::StringView{module->tlk} : nw::StringView{})};
}

} // namespace

void register_core_module(Runtime& rt)
{
    if (rt.get_native_module("core.module")) {
        return;
    }

    rt.module("core.module")
        .function("get_name", &get_name)
        .function("get_description", &get_description)
        .function("get_minimum_game_version", &get_minimum_game_version)
        .function("get_custom_tlk", &get_custom_tlk)
        .function("get_area_count", &get_area_count)
        .function("get_entry_area", &get_module_member<&nw::Module::entry_area>)
        .function("get_entry_position", &get_module_member<&nw::Module::entry_position>)
        .function("get_entry_orientation", &get_module_member<&nw::Module::entry_orientation>)
        .function("get_start_movie", &get_module_member<&nw::Module::start_movie>)
        .function("get_creator_id", &get_integer_member<&nw::Module::creator>)
        .function("get_start_year", &get_integer_member<&nw::Module::start_year>)
        .function("get_version", &get_integer_member<&nw::Module::version>)
        .function("get_expansion_pack", &get_integer_member<&nw::Module::expansion_pack>)
        .function("get_dawn_hour", &get_integer_member<&nw::Module::dawn_hour>)
        .function("get_dusk_hour", &get_integer_member<&nw::Module::dusk_hour>)
        .function("get_minutes_per_hour", &get_integer_member<&nw::Module::minutes_per_hour>)
        .function("get_start_day", &get_integer_member<&nw::Module::start_day>)
        .function("get_start_hour", &get_integer_member<&nw::Module::start_hour>)
        .function("get_start_month", &get_integer_member<&nw::Module::start_month>)
        .function("get_xp_scale", &get_integer_member<&nw::Module::xpscale>)
        .function("is_save_game", &get_module_member<&nw::Module::is_save_game>)
        .function("get_on_client_enter", &get_script_member<&nw::ModuleScripts::on_client_enter>)
        .function("get_on_client_leave", &get_script_member<&nw::ModuleScripts::on_client_leave>)
        .function("get_on_cutscene_abort", &get_script_member<&nw::ModuleScripts::on_cutsnabort>)
        .function("get_on_heartbeat", &get_script_member<&nw::ModuleScripts::on_heartbeat>)
        .function("get_on_item_acquired", &get_script_member<&nw::ModuleScripts::on_item_acquire>)
        .function("get_on_item_activated", &get_script_member<&nw::ModuleScripts::on_item_activate>)
        .function("get_on_item_unacquired", &get_script_member<&nw::ModuleScripts::on_item_unaquire>)
        .function("get_on_load", &get_script_member<&nw::ModuleScripts::on_load>)
        .function("get_on_player_chat", &get_script_member<&nw::ModuleScripts::on_player_chat>)
        .function("get_on_player_death", &get_script_member<&nw::ModuleScripts::on_player_death>)
        .function("get_on_player_dying", &get_script_member<&nw::ModuleScripts::on_player_dying>)
        .function("get_on_player_equip", &get_script_member<&nw::ModuleScripts::on_player_equip>)
        .function("get_on_player_level_up", &get_script_member<&nw::ModuleScripts::on_player_level_up>)
        .function("get_on_player_rest", &get_script_member<&nw::ModuleScripts::on_player_rest>)
        .function("get_on_player_unequip", &get_script_member<&nw::ModuleScripts::on_player_uneqiup>)
        .function("get_on_spawn_button_down", &get_script_member<&nw::ModuleScripts::on_spawnbtndn>)
        .function("get_on_start", &get_script_member<&nw::ModuleScripts::on_start>)
        .function("get_on_user_defined", &get_script_member<&nw::ModuleScripts::on_user_defined>)
        .finalize();
}

} // namespace nw::smalls
