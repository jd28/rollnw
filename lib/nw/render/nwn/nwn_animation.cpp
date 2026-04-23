#include "nwn_animation.hpp"

#include <nw/model/Mdl.hpp>
#include <nw/render/animation.hpp>
#include <nw/render/nwn/model_loader.hpp>

#include <nw/log.hpp>
#include <nw/util/string.hpp>

#include <unordered_map>

namespace nwm = nw::model;

namespace nw::render::nwn {

namespace {

glm::mat4 nwn_node_bind_local_transform(const nwm::Node& node)
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    auto pos = node.get_controller(nwm::ControllerType::Position, false);
    if (pos.data.size() >= 3) {
        position = glm::vec3{pos.data[0], pos.data[1], pos.data[2]};
    }

    auto ori = node.get_controller(nwm::ControllerType::Orientation, false);
    if (ori.data.size() >= 4) {
        rotation = glm::quat{ori.data[3], ori.data[0], ori.data[1], ori.data[2]};
    }

    auto scale_ctl = node.get_controller(nwm::ControllerType::Scale, false);
    if (!scale_ctl.data.empty()) {
        if (scale_ctl.data.size() >= 3) {
            scale = glm::vec3{scale_ctl.data[0], scale_ctl.data[1], scale_ctl.data[2]};
        } else {
            scale = glm::vec3{scale_ctl.data[0]};
        }
    }

    auto transform = glm::translate(glm::mat4{1.0f}, position) * glm::toMat4(rotation);
    return glm::scale(transform, scale);
}

} // namespace

nw::render::Skeleton build_nwn_skeleton(
    const nwm::Mdl& mdl,
    std::vector<int32_t>& out_joint_to_source_node,
    std::string_view root_name)
{
    const nwm::Node* root_node = nullptr;
    const auto desired_root = root_name.empty() ? std::string_view{mdl.model.name} : root_name;
    for (const auto& node : mdl.model.nodes) {
        if (node && nw::string::icmp(node->name, desired_root)) {
            root_node = node.get();
            break;
        }
    }
    if (!root_node) {
        LOG_F(WARNING, "build_nwn_skeleton: no root node found for {}", desired_root);
        out_joint_to_source_node.clear();
        return {};
    }

    const size_t node_count = mdl.model.nodes.size();
    std::unordered_map<const nwm::Node*, int32_t> node_to_source_index;
    node_to_source_index.reserve(node_count);
    for (size_t i = 0; i < node_count; ++i) {
        if (mdl.model.nodes[i]) {
            node_to_source_index[mdl.model.nodes[i].get()] = static_cast<int32_t>(i);
        }
    }

    std::vector<const nwm::Node*> source_nodes(node_count, nullptr);
    std::vector<int32_t> parent_source_index(node_count, -1);
    std::vector<glm::mat4> bind_pose(node_count, glm::mat4{1.0f});

    auto visit = [&](auto&& self, const nwm::Node* node, int32_t parent_idx, const glm::mat4& parent_bind) -> void {
        auto it = node_to_source_index.find(node);
        if (it == node_to_source_index.end()) {
            return;
        }

        const int32_t source_idx = it->second;
        source_nodes[static_cast<size_t>(source_idx)] = node;
        parent_source_index[static_cast<size_t>(source_idx)] = parent_idx;
        bind_pose[static_cast<size_t>(source_idx)] = parent_bind * nwn_node_bind_local_transform(*node);

        for (const auto* child : node->children) {
            self(self, child, source_idx, bind_pose[static_cast<size_t>(source_idx)]);
        }
    };
    visit(visit, root_node, -1, glm::mat4{1.0f});

    std::vector<int32_t> source_to_joint(node_count, -1);
    out_joint_to_source_node.clear();
    for (size_t i = 0; i < node_count; ++i) {
        if (source_nodes[i]) {
            source_to_joint[i] = static_cast<int32_t>(out_joint_to_source_node.size());
            out_joint_to_source_node.push_back(static_cast<int32_t>(i));
        }
    }

    nw::render::Skeleton skeleton;
    skeleton.joints.resize(out_joint_to_source_node.size());

    for (size_t ji = 0; ji < out_joint_to_source_node.size(); ++ji) {
        const int32_t source_idx = out_joint_to_source_node[ji];
        const auto* node = source_nodes[static_cast<size_t>(source_idx)];
        auto& joint = skeleton.joints[ji];

        joint.name = node && !node->name.empty()
            ? std::string(node->name)
            : "joint_" + std::to_string(ji);
        joint.node = source_idx;
        joint.inverse_bind_matrix = glm::inverse(bind_pose[static_cast<size_t>(source_idx)]);

        int32_t parent_joint = -1;
        int32_t parent_idx = parent_source_index[static_cast<size_t>(source_idx)];
        while (parent_idx >= 0 && parent_joint < 0) {
            parent_joint = source_to_joint[static_cast<size_t>(parent_idx)];
            if (parent_joint < 0) {
                parent_idx = parent_source_index[static_cast<size_t>(parent_idx)];
            }
        }

        joint.parent = parent_joint;
        if (parent_joint >= 0) {
            const int32_t parent_source = out_joint_to_source_node[static_cast<size_t>(parent_joint)];
            joint.bind_local = nw::render::Transform::from_matrix(
                glm::inverse(bind_pose[static_cast<size_t>(parent_source)]) * bind_pose[static_cast<size_t>(source_idx)]);
        } else {
            joint.bind_local = nw::render::Transform::from_matrix(bind_pose[static_cast<size_t>(source_idx)]);
            joint.root_correction = glm::mat4(1.0f);
        }
    }

    if (skeleton.joints.empty()) {
        LOG_F(WARNING, "build_nwn_skeleton: no nodes found");
    }

    nw::render::build_eval_order(skeleton);
    return skeleton;
}

nw::render::Skeleton build_nwn_skeleton(
    const ModelInstance& inst,
    std::vector<int32_t>& out_joint_to_source_node)
{
    const size_t node_count = inst.source_nodes_.size();

    // Mirror the loaded NWN hierarchy so both skinned and static transform animation
    // can be sampled through the same backend.
    std::vector<int32_t> source_to_joint(node_count, -1);
    out_joint_to_source_node.clear();
    for (size_t i = 0; i < node_count; ++i) {
        if (inst.source_nodes_[i]) {
            source_to_joint[i] = static_cast<int32_t>(out_joint_to_source_node.size());
            out_joint_to_source_node.push_back(static_cast<int32_t>(i));
        }
    }

    nw::render::Skeleton skeleton;
    skeleton.joints.resize(out_joint_to_source_node.size());

    for (size_t ji = 0; ji < out_joint_to_source_node.size(); ++ji) {
        const int32_t src_idx = out_joint_to_source_node[ji];
        const Node* node = inst.source_nodes_[static_cast<size_t>(src_idx)];
        auto& joint = skeleton.joints[ji];

        joint.name = (node->orig_ && !node->orig_->name.empty())
            ? std::string(node->orig_->name)
            : "joint_" + std::to_string(ji);
        joint.node = src_idx;
        joint.inverse_bind_matrix = glm::inverse(node->bind_pose_);

        // Walk the loaded node parent chain to find the nearest skeleton ancestor.
        int32_t parent_joint = -1;
        const Node* p = node->parent_;
        while (p && parent_joint < 0) {
            for (size_t si = 0; si < node_count; ++si) {
                if (inst.source_nodes_[si] == p) {
                    parent_joint = source_to_joint[si];
                    break;
                }
            }
            if (parent_joint < 0) p = p->parent_;
        }

        joint.parent = parent_joint;
        if (parent_joint >= 0) {
            const Node* parent_bone = inst.source_nodes_[static_cast<size_t>(out_joint_to_source_node[static_cast<size_t>(parent_joint)])];
            joint.bind_local = nw::render::Transform::from_matrix(
                glm::inverse(parent_bone->bind_pose_) * node->bind_pose_);
        } else {
            // Root node: bind_local is in world space. NWN is already Z-up so no correction needed.
            joint.bind_local = nw::render::Transform::from_matrix(node->bind_pose_);
            joint.root_correction = glm::mat4(1.0f);
        }
    }

    if (skeleton.joints.empty()) {
        LOG_F(WARNING, "build_nwn_skeleton: no nodes found");
    }

    nw::render::build_eval_order(skeleton);
    return skeleton;
}

nw::render::AnimationClip build_nwn_clip(
    const nw::model::Animation& anim,
    const nw::render::Skeleton& skeleton,
    uint32_t skeleton_index)
{
    nw::render::AnimationClip clip;
    clip.name = anim.name;
    clip.duration = anim.length; // seconds
    clip.skeleton = skeleton_index;
    clip.tracks.resize(skeleton.joints.size());

    std::unordered_map<std::string_view, uint32_t> name_to_joint;
    name_to_joint.reserve(skeleton.joints.size());
    for (uint32_t ji = 0; ji < static_cast<uint32_t>(skeleton.joints.size()); ++ji)
        name_to_joint[skeleton.joints[ji].name] = ji;

    for (const auto& anim_node_ptr : anim.nodes) {
        if (!anim_node_ptr) continue;
        auto it = name_to_joint.find(std::string_view(anim_node_ptr->name));
        if (it == name_to_joint.end()) continue;
        auto& track = clip.tracks[it->second];

        auto poskey = anim_node_ptr->get_controller(nwm::ControllerType::Position, true);
        track.translations.reserve(poskey.time.size());
        for (size_t i = 0; i < poskey.time.size(); ++i) {
            track.translations.push_back({
                poskey.time[i],
                glm::vec3{poskey.data[i * 3], poskey.data[i * 3 + 1], poskey.data[i * 3 + 2]}});
        }

        auto orikey = anim_node_ptr->get_controller(nwm::ControllerType::Orientation, true);
        track.rotations.reserve(orikey.time.size());
        for (size_t i = 0; i < orikey.time.size(); ++i) {
            // NWN orientation: [x, y, z, w] packed
            track.rotations.push_back({
                orikey.time[i],
                glm::normalize(glm::quat{orikey.data[i * 4 + 3], orikey.data[i * 4],
                    orikey.data[i * 4 + 1], orikey.data[i * 4 + 2]})});
        }

        auto scalekey = anim_node_ptr->get_controller(nwm::ControllerType::Scale, true);
        track.scales.reserve(scalekey.time.size());
        for (size_t i = 0; i < scalekey.time.size(); ++i) {
            const float s = scalekey.data[i];
            track.scales.push_back({scalekey.time[i], glm::vec3{s, s, s}});
        }
    }

    return clip;
}

} // namespace nw::render::nwn
