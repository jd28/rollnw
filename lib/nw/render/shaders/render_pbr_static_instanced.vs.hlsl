// PBR static mesh vertex shader for a batch of model root poses.

struct ModelSurfaceInstance {
    float4x4 root;
    float4x4 root_normal_matrix;
};

StructuredBuffer<ModelSurfaceInstance> g_model_surface_instances
    : register(t3, space0);

#include "scene_constants.inc.hlsl"

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent  : TANGENT;
};

struct VSOutput {
    float4 position   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float3 normal     : TEXCOORD1;
    float2 texcoord   : TEXCOORD2;
    float3 view_dir   : TEXCOORD3;
    float3 tangent    : TEXCOORD4;
    float3 bitangent  : TEXCOORD5;
    float view_depth  : TEXCOORD6;
};

VSOutput main(VSInput input, uint instance_id : SV_InstanceID) {
    VSOutput output;
    const ModelSurfaceInstance instance
        = g_model_surface_instances[instance_id];

    const float4 model_pos
        = mul(model, float4(input.position, 1.0));
    float4 world_pos = mul(instance.root, model_pos);
    float4 view_pos = mul(view, world_pos);
    output.world_pos = world_pos.xyz;
    output.position = mul(projection, view_pos);
    output.texcoord = input.texcoord;
    output.view_dir = camera_pos.xyz - world_pos.xyz;
    output.view_depth = -view_pos.z;

    const float3 model_normal
        = mul((float3x3)normal_matrix, input.normal);
    const float3 model_tangent
        = mul((float3x3)normal_matrix, input.tangent.xyz);
    float3 N = normalize(mul(
        (float3x3)instance.root_normal_matrix, model_normal));
    float3 T = normalize(mul(
        (float3x3)instance.root_normal_matrix, model_tangent));
    T = normalize(T - dot(T, N) * N);

    output.normal = N;
    output.tangent = T;
    output.bitangent = cross(N, T) * input.tangent.w;
    return output;
}
