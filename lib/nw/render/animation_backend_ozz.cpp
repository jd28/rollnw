#include "animation_backend.hpp"

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>

#include <ozz/base/containers/vector.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>


#include <array>
#include <cassert>
#include <memory>
#include <vector>

namespace nw::render {

namespace {

class OzzAnimationBackend final : public AnimationBackend {
public:
    bool build_skeleton(uint32_t skeleton_index, const Skeleton& skeleton) override
    {
        if (skeleton_index >= skeletons_.size()) {
            skeletons_.resize(skeleton_index + 1);
        }
        skeletons_[skeleton_index] = skeleton;

        // Build children-of lists first so we can pre-size each joint's children
        // vector before recursing. Storing raw pointers into a growing
        // std::vector<Joint> and then calling emplace_back on that same vector
        // causes pointer invalidation — pre-sizing avoids reallocation entirely.
        std::vector<std::vector<int32_t>> children_of(skeleton.joints.size());
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            const int32_t p = skeleton.joints[i].parent;
            if (p >= 0 && static_cast<size_t>(p) < children_of.size()) {
                children_of[static_cast<size_t>(p)].push_back(static_cast<int32_t>(i));
            }
        }

        auto fill_joint = [&](auto&& self, int32_t idx, ozz::animation::offline::RawSkeleton::Joint& dst) -> void {
            const auto& src = skeleton.joints[static_cast<size_t>(idx)];
            dst.name = src.name.c_str();
            dst.transform.translation = {src.bind_local.translation.x, src.bind_local.translation.y, src.bind_local.translation.z};
            dst.transform.rotation = {src.bind_local.rotation.x, src.bind_local.rotation.y, src.bind_local.rotation.z, src.bind_local.rotation.w};
            dst.transform.scale = {src.bind_local.scale.x, src.bind_local.scale.y, src.bind_local.scale.z};
            const auto& clist = children_of[static_cast<size_t>(idx)];
            dst.children.resize(clist.size()); // pre-size so no reallocation during recursion
            for (size_t ci = 0; ci < clist.size(); ++ci) {
                self(self, clist[ci], dst.children[ci]);
            }
        };

        ozz::animation::offline::RawSkeleton raw;
        size_t root_count = 0;
        for (const auto& j : skeleton.joints) {
            if (j.parent < 0) ++root_count;
        }
        raw.roots.reserve(root_count);
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            if (skeleton.joints[i].parent < 0) {
                raw.roots.emplace_back();
                fill_joint(fill_joint, static_cast<int32_t>(i), raw.roots.back());
            }
        }

        ozz::animation::offline::SkeletonBuilder builder;
        auto built = builder(raw);
        if (!built) return false;

        if (skeleton_index >= runtime_skeletons_.size()) {
            runtime_skeletons_.resize(skeleton_index + 1);
        }
        if (skeleton_index >= runtime_to_original_.size()) {
            runtime_to_original_.resize(skeleton_index + 1);
        }
        if (skeleton_index >= original_to_runtime_.size()) {
            original_to_runtime_.resize(skeleton_index + 1);
        }
        runtime_skeletons_[skeleton_index] = std::move(*built);

        runtime_to_original_[skeleton_index].clear();
        auto dfs = [&](auto&& self, int32_t joint) -> void {
            runtime_to_original_[skeleton_index].push_back(joint);
            for (int32_t child : children_of[static_cast<size_t>(joint)]) {
                self(self, child);
            }
        };
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            if (skeleton.joints[i].parent < 0) {
                dfs(dfs, static_cast<int32_t>(i));
            }
        }
        original_to_runtime_[skeleton_index].assign(skeleton.joints.size(), -1);
        for (size_t runtime_index = 0; runtime_index < runtime_to_original_[skeleton_index].size(); ++runtime_index) {
            const int32_t original_index = runtime_to_original_[skeleton_index][runtime_index];
            if (original_index >= 0 && static_cast<size_t>(original_index) < original_to_runtime_[skeleton_index].size()) {
                original_to_runtime_[skeleton_index][static_cast<size_t>(original_index)] = static_cast<int32_t>(runtime_index);
            }
        }

        // Validate that our DFS traversal order matches ozz's internal joint ordering.
        // If ozz ever changes its ordering this assertion will catch it in debug builds.
#ifndef NDEBUG
        {
            auto names = runtime_skeletons_[skeleton_index].joint_names();
            const auto& r2o = runtime_to_original_[skeleton_index];
            for (size_t rt = 0; rt < static_cast<size_t>(names.size()) && rt < r2o.size(); ++rt) {
                int32_t orig = r2o[rt];
                assert(orig >= 0 && skeleton.joints[static_cast<size_t>(orig)].name == names[rt]);
            }
        }
#endif

        // Grow the scratch buffer to cover this skeleton; never shrink so it
        // stays at the high-water mark across all skeletons built so far.
        const size_t needed = static_cast<size_t>(runtime_skeletons_[skeleton_index].num_soa_joints());
        if (locals_scratch_.size() < needed) {
            locals_scratch_.resize(needed);
        }

        return true;
    }

    bool build_clip(uint32_t clip_index, const AnimationClip& clip) override
    {
        if (clip_index >= clip_skeletons_.size()) {
            clip_skeletons_.resize(clip_index + 1, ~0u);
        }
        clip_skeletons_[clip_index] = clip.skeleton;

        if (clip.skeleton >= original_to_runtime_.size()) return false;
        if (clip.skeleton >= skeletons_.size()) return false;
        const auto& original_to_runtime = original_to_runtime_[clip.skeleton];
        const auto& skeleton = skeletons_[clip.skeleton];

        ozz::animation::offline::RawAnimation raw;
        raw.duration = clip.duration;
        raw.tracks.resize(clip.tracks.size());
        for (size_t original_index = 0; original_index < clip.tracks.size(); ++original_index) {
            if (original_index >= original_to_runtime.size()) continue;
            const int32_t runtime_index = original_to_runtime[original_index];
            if (runtime_index < 0 || static_cast<size_t>(runtime_index) >= raw.tracks.size()) continue;

            const auto& src = clip.tracks[original_index];
            auto& dst = raw.tracks[static_cast<size_t>(runtime_index)];
            const auto& bind = skeleton.joints[original_index].bind_local;
            if (src.translations.empty()) {
                dst.translations.push_back({0.0f, {bind.translation.x, bind.translation.y, bind.translation.z}});
            } else {
                for (const auto& key : src.translations) {
                    dst.translations.push_back({key.time, {key.value.x, key.value.y, key.value.z}});
                }
            }
            if (src.rotations.empty()) {
                dst.rotations.push_back({0.0f, {bind.rotation.x, bind.rotation.y, bind.rotation.z, bind.rotation.w}});
            } else {
                for (const auto& key : src.rotations) {
                    dst.rotations.push_back({key.time, {key.value.x, key.value.y, key.value.z, key.value.w}});
                }
            }
            if (src.scales.empty()) {
                dst.scales.push_back({0.0f, {bind.scale.x, bind.scale.y, bind.scale.z}});
            } else {
                for (const auto& key : src.scales) {
                    dst.scales.push_back({key.time, {key.value.x, key.value.y, key.value.z}});
                }
            }
        }

        ozz::animation::offline::AnimationBuilder builder;
        auto built = builder(raw);
        if (!built) return false;
        if (clip_index >= runtime_clips_.size()) {
            runtime_clips_.resize(clip_index + 1);
        }
        if (clip_index >= sampling_contexts_.size()) {
            sampling_contexts_.resize(clip_index + 1);
        }
        runtime_clips_[clip_index] = std::move(*built);
        sampling_contexts_[clip_index] = std::make_unique<ozz::animation::SamplingJob::Context>(runtime_clips_[clip_index].num_tracks());
        return true;
    }

    // Fills out_pose.local in original skeleton order. Does not fill out_pose.model;
    // the caller is responsible for calling build_model_matrices() afterward.
    bool sample(uint32_t clip_index, float time, Pose& out_pose, bool looping = true) override
    {
        if (clip_index >= clip_skeletons_.size()) return false;
        const uint32_t skeleton_index = clip_skeletons_[clip_index];
        if (skeleton_index >= skeletons_.size() || skeleton_index >= runtime_skeletons_.size()
            || skeleton_index >= runtime_to_original_.size()) return false;
        if (clip_index >= runtime_clips_.size() || clip_index >= sampling_contexts_.size()
            || !sampling_contexts_[clip_index]) return false;

        const auto& runtime_skeleton = runtime_skeletons_[skeleton_index];
        const auto& runtime_clip = runtime_clips_[clip_index];
        const auto& skeleton = skeletons_[skeleton_index];
        const auto& runtime_to_original = runtime_to_original_[skeleton_index];

        if (runtime_clip.num_tracks() == 0 || runtime_skeleton.num_joints() == 0) {
            return false;
        }

        const float duration = runtime_clip.duration();
        float sample_time = time;
        if (looping && duration > 0.0f) {
            sample_time = std::fmod(sample_time, duration);
            if (sample_time < 0.0f) sample_time += duration;
        } else {
            sample_time = std::clamp(sample_time, 0.0f, duration);
        }

        // Guard-size the scratch buffer (grows to high-water mark, never shrinks).
        const size_t needed = static_cast<size_t>(runtime_skeleton.num_soa_joints());
        if (locals_scratch_.size() < needed) {
            locals_scratch_.resize(needed);
        }

        ozz::animation::SamplingJob sampling;
        sampling.animation = &runtime_clip;
        sampling.context = sampling_contexts_[clip_index].get();
        sampling.ratio = duration > 0.0f ? sample_time / duration : 0.0f;
        sampling.output = {locals_scratch_.data(), needed};
        if (!sampling.Run()) {
            return false;
        }

        out_pose.local.resize(skeleton.joints.size());
        std::array<float, 4> tx{}, ty{}, tz{}, qx{}, qy{}, qz{}, qw{}, sx{}, sy{}, sz{};
        for (size_t soa_index = 0; soa_index < needed; ++soa_index) {
            const auto& soa = locals_scratch_[soa_index];
            ozz::math::StorePtrU(soa.translation.x, &tx[0]);
            ozz::math::StorePtrU(soa.translation.y, &ty[0]);
            ozz::math::StorePtrU(soa.translation.z, &tz[0]);
            ozz::math::StorePtrU(soa.rotation.x, &qx[0]);
            ozz::math::StorePtrU(soa.rotation.y, &qy[0]);
            ozz::math::StorePtrU(soa.rotation.z, &qz[0]);
            ozz::math::StorePtrU(soa.rotation.w, &qw[0]);
            ozz::math::StorePtrU(soa.scale.x, &sx[0]);
            ozz::math::StorePtrU(soa.scale.y, &sy[0]);
            ozz::math::StorePtrU(soa.scale.z, &sz[0]);
            for (size_t lane = 0; lane < 4; ++lane) {
                const size_t runtime_joint = soa_index * 4 + lane;
                if (runtime_joint >= runtime_to_original.size()) break;
                const size_t joint_index = static_cast<size_t>(runtime_to_original[runtime_joint]);
                if (joint_index >= out_pose.local.size()) continue;
                out_pose.local[joint_index].translation = {tx[lane], ty[lane], tz[lane]};
                out_pose.local[joint_index].rotation = glm::normalize(glm::quat(qw[lane], qx[lane], qy[lane], qz[lane]));
                out_pose.local[joint_index].scale = {sx[lane], sy[lane], sz[lane]};
            }
        }
        return true;
    }

private:
    std::vector<Skeleton> skeletons_;
    std::vector<uint32_t> clip_skeletons_; // per clip: which skeleton index it targets
    std::vector<ozz::animation::Skeleton> runtime_skeletons_;
    std::vector<std::vector<int32_t>> runtime_to_original_;
    std::vector<std::vector<int32_t>> original_to_runtime_;
    std::vector<ozz::animation::Animation> runtime_clips_;
    std::vector<std::unique_ptr<ozz::animation::SamplingJob::Context>> sampling_contexts_;
    ozz::vector<ozz::math::SoaTransform> locals_scratch_; // reused across frames; grows to high-water mark
};

} // namespace

std::unique_ptr<AnimationBackend> make_animation_backend_ozz()
{
    return std::make_unique<OzzAnimationBackend>();
}

} // namespace nw::render
