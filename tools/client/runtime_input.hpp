#pragma once

#include "pc_input.hpp"
#include "viewport_rect.hpp"

#include <memory>
#include <optional>

struct SDL_Gamepad;
class ClientRenderer;

namespace nw::toolset {

struct RuntimeGamepadDeleter {
    void operator()(SDL_Gamepad* gamepad) const noexcept;
};

// One acquired controller and one pending input accumulator for the displayed
// PC. Stable until map/PC transition; the controller must close before SDL_Quit.
struct RuntimeInputState {
    std::unique_ptr<SDL_Gamepad, RuntimeGamepadDeleter> gamepad;
    PreviewInputSample pending;
    glm::vec2 mouse_look_pixels{0.0f};
    double mouse_sample_seconds = 0.0;
    float wheel_zoom = 0.0f;
};

void open_runtime_gamepad(RuntimeInputState& input, uint32_t id);
void close_runtime_gamepad(RuntimeInputState& input) noexcept;
void reset_runtime_pending_input(RuntimeInputState& input) noexcept;
void consume_runtime_input_edges(RuntimeInputState& input) noexcept;

// SDL acquisition is a true singleton. Negative elapsed contribution is zero;
// nonfinite time rejects. Claimed pending sources are discarded before capture.
// The result is borrowed only for the following pure count-one translation.
PreviewStatus acquire_pc_device_sample(RuntimeInputState& input, double frame_seconds,
    PcSourceEligibility eligibility, PcDeviceSample& sample) noexcept;

// Call-scoped renderer results, not actor/session authority. Missing/invalid
// rays and hits are unavailable. Dense door indices belong to the supplied
// current door span. Rml/SDL pointers never enter the pure translator.
std::optional<ClientViewportRay> acquire_runtime_viewport_ray(ClientRenderer& renderer,
    glm::vec2 point, ClientViewportRect viewport);
std::optional<ClientAreaDoorHit> acquire_runtime_door_hit(ClientRenderer& renderer,
    glm::vec2 point, ClientViewportRect viewport, std::span<const ObjectHandle> doors);

} // namespace nw::toolset
