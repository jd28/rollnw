#pragma once

#include <nw/render/animation.hpp>

#include <cstdint>
#include <memory>

namespace nw::render {

class AnimationBackend {
public:
    virtual ~AnimationBackend() = default;

    virtual bool build_skeleton(uint32_t skeleton_index, const Skeleton& skeleton) = 0;
    virtual bool build_clip(uint32_t clip_index, const AnimationClip& clip) = 0;

    // Fills out_pose.local with sampled joint transforms in original skeleton order.
    // Does NOT fill out_pose.model — the caller is responsible for calling
    // build_model_matrices() after a successful sample.
    virtual bool sample(uint32_t clip_index, float time, Pose& out_pose, bool looping = true) = 0;
};

// Placeholder for future backend selection. The first backend candidate is Ozz, but the
// public runtime API remains backend-agnostic so the renderer and importers stay decoupled.
enum class AnimationBackendKind {
    custom,
    ozz,
};

std::unique_ptr<AnimationBackend> make_animation_backend(AnimationBackendKind kind = AnimationBackendKind::custom);

} // namespace nw::render
