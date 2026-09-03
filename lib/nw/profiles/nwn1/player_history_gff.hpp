#pragma once

namespace nw {
struct GffBuilderStruct;
struct GffStruct;
struct Player;
} // namespace nw

namespace nw::smalls {
struct Runtime;
} // namespace nw::smalls

namespace nwn1 {

[[nodiscard]] bool import_player_history_from_gff(
    nw::smalls::Runtime* runtime, nw::Player* player, const nw::GffStruct& archive);

[[nodiscard]] bool export_player_history_to_gff(
    nw::smalls::Runtime* runtime, const nw::Player* player, nw::GffBuilderStruct& archive);

} // namespace nwn1
