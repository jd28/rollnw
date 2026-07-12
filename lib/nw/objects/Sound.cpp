#include "Sound.hpp"

#include "../kernel/Kernel.hpp"
#include "../profiles/nwn1/object_compat_fields.hpp"
#include "../profiles/nwn1/object_name_preview.hpp"
#include "../profiles/nwn1/propset_gff_object_io.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "../serialization/component_propset_json.hpp"
#include "../smalls/runtime.hpp"
#include "../util/platform.hpp"
#include "ObjectManager.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace nw {

Sound::Sound()
    : Sound(nw::kernel::global_allocator())
{
}

Sound::Sound(nw::MemoryResource* allocator)
    : ObjectBase(allocator)
{
    set_handle(ObjectHandle{object_invalid, ObjectType::sound});
}

bool Sound::save(const std::filesystem::path& path, std::string_view format)
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

String Sound::get_name_from_file(const std::filesystem::path& path)
{
    return nwn1::preview_object_name_from_file(path, serial_id, object_type);
}

// == Sound - Serialization - Gff =============================================
// ============================================================================

bool deserialize(Sound* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    nwn1::obj_compat_fields_from_gff(*obj, archive, profile, ObjectType::sound);
    nw::kernel::objects().components().deserialize_spatial(obj->handle(), archive, profile);
    nw::kernel::objects().components().deserialize_locals(obj->handle(), archive);

    nwn1::import_sound_propsets_from_gff(&nw::kernel::runtime(), obj, archive, profile);
    return true;
}

bool serialize(const Sound* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    nwn1::obj_compat_fields_to_gff(*obj, archive, profile, ObjectType::sound);
    if (profile != SerializationProfile::blueprint) {
        nw::kernel::objects().components().serialize_position(obj->handle(), archive, profile);
    }

    nw::kernel::objects().components().serialize_locals(obj->handle(), archive, profile);

    nwn1::export_sound_propsets_to_gff(&kernel::runtime(), obj, archive, profile);

    return true;
}

GffBuilder serialize(const Sound* obj, SerializationProfile profile)
{
    GffBuilder result{"UTS"};
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    serialize(obj, result.top, profile);
    result.build();
    return result;
}

// == Sound - Serialization - JSON ============================================
// ============================================================================

bool deserialize(Sound* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    auto result = object_from_component_propset_json(obj, archive, &kernel::runtime(), profile);
    if (!result) {
        LOG_F(ERROR, "sound: component/propset JSON load failed: {}", result.error);
        return false;
    }
    return true;
}

bool serialize(const Sound* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    auto result = object_to_component_propset_json(obj, archive, &kernel::runtime(), profile);
    if (!result) {
        LOG_F(ERROR, "sound: component/propset JSON save failed: {}", result.error);
        return false;
    }
    return true;
}

} // namespace nw
