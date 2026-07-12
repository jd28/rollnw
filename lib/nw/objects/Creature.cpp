#include "Creature.hpp"

#include "../kernel/TwoDACache.hpp"
#include "../profiles/nwn1/creature_ability_loadout.hpp"
#include "../profiles/nwn1/equipment_gff.hpp"
#include "../profiles/nwn1/inventory_component_gff.hpp"
#include "../profiles/nwn1/legacy_gff_compat.hpp"
#include "../profiles/nwn1/object_compat_fields.hpp"
#include "../profiles/nwn1/object_name_preview.hpp"
#include "../profiles/nwn1/propset_gff_object_io.hpp"
#include "../profiles/nwn1/scriptbridge.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "../serialization/component_propset_json.hpp"
#include "../util/platform.hpp"
#include "../util/profile.hpp"
#include "ObjectManager.hpp"

#include <nlohmann/json.hpp>

#include <chrono>

namespace fs = std::filesystem;

namespace nw {

Creature::Creature()
    : Creature(nw::kernel::global_allocator())
{
}

Creature::Creature(nw::MemoryResource* allocator)
    : ObjectBase(allocator)
    , equipment{this}
{
    set_handle(ObjectHandle{object_invalid, ObjectType::creature});
}

Creature::~Creature()
{
    if (auto* objects = nw::kernel::services().get_mut<ObjectManager>()) {
        objects->components().remove_inventory(*this);
    }
}

Inventory& Creature::inventory()
{
    return *nw::kernel::objects().components().get_or_create_inventory(*this, 6, 6, 10);
}

const Inventory& Creature::inventory() const
{
    return const_cast<Creature*>(this)->inventory();
}

void Creature::clear()
{
    equipment.destroy();
    ObjectBase::clear();

    instantiated_ = false;
}

bool Creature::instantiate()
{
    NW_PROFILE_SCOPE_N("Creature::instantiate");
    if (instantiated_) return true;

    auto t0 = std::chrono::high_resolution_clock::now();
    bool inventory_instantiated = true;
    if (auto* inventory = nw::kernel::objects().components().find_inventory(*this)) {
        inventory_instantiated = inventory->instantiate();
    }
    instantiated_ = (inventory_instantiated && equipment.instantiate());
    auto t1 = std::chrono::high_resolution_clock::now();

    int32_t equipment_item_count = 0;
    int32_t item_effects_processed = 0;
    {
        NW_PROFILE_SCOPE_N("Creature::instantiate::item_effects");
        size_t i = 0;
        for (auto& equip : equipment.equips) {
            if (auto* item = equip_item_ptr(equip)) {
                ++equipment_item_count;
                item_effects_processed += nwn1::process_item_properties(this, item,
                    static_cast<EquipIndex>(i), false);
            }
            ++i;
        }
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    nw::kernel::objects().run_instantiate_callback(this);
    auto t3 = std::chrono::high_resolution_clock::now();

    auto inv_equip_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    auto item_effects_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    auto callback_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    NW_PROFILE_PLOT("nw.creature.instantiate.inv_equip_us", inv_equip_us);
    NW_PROFILE_PLOT("nw.creature.instantiate.item_effects_us", item_effects_us);
    NW_PROFILE_PLOT("nw.creature.instantiate.callback_us", callback_us);
    NW_PROFILE_PLOT("nw.creature.instantiate.equipment_items", equipment_item_count);
    NW_PROFILE_PLOT("nw.creature.instantiate.item_effects_applied", item_effects_processed);

    return instantiated_;
}

String Creature::get_name_from_file(const std::filesystem::path& path)
{
    return nwn1::preview_creature_name_from_file(path);
}

bool Creature::save(const std::filesystem::path& path, std::string_view format)
{
    bool result = false;
    if (format == "json") {
        nlohmann::json out;
        result = serialize(this, out, SerializationProfile::blueprint);
        if (result) {
            fs::path temp = fs::temp_directory_path() / path.filename();
            std::ofstream of{temp};
            of << std::setw(4) << out;
            of.close();
            result = move_file_safely(temp, path);
        }
    } else if (format == "gff") {
        GffBuilder out{serial_id};
        result = serialize(this, out.top, SerializationProfile::blueprint);
        if (result) {
            out.build();
            result = out.write_to(path);
        }
    } else {
        LOG_F(ERROR, "[objects] invalid format type: {}", format);
    }
    return result;
}

// == Creature - Serialization - Gff ==========================================
// ============================================================================

bool deserialize(Creature* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    nwn1::obj_compat_fields_from_gff(*obj, archive, profile, ObjectType::creature);
    nw::kernel::objects().components().deserialize_spatial(obj->handle(), archive, profile);
    nw::kernel::objects().components().deserialize_locals(obj->handle(), archive);
    if (!nwn1::import_creature_equipment_from_gff(obj->equipment, archive, profile)) { return false; }
    if (!nwn1::import_inventory_component_from_gff(*obj, archive, profile, 6, 6, 10)) { return false; }
    if (!nwn1::import_creature_ability_loadout_from_gff(*obj, archive)) {
        return false;
    }

    nwn1::import_creature_propsets_from_gff(&nw::kernel::runtime(), obj, archive, profile);

    return true;
}

bool serialize(const Creature* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    nwn1::obj_compat_fields_to_gff(*obj, archive, profile, ObjectType::creature);
    if (profile != SerializationProfile::blueprint) {
        nw::kernel::objects().components().serialize_position_orientation(obj->handle(), archive, profile);
    }

    nw::kernel::objects().components().serialize_locals(obj->handle(), archive, profile);
    if (!nwn1::export_creature_equipment_to_gff(obj->equipment, archive, profile)) { return false; }
    if (!nwn1::export_inventory_component_to_gff(*obj, archive, profile)) { return false; }

    nwn1::export_creature_propsets_to_gff(&kernel::runtime(), obj, archive, profile);

    nwn1::export_creature_legacy_gff_fields(archive);

    return true;
}

GffBuilder serialize(const Creature* obj, SerializationProfile profile)
{
    GffBuilder out{Creature::serial_id};
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }
    serialize(obj, out.top, profile);
    out.build();
    return out;
}

// == Creature - Serialization - JSON =========================================
// ============================================================================

bool deserialize(Creature* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    auto result = object_from_component_propset_json(obj, archive, &kernel::runtime(), profile);
    if (!result) {
        LOG_F(ERROR, "[creature] component/propset JSON load failed: {}", result.error);
        return false;
    }

    return true;
}

bool serialize(const Creature* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    auto result = object_to_component_propset_json(obj, archive, &kernel::runtime(), profile);
    if (!result) {
        LOG_F(ERROR, "[creature] component/propset JSON save failed: {}", result.error);
        return false;
    }

    return true;
}

} // namespace nw
