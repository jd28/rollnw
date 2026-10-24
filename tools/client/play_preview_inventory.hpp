#pragma once

#include "play_preview_view.hpp"
#include <RmlUi/Core/EventListener.h>

namespace nw::toolset {

// UI document/context owns elements. This adapter only queues session-bound
// requests; simulation mutation occurs at the next preview tick boundary.
class PlayPreviewInventoryListener final : public Rml::EventListener {
public:
    explicit PlayPreviewInventoryListener(PlayPreviewState& preview)
        : preview_(preview)
    {
    }
    void ProcessEvent(Rml::Event& event) override;

private:
    PlayPreviewState& preview_;
};

void sync_play_preview_inventory(PlayPreviewState& preview);
void toggle_play_preview_inventory(PlayPreviewState& preview, RuntimeInputState& input);

} // namespace nw::toolset
