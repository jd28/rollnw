#pragma once

#include <nw/formats/Plt.hpp>
#include <nw/objects/Appearance.hpp>
#include <nw/objects/Equips.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/items.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nw {
struct StaticTwoDA;
struct Item;
}

namespace nw::model {
struct Mdl;
}

namespace nw::render::nwn {
struct ModelInstance;
}

namespace nw::render::viewer {

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

struct ResolvedHumanoidBodyPartModel {
    std::string_view token;
    std::string anchor;
    nw::PltColors colors{};
    std::optional<std::string> resref;
    uint16_t model_part = 0;
    bool missing_requested_part = false;
};

struct ResolvedItemModelPart {
    std::string resref;
    nw::ItemModelParts::type part = nw::ItemModelParts::model1;
};

struct CreatureAttachmentModelLookup {
    std::string_view table_name;
    uint32_t row = 0;
    std::string model_name;
    std::string owner_socket;
    std::string source_socket;
    std::string warning;
    bool requested = false;
    bool resolved = false;
    bool null_model = false;
};

nw::BodyParts normalized_body_parts(nw::BodyParts body_parts);
nw::Item* equipped_item(const nw::Equips& equips, nw::EquipIndex slot);

std::string anchor_name_for_equipped_item(nw::EquipIndex slot);
std::string anchor_name_for_attachment(std::string_view table_name);
std::string source_anchor_name_for_attachment(std::string_view table_name);

float appearance_wing_tail_scale(const nw::StaticTwoDA* appearance_tda, nw::Appearance appearance_id);
float appearance_helmet_scale(const nw::StaticTwoDA* appearance_tda, nw::Appearance appearance_id, uint8_t gender);

NwnAppearanceHandItemVisualPolicy resolve_nwn_appearance_hand_item_visual_policy(
    const nw::StaticTwoDA* appearance_tda,
    nw::Appearance appearance_id);

NwnWingAttachmentVisualPolicy resolve_nwn_wing_attachment_visual_policy(
    nw::Appearance appearance_id,
    uint32_t wing_row);

int resolve_creature_phenotype(nw::Phenotype phenotype) noexcept;

size_t apply_nwn_wing_attachment_visual_policy(
    nw::render::nwn::ModelInstance& model,
    NwnWingAttachmentVisualPolicy policy);
size_t count_nwn_wing_attachment_visual_policy_stripped_meshes(
    const nw::model::Mdl& mdl,
    NwnWingAttachmentVisualPolicy policy);

std::optional<std::string> resolve_creature_part_model(char sex, std::string_view race, int phenotype,
    std::initializer_list<std::string_view> part_tokens, uint16_t part);
std::optional<std::string> resolve_creature_cloak_model(char sex, std::string_view race, int phenotype, uint16_t part);
std::optional<std::string> resolve_creature_base_rig(const nw::AppearanceInfo& appearance, std::string_view race, char sex);

void preserve_creature_identity_plt_colors(nw::PltColors& colors, const nw::PltColors& creature_colors);

std::vector<ResolvedHumanoidBodyPartModel> resolve_humanoid_body_part_models(
    char sex,
    std::string_view race,
    int phenotype,
    nw::BodyParts body_parts,
    const nw::PltColors& plt_colors,
    const nw::Item* chest_item,
    const nw::Item* head_item);

std::vector<ResolvedItemModelPart> resolve_item_model_parts(
    const nw::Item& item,
    const nw::BaseItemInfo& baseitem);

CreatureAttachmentModelLookup resolve_creature_attachment_model_lookup(
    std::string_view table_name,
    uint32_t row);

} // namespace nw::render::viewer
