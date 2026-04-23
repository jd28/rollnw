#pragma once

#include <nw/model/Mdl.hpp>
#include <nw/render/animation.hpp>

#include <vector>

namespace nw::render::nwn {

struct ModelInstance;

// Builds a render Skeleton from the loaded NWN node hierarchy.
// Fills out_joint_to_source_node with the mapping joint_idx → mdl source_node_idx.
// These functions are standalone (not ModelInstance methods) so they can be called
// from a future offline converter that works directly on nw::model::Mdl.
nw::render::Skeleton build_nwn_skeleton(
    const nw::model::Mdl& mdl,
    std::vector<int32_t>& out_joint_to_source_node,
    std::string_view root_name = {});

nw::render::Skeleton build_nwn_skeleton(
    const ModelInstance& inst,
    std::vector<int32_t>& out_joint_to_source_node);

// Converts a single NWN animation to an AnimationClip targeting the given skeleton.
nw::render::AnimationClip build_nwn_clip(
    const nw::model::Animation& anim,
    const nw::render::Skeleton& skeleton,
    uint32_t skeleton_index);

} // namespace nw::render::nwn
