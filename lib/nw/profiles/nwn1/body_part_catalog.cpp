#include "body_part_catalog.hpp"

#include "../../formats/StaticTwoDA.hpp"
#include "../../resources/ResourceManager.hpp"
#include "../../rules/attributes.hpp"
#include "../../rules/creature_body_parts.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <string_view>

#include <fmt/format.h>

namespace nwn1 {
namespace {

struct PartDescriptor {
    int32_t part_id;
    int32_t mirror_part_id;
    std::string_view token;
    std::string_view anchor;
    std::string_view label;
    uint32_t flags;
};

constexpr int32_t part_belt = 0;
constexpr int32_t part_bicep_left = 1;
constexpr int32_t part_bicep_right = 2;
constexpr int32_t part_foot_left = 3;
constexpr int32_t part_foot_right = 4;
constexpr int32_t part_forearm_left = 5;
constexpr int32_t part_forearm_right = 6;
constexpr int32_t part_hand_left = 7;
constexpr int32_t part_hand_right = 8;
constexpr int32_t part_head = 9;
constexpr int32_t part_neck = 10;
constexpr int32_t part_pelvis = 11;
constexpr int32_t part_shin_left = 12;
constexpr int32_t part_shin_right = 13;
constexpr int32_t part_shoulder_left = 14;
constexpr int32_t part_shoulder_right = 15;
constexpr int32_t part_thigh_left = 16;
constexpr int32_t part_thigh_right = 17;
constexpr int32_t part_torso = 18;
constexpr int32_t part_robe = 19;

constexpr uint32_t visible = nw::creature_body_part_info_flag_editor_visible;
constexpr std::array part_descriptors{
    PartDescriptor{part_head, -1, "head", "head_g", "Head", visible},
    PartDescriptor{part_neck, -1, "neck", "neck_g", "Neck", visible},
    PartDescriptor{part_torso, -1, "chest", "torso_g", "Torso", visible},
    PartDescriptor{part_pelvis, -1, "pelvis", "pelvis_g", "Pelvis", visible},
    PartDescriptor{part_bicep_left, part_bicep_right, "bicepl", "lbicep_g", "Bicep, Left", visible},
    PartDescriptor{part_bicep_right, part_bicep_left, "bicepr", "rbicep_g", "Bicep, Right", visible},
    PartDescriptor{part_forearm_left, part_forearm_right, "forel", "lforearm_g", "Forearm, Left", visible},
    PartDescriptor{part_forearm_right, part_forearm_left, "forer", "rforearm_g", "Forearm, Right", visible},
    PartDescriptor{part_hand_left, part_hand_right, "handl", "lhand_g", "Hand, Left", visible},
    PartDescriptor{part_hand_right, part_hand_left, "handr", "rhand_g", "Hand, Right", visible},
    PartDescriptor{part_thigh_left, part_thigh_right, "legl", "lthigh_g", "Thigh, Left", visible},
    PartDescriptor{part_thigh_right, part_thigh_left, "legr", "rthigh_g", "Thigh, Right", visible},
    PartDescriptor{part_shin_left, part_shin_right, "shinl", "lshin_g", "Shin, Left", visible},
    PartDescriptor{part_shin_right, part_shin_left, "shinr", "rshin_g", "Shin, Right", visible},
    PartDescriptor{part_foot_left, part_foot_right, "footl", "lfoot_g", "Foot, Left", visible},
    PartDescriptor{part_foot_right, part_foot_left, "footr", "rfoot_g", "Foot, Right", visible},
    PartDescriptor{part_belt, -1, "belt", "belt_g", "Belt", visible},
    PartDescriptor{part_shoulder_left, part_shoulder_right, "shol", "lshoulder_g", "Shoulder, Left", visible},
    PartDescriptor{part_shoulder_right, part_shoulder_left, "shor", "rshoulder_g", "Shoulder, Right", visible},
    PartDescriptor{part_robe, -1, "robe", "", "Robe", nw::creature_body_part_info_flag_robe},
};

struct AssemblyDescriptor {
    int32_t appearance = -1;
    int32_t gender = 0;
    int32_t phenotype = 0;
    int32_t fallback_phenotype = 0;
    int32_t assembly_id = -1;
    nw::String prefix;
    nw::String fallback_prefix;
};

struct AssemblyLookup {
    int32_t appearance = -1;
    int32_t gender = 0;
    int32_t phenotype = 0;
    int32_t fallback_phenotype = 0;
    int32_t assembly_id = -1;
};

using ModelKey = std::pair<nw::String, int32_t>;
using PrefixModels = std::map<ModelKey, nw::Resref>;
using ModelBatch = std::map<nw::String, PrefixModels>;

nw::Vector<AssemblyLookup> assembly_lookup;

const PartDescriptor* descriptor(int32_t part_id) noexcept
{
    const auto found = std::ranges::find(
        part_descriptors, part_id, &PartDescriptor::part_id);
    return found == part_descriptors.end() ? nullptr : &*found;
}

const nw::Resref* find_model(
    const ModelBatch& models,
    const nw::String& prefix,
    std::string_view token,
    int32_t option_id)
{
    const auto prefix_it = models.find(prefix);
    if (prefix_it == models.end()) { return nullptr; }
    const auto model_it = prefix_it->second.find(
        {nw::String{token}, option_id});
    return model_it == prefix_it->second.end() ? nullptr : &model_it->second;
}

nw::Resref resolve_model(
    const ModelBatch& models,
    const AssemblyDescriptor& assembly,
    const PartDescriptor& part,
    int32_t option_id)
{
    const auto* mirror = descriptor(part.mirror_part_id);
    if (const auto* model = find_model(
            models, assembly.prefix, part.token, option_id)) {
        return *model;
    }
    if (mirror) {
        if (const auto* model = find_model(
                models, assembly.prefix, mirror->token, option_id)) {
            return *model;
        }
    }
    if (assembly.fallback_prefix != assembly.prefix) {
        if (const auto* model = find_model(
                models, assembly.fallback_prefix, part.token, option_id)) {
            return *model;
        }
        if (mirror) {
            if (const auto* model = find_model(
                    models, assembly.fallback_prefix, mirror->token, option_id)) {
                return *model;
            }
        }
    }
    return {};
}

bool decode_model_resource(
    nw::Resource resource,
    ModelBatch& models)
{
    if (resource.type != nw::ResourceType::mdl) { return false; }
    const auto name = resource.resref.view();
    const auto separator = name.find('_');
    if (separator == nw::StringView::npos || separator + 4 >= name.size()) {
        return false;
    }

    const nw::String prefix{name.substr(0, separator + 1)};
    const auto prefix_it = models.try_emplace(prefix).first;

    const auto suffix = name.substr(name.size() - 3);
    int32_t option_id = -1;
    const auto parsed = std::from_chars(
        suffix.data(), suffix.data() + suffix.size(), option_id);
    if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size()
        || option_id <= 0 || option_id >= 255) {
        return false;
    }

    const auto token = name.substr(separator + 1, name.size() - separator - 4);
    if (std::ranges::find(part_descriptors, token, &PartDescriptor::token)
        == part_descriptors.end()) {
        return false;
    }
    prefix_it->second.insert_or_assign(
        {nw::String{token}, option_id}, resource.resref);
    return true;
}

} // namespace

bool build_body_part_catalog(
    const nw::AppearanceArray& appearances,
    const nw::StaticTwoDA& phenotypes,
    const nw::ResourceManager& resources,
    nw::CreatureBodyPartCatalog& output,
    nw::String& diagnostic)
{
    diagnostic.clear();
    if (!phenotypes.is_valid() || phenotypes.rows() == 0) {
        diagnostic = "Creature body-part catalog requires phenotype.2da";
        return false;
    }

    nw::Vector<int32_t> phenotype_fallbacks(phenotypes.rows(), 0);
    for (size_t phenotype = 0; phenotype < phenotypes.rows(); ++phenotype) {
        int32_t fallback = 0;
        if (!phenotypes.get_to(
                phenotype, "DefaultPhenoType", fallback, false)
            || fallback < 0
            || static_cast<size_t>(fallback) >= phenotypes.rows()) {
            fallback = 0;
        }
        phenotype_fallbacks[phenotype] = fallback;
    }

    nw::Vector<AssemblyDescriptor> assemblies;
    ModelBatch models;
    const auto append_assembly = [&](size_t appearance,
                                     int32_t gender,
                                     int32_t phenotype,
                                     int32_t fallback) {
        const auto duplicate = std::ranges::find_if(
            assemblies, [&](const auto& row) {
                return row.appearance == static_cast<int32_t>(appearance)
                    && row.gender == gender
                    && row.phenotype == phenotype;
            });
        if (duplicate != assemblies.end()) { return; }

        const auto& info = appearances.entries[appearance];
        const char sex = gender == 1 ? 'f' : 'm';
        AssemblyDescriptor assembly;
        assembly.appearance = static_cast<int32_t>(appearance);
        assembly.gender = gender;
        assembly.phenotype = phenotype;
        assembly.fallback_phenotype = fallback;
        assembly.assembly_id = static_cast<int32_t>(assemblies.size());
        assembly.prefix = fmt::format(
            "p{}{}{}_", sex, info.model.view(), phenotype);
        assembly.fallback_prefix = fmt::format(
            "p{}{}{}_", sex, info.model.view(), fallback);
        models.try_emplace(assembly.prefix);
        models.try_emplace(assembly.fallback_prefix);
        assemblies.push_back(std::move(assembly));
    };

    for (size_t appearance = 0; appearance < appearances.entries.size(); ++appearance) {
        const auto& info = appearances.entries[appearance];
        if (!info.valid()
            || info.model_type != nw::AppearanceModelType::parts
            || info.model.empty()) {
            continue;
        }
        for (int32_t gender = 0; gender <= 1; ++gender) {
            for (size_t phenotype = 0; phenotype < phenotype_fallbacks.size(); ++phenotype) {
                append_assembly(appearance, gender,
                    static_cast<int32_t>(phenotype),
                    phenotype_fallbacks[phenotype]);
            }
        }
    }

    resources.visit([&](nw::Resource resource) {
        (void)decode_model_resource(resource, models);
    });

    // Persisted phenotypes and hak resources are not constrained to the rows
    // declared by phenotype.2da. Add every assembly prefix observed in the
    // resolved model registry, using the table only to select its fallback.
    for (size_t appearance = 0; appearance < appearances.entries.size(); ++appearance) {
        const auto& info = appearances.entries[appearance];
        if (!info.valid()
            || info.model_type != nw::AppearanceModelType::parts
            || info.model.empty()) {
            continue;
        }
        for (int32_t gender = 0; gender <= 1; ++gender) {
            const char sex = gender == 1 ? 'f' : 'm';
            const nw::String base = fmt::format("p{}{}", sex, info.model.view());
            for (const auto& [prefix, _] : models) {
                const nw::StringView name{prefix};
                if (!name.starts_with(base) || name.back() != '_') {
                    continue;
                }
                const auto digits = name.substr(
                    base.size(), name.size() - base.size() - 1);
                int32_t phenotype = -1;
                const auto parsed = std::from_chars(
                    digits.data(), digits.data() + digits.size(), phenotype);
                if (digits.empty() || parsed.ec != std::errc{}
                    || parsed.ptr != digits.data() + digits.size()
                    || phenotype < 0) {
                    continue;
                }
                const int32_t fallback = static_cast<size_t>(phenotype)
                        < phenotype_fallbacks.size()
                    ? phenotype_fallbacks[static_cast<size_t>(phenotype)]
                    : 0;
                append_assembly(appearance, gender, phenotype, fallback);
            }
        }
    }

    nw::Vector<nw::CreatureBodyPartSet> sets;
    nw::Vector<nw::CreatureBodyPartInfo> parts;
    nw::Vector<nw::CreatureBodyPartOption> options;
    nw::Vector<AssemblyLookup> lookup;
    sets.reserve(assemblies.size());
    lookup.reserve(assemblies.size());

    for (const auto& assembly : assemblies) {
        nw::CreatureBodyPartSet set;
        set.assembly_id = assembly.assembly_id;
        set.part_offset = static_cast<uint32_t>(parts.size());

        for (const auto& descriptor : part_descriptors) {
            std::array<nw::Resref, 255> resolved;
            uint32_t resolved_count = 0;
            for (int32_t option_id = 1; option_id < 255; ++option_id) {
                resolved[static_cast<size_t>(option_id)] = resolve_model(
                    models, assembly, descriptor, option_id);
                if (!resolved[static_cast<size_t>(option_id)].empty()) {
                    ++resolved_count;
                }
            }
            // Robe selections are pure NWN1 policy values: 0 inherits the
            // equipped armor robe and 255 suppresses it. They remain valid
            // even when the assembly has no standalone robe model resource.
            if (resolved_count == 0 && descriptor.part_id != part_robe) {
                continue;
            }

            nw::CreatureBodyPartInfo part;
            part.part_id = descriptor.part_id;
            part.mirror_part_id = descriptor.mirror_part_id;
            part.option_offset = static_cast<uint32_t>(options.size());
            part.flags = descriptor.flags;
            part.anchor = nw::Resref{descriptor.anchor};
            part.label = descriptor.label;

            options.push_back({
                .part_id = descriptor.part_id,
                .option_id = 0,
                .flags = descriptor.part_id == part_robe
                    ? nw::creature_body_part_option_flag_armor
                    : nw::creature_body_part_option_flag_empty,
            });
            for (int32_t option_id = 1; option_id < 255; ++option_id) {
                auto& model = resolved[static_cast<size_t>(option_id)];
                if (model.empty()) { continue; }
                options.push_back({
                    .part_id = descriptor.part_id,
                    .option_id = option_id,
                    .model = model,
                });
            }
            if (descriptor.mirror_part_id >= 0 || descriptor.part_id == part_robe) {
                options.push_back({
                    .part_id = descriptor.part_id,
                    .option_id = 255,
                    .flags = descriptor.part_id == part_robe
                        ? nw::creature_body_part_option_flag_empty
                        : nw::creature_body_part_option_flag_mirror,
                });
            }
            part.option_count = static_cast<uint32_t>(options.size())
                - part.option_offset;
            parts.push_back(std::move(part));
        }

        set.part_count = static_cast<uint32_t>(parts.size()) - set.part_offset;
        sets.push_back(set);
        lookup.push_back({
            .appearance = assembly.appearance,
            .gender = assembly.gender,
            .phenotype = assembly.phenotype,
            .fallback_phenotype = assembly.fallback_phenotype,
            .assembly_id = assembly.assembly_id,
        });
    }

    if (!output.publish(sets, parts, options, diagnostic)) {
        return false;
    }
    assembly_lookup = std::move(lookup);
    return true;
}

int32_t body_part_assembly_id(
    int32_t appearance,
    int32_t gender,
    int32_t phenotype,
    int32_t fallback_phenotype) noexcept
{
    const int32_t normalized_gender = gender == 1 ? 1 : 0;
    const auto exact = std::ranges::find_if(assembly_lookup, [&](const auto& row) {
        return row.appearance == appearance
            && row.gender == normalized_gender
            && row.phenotype == phenotype;
    });
    if (exact != assembly_lookup.end()) { return exact->assembly_id; }

    const auto fallback = std::ranges::find_if(assembly_lookup, [&](const auto& row) {
        return row.appearance == appearance
            && row.gender == normalized_gender
            && row.phenotype == fallback_phenotype;
    });
    return fallback == assembly_lookup.end() ? -1 : fallback->assembly_id;
}

} // namespace nwn1
