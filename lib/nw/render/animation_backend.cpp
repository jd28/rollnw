#include "animation_backend.hpp"

#include <memory>
#include <vector>

namespace nw::render {

std::unique_ptr<AnimationBackend> make_animation_backend_ozz();

class CustomAnimationBackend final : public AnimationBackend {
public:
    bool build_skeleton(uint32_t skeleton_index, const Skeleton& skeleton) override
    {
        if (skeleton_index >= skeletons_.size()) {
            skeletons_.resize(skeleton_index + 1);
        }
        skeletons_[skeleton_index] = skeleton;
        return true;
    }

    bool build_clip(uint32_t clip_index, const AnimationClip& clip) override
    {
        if (clip_index >= clips_.size()) {
            clips_.resize(clip_index + 1);
        }
        clips_[clip_index] = clip;
        return true;
    }

    bool sample(uint32_t clip_index, float time, Pose& out_pose, bool looping = true) override
    {
        if (clip_index >= clips_.size()) return false;
        const auto& clip = clips_[clip_index];
        if (clip.skeleton >= skeletons_.size()) return false;
        return sample_clip(skeletons_[clip.skeleton], clip, time, out_pose, looping);
    }

private:
    std::vector<Skeleton> skeletons_;
    std::vector<AnimationClip> clips_;
};

} // namespace

std::unique_ptr<nw::render::AnimationBackend> nw::render::make_animation_backend(nw::render::AnimationBackendKind kind)
{
    switch (kind) {
    case nw::render::AnimationBackendKind::ozz:
        if (auto backend = make_animation_backend_ozz()) {
            return backend;
        }
        [[fallthrough]];
    case nw::render::AnimationBackendKind::custom:
    default:
        return std::make_unique<CustomAnimationBackend>();
    }
}
