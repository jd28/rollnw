#pragma once

#include "../ui/smalls_item_icons.hpp"
#include "object_document.hpp"

namespace Rml {
class ElementDocument;
}

namespace nw::toolset {

// One displayed workbench owns one preview tree. The real Item and Creature
// are borrowed identities; neither can become owned by the mannequin.
struct ItemPreviewState {
    ObjectHandle item{};
    ObjectHandle source{};
    ObjectDocument mannequin;
    ItemIconTextureCache icons;
    std::string icon;
    std::string diagnostic;
    uint64_t mutation_epoch = 0;
    uint64_t resource_generation = 0;
    uint64_t service_generation = 0;
    uint64_t revision = 0;
    int32_t gender = -1;
    int32_t displayed_gender = 0;
    bool armor = false;
    bool dirty = true;
    bool dragging = false;

    [[nodiscard]] ObjectHandle preview_object() const noexcept
    {
        return armor ? mannequin.object() : item;
    }
};

// Cold singleton refresh. Invalid/stale identities clear the preview. Errors
// are retained until inputs change; unchanged frames do no serialization work.
bool refresh_item_preview(ItemPreviewState& state, ObjectHandle item,
    ObjectHandle source, uint64_t mutation_epoch);
bool select_item_preview_gender(ItemPreviewState& state, ObjectHandle item, int32_t gender);
void hydrate_item_preview(Rml::ElementDocument* document, const ItemPreviewState& state);

} // namespace nw::toolset
