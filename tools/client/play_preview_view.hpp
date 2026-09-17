#pragma once

#include "preview_session.hpp"
#include "viewport_rect.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

class ClientRenderer;
namespace Rml {
class ElementDocument;
}

namespace nw::toolset {

class ShellController;
struct RuntimeInputState;
struct PcSourceEligibility;

// One detached F9 session/picker for the displayed area. The device connection
// is owned separately. Simulation inputs/outputs retain existing batch layouts.
struct PlayPreviewState {
    nw::toolset::ToolsetPreviewSession session;
    nw::toolset::PreviewFixedStepState fixed_step;
    std::array<nw::toolset::PreviewInputSample, 6> tick_inputs{};
    std::array<nw::ObjectSpatialState, 1> spatial_rows{};
    std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion_rows{};
    nw::Resource pending_actor{};
    nw::ObjectHandle area{};
    uint64_t module_generation = 0;
    std::string tab_id;
    std::string placement_diagnostic;
    std::string picker_previous_query;
    bool selecting_actor = false;
    bool picker_was_showing_project_tree = false;
    bool picker_was_showing_areas = false;
    bool visuals_attached = false;

    bool placement_pending() const noexcept
    {
        return pending_actor.valid();
    }
};

struct PlayPreviewActorResult {
    Resource actor;
    std::string diagnostic;
};

// The project preview setting is a genuine singleton. Invalid/missing actor
// paths return an unavailable resource and a picker diagnostic.
PlayPreviewActorResult resolve_play_preview_actor(const std::filesystem::path& project,
    const std::filesystem::path& selected_actor);
float play_preview_yaw(const ClientViewportRay& ray) noexcept;
void request_play_preview_actor(Rml::ElementDocument* doc, PlayPreviewState& preview,
    ShellController& shell, std::string_view reason);
bool restore_play_preview_picker_shell(Rml::ElementDocument* doc, PlayPreviewState& preview,
    ShellController& shell);
void sync_play_preview_viewport_overlay(Rml::ElementDocument* doc,
    const std::optional<ClientViewportRect>& viewport, const PlayPreviewState& preview);

void reset_play_preview_input(PlayPreviewState& preview, RuntimeInputState& input) noexcept;
void arm_play_preview(PlayPreviewState& preview, RuntimeInputState& input, Resource actor,
    ObjectHandle area, uint64_t module_generation, std::string_view tab_id);
void stop_play_preview(ClientRenderer& renderer, PlayPreviewState& preview,
    RuntimeInputState& input, ShellController& shell);

enum class PlayPreviewStartStatus : uint8_t { unavailable,
    spawn_failed,
    visuals_failed,
    started };
struct PlayPreviewStartResult {
    PlayPreviewStartStatus status = PlayPreviewStartStatus::unavailable;
    std::string message;
};

// Requires current pending placement; failure retains it for another spawn
// ray unless visual attachment fails, which stops the detached session.
PlayPreviewStartResult start_play_preview_from_ray(ClientRenderer& renderer,
    PlayPreviewState& preview, RuntimeInputState& input, ShellController& shell,
    const ClientViewportRay& ray);
// Idle/zero-tick frames retain eligible pending edges. Error stops the session
// after the original update/edge-consumption order; caller restores shell UI.
PreviewStatus update_play_preview_frame(ClientRenderer& renderer, PlayPreviewState& preview,
    RuntimeInputState& input, ShellController& shell, double frame_seconds,
    PcSourceEligibility eligibility);
void apply_play_preview_pointer_action(ClientRenderer& renderer, PlayPreviewState& preview,
    RuntimeInputState& input, glm::vec2 point, ClientViewportRect viewport);

void toggle_play_preview_navigation_debug(ClientRenderer& renderer, PlayPreviewState& preview, ShellController& shell);
const char* play_preview_pointer_cursor(ClientRenderer& renderer, const PlayPreviewState& preview,
    glm::vec2 point, ClientViewportRect viewport);

} // namespace nw::toolset
