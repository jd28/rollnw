#include "preview_nwn_creature.hpp"

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ModelCache.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/objects/Item.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/format.h>
#include <limits>

namespace nw::render::viewer {

namespace {

uint16_t resolve_body_part_value(uint16_t value, uint16_t mirror_value)
{
    if (value == 255) {
        return mirror_value != 0 && mirror_value != 255 ? mirror_value : 0;
    }
    return value;
}

uint16_t resolve_armor_part_value(uint16_t value, uint16_t mirror_value)
{
    if (value == 0 || value == 255) {
        return mirror_value != 0 && mirror_value != 255 ? mirror_value : value;
    }
    return value;
}

bool body_part_tattoo_model_uses_tattoo_palette(std::string_view token)
{
    return token == "bicepl"
        || token == "bicepr"
        || token == "chest"
        || token == "forel"
        || token == "forer"
        || token == "legl"
        || token == "legr"
        || token == "shinl"
        || token == "shinr";
}

uint16_t resolve_armor_skin_tattoo_model_part(std::string_view token, uint16_t creature_model_part,
    uint16_t armor_model_part)
{
    constexpr uint16_t skin_model_part = 1;
    constexpr uint16_t tattoo_model_part = 2;

    if (armor_model_part == skin_model_part
        && creature_model_part == tattoo_model_part
        && body_part_tattoo_model_uses_tattoo_palette(token)) {
        return tattoo_model_part;
    }
    return armor_model_part;
}

std::optional<nw::ItemModelParts::type> armor_item_part_for_token(std::string_view token)
{
    using Parts = nw::ItemModelParts;
    if (token == "belt") {
        return Parts::armor_belt;
    }
    if (token == "bicepl") {
        return Parts::armor_lbicep;
    }
    if (token == "bicepr") {
        return Parts::armor_rbicep;
    }
    if (token == "chest") {
        return Parts::armor_torso;
    }
    if (token == "forel") {
        return Parts::armor_lfarm;
    }
    if (token == "forer") {
        return Parts::armor_rfarm;
    }
    if (token == "footl") {
        return Parts::armor_lfoot;
    }
    if (token == "footr") {
        return Parts::armor_rfoot;
    }
    if (token == "handl") {
        return Parts::armor_lhand;
    }
    if (token == "handr") {
        return Parts::armor_rhand;
    }
    if (token == "neck") {
        return Parts::armor_neck;
    }
    if (token == "pelvis") {
        return Parts::armor_pelvis;
    }
    if (token == "shinl") {
        return Parts::armor_lshin;
    }
    if (token == "shinr") {
        return Parts::armor_rshin;
    }
    if (token == "shol") {
        return Parts::armor_lshoul;
    }
    if (token == "shor") {
        return Parts::armor_rshoul;
    }
    if (token == "legl") {
        return Parts::armor_lthigh;
    }
    if (token == "legr") {
        return Parts::armor_rthigh;
    }
    return std::nullopt;
}

std::string_view robe_hide_column(std::string_view token)
{
    if (token == "belt") {
        return "HIDEBELT";
    }
    if (token == "bicepl") {
        return "HIDEBICEPL";
    }
    if (token == "bicepr") {
        return "HIDEBICEPR";
    }
    if (token == "footl") {
        return "HIDEFOOTL";
    }
    if (token == "footr") {
        return "HIDEFOOTR";
    }
    if (token == "forel") {
        return "HIDEFOREL";
    }
    if (token == "forer") {
        return "HIDEFORER";
    }
    if (token == "handl") {
        return "HIDEHANDL";
    }
    if (token == "handr") {
        return "HIDEHANDR";
    }
    if (token == "head") {
        return "HIDEHEAD";
    }
    if (token == "neck") {
        return "HIDENECK";
    }
    if (token == "pelvis") {
        return "HIDEPELVIS";
    }
    if (token == "shinl") {
        return "HIDESHINL";
    }
    if (token == "shinr") {
        return "HIDESHINR";
    }
    if (token == "shol") {
        return "HIDESHOL";
    }
    if (token == "shor") {
        return "HIDESHOR";
    }
    if (token == "legl") {
        return "HIDELEGL";
    }
    if (token == "legr") {
        return "HIDELEGR";
    }
    if (token == "chest") {
        return "HIDECHEST";
    }
    return {};
}

bool robe_hides_body_part(uint16_t robe_part, std::string_view token)
{
    auto column = robe_hide_column(token);
    if (column.empty()) {
        return false;
    }

    auto* tda = nw::kernel::twodas().get("parts_robe");
    if (!tda || robe_part >= tda->rows()) {
        return false;
    }

    int hidden = 0;
    return tda->get_to(static_cast<size_t>(robe_part), column, hidden, false) && hidden != 0;
}

std::optional<std::string_view> mirrored_part_token(std::string_view token)
{
    if (token == "bicepl") {
        return "bicepr";
    }
    if (token == "bicepr") {
        return "bicepl";
    }
    if (token == "footl") {
        return "footr";
    }
    if (token == "footr") {
        return "footl";
    }
    if (token == "forel") {
        return "forer";
    }
    if (token == "forer") {
        return "forel";
    }
    if (token == "handl") {
        return "handr";
    }
    if (token == "handr") {
        return "handl";
    }
    if (token == "shinl") {
        return "shinr";
    }
    if (token == "shinr") {
        return "shinl";
    }
    if (token == "shol") {
        return "shor";
    }
    if (token == "shor") {
        return "shol";
    }
    if (token == "legl") {
        return "legr";
    }
    if (token == "legr") {
        return "legl";
    }
    return std::nullopt;
}

bool prefers_symmetric_mirrored_armor_model(std::string_view token)
{
    return token == "legr";
}

std::string anchor_name_for_part(std::string_view token)
{
    if (token == "belt") {
        return "belt_g";
    }
    if (token == "bicepl") {
        return "lbicep_g";
    }
    if (token == "bicepr") {
        return "rbicep_g";
    }
    if (token == "chest") {
        return "torso_g";
    }
    if (token == "forel") {
        return "lforearm_g";
    }
    if (token == "forer") {
        return "rforearm_g";
    }
    if (token == "footl") {
        return "lfoot_g";
    }
    if (token == "footr") {
        return "rfoot_g";
    }
    if (token == "handl") {
        return "lhand_g";
    }
    if (token == "handr") {
        return "rhand_g";
    }
    if (token == "pelvis") {
        return "pelvis_g";
    }
    if (token == "legl") {
        return "lthigh_g";
    }
    if (token == "legr") {
        return "rthigh_g";
    }
    if (token == "neck") {
        return "neck_g";
    }
    if (token == "head") {
        return "head_g";
    }
    if (token == "shinl") {
        return "lshin_g";
    }
    if (token == "shinr") {
        return "rshin_g";
    }
    if (token == "shol") {
        return "lshoulder_g";
    }
    if (token == "shor") {
        return "rshoulder_g";
    }
    return {};
}

struct HumanoidBodyPartSpec {
    uint16_t nw::BodyParts::* field;
    std::string_view token;
};

constexpr std::array<HumanoidBodyPartSpec, 19> kHumanoidBodyPartSpecs{{
    {&nw::BodyParts::belt, "belt"},
    {&nw::BodyParts::bicep_left, "bicepl"},
    {&nw::BodyParts::bicep_right, "bicepr"},
    {&nw::BodyParts::foot_left, "footl"},
    {&nw::BodyParts::foot_right, "footr"},
    {&nw::BodyParts::forearm_left, "forel"},
    {&nw::BodyParts::forearm_right, "forer"},
    {&nw::BodyParts::hand_left, "handl"},
    {&nw::BodyParts::hand_right, "handr"},
    {&nw::BodyParts::head, "head"},
    {&nw::BodyParts::neck, "neck"},
    {&nw::BodyParts::pelvis, "pelvis"},
    {&nw::BodyParts::shin_left, "shinl"},
    {&nw::BodyParts::shin_right, "shinr"},
    {&nw::BodyParts::shoulder_left, "shol"},
    {&nw::BodyParts::shoulder_right, "shor"},
    {&nw::BodyParts::thigh_left, "legl"},
    {&nw::BodyParts::thigh_right, "legr"},
    {&nw::BodyParts::torso, "chest"},
}};

} // namespace

nw::BodyParts normalized_body_parts(nw::BodyParts body_parts)
{
    body_parts.bicep_left = resolve_body_part_value(body_parts.bicep_left, body_parts.bicep_right);
    body_parts.bicep_right = resolve_body_part_value(body_parts.bicep_right, body_parts.bicep_left);
    body_parts.forearm_left = resolve_body_part_value(body_parts.forearm_left, body_parts.forearm_right);
    body_parts.forearm_right = resolve_body_part_value(body_parts.forearm_right, body_parts.forearm_left);
    body_parts.hand_left = resolve_body_part_value(body_parts.hand_left, body_parts.hand_right);
    body_parts.hand_right = resolve_body_part_value(body_parts.hand_right, body_parts.hand_left);
    body_parts.foot_left = resolve_body_part_value(body_parts.foot_left, body_parts.foot_right);
    body_parts.foot_right = resolve_body_part_value(body_parts.foot_right, body_parts.foot_left);
    body_parts.shin_left = resolve_body_part_value(body_parts.shin_left, body_parts.shin_right);
    body_parts.shin_right = resolve_body_part_value(body_parts.shin_right, body_parts.shin_left);
    body_parts.thigh_left = resolve_body_part_value(body_parts.thigh_left, body_parts.thigh_right);
    body_parts.thigh_right = resolve_body_part_value(body_parts.thigh_right, body_parts.thigh_left);
    return body_parts;
}

nw::Item* equipped_item(const nw::Equips& equips, nw::EquipIndex slot)
{
    auto idx = static_cast<size_t>(slot);
    if (idx >= equips.equips.size()) {
        return nullptr;
    }
    const auto& equip = equips.equips[idx];
    return nw::equip_item_ptr(equip);
}

std::string anchor_name_for_equipped_item(nw::EquipIndex slot)
{
    switch (slot) {
    case nw::EquipIndex::head:
        return "head_g";
    case nw::EquipIndex::righthand:
        return "rhand";
    case nw::EquipIndex::lefthand:
        return "lhand";
    default:
        return {};
    }
}

std::string anchor_name_for_attachment(std::string_view table_name)
{
    if (table_name == "wingmodel") {
        return "wings";
    }
    if (table_name == "tailmodel") {
        return "tail";
    }
    return {};
}

std::string source_anchor_name_for_attachment(std::string_view table_name)
{
    if (table_name == "wingmodel") {
        return "wings";
    }
    if (table_name == "tailmodel") {
        return "tail";
    }
    return {};
}

float appearance_wing_tail_scale(const nw::StaticTwoDA* appearance_tda, nw::Appearance appearance_id)
{
    if (!appearance_tda || appearance_id == nw::Appearance::invalid()) {
        return 1.0f;
    }

    float scale = 1.0f;
    appearance_tda->get_to(*appearance_id, "WING_TAIL_SCALE", scale, false);
    return scale;
}

float appearance_helmet_scale(const nw::StaticTwoDA* appearance_tda, nw::Appearance appearance_id, uint8_t gender)
{
    if (!appearance_tda || appearance_id == nw::Appearance::invalid()) {
        return 1.0f;
    }

    float scale = 1.0f;
    const auto* column = gender == 1 ? "HELMET_SCALE_F" : "HELMET_SCALE_M";
    appearance_tda->get_to(*appearance_id, column, scale, false);
    return scale;
}

NwnAppearanceHandItemVisualPolicy resolve_nwn_appearance_hand_item_visual_policy(
    const nw::StaticTwoDA* appearance_tda,
    nw::Appearance appearance_id)
{
    NwnAppearanceHandItemVisualPolicy result;
    if (!appearance_tda
        || appearance_id == nw::Appearance::invalid()
        || appearance_id.idx() >= appearance_tda->rows()) {
        return result;
    }

    int32_t has_arms = 1;
    if (appearance_tda->get_to(*appearance_id, "HASARMS", has_arms, false) && has_arms == 0) {
        result.visible = false;
        result.reason = NwnAppearanceHandItemVisualPolicyReason::hidden_no_arms;
        return result;
    }

    const size_t weapon_scale_column = appearance_tda->column_index("WEAPONSCALE");
    if (weapon_scale_column == nw::StaticTwoDA::npos) {
        return result;
    }

    nw::StringView raw_scale;
    if (!appearance_tda->get_to(*appearance_id, weapon_scale_column, raw_scale)) {
        result.visible = false;
        result.reason = NwnAppearanceHandItemVisualPolicyReason::hidden_null_weapon_scale;
        return result;
    }

    const auto scale = nw::string::from<float>(raw_scale);
    if (!scale || !std::isfinite(*scale) || *scale <= 0.0f) {
        result.visible = false;
        result.reason = NwnAppearanceHandItemVisualPolicyReason::hidden_invalid_weapon_scale;
        return result;
    }

    result.scale = *scale;
    return result;
}

NwnWingAttachmentVisualPolicy resolve_nwn_wing_attachment_visual_policy(
    nw::Appearance appearance_id,
    uint32_t wing_row)
{
    constexpr uint32_t any_appearance_row = std::numeric_limits<uint32_t>::max();
    struct PolicyRow {
        uint32_t appearance_row;
        uint32_t wing_row;
        NwnWingAttachmentVisualPolicyReason reason;
    };

    constexpr std::array policy_rows = {
        PolicyRow{
            .appearance_row = any_appearance_row,
            .wing_row = 1u,
            .reason = NwnWingAttachmentVisualPolicyReason::strip_non_render_meshes,
        },
    };

    if (wing_row == 0) {
        return {};
    }

    const uint32_t appearance_row = appearance_id == nw::Appearance::invalid()
        ? any_appearance_row
        : static_cast<uint32_t>(appearance_id.idx());
    for (const auto& row : policy_rows) {
        if (row.wing_row != wing_row) {
            continue;
        }
        if (row.appearance_row != any_appearance_row && row.appearance_row != appearance_row) {
            continue;
        }
        return {
            .strip_non_render_meshes = row.reason == NwnWingAttachmentVisualPolicyReason::strip_non_render_meshes,
            .reason = row.reason,
        };
    }
    return {};
}

size_t apply_nwn_wing_attachment_visual_policy(
    nw::render::nwn::ModelInstance& model,
    NwnWingAttachmentVisualPolicy policy)
{
    if (!policy.strip_non_render_meshes) {
        return 0;
    }

    size_t stripped_mesh_count = 0;
    for (const auto& node : model.nodes_) {
        if (!node || !node->orig_ || !node->is_mesh || node->is_skin) {
            continue;
        }
        const auto* source_mesh = dynamic_cast<const nw::model::TrimeshNode*>(node->orig_);
        if (!source_mesh || source_mesh->render) {
            continue;
        }
        if (auto* mesh = dynamic_cast<nw::render::nwn::Mesh*>(node.get())) {
            mesh->vertices = {};
            mesh->indices = {};
            mesh->index_count = 0;
            ++stripped_mesh_count;
        }
    }
    return stripped_mesh_count;
}

size_t count_nwn_wing_attachment_visual_policy_stripped_meshes(
    const nw::model::Mdl& mdl,
    NwnWingAttachmentVisualPolicy policy)
{
    if (!policy.strip_non_render_meshes) {
        return 0;
    }

    size_t stripped_mesh_count = 0;
    for (const auto& node : mdl.model.nodes) {
        if (!node || !(node->type & nw::model::NodeFlags::mesh) || (node->type & nw::model::NodeFlags::skin)) {
            continue;
        }
        const auto* source_mesh = dynamic_cast<const nw::model::TrimeshNode*>(node.get());
        if (source_mesh && !source_mesh->render) {
            ++stripped_mesh_count;
        }
    }
    return stripped_mesh_count;
}

std::optional<std::string> resolve_creature_part_model(char sex, std::string_view race, int phenotype,
    std::initializer_list<std::string_view> part_tokens, uint16_t part)
{
    if (part == 0 || part == 255 || race.empty()) {
        return std::nullopt;
    }

    for (auto token : part_tokens) {
        auto resref = fmt::format("p{}{}{}_{}{:03d}", sex, race, phenotype, token, part);
        if (nw::kernel::resman().contains({resref, nw::ResourceType::mdl})) {
            return resref;
        }
    }
    return std::nullopt;
}

std::optional<std::string> resolve_creature_cloak_model(char sex, std::string_view race, int phenotype, uint16_t part)
{
    return resolve_creature_part_model(sex, race, phenotype, {"cloak_", "cloak"}, part);
}

std::optional<std::string> resolve_creature_base_rig(const nw::AppearanceInfo& appearance, std::string_view race, char sex)
{
    nw::String label = appearance.label;
    nw::string::tolower(&label);
    for (auto& ch : label) {
        if (ch == ' ' || ch == '-') {
            ch = '_';
        }
    }

    const std::array<std::string, 3> candidates = {
        fmt::format("p{}{}0", sex, race),
        fmt::format("a_{}", label.c_str()),
        fmt::format("a_{}a", sex),
    };

    for (const auto& candidate : candidates) {
        if (nw::kernel::resman().contains({nw::Resref{candidate}, nw::ResourceType::mdl})) {
            return candidate;
        }
    }
    return std::nullopt;
}

void preserve_creature_identity_plt_colors(nw::PltColors& colors, const nw::PltColors& creature_colors)
{
    colors.data[nw::plt_layer_skin] = creature_colors.data[nw::plt_layer_skin];
    colors.data[nw::plt_layer_hair] = creature_colors.data[nw::plt_layer_hair];
    colors.data[nw::plt_layer_tattoo1] = creature_colors.data[nw::plt_layer_tattoo1];
    colors.data[nw::plt_layer_tattoo2] = creature_colors.data[nw::plt_layer_tattoo2];
}

std::vector<ResolvedHumanoidBodyPartModel> resolve_humanoid_body_part_models(
    char sex,
    std::string_view race,
    int phenotype,
    nw::BodyParts body_parts,
    const nw::PltColors& plt_colors,
    const nw::Item* chest_item,
    const nw::Item* head_item)
{
    std::vector<ResolvedHumanoidBodyPartModel> result;
    result.reserve(kHumanoidBodyPartSpecs.size());

    for (const auto& part : kHumanoidBodyPartSpecs) {
        auto model_part = body_parts.*(part.field);
        const uint16_t creature_model_part = model_part;
        nw::PltColors part_colors = plt_colors;
        bool prefer_mirrored_part_model = false;
        const uint16_t robe_part = chest_item && chest_item->model_type == nw::ItemModelType::armor
            ? static_cast<uint16_t>(chest_item->model_parts[nw::ItemModelParts::armor_robe])
            : 0;

        if (head_item && part.token == "head") {
            continue;
        }
        if (robe_part > 0 && robe_hides_body_part(robe_part, part.token)) {
            continue;
        }

        if (chest_item && chest_item->model_type == nw::ItemModelType::armor) {
            if (auto armor_part = armor_item_part_for_token(part.token)) {
                uint16_t armor_model_part = chest_item->model_parts[*armor_part];
                nw::PltColors armor_part_colors = chest_item->part_to_plt_colors(*armor_part);
                preserve_creature_identity_plt_colors(armor_part_colors, plt_colors);

                if (auto mirror = mirrored_part_token(part.token)) {
                    if (auto mirror_armor_part = armor_item_part_for_token(*mirror)) {
                        const uint16_t raw_armor_model_part = chest_item->model_parts[*armor_part];
                        const uint16_t mirrored_model_part = chest_item->model_parts[*mirror_armor_part];
                        armor_model_part = resolve_armor_part_value(armor_model_part, mirrored_model_part);
                        const bool inherited_from_mirror = (raw_armor_model_part == 0 || raw_armor_model_part == 255)
                            && mirrored_model_part != 0 && mirrored_model_part != 255;
                        const bool symmetric_right_side = prefers_symmetric_mirrored_armor_model(part.token)
                            && armor_model_part != 0 && armor_model_part != 255
                            && armor_model_part == mirrored_model_part;
                        prefer_mirrored_part_model = prefers_symmetric_mirrored_armor_model(part.token)
                            && (inherited_from_mirror || symmetric_right_side);

                        if (inherited_from_mirror) {
                            prefer_mirrored_part_model = true;
                            armor_part_colors = chest_item->part_to_plt_colors(*mirror_armor_part);
                            preserve_creature_identity_plt_colors(armor_part_colors, plt_colors);
                        }
                    }
                }

                armor_model_part = resolve_armor_skin_tattoo_model_part(
                    part.token, creature_model_part, armor_model_part);
                if (armor_model_part > 0 && armor_model_part != 255) {
                    model_part = armor_model_part;
                    part_colors = armor_part_colors;
                }
            }
        }

        auto part_resref = resolve_creature_part_model(sex, race, phenotype, {part.token}, model_part);
        if (auto mirror = mirrored_part_token(part.token)) {
            if (prefer_mirrored_part_model) {
                part_resref = resolve_creature_part_model(sex, race, phenotype, {*mirror, part.token}, model_part);
            } else if (!part_resref) {
                part_resref = resolve_creature_part_model(sex, race, phenotype, {*mirror, part.token}, model_part);
            }
        }

        result.push_back(ResolvedHumanoidBodyPartModel{
            .token = part.token,
            .anchor = anchor_name_for_part(part.token),
            .colors = part_colors,
            .resref = std::move(part_resref),
            .model_part = model_part,
            .missing_requested_part = !part_resref && model_part > 0 && model_part != 255,
        });
    }

    return result;
}

std::vector<ResolvedItemModelPart> resolve_item_model_parts(
    const nw::Item& item,
    const nw::BaseItemInfo& baseitem)
{
    std::vector<ResolvedItemModelPart> result;
    result.reserve(3);

    auto add = [&](std::string resref, nw::ItemModelParts::type part) {
        if (!resref.empty()) {
            result.push_back(ResolvedItemModelPart{
                .resref = std::move(resref),
                .part = part,
            });
        }
    };

    switch (item.model_type) {
    case nw::ItemModelType::simple:
    case nw::ItemModelType::layered:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            add(fmt::format("{}_{:03d}", baseitem.item_class.view(), item.model_parts[nw::ItemModelParts::model1]),
                nw::ItemModelParts::model1);
        }
        break;
    case nw::ItemModelType::composite:
        if (item.model_parts[nw::ItemModelParts::model1] > 0) {
            add(fmt::format("{}_b_{:03d}", baseitem.item_class.view(), item.model_parts[nw::ItemModelParts::model1]),
                nw::ItemModelParts::model1);
        }
        if (item.model_parts[nw::ItemModelParts::model2] > 0) {
            add(fmt::format("{}_m_{:03d}", baseitem.item_class.view(), item.model_parts[nw::ItemModelParts::model2]),
                nw::ItemModelParts::model2);
        }
        if (item.model_parts[nw::ItemModelParts::model3] > 0) {
            add(fmt::format("{}_t_{:03d}", baseitem.item_class.view(), item.model_parts[nw::ItemModelParts::model3]),
                nw::ItemModelParts::model3);
        }
        break;
    case nw::ItemModelType::armor:
        break;
    }

    return result;
}

CreatureAttachmentModelLookup resolve_creature_attachment_model_lookup(
    std::string_view table_name,
    uint32_t row)
{
    CreatureAttachmentModelLookup result{
        .table_name = table_name,
        .row = row,
        .model_name = {},
        .owner_socket = anchor_name_for_attachment(table_name),
        .source_socket = source_anchor_name_for_attachment(table_name),
        .warning = {},
        .requested = row != 0,
        .resolved = false,
        .null_model = false,
    };
    if (!result.requested) {
        return result;
    }

    auto* tda = nw::kernel::twodas().get(table_name);
    if (!tda) {
        result.warning = fmt::format("Dynamic creature attachment table '{}' was not loaded", table_name);
        return result;
    }
    if (!tda->get_to(row, "MODEL", result.model_name) || result.model_name.empty()) {
        result.warning = fmt::format("Dynamic creature attachment '{}' row {} has no MODEL", table_name, row);
        return result;
    }
    if (result.model_name == "c_nulltail") {
        result.null_model = true;
        return result;
    }

    result.resolved = true;
    return result;
}

} // namespace nw::render::viewer
