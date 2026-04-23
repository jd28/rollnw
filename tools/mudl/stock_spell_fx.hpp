#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace mudl {

struct StockSpellProjectileFx {
    std::string_view impact_script;
    std::string_view vfx_label;
    uint32_t projectile_count = 1;
    int cadence_ms = 0;
};

std::optional<StockSpellProjectileFx> resolve_stock_spell_projectile_fx(std::string_view impact_script);

} // namespace mudl
