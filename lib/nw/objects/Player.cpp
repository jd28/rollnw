#include "Player.hpp"

#include "../kernel/Kernel.hpp"
#include "../profiles/nwn1/player_history_gff.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "../util/platform.hpp"

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
    return serialize(obj->as_creature(), archive, SerializationProfile::instance)
        && nwn1::export_player_history_to_gff(&kernel::runtime(), obj, archive);
}

bool deserialize(Player* obj, const GffStruct& archive)
{
    return deserialize(obj->as_creature(), archive, SerializationProfile::instance)
        && nwn1::import_player_history_from_gff(&kernel::runtime(), obj, archive);
}

// == Player - Serialization - JSON ===========================================
// ============================================================================

bool deserialize(Player* obj, const nlohmann::json& archive)
{
    return deserialize(obj->as_creature(), archive, SerializationProfile::instance);
}

bool serialize(const Player* obj, nlohmann::json& archive)
{
    return serialize(obj->as_creature(), archive, SerializationProfile::instance);
}

} // namespace nw
