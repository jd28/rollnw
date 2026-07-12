#include "../runtime.hpp"

#include "../../objects/ObjectManager.hpp"
#include "../../objects/Player.hpp"

#include <algorithm>
#include <limits>

namespace nw::smalls {

namespace {

const nw::Player* const_player_from_handle(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    return base ? base->as_player() : nullptr;
}

} // namespace

void register_core_player(Runtime& rt)
{
    if (rt.get_native_module("core.player")) {
        return;
    }

    rt.module("core.player")
        .function("history_entry_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* player = const_player_from_handle(obj);
            if (!player) { return 0; }

            const auto count = player->history.entries.size();
            constexpr auto max = static_cast<size_t>(std::numeric_limits<int32_t>::max());
            return static_cast<int32_t>(std::min(count, max)); })
        .function("history_entry_class", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* player = const_player_from_handle(obj);
            if (!player || index < 0) { return -1; }

            const auto i = static_cast<size_t>(index);
            if (i >= player->history.entries.size()) { return -1; }

            const auto class_id = player->history.entries[i].class_;
            return class_id == nw::Class::invalid() ? -1 : *class_id; })
        .finalize();
}

} // namespace nw::smalls
