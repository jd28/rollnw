#include "Store.hpp"

#include "../kernel/Kernel.hpp"
#include "../profiles/nwn1/object_compat_fields.hpp"
#include "../profiles/nwn1/object_name_preview.hpp"
#include "../profiles/nwn1/propset_gff_object_io.hpp"
#include "../profiles/nwn1/store_inventory_gff.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "../serialization/component_propset_json.hpp"
#include "../util/platform.hpp"
#include "ObjectManager.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace nw {

void Store::clear()
{
    ObjectBase::clear();
}

bool Store::instantiate()
{
    auto* inventory = nw::kernel::objects().components().find_store_inventory(*this);
    if (!inventory) {
        return true;
    }

    return inventory->armor.instantiate()
        && inventory->miscellaneous.instantiate()
        && inventory->potions.instantiate()
        && inventory->rings.instantiate()
        && inventory->weapons.instantiate();
}

Store::Store()
    : Store(nw::kernel::global_allocator())
{
}

Store::Store(nw::MemoryResource* allocator)
    : ObjectBase(allocator)
{
    set_handle(ObjectHandle{object_invalid, ObjectType::store});
}

Store::~Store()
{
    if (auto* objects = nw::kernel::services().get_mut<ObjectManager>()) {
        objects->components().remove_store_inventory(*this);
    }
}

StoreInventory& Store::inventory()
{
    return *nw::kernel::objects().components().get_or_create_store_inventory(*this);
}

const StoreInventory& Store::inventory() const
{
    return const_cast<Store*>(this)->inventory();
}

bool Store::save(const std::filesystem::path& path, std::string_view format)
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

String Store::get_name_from_file(const std::filesystem::path& path)
{
    return nwn1::preview_object_name_from_file(path, serial_id, object_type);
}

// == Store - Serialization - Gff =============================================
// ============================================================================

bool deserialize(Store* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        return false;
    }
    if (!nwn1::obj_compat_fields_from_gff(*obj, archive, profile, ObjectType::store)) {
        return false;
    }
    nw::kernel::objects().components().deserialize_spatial(obj->handle(), archive, profile);
    nw::kernel::objects().components().deserialize_locals(obj->handle(), archive);
    if (!nwn1::import_store_inventory_from_gff(*obj, archive, profile)) { return false; }

    nwn1::import_store_propsets_from_gff(&nw::kernel::runtime(), obj, archive, profile);
    return true;
}

bool serialize(const Store* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    nwn1::obj_compat_fields_to_gff(*obj, archive, profile, ObjectType::store);
    if (profile != SerializationProfile::blueprint) {
        nw::kernel::objects().components().serialize_position_orientation(obj->handle(), archive, profile);
    }

    nw::kernel::objects().components().serialize_locals(obj->handle(), archive, profile);
    if (!nwn1::export_store_inventory_to_gff(*obj, archive, profile)) { return false; }

    nwn1::export_store_propsets_to_gff(&kernel::runtime(), obj, archive, profile);

    return true;
}

GffBuilder serialize(const Store* obj, SerializationProfile profile)
{
    GffBuilder out{"UTS"};
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    serialize(obj, out.top, profile);
    out.build();
    return out;
}

// == Store - Serialization - JSON ============================================
// ============================================================================

bool deserialize(Store* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    auto result = object_from_component_propset_json(obj, archive, &kernel::runtime(), profile);
    if (!result) {
        LOG_F(ERROR, "store: component/propset JSON load failed: {}", result.error);
        return false;
    }
    return true;
}

bool serialize(const Store* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    auto result = object_to_component_propset_json(obj, archive, &kernel::runtime(), profile);
    if (!result) {
        LOG_F(ERROR, "store: component/propset JSON save failed: {}", result.error);
        return false;
    }
    return true;
}

} // namespace nw
