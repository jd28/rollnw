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

bool resolve_creature_action_animation_timings(std::span<const nw::ObjectHandle> actors,
    std::span<CreatureActionAnimationTimings> timings)
{
    std::fill(timings.begin(), timings.end(), CreatureActionAnimationTimings{});
    if (actors.size() != timings.size()) { return false; }
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
        constexpr std::array names{"getlow", "drwright", "drwleft"};
        for (size_t animation = 0; animation < names.size(); ++animation) {
            for (auto* source = mdl; source; source = source->model.supermodel.get()) {
                const auto* clip = source->model.find_animation(names[animation]);
                if (!clip) { continue; }
                if (!std::isfinite(clip->length) || clip->length <= 0.0f) { break; }
                if (animation == 0) {
                    timings[index].pickup_duration = clip->length;
                } else {
                    auto& hand = timings[index].hands[animation - 1];
                    hand.duration = clip->length;
                    for (const auto& event : clip->events) {
                        if (event.name == "draw_weapon" && std::isfinite(event.time)
                            && event.time >= 0.0f && event.time <= clip->length
                            && (hand.draw_marker < 0.0f || event.time < hand.draw_marker)) {
                            hand.draw_marker = event.time;
                        }
                    }
                }
                break;
            }
        }
        nw::kernel::models().release(*resource);
    }
    return true;
}

} // namespace nwn1
