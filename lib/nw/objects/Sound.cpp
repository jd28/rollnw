#include "Sound.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Sound::Sound()
{
    set_handle({object_invalid, ObjectType::sound, 0});
}

bool Sound::deserialize(Sound* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    try {
        obj->common.from_json(archive.at("common"), profile, ObjectType::sound);
        archive.at("sounds").get_to(obj->sounds);

        archive.at("distance_min").get_to(obj->distance_min);
        archive.at("distance_max").get_to(obj->distance_max);
        archive.at("elevation").get_to(obj->elevation);

        if (profile == SerializationProfile::instance) {
            archive.at("generated_type").get_to(obj->generated_type);
        }

        archive.at("hours").get_to(obj->hours);
        archive.at("interval").get_to(obj->interval);
        archive.at("interval_variation").get_to(obj->interval_variation);
        archive.at("pitch_variation").get_to(obj->pitch_variation);
        archive.at("random_x").get_to(obj->random_x);
        archive.at("random_y").get_to(obj->random_y);

        archive.at("active").get_to(obj->active);
        archive.at("continuous").get_to(obj->continuous);
        archive.at("looping").get_to(obj->looping);
        archive.at("positional").get_to(obj->positional);
        archive.at("priority").get_to(obj->priority);
        archive.at("random").get_to(obj->random);
        archive.at("random_position").get_to(obj->random_position);
        archive.at("times").get_to(obj->times);
        archive.at("volume").get_to(obj->volume);
        archive.at("volume_variation").get_to(obj->volume_variation);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Sound::from_json exception: {}", e.what());
        return false;
    }

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }

    return true;
}

void Sound::serialize(const Sound* obj, nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive["$type"] = "UTS";
    archive["$version"] = json_archive_version;

    archive["common"] = obj->common.to_json(profile, ObjectType::sound);
    archive["sounds"] = obj->sounds;

    archive["distance_min"] = obj->distance_min;
    archive["distance_max"] = obj->distance_max;
    archive["elevation"] = obj->elevation;

    if (profile == SerializationProfile::instance) {
        archive["generated_type"] = obj->generated_type;
    }

    archive["hours"] = obj->hours;
    archive["interval"] = obj->interval;
    archive["interval_variation"] = obj->interval_variation;
    archive["pitch_variation"] = obj->pitch_variation;
    archive["random_x"] = obj->random_x;
    archive["random_y"] = obj->random_y;

    archive["active"] = obj->active;
    archive["continuous"] = obj->continuous;
    archive["looping"] = obj->looping;
    archive["positional"] = obj->positional;
    archive["priority"] = obj->priority;
    archive["random"] = obj->random;
    archive["random_position"] = obj->random_position;
    archive["times"] = obj->times;
    archive["volume"] = obj->volume;
    archive["volume_variation"] = obj->volume_variation;
}

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(Sound* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    deserialize(obj->common, archive, profile, ObjectType::sound);

    size_t sz = archive["Sounds"].size();
    obj->sounds.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        archive["Sounds"][i].get_to("Sound", obj->sounds[i]);
    }

    archive.get_to("MaxDistance", obj->distance_max);
    archive.get_to("MinDistance", obj->distance_min);
    archive.get_to("Elevation", obj->elevation);

    if (profile == SerializationProfile::instance) {
        archive.get_to("GeneratedType", obj->generated_type);
    }

    archive.get_to("Hours", obj->hours);
    archive.get_to("Interval", obj->interval);
    archive.get_to("IntervalVrtn", obj->interval_variation);
    archive.get_to("PitchVariation", obj->pitch_variation);
    archive.get_to("RandomRangeX", obj->random_x);
    archive.get_to("RandomRangeY", obj->random_y);

    archive.get_to("Active", obj->active);
    archive.get_to("Continuous", obj->continuous);
    archive.get_to("Looping", obj->looping);
    archive.get_to("Positional", obj->positional);
    archive.get_to("Priority", obj->priority);
    archive.get_to("Random", obj->random);
    archive.get_to("RandomPosition", obj->random_position);
    archive.get_to("Times", obj->times);
    archive.get_to("Volume", obj->volume);
    archive.get_to("VolumeVrtn", obj->volume_variation);

    if (profile == nw::SerializationProfile::instance) {
        obj->instantiated_ = true;
    }

    return true;
}

bool serialize(const Sound* obj, GffBuilderStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive.add_field("TemplateResRef", obj->common.resref)
        .add_field("LocName", obj->common.name)
        .add_field("Tag", obj->common.tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", obj->common.comment);
        archive.add_field("PaletteID", obj->common.palette_id);
    } else {
        archive.add_field("PositionX", obj->common.location.position.x)
            .add_field("PositionY", obj->common.location.position.y)
            .add_field("PositionZ", obj->common.location.position.z);
    }

    if (obj->common.locals.size()) {
        serialize(obj->common.locals, archive, profile);
    }

    auto& list = archive.add_list("Sounds");
    for (const auto& s : obj->sounds) {
        list.push_back(0).add_field("Sound", s);
    }

    archive.add_field("MaxDistance", obj->distance_max);
    archive.add_field("MinDistance", obj->distance_min);
    archive.add_field("Elevation", obj->elevation);
    archive.add_field("Hours", obj->hours);
    archive.add_field("Interval", obj->interval);
    archive.add_field("IntervalVrtn", obj->interval_variation);

    if (profile == SerializationProfile::instance) {
        archive.add_field("GeneratedType", obj->generated_type);
    }

    archive.add_field("RandomRangeX", obj->random_x);
    archive.add_field("RandomRangeY", obj->random_y);

    archive.add_field("Active", obj->active);
    archive.add_field("Continuous", obj->continuous);
    archive.add_field("Looping", obj->looping);
    archive.add_field("PitchVariation", obj->pitch_variation);
    archive.add_field("Positional", obj->positional);
    archive.add_field("Priority", obj->priority);
    archive.add_field("Random", obj->random);
    archive.add_field("RandomPosition", obj->random_position);
    archive.add_field("Times", obj->times);
    archive.add_field("Volume", obj->volume);
    archive.add_field("VolumeVrtn", obj->volume_variation);

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

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
