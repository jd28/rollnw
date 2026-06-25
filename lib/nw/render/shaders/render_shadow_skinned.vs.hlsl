// Skinned shadow-map vertex shader.

StructuredBuffer<float4x4> g_bones : register(t3, space0);

#include "scene_constants.inc.hlsl"
#include "skinning.inc.hlsl"

struct VSInput {
    float3 position      : POSITION;
    float3 normal        : NORMAL;
    float2 texcoord      : TEXCOORD0;
    float4 tangent       : TANGENT;
    uint4  joint_indices : BLENDINDICES;
    float4 joint_weights : BLENDWEIGHT;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;

    float4x4 skin_matrix = blend_bone_matrices(g_bones, input.joint_indices, input.joint_weights);

    float4 local_pos = mul(skin_matrix, float4(input.position, 1.0));
    float4 world_pos = mul(model, local_pos);
    output.position = mul(projection, mul(view, world_pos));
    output.texcoord = input.texcoord;
    return output;
}
