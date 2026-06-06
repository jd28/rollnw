#pragma once

#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Player.hpp>

#include <filesystem>
#include <string_view>

namespace nw::render::viewer {

bool is_object_preview_path(std::string_view value);
bool load_player_from_file(const std::filesystem::path& path, nw::Player& out);
bool load_creature_from_file(const std::filesystem::path& path, nw::Creature& out);
bool load_item_from_file(const std::filesystem::path& path, nw::Item& out);
bool load_door_from_file(const std::filesystem::path& path, nw::Door& out);
bool load_placeable_from_file(const std::filesystem::path& path, nw::Placeable& out);

} // namespace nw::render::viewer
