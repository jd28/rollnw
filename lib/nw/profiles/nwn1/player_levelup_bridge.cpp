#include "player_levelup_bridge.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/Player.hpp"
#include "../../smalls/runtime.hpp"

namespace nwn1 {

void pc_sync_levelup_class_slots(nw::Player& player)
{
    auto& rt = nw::kernel::runtime();

    nw::Vector<nw::smalls::Value> args;
    auto value = nw::smalls::Value::make_object(player.handle());
    value.type_id = rt.object_subtype_for_tag(player.handle().type);
    args.push_back(value);

    auto result = rt.execute_script("core.player", "sync_levelup_class_slots", args);
    if (!result.ok()) {
        LOG_F(WARNING, "[nwn1.player] core.player.sync_levelup_class_slots failed: {}",
            result.error_message);
    }
}

} // namespace nwn1
