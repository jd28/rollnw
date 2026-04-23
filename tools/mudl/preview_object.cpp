#include "preview_object.hpp"

#include <nw/serialization/Gff.hpp>
#include <nw/util/string.hpp>

namespace mudl {

bool is_object_preview_path(std::string_view value)
{
    auto path = std::filesystem::path{value};
    if (!std::filesystem::exists(path)) {
        return false;
    }
    nw::String ext = path.extension().string();
    nw::string::tolower(&ext);
    return ext == ".bic" || ext == ".utc" || ext == ".uti";
}

bool load_player_from_file(const std::filesystem::path& path, nw::Player& out)
{
    auto data = nw::ResourceData::from_file(path);
    if (data.bytes.size() == 0) {
        return false;
    }
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        return false;
    }
    return nw::deserialize(&out, gff.toplevel());
}

bool load_creature_from_file(const std::filesystem::path& path, nw::Creature& out)
{
    auto data = nw::ResourceData::from_file(path);
    if (data.bytes.size() == 0) { return false; }
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) { return false; }
    return nw::deserialize(&out, gff.toplevel(), nw::SerializationProfile::instance);
}

bool load_item_from_file(const std::filesystem::path& path, nw::Item& out)
{
    auto data = nw::ResourceData::from_file(path);
    if (data.bytes.size() == 0) {
        return false;
    }
    nw::Gff gff{std::move(data)};
    if (!gff.valid()) {
        return false;
    }
    return nw::deserialize(&out, gff.toplevel(), nw::SerializationProfile::instance);
}

} // namespace mudl
