#include "Player.hpp"

#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "../util/platform.hpp"
#include "LevelHistory.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace nw {

Player::Player()
    : Player{nw::kernel::global_allocator()}
{
}

Player::Player(nw::MemoryResource* allocator)
    : Creature(allocator)
{
}

bool Player::save(const std::filesystem::path& path, std::string_view format)
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
// == Player - Serialization - Gff ============================================
// ============================================================================

GffBuilder serialize(const Player* obj)
{
    GffBuilder result{"BIC"};
    serialize(obj, result.top);
    return result;
}

bool serialize(const Player* obj, GffBuilderStruct& archive)
{
    serialize(obj->as_creature(), archive, SerializationProfile::instance);
    auto list = archive.add_list("LvlStatList");
    for (auto it : obj->history.entries) {
        serialize(it, list.push_back(0));
    }
    return true;
}

bool deserialize(Player* obj, const GffStruct& archive)
{
    obj->pc = true;
    deserialize(obj->as_creature(), archive, SerializationProfile::instance);

    auto level_stats = archive["LvlStatList"];
    obj->history.entries.resize(level_stats.size());

    for (size_t i = 0; i < level_stats.size(); ++i) {
        deserialize(obj->history.entries[i], level_stats[i]);
    }

    return true;
}

// == Player - Serialization - JSON ===========================================
// ============================================================================

bool deserialize(Player* obj, const nlohmann::json& archive)
{
    obj->pc = true;
    deserialize(obj->as_creature(), archive, SerializationProfile::instance);
    archive.at("history").get_to(obj->history.entries);
    return true;
}

bool serialize(const Player* obj, nlohmann::json& archive)
{
    serialize(obj->as_creature(), archive, SerializationProfile::instance);
    archive["history"] = obj->history.entries;
    return true;
}

} // namespace nw
