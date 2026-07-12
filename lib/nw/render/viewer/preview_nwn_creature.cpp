#include "preview_nwn_creature.hpp"

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ModelCache.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fmt/format.h>
#include <limits>

namespace nw::render::viewer {

namespace {

nw::smalls::Value script_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    const nw::smalls::StructDef* def = rt.get_struct_def(value.type_id);
    if (!def) {
        return {};
    }

    uint32_t index = def->field_index(field);
    if (index == UINT32_MAX) {
        return {};
    }

    const auto& fd = def->fields[index];
    return rt.read_value_field_at_offset(value, fd.offset, fd.type_id);
}

int32_t script_int_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value,
    const char* field, int32_t fallback = 0)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.int_type() ? field_value.data.ival : fallback;
}

float script_float_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value,
    const char* field, float fallback = 0.0f)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.float_type() ? field_value.data.fval : fallback;
}

bool script_bool_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.bool_type() && field_value.data.bval;
}

std::string script_string_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    if (field_value.type_id != rt.string_type()) {
        return {};
    }
    return std::string{nw::smalls::ScriptString{field_value.data.hptr}.view(rt)};
}

nw::Resref script_resref_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    nw::smalls::TypeID resref_type = rt.type_id("core.types.ResRef", false);
    if (field_value.type_id != resref_type) {
        return {};
    }

    const auto* resref = static_cast<const nw::Resref*>(rt.get_value_data_ptr(field_value));
    return resref ? *resref : nw::Resref{};
}

} // namespace

nw::Item* equipped_item(const nw::Equips& equips, nw::EquipIndex slot)
{
    auto idx = static_cast<size_t>(slot);
    if (idx >= equips.equips.size()) {
        return nullptr;
    }
    const auto& equip = equips.equips[idx];
    return nw::equip_item_ptr(equip);
}

nw::Appearance visual_appearance(const nw::ObjectVisualState* visual) noexcept
{
    if (!visual || visual->appearance < 0) {
        return nw::Appearance::invalid();
    }
    return nw::Appearance::make(visual->appearance);
}

uint8_t visual_body_variant(const nw::ObjectVisualState* visual) noexcept
{
    return visual && visual->body_variant == 1 ? uint8_t{1} : uint8_t{0};
}

nw::PltColors visual_base_plt_colors(const nw::ObjectVisualState* visual) noexcept
{
    nw::PltColors colors{};
    if (!visual) {
        return colors;
    }

    for (size_t layer = 0; layer < colors.data.size(); ++layer) {
        const uint32_t layer_mask = uint32_t{1} << layer;
        if ((visual->base_plt_color_mask & layer_mask) != 0) {
            colors.data[layer] = visual->base_plt_colors.data[layer];
        }
    }
    return colors;
}

bool visual_row_matches_slot(const nw::ObjectVisualModel& row, nw::EquipIndex slot, int32_t kind) noexcept
{
    return row.slot == static_cast<int32_t>(slot) && row.kind == kind;
}

nw::PltColors visual_row_plt_colors(const nw::ObjectVisualModel& row, nw::PltColors colors) noexcept
{
    for (size_t layer = 0; layer < colors.data.size(); ++layer) {
        const uint32_t layer_mask = uint32_t{1} << layer;
        if ((row.plt_color_mask & layer_mask) != 0) {
            colors.data[layer] = row.plt_colors.data[layer];
        }
    }
    return colors;
}

bool visual_row_is_humanoid_body_part(const nw::ObjectVisualModel& row) noexcept
{
    return row.kind == nw::ObjectVisualModelKind::creature_model_part
        && (row.slot == -1
            || row.slot == static_cast<int32_t>(nw::EquipIndex::chest));
}

bool visual_row_requests_model_part(const nw::ObjectVisualModel& row) noexcept
{
    return row.model_part > 0 && row.model_part != 255;
}

std::string visual_row_body_part_name(const nw::ObjectVisualModel& row)
{
    if (row.part == static_cast<int32_t>(nw::ItemModelParts::armor_robe)) {
        return "robe";
    }
    if (!row.attach_to.empty()) {
        return row.attach_to.string();
    }
    return fmt::format("part {}", row.part);
}

bool visual_row_is_creature_attachment(const nw::ObjectVisualModel& row) noexcept
{
    return row.kind == nw::ObjectVisualModelKind::creature_attachment
        && (row.part == nw::ObjectVisualCreatureAttachmentPart::wing
            || row.part == nw::ObjectVisualCreatureAttachmentPart::tail);
}

bool visual_row_is_creature_wing_attachment(const nw::ObjectVisualModel& row) noexcept
{
    return row.kind == nw::ObjectVisualModelKind::creature_attachment
        && row.part == nw::ObjectVisualCreatureAttachmentPart::wing;
}

std::string_view visual_creature_attachment_name(const nw::ObjectVisualModel& row) noexcept
{
    if (row.part == nw::ObjectVisualCreatureAttachmentPart::wing) {
        return "wing";
    }
    if (row.part == nw::ObjectVisualCreatureAttachmentPart::tail) {
        return "tail";
    }
    return "attachment";
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

PreviewCreatureModelLoad resolve_creature_model_from_appearance(nw::Appearance appearance)
{
    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(*appearance));

    auto executed = rt.execute_script("nwn1.creature", "resolve_creature_model", args);
    PreviewCreatureModelLoad result;
    if (!executed.ok()) {
        result.error = fmt::format("nwn1.creature.resolve_creature_model failed: {}", executed.error_message);
        return result;
    }

    result.appearance = script_int_field(rt, executed.value, "appearance", -1);
    result.model_type = script_int_field(rt, executed.value, "model_type", -1);
    result.hand_item_reason = script_int_field(rt, executed.value, "hand_item_reason");
    result.wing_tail_scale = script_float_field(rt, executed.value, "wing_tail_scale", 1.0f);
    result.helmet_scale_m = script_float_field(rt, executed.value, "helmet_scale_m", 1.0f);
    result.helmet_scale_f = script_float_field(rt, executed.value, "helmet_scale_f", 1.0f);
    result.hand_item_scale = script_float_field(rt, executed.value, "hand_item_scale", 1.0f);
    result.hand_item_visible = script_bool_field(rt, executed.value, "hand_item_visible");
    result.humanoid = script_bool_field(rt, executed.value, "humanoid");
    result.resolved = script_bool_field(rt, executed.value, "valid");
    result.error = script_string_field(rt, executed.value, "error");
    result.race = script_resref_field(rt, executed.value, "race");
    result.model = script_resref_field(rt, executed.value, "model");

    if (!result.resolved) {
        if (result.error.empty()) {
            result.error = "nwn1.creature.resolve_creature_model returned invalid data";
        }
        result.model = {};
        result.race = {};
        return result;
    }

    if (!result.humanoid && result.model.empty()) {
        result.resolved = false;
        if (result.error.empty()) {
            result.error = "nwn1.creature.resolve_creature_model returned no model";
        }
        result.model = {};
    }

    return result;
}

NwnAppearanceHandItemVisualPolicy hand_item_visual_policy_from_creature_model(
    const PreviewCreatureModelLoad& model_ref)
{
    NwnAppearanceHandItemVisualPolicy result;
    result.visible = model_ref.hand_item_visible;
    result.scale = model_ref.hand_item_scale;

    switch (model_ref.hand_item_reason) {
    case 0:
        result.reason = NwnAppearanceHandItemVisualPolicyReason::visible;
        break;
    case 1:
        result.reason = NwnAppearanceHandItemVisualPolicyReason::hidden_no_arms;
        break;
    case 2:
        result.reason = NwnAppearanceHandItemVisualPolicyReason::hidden_null_weapon_scale;
        break;
    case 3:
    default:
        result.reason = NwnAppearanceHandItemVisualPolicyReason::hidden_invalid_weapon_scale;
        result.visible = false;
        break;
    }

    return result;
}

float helmet_scale_from_creature_model(const PreviewCreatureModelLoad& model_ref, uint8_t gender)
{
    return gender == 1 ? model_ref.helmet_scale_f : model_ref.helmet_scale_m;
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

} // namespace nw::render::viewer
