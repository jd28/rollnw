#include "stock_spell_fx.hpp"

#include <nw/util/string.hpp>

#include <array>

namespace mudl {
namespace {

constexpr std::array k_stock_spell_projectiles{
    StockSpellProjectileFx{
        .impact_script = "NW_S0_MagMiss",
        .vfx_label = "VFX_IMP_MIRV",
        .projectile_count = 1,
        .cadence_ms = 0,
    },
};

} // namespace

std::optional<StockSpellProjectileFx> resolve_stock_spell_projectile_fx(std::string_view impact_script)
{
    for (const auto& entry : k_stock_spell_projectiles) {
        if (nw::string::icmp(impact_script, entry.impact_script)) {
            return entry;
        }
    }
    return std::nullopt;
}

} // namespace mudl
