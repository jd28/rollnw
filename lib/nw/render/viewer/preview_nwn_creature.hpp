#pragma once

#include <nw/formats/Plt.hpp>
#include <nw/objects/Equips.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/resources/assets.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/items.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nw {
struct Creature;
struct StaticTwoDA;
struct Item;
}

namespace nw::model {
class Mdl;
}

namespace nw::render::nwn {
struct ModelInstance;
}

namespace nw::render::viewer {

struct PreviewCreatureModelLoad {
    nw::Resref model;
    nw::Resref race;
    std::string error;
    int32_t appearance = -1;
    int32_t model_type = -1;
    int32_t hand_item_reason = 0;
    float wing_tail_scale = 1.0f;
    float helmet_scale_m = 1.0f;
    float helmet_scale_f = 1.0f;
    float hand_item_scale = 1.0f;
    bool hand_item_visible = true;
    bool humanoid = false;
    bool resolved = false;

    bool valid() const noexcept { return resolved; }
    bool has_model() const noexcept { return !model.empty(); }
};

enum class NwnAppearanceHandItemVisualPolicyReason : uint8_t {
    visible,
    hidden_no_arms,
    hidden_null_weapon_scale,
    hidden_invalid_weapon_scale,
};

struct NwnAppearanceHandItemVisualPolicy {
    bool visible = true;
    float scale = 1.0f;
    NwnAppearanceHandItemVisualPolicyReason reason = NwnAppearanceHandItemVisualPolicyReason::visible;
};

enum class NwnWingAttachmentVisualPolicyReason : uint8_t {
    default_visible,
    strip_non_render_meshes,
};

struct NwnWingAttachmentVisualPolicy {
    bool strip_non_render_meshes = false;
    NwnWingAttachmentVisualPolicyReason reason = NwnWingAttachmentVisualPolicyReason::default_visible;
};

constexpr std::array<nw::EquipIndex, 3> kPreviewAttachedEquipmentSlots{
    nw::EquipIndex::righthand,
    nw::EquipIndex::lefthand,
    nw::EquipIndex::head,
};

nw::Item* equipped_item(const nw::Equips& equips, nw::EquipIndex slot);
nw::Appearance visual_appearance(const nw::ObjectVisualState* visual) noexcept;
uint8_t visual_body_variant(const nw::ObjectVisualState* visual) noexcept;
nw::PltColors visual_base_plt_colors(const nw::ObjectVisualState* visual) noexcept;
bool visual_row_matches_slot(const nw::ObjectVisualModel& row, nw::EquipIndex slot, int32_t kind) noexcept;
nw::PltColors visual_row_plt_colors(const nw::ObjectVisualModel& row, nw::PltColors colors = {}) noexcept;
bool visual_row_is_humanoid_body_part(const nw::ObjectVisualModel& row) noexcept;
bool visual_row_requests_model_part(const nw::ObjectVisualModel& row) noexcept;
std::string visual_row_body_part_name(const nw::ObjectVisualModel& row);
bool visual_row_is_creature_attachment(const nw::ObjectVisualModel& row) noexcept;
bool visual_row_is_creature_wing_attachment(const nw::ObjectVisualModel& row) noexcept;
std::string_view visual_creature_attachment_name(const nw::ObjectVisualModel& row) noexcept;

std::string anchor_name_for_equipped_item(nw::EquipIndex slot);

float appearance_wing_tail_scale(const nw::StaticTwoDA* appearance_tda, nw::Appearance appearance_id);
float appearance_helmet_scale(const nw::StaticTwoDA* appearance_tda, nw::Appearance appearance_id, uint8_t gender);

NwnAppearanceHandItemVisualPolicy resolve_nwn_appearance_hand_item_visual_policy(
    const nw::StaticTwoDA* appearance_tda,
    nw::Appearance appearance_id);
PreviewCreatureModelLoad resolve_creature_model_from_appearance(nw::Appearance appearance);
NwnAppearanceHandItemVisualPolicy hand_item_visual_policy_from_creature_model(
    const PreviewCreatureModelLoad& model_ref);
float helmet_scale_from_creature_model(const PreviewCreatureModelLoad& model_ref, uint8_t gender);

NwnWingAttachmentVisualPolicy resolve_nwn_wing_attachment_visual_policy(
    nw::Appearance appearance_id,
    uint32_t wing_row);

size_t apply_nwn_wing_attachment_visual_policy(
    nw::render::nwn::ModelInstance& model,
    NwnWingAttachmentVisualPolicy policy);
size_t count_nwn_wing_attachment_visual_policy_stripped_meshes(const nw::model::Mdl& mdl, NwnWingAttachmentVisualPolicy policy);

std::optional<std::string> resolve_creature_base_rig(const nw::AppearanceInfo& appearance, std::string_view race, char sex);

} // namespace nw::render::viewer
