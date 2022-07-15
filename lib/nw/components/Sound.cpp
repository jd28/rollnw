#include "Sound.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Sound::deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    auto common = ent.get_mut<Common>();
    common->from_gff(archive, profile);

    auto snd = ent.get_mut<Sound>();

    size_t sz = archive["Sounds"].size();
    snd->sounds.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        archive["Sounds"][i].get_to("Sound", snd->sounds[i]);
    }

    archive.get_to("MaxDistance", snd->distance_max);
    archive.get_to("MinDistance", snd->distance_min);
    archive.get_to("Elevation", snd->elevation);

    if (profile == SerializationProfile::instance) {
        archive.get_to("GeneratedType", snd->generated_type);
    }

    archive.get_to("Hours", snd->hours);
    archive.get_to("Interval", snd->interval);
    archive.get_to("IntervalVrtn", snd->interval_variation);
    archive.get_to("PitchVariation", snd->pitch_variation);
    archive.get_to("RandomRangeX", snd->random_x);
    archive.get_to("RandomRangeY", snd->random_y);

    archive.get_to("Active", snd->active);
    archive.get_to("Continuous", snd->continuous);
    archive.get_to("Looping", snd->looping);
    archive.get_to("Positional", snd->positional);
    archive.get_to("Priority", snd->priority);
    archive.get_to("Random", snd->random);
    archive.get_to("RandomPosition", snd->random_position);
    archive.get_to("Times", snd->times);
    archive.get_to("Volume", snd->volume);
    archive.get_to("VolumeVrtn", snd->volume_variation);

    return true;
}

bool Sound::deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile)
{
    try {
        auto common = ent.get_mut<Common>();
        common->from_json(archive.at("common"), profile);

        auto snd = ent.get_mut<Sound>();
        archive.at("sounds").get_to(snd->sounds);

        archive.at("distance_min").get_to(snd->distance_min);
        archive.at("distance_max").get_to(snd->distance_max);
        archive.at("elevation").get_to(snd->elevation);

        if (profile == SerializationProfile::instance) {
            archive.at("generated_type").get_to(snd->generated_type);
        }

        archive.at("hours").get_to(snd->hours);
        archive.at("interval").get_to(snd->interval);
        archive.at("interval_variation").get_to(snd->interval_variation);
        archive.at("pitch_variation").get_to(snd->pitch_variation);
        archive.at("random_x").get_to(snd->random_x);
        archive.at("random_y").get_to(snd->random_y);

        archive.at("active").get_to(snd->active);
        archive.at("continuous").get_to(snd->continuous);
        archive.at("looping").get_to(snd->looping);
        archive.at("positional").get_to(snd->positional);
        archive.at("priority").get_to(snd->priority);
        archive.at("random").get_to(snd->random);
        archive.at("random_position").get_to(snd->random_position);
        archive.at("times").get_to(snd->times);
        archive.at("volume").get_to(snd->volume);
        archive.at("volume_variation").get_to(snd->volume_variation);

    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Sound::from_json exception: {}", e.what());
        return false;
    }

    return true;
}

bool Sound::serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile)
{
    auto common = ent.get<Common>();
    if (!common) { return false; }

    archive.add_field("TemplateResRef", common->resref)
        .add_field("LocName", common->name)
        .add_field("Tag", common->tag);

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common->comment);
        archive.add_field("PaletteID", common->palette_id);
    } else {
        archive.add_field("PositionX", common->location.position.x)
            .add_field("PositionY", common->location.position.y)
            .add_field("PositionZ", common->location.position.z);
    }

    if (common->locals.size()) {
        common->locals.to_gff(archive);
    }

    auto snd = ent.get<Sound>();
    if (!snd) { return false; }

    auto& list = archive.add_list("Sounds");
    for (const auto& s : snd->sounds) {
        list.push_back(0).add_field("Sound", s);
    }

    archive.add_field("MaxDistance", snd->distance_max);
    archive.add_field("MinDistance", snd->distance_min);
    archive.add_field("Elevation", snd->elevation);
    archive.add_field("Hours", snd->hours);
    archive.add_field("Interval", snd->interval);
    archive.add_field("IntervalVrtn", snd->interval_variation);

    if (profile == SerializationProfile::instance) {
        archive.add_field("GeneratedType", snd->generated_type);
    }

    archive.add_field("RandomRangeX", snd->random_x);
    archive.add_field("RandomRangeY", snd->random_y);

    archive.add_field("Active", snd->active);
    archive.add_field("Continuous", snd->continuous);
    archive.add_field("Looping", snd->looping);
    archive.add_field("PitchVariation", snd->pitch_variation);
    archive.add_field("Positional", snd->positional);
    archive.add_field("Priority", snd->priority);
    archive.add_field("Random", snd->random);
    archive.add_field("RandomPosition", snd->random_position);
    archive.add_field("Times", snd->times);
    archive.add_field("Volume", snd->volume);
    archive.add_field("VolumeVrtn", snd->volume_variation);

    return true;
}

GffOutputArchive Sound::serialize(const flecs::entity ent, SerializationProfile profile)
{
    GffOutputArchive result{"UTS"};
    Sound::serialize(ent, result.top, profile);
    result.build();
    return result;
}

void Sound::serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile)
{
    auto common = ent.get<Common>();
    if (!common) { return; }

    auto snd = ent.get<Sound>();
    if (!snd) { return; }

    archive["$type"] = "UTS";
    archive["$version"] = json_archive_version;

    archive["common"] = common->to_json(profile);
    archive["sounds"] = snd->sounds;

    archive["distance_min"] = snd->distance_min;
    archive["distance_max"] = snd->distance_max;
    archive["elevation"] = snd->elevation;

    if (profile == SerializationProfile::instance) {
        archive["generated_type"] = snd->generated_type;
    }

    archive["hours"] = snd->hours;
    archive["interval"] = snd->interval;
    archive["interval_variation"] = snd->interval_variation;
    archive["pitch_variation"] = snd->pitch_variation;
    archive["random_x"] = snd->random_x;
    archive["random_y"] = snd->random_y;

    archive["active"] = snd->active;
    archive["continuous"] = snd->continuous;
    archive["looping"] = snd->looping;
    archive["positional"] = snd->positional;
    archive["priority"] = snd->priority;
    archive["random"] = snd->random;
    archive["random_position"] = snd->random_position;
    archive["times"] = snd->times;
    archive["volume"] = snd->volume;
    archive["volume_variation"] = snd->volume_variation;
}

} // namespace nw
