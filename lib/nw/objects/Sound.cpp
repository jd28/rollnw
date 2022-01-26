#include "Sound.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Sound::Sound(const GffInputArchiveStruct& archive, SerializationProfile profile)
    : common_{ObjectType::sound}
{
    this->from_gff(archive, profile);
}

Sound::Sound(const nlohmann::json& archive, SerializationProfile profile)
    : common_{ObjectType::sound}
{
    this->from_json(archive, profile);
}

bool Sound::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    common_.from_gff(archive, profile);
    archive.get_to("Active", active);
    archive.get_to("Continuous", continuous);
    archive.get_to("Elevation", elevation);
    archive.get_to("Hours", hours);
    archive.get_to("Interval", interval);
    archive.get_to("IntervalVrtn", interval_variation);
    archive.get_to("Looping", looping);
    archive.get_to("MaxDistance", distance_max);
    archive.get_to("MinDistance", distance_min);
    archive.get_to("PitchVariation", pitch_variation);
    archive.get_to("Positional", positional);
    archive.get_to("Priority", priority);
    archive.get_to("Random", random);
    archive.get_to("RandomPosition", random_position);
    if (random_position) {
        archive.get_to("RandomRangeX", random_x);
        archive.get_to("RandomRangeY", random_y);
    }

    size_t sz = archive["Sounds"].size();
    sounds.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        archive["Sounds"][i].get_to("Sound", sounds[i]);
    }

    archive.get_to("Times", times);
    archive.get_to("Volume", volume);
    archive.get_to("VolumeVrtn", volume_variation);

    if (profile == SerializationProfile::instance) {
        archive.get_to("GeneratedType", generated_type);
    }
    return true;
}

bool Sound::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        common_.from_json(archive.at("common"), profile);
        archive.at("distance_min").get_to(distance_min);
        archive.at("distance_max").get_to(distance_max);
        archive.at("elevation").get_to(elevation);
        archive.at("hours").get_to(hours);
        archive.at("interval").get_to(interval);
        archive.at("interval_variation").get_to(interval_variation);
        archive.at("pitch_variation").get_to(pitch_variation);
        archive.at("random_x").get_to(random_x);
        archive.at("random_y").get_to(random_y);
        archive.at("sounds").get_to(sounds);

        archive.at("active").get_to(active);
        archive.at("continuous").get_to(continuous);
        archive.at("looping").get_to(looping);
        archive.at("positional").get_to(positional);
        archive.at("priority").get_to(priority);
        archive.at("random").get_to(random);
        archive.at("random_position").get_to(random_position);
        archive.at("times").get_to(times);
        archive.at("volume").get_to(volume);
        archive.at("volume_variation").get_to(volume_variation);

        if (profile == SerializationProfile::instance) {
            archive.at("generated_type").get_to(generated_type);
        }

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Sound::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Sound::to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const
{
    archive.add_field("TemplateResRef", common_.resref)
        .add_field("LocName", common_.name)
        .add_field("Tag", common_.tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common_.comment);
        archive.add_field("PaletteID", common_.palette_id);
    } else {
        archive.add_field("PositionX", common_.location.position.x)
            .add_field("PositionY", common_.location.position.y)
            .add_field("PositionZ", common_.location.position.z);
    }

    if (common_.local_data.size()) {
        common_.local_data.to_gff(archive);
    }

    archive.add_field("Active", active);
    archive.add_field("Continuous", continuous);
    archive.add_field("Elevation", elevation);
    archive.add_field("Hours", hours);
    archive.add_field("Interval", interval);
    archive.add_field("IntervalVrtn", interval_variation);
    archive.add_field("Looping", looping);
    archive.add_field("MaxDistance", distance_max);
    archive.add_field("MinDistance", distance_min);
    archive.add_field("PitchVariation", pitch_variation);
    archive.add_field("Positional", positional);
    archive.add_field("Priority", priority);
    archive.add_field("Random", random);
    archive.add_field("RandomPosition", random_position);
    archive.add_field("RandomRangeX", random_x);
    archive.add_field("RandomRangeY", random_y);
    archive.add_field("Times", times);
    archive.add_field("Volume", volume);
    archive.add_field("VolumeVrtn", volume_variation);

    auto& list = archive.add_list("Sounds");
    for (const auto& snd : sounds) {
        list.push_back(0).add_field("Sound", snd);
    }

    if (profile == SerializationProfile::instance) {
        archive.add_field("GeneratedType", generated_type);
    }

    return true;
}

GffOutputArchive Sound::to_gff(SerializationProfile profile) const
{
    GffOutputArchive result{"UTS"};
    this->to_gff(result.top, profile);
    result.build();
    return result;
}

nlohmann::json Sound::to_json(SerializationProfile profile) const
{
    nlohmann::json j;

    j["$type"] = "UTS";
    j["$version"] = json_archive_version;

    j["common"] = common_.to_json(profile);
    j["distance_min"] = distance_min;
    j["distance_max"] = distance_max;
    j["elevation"] = elevation;
    j["hours"] = hours;
    j["interval"] = interval;
    j["interval_variation"] = interval_variation;
    j["pitch_variation"] = pitch_variation;
    j["random_x"] = random_x;
    j["random_y"] = random_y;
    j["sounds"] = sounds;

    j["active"] = active;
    j["continuous"] = continuous;
    j["looping"] = looping;
    j["positional"] = positional;
    j["priority"] = priority;
    j["random"] = random;
    j["random_position"] = random_position;
    j["times"] = times;
    j["volume"] = volume;
    j["volume_variation"] = volume_variation;

    if (profile == SerializationProfile::instance) {
        j["generated_type"] = generated_type;
    }

    return j;
}

} // namespace nw
