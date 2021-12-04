#include "Sound.hpp"

namespace nw {

Sound::Sound(const GffStruct gff, SerializationProfile profile)
    : common_{ObjectType::sound, gff, profile}
{
    gff.get_to("Active", active);
    gff.get_to("Continuous", continuous);
    gff.get_to("Elevation", elevation);
    gff.get_to("Hours", hours);
    gff.get_to("Interval", interval);
    gff.get_to("IntervalVrtn", interval_variation);
    gff.get_to("Looping", looping);
    gff.get_to("MaxDistance", distance_max);
    gff.get_to("MinDistance", distance_min);
    gff.get_to("PitchVariation", pitch_variation);
    gff.get_to("Positional", positional);
    gff.get_to("Priority", priority);
    gff.get_to("Random", random);
    gff.get_to("RandomPosition", random_position);
    if (random_position) {
        gff.get_to("RandomRangeX", random_x);
        gff.get_to("RandomRangeY", random_y);
    }

    size_t sz = gff["Sounds"].size();
    sounds.resize(sz);
    for (size_t i = 0; i < sz; ++i) {
        gff["Sounds"][i].get_to("Sound", sounds[i]);
    }

    gff.get_to("Times", times);
    gff.get_to("Volume", volume);
    gff.get_to("VolumeVrtn", volume_variation);

    if (profile == SerializationProfile::instance) {
        gff.get_to("GeneratedType", generated_type);
    }
}

} // namespace nw
