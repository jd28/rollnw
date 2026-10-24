#include "animation.hpp"

#include "../../kernel/ModelCache.hpp"
#include "../../kernel/Rules.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../resources/ResourceManager.hpp"
#include "../../util/string.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace nwn1 {

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

bool resolve_pickup_animation_durations(std::span<const nw::ObjectHandle> actors,
    std::span<float> durations)
{
    std::fill(durations.begin(), durations.end(), 0.0f);
    if (actors.size() != durations.size()) { return false; }
    for (size_t index = 0; index < actors.size(); ++index) {
        const auto actor = actors[index];
        const auto* visual = actor.type == nw::ObjectType::creature && nw::kernel::objects().valid(actor)
            ? nw::kernel::objects().components().find_visual(actor)
            : nullptr;
        if (!visual) { continue; }
        const auto* appearance = nw::kernel::rules().appearances.get(nw::Appearance::make(visual->appearance));
        if (!appearance) { continue; }
        std::optional<std::string> resource;
        if (appearance->model_type == nw::AppearanceModelType::parts) {
            resource = resolve_creature_base_rig(*appearance, appearance->model.view(),
                visual->body_variant == 1 ? 'f' : 'm');
        } else {
            for (const auto& row : visual->models) {
                if (row.kind == nw::ObjectVisualModelKind::root) {
                    resource = std::string{row.model.view()};
                    break;
                }
            }
        }
        if (!resource || resource->empty()) { continue; }
        auto* mdl = nw::kernel::models().load(*resource);
        if (!mdl) { continue; }
        for (auto* source = mdl; source; source = source->model.supermodel.get()) {
            const auto* clip = source->model.find_animation("getlow");
            if (!clip) { continue; }
            if (std::isfinite(clip->length) && clip->length > 0.0f) { durations[index] = clip->length; }
            break;
        }
        nw::kernel::models().release(*resource);
    }
    return true;
}

} // namespace nwn1
