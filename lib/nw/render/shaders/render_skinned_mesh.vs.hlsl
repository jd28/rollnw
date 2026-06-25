// Skinned mesh vertex shader (Phase 2)
// GPU skinning via SSBO bone matrices.

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
    float4 position  : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 texcoord  : TEXCOORD2;
    float3 view_dir  : TEXCOORD3;
    float3 tangent   : TEXCOORD4;
    float3 bitangent : TEXCOORD5;
    float view_depth : TEXCOORD6;
};

VSOutput main(VSInput input) {
    VSOutput output;

    float4x4 skin_matrix = blend_bone_matrices(g_bones, input.joint_indices, input.joint_weights);

    float4 local_pos = mul(skin_matrix, float4(input.position, 1.0));
    float4 world_pos = mul(model, local_pos);
    float4 view_pos = mul(view, world_pos);
    output.world_pos = world_pos.xyz;
    output.position  = mul(projection, view_pos);

    // Use inverse-transpose normal matrix; skinning also affects normals
    float3 skinned_normal = mul((float3x3)skin_matrix, input.normal);
    float3 skinned_tangent = mul((float3x3)skin_matrix, input.tangent.xyz);
    float3 N = normalize(mul((float3x3)normal_matrix, skinned_normal));
    float3 T = normalize(mul((float3x3)normal_matrix, skinned_tangent));
    T = normalize(T - dot(T, N) * N);
    output.normal = N;
    output.tangent = T;
    output.bitangent = cross(N, T) * input.tangent.w;
    output.texcoord = input.texcoord;
    output.view_dir = camera_pos.xyz - world_pos.xyz;
    output.view_depth = -view_pos.z;

    return output;
}
