#include "Item.hpp"

#include "../profiles/nwn1/inventory_component_gff.hpp"
#include "../profiles/nwn1/item_materialization.hpp"
#include "../profiles/nwn1/object_compat_fields.hpp"
#include "../profiles/nwn1/object_name_preview.hpp"
#include "../profiles/nwn1/propset_gff_object_io.hpp"
#include "ObjectManager.hpp"

#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "../serialization/component_propset_json.hpp"
#include "../util/platform.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace nw {

Item::Item()
    : Item(nw::kernel::global_allocator())
{
}

Item::Item(nw::MemoryResource* allocator)
    : ObjectBase(allocator)
{
    set_handle(ObjectHandle{object_invalid, ObjectType::item});
}

Item::~Item()
{
    if (auto* objects = nw::kernel::services().get_mut<ObjectManager>()) {
        objects->components().remove_inventory(*this);
    }
}

Inventory& Item::inventory()
{
    return *nw::kernel::objects().components().get_or_create_inventory(*this, 1, 6, 10);
}

const Inventory& Item::inventory() const
{
    return const_cast<Item*>(this)->inventory();
}

void Item::clear()
{
    ObjectBase::clear();
    instantiated_ = false;
}

bool Item::instantiate()
{
    if (instantiated_) { return true; }
    if (auto result = nwn1::materialize_item_native_components(*this, nw::kernel::runtime()); !result) {
        LOG_F(WARNING, "[item] failed to materialize native components: {}", result.error);
    }
    if (auto* inventory = nw::kernel::objects().components().find_inventory(*this)) {
        instantiated_ = inventory->instantiate();
    } else {
        instantiated_ = true;
    }

    nw::kernel::objects().run_instantiate_callback(this);
    return instantiated_;
}

bool Item::save(const std::filesystem::path& path, std::string_view format)
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

String Item::get_name_from_file(const std::filesystem::path& path)
{
    return nwn1::preview_object_name_from_file(path, serial_id, object_type);
}

// == Item - Serialization - Gff ==============================================
// ============================================================================

bool deserialize(Item* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }
    nwn1::obj_compat_fields_from_gff(*obj, archive, profile, ObjectType::item);
    nw::kernel::objects().components().deserialize_spatial(obj->handle(), archive, profile);
    nw::kernel::objects().components().deserialize_locals(obj->handle(), archive);
    if (!nwn1::import_inventory_component_from_gff(*obj, archive, profile, 1, 6, 10)) { return false; }

    nwn1::import_item_propsets_from_gff(&nw::kernel::runtime(), obj, archive, profile);
    if (auto result = nwn1::materialize_item_native_components(*obj, nw::kernel::runtime()); !result) {
        LOG_F(WARNING, "[item] failed to materialize native components from GFF: {}", result.error);
    }

    return true;
}

bool serialize(const Item* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    nwn1::obj_compat_fields_to_gff(*obj, archive, profile, ObjectType::item);
    if (profile != SerializationProfile::blueprint) {
        nw::kernel::objects().components().serialize_position_orientation(obj->handle(), archive, profile);
    }

    nw::kernel::objects().components().serialize_locals(obj->handle(), archive, profile);
    if (!nwn1::export_inventory_component_to_gff(*obj, archive, profile)) { return false; }

    nwn1::export_item_propsets_to_gff(&kernel::runtime(), obj, archive, profile);

    return true;
}

GffBuilder serialize(const Item* obj, SerializationProfile profile)
{
    GffBuilder out{"UTI"};
    if (!obj) return out;

    serialize(obj, out.top, profile);
    out.build();
    return out;
}

// == Item - Serialization - JSON =============================================
// ============================================================================

bool deserialize(Item* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    auto result = object_from_component_propset_json(obj, archive, &kernel::runtime(), profile);
    if (!result) {
        LOG_F(ERROR, "[item] component/propset JSON load failed: {}", result.error);
        return false;
    }
    if (auto materialized = nwn1::materialize_item_native_components(*obj, nw::kernel::runtime()); !materialized) {
        LOG_F(WARNING, "[item] failed to materialize native components from JSON: {}", materialized.error);
    }

    return true;
}

bool serialize(const Item* obj, nlohmann::json& archive, SerializationProfile profile)
{
    CHECK_F(!!obj, "[item] unable to serialize null object");

    auto result = object_to_component_propset_json(obj, archive, &kernel::runtime(), profile);
    if (!result) {
        LOG_F(ERROR, "[item] component/propset JSON save failed: {}", result.error);
        return false;
    }

    return true;
}

} // namespace nw
