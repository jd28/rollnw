#pragma once

#include <nw/render/viewer/camera.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class ClientViewerSceneKind : uint8_t {
    area,
    preview,
};

struct ClientViewerCameraState {
    ClientViewerSceneKind kind = ClientViewerSceneKind::area;
    std::string resource;
    nw::render::viewer::Camera camera;
};

class ClientViewerCameraStates {
public:
    // One viewport has exactly one active camera at a scene transition, so the
    // singular input is a true singleton. The stored records remain a flat batch.
    [[nodiscard]] bool store(ClientViewerSceneKind kind,
        std::string_view resource,
        const nw::render::viewer::Camera& camera)
    {
        if (!valid_kind(kind) || resource.empty()) {
            return false;
        }

        for (auto& state : states_) {
            if (state.kind == kind && state.resource == resource) {
                state.camera = camera;
                return true;
            }
        }
        states_.push_back({kind, std::string{resource}, camera});
        return true;
    }

    [[nodiscard]] bool restore(ClientViewerSceneKind kind,
        std::string_view resource,
        nw::render::viewer::Camera& camera) const
    {
        if (!valid_kind(kind) || resource.empty()) {
            return false;
        }

        for (const auto& state : states_) {
            if (state.kind == kind && state.resource == resource) {
                camera = state.camera;
                return true;
            }
        }
        return false;
    }

    void clear() noexcept { states_.clear(); }

    [[nodiscard]] size_t size() const noexcept { return states_.size(); }

private:
    [[nodiscard]] static bool valid_kind(ClientViewerSceneKind kind) noexcept
    {
        return kind == ClientViewerSceneKind::area
            || kind == ClientViewerSceneKind::preview;
    }

    std::vector<ClientViewerCameraState> states_;
};
