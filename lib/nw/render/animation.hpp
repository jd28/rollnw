#pragma once

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace nw::render {

struct Transform {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 matrix() const noexcept
    {
        return glm::translate(glm::mat4{1.0f}, translation) * glm::toMat4(rotation)
            * glm::scale(glm::mat4{1.0f}, scale);
    }

    static Transform from_matrix(const glm::mat4& matrix)
    {
        Transform t{};
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(matrix, t.scale, t.rotation, t.translation, skew, perspective);
        t.rotation = glm::normalize(t.rotation);
        return t;
    }
};

struct Joint {
    std::string name;
    int32_t parent = -1;
    int32_t node = -1;
    Transform bind_local;
    glm::mat4 root_correction{1.0f};
    glm::mat4 inverse_bind_matrix{1.0f};
};

struct Skeleton {
    std::vector<Joint> joints;
    std::vector<uint32_t> eval_order; // topological order for build_model_matrices
    std::vector<int32_t> node_to_joint; // node index -> joint index, built with eval_order
};

inline void build_node_to_joint_map(Skeleton& skeleton)
{
    size_t node_count = 0;
    for (const auto& joint : skeleton.joints) {
        if (joint.node >= 0) {
            node_count = std::max(node_count, static_cast<size_t>(joint.node) + 1u);
        }
    }

    skeleton.node_to_joint.assign(node_count, -1);
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        const int32_t node = skeleton.joints[i].node;
        if (node >= 0 && static_cast<size_t>(node) < skeleton.node_to_joint.size()
            && i <= static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            skeleton.node_to_joint[static_cast<size_t>(node)] = static_cast<int32_t>(i);
        }
    }
}

inline void build_eval_order(Skeleton& skeleton)
{
    std::vector<std::vector<int32_t>> children_of(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        const int32_t p = skeleton.joints[i].parent;
        if (p >= 0 && static_cast<size_t>(p) < children_of.size())
            children_of[static_cast<size_t>(p)].push_back(static_cast<int32_t>(i));
    }
    skeleton.eval_order.clear();
    skeleton.eval_order.reserve(skeleton.joints.size());
    auto dfs = [&](auto&& self, int32_t idx) -> void {
        skeleton.eval_order.push_back(static_cast<uint32_t>(idx));
        for (int32_t c : children_of[static_cast<size_t>(idx)])
            self(self, c);
    };
    for (size_t i = 0; i < skeleton.joints.size(); ++i)
        if (skeleton.joints[i].parent < 0) dfs(dfs, static_cast<int32_t>(i));
    build_node_to_joint_map(skeleton);
}

enum class InterpolationMode {
    linear,
    step,
};

template <typename T>
struct Keyframe {
    float time = 0.0f;
    T value{};
};

struct JointTrack {
    std::vector<Keyframe<glm::vec3>> translations;
    std::vector<Keyframe<glm::quat>> rotations;
    std::vector<Keyframe<glm::vec3>> scales;
    InterpolationMode translation_mode = InterpolationMode::linear;
    InterpolationMode rotation_mode = InterpolationMode::linear;
    InterpolationMode scale_mode = InterpolationMode::linear;
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    uint32_t skeleton = 0;
    std::vector<JointTrack> tracks;
};

struct Pose {
    std::vector<Transform> local;
    std::vector<glm::mat4> model;
};

struct AnimationState {
    uint32_t clip = 0;
    float time = 0.0f;
    bool looping = true;
};

namespace detail {

template <typename T>
T sample_keys(const std::vector<Keyframe<T>>& keys, float time, InterpolationMode mode)
{
    if (keys.empty()) return T{};
    if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;

    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto& a = keys[i];
        const auto& b = keys[i + 1];
        if (time < b.time) {
            if (mode == InterpolationMode::step || b.time <= a.time) return a.value;
            float t = (time - a.time) / (b.time - a.time);
            if constexpr (std::is_same_v<T, glm::quat>) {
                return glm::normalize(glm::slerp(a.value, b.value, t));
            } else {
                return glm::mix(a.value, b.value, t);
            }
        }
    }
    return keys.back().value;
}

} // namespace detail

inline bool sample_clip(const Skeleton& skeleton, const AnimationClip& clip, float time, Pose& out_pose, bool looping = true)
{
    const size_t joint_count = skeleton.joints.size();
    if (clip.tracks.size() != joint_count) return false;

    out_pose.local.resize(joint_count);
    out_pose.model.resize(joint_count);

    float sample_time = time;
    if (looping && clip.duration > 0.0f) {
        sample_time = std::fmod(sample_time, clip.duration);
        if (sample_time < 0.0f) sample_time += clip.duration;
    } else {
        sample_time = std::clamp(sample_time, 0.0f, clip.duration);
    }

    for (size_t i = 0; i < joint_count; ++i) {
        const auto& joint = skeleton.joints[i];
        const auto& track = clip.tracks[i];
        Transform local = joint.bind_local;
        if (!track.translations.empty()) {
            local.translation = detail::sample_keys(track.translations, sample_time, track.translation_mode);
        }
        if (!track.rotations.empty()) {
            local.rotation = detail::sample_keys(track.rotations, sample_time, track.rotation_mode);
        }
        if (!track.scales.empty()) {
            local.scale = detail::sample_keys(track.scales, sample_time, track.scale_mode);
        }
        out_pose.local[i] = local;
    }
    return true;
}

inline void build_model_matrices(const Skeleton& skeleton, Pose& pose)
{
    const size_t joint_count = skeleton.joints.size();
    pose.model.resize(joint_count);
    const auto& order = skeleton.eval_order;
    const bool has_order = order.size() == joint_count;
    for (size_t step = 0; step < joint_count; ++step) {
        const size_t i = has_order ? static_cast<size_t>(order[step]) : step;
        const auto& joint = skeleton.joints[i];
        glm::mat4 local = pose.local[i].matrix();
        if (joint.parent >= 0) {
            pose.model[i] = pose.model[static_cast<size_t>(joint.parent)] * local;
        } else {
            pose.model[i] = joint.root_correction * local;
        }
    }
}

inline void build_skin_matrices(const Skeleton& skeleton, const Pose& pose, std::span<glm::mat4> out_skin)
{
    const size_t joint_count = std::min(skeleton.joints.size(), out_skin.size());
    for (size_t i = 0; i < joint_count; ++i) {
        out_skin[i] = pose.model[i] * skeleton.joints[i].inverse_bind_matrix;
    }
}

} // namespace nw::render
