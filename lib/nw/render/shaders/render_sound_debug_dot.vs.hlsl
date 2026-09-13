cbuffer DebugShapeConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
};

struct SoundDebugDotInstance {
    float4 center_radius;
    float4 normal;
    float4 color;
};

StructuredBuffer<SoundDebugDotInstance> g_instances : register(t2, space0);

struct VSInput {
    float2 position : POSITION;
    uint instance_id : SV_InstanceID;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    SoundDebugDotInstance instance = g_instances[input.instance_id];
    float3 normal = instance.normal.xyz;
    float3 tangent = abs(normal.z) > 0.99
        ? float3(1.0, 0.0, 0.0)
        : normalize(cross(normal, float3(0.0, 0.0, 1.0)));
    float3 bitangent = normalize(cross(normal, tangent));
    float3 world_position = instance.center_radius.xyz
        + (tangent * input.position.x + bitangent * input.position.y)
            * instance.center_radius.w;

    VSOutput output;
    output.position = mul(projection, mul(view, float4(world_position, 1.0)));
    output.color = instance.color;
    return output;
}
