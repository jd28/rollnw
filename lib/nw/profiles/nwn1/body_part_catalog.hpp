#pragma once

#include "../../rules/attributes.hpp"
#include "../../rules/creature_body_parts.hpp"

#include <cstdint>

namespace nw {
struct ResourceManager;
struct StaticTwoDA;
}

namespace nwn1 {

// Decodes the resolved NWN1 model-resource batch and atomically publishes the
// generic catalog. Model-name grammar and legacy selection sentinels are
// confined to this adapter.
[[nodiscard]] bool build_body_part_catalog(
    const nw::AppearanceArray& appearances,
    const nw::StaticTwoDA& phenotypes,
    const nw::ResourceManager& resources,
    nw::CreatureBodyPartCatalog& output,
    nw::String& diagnostic);

// The lookup is cold profile state keyed by the actual persisted assembly
// inputs. Gender is normalized to NWN1 female (1) or male (all other values).
[[nodiscard]] int32_t body_part_assembly_id(
    int32_t appearance,
    int32_t gender,
    int32_t phenotype,
    int32_t fallback_phenotype) noexcept;

} // namespace nwn1
