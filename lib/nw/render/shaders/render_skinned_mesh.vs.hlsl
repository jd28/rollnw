// Skinned mesh vertex shader (Phase 2)
// GPU skinning via SSBO bone matrices.
// SceneConstants layout must match SceneConstants in renderer.hpp exactly.

StructuredBuffer<float4x4> g_bones : register(t3, space0);

cbuffer SceneConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
    float4x4 model;
    float4x4 normal_matrix;   // inverse-transpose of model
    uint texture_index;
    uint alpha_cutout;
    float alpha_cutout_threshold;
    float _pad_tex;
    float2 color_key_rg;
    float color_key_b;
    float color_key_threshold;
    float4 pad_alpha;
    float4 camera_pos;
    float4 ambient;
    float4 key_dir_intensity;
    float4 key_color;
    float4 fill_dir_intensity;
    float4 fill_color;
    float4 rim_dir_intensity;
    float4 rim_color;
    float4 fog_color;
    float2 fog_range;
    float fog_amount;
    uint fog_enabled;
    uint plt_enabled;
    uint ibl_diffuse_texture_index;
    uint ibl_specular_texture_index;
    uint ibl_brdf_lut_texture_index;
    uint4 plt_colors0;
    uint4 plt_colors1;
    uint4 plt_colors2;
};

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
};

VSOutput main(VSInput input) {
    VSOutput output;

    uint4  indices = input.joint_indices;
    float4 weights = input.joint_weights;

    float4x4 skin_matrix =
        g_bones[indices.x] * weights.x +
        g_bones[indices.y] * weights.y +
        g_bones[indices.z] * weights.z +
        g_bones[indices.w] * weights.w;

    float4 local_pos = mul(skin_matrix, float4(input.position, 1.0));
    float4 world_pos = mul(model, local_pos);
    output.world_pos = world_pos.xyz;
    output.position  = mul(projection, mul(view, world_pos));

    // Use inverse-transpose normal matrix; skinning also affects normals
    float3 skinned_normal = mul((float3x3)skin_matrix, input.normal);
    output.normal   = normalize(mul((float3x3)normal_matrix, skinned_normal));
    output.texcoord = input.texcoord;
    output.view_dir = camera_pos.xyz - world_pos.xyz;

    return output;
}
