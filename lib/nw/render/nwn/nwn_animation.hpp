#pragma once

#include <nw/model/Mdl.hpp>
#include <nw/render/animation.hpp>

#include <vector>

namespace nw::render::nwn {

// Builds a render Skeleton from the loaded NWN node hierarchy.
// Fills out_joint_to_source_node with the mapping joint_idx → mdl source_node_idx.
nw::render::Skeleton build_nwn_skeleton(
    const nw::model::Mdl& mdl,
    std::vector<int32_t>& out_joint_to_source_node,
    std::string_view root_name = {});

// Converts a single NWN animation to an AnimationClip targeting the given skeleton.
// translation_scale applies to position keys; inherited supermodel clips use the
// target model's animationscale, while locally authored clips use 1.0.
nw::render::AnimationClip build_nwn_clip(
    const nw::model::Animation& anim,
    const nw::render::Skeleton& skeleton,
    uint32_t skeleton_index,
    float translation_scale);

} // namespace nw::render::nwn
