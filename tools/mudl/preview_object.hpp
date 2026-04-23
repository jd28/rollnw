#pragma once

#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Player.hpp>

#include <filesystem>
#include <string_view>

namespace mudl {

bool is_object_preview_path(std::string_view value);
bool load_player_from_file(const std::filesystem::path& path, nw::Player& out);
bool load_creature_from_file(const std::filesystem::path& path, nw::Creature& out);
bool load_item_from_file(const std::filesystem::path& path, nw::Item& out);

} // namespace mudl
