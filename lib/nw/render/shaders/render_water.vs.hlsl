// NWN water vertex shader.

#include "scene_constants.inc.hlsl"

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent  : TANGENT;
};

struct VSOutput {
    float4 position  : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 texcoord  : TEXCOORD2;
    float3 view_dir  : TEXCOORD3;
    float view_depth  : TEXCOORD4;
};

static const float kTwoPi = 6.28318530718;
static const float kWaterHeightStrength = 0.026;

void add_height_wave(
    float2 sample_xy,
    float2 dir,
    float wavelength,
    float amplitude,
    float phase_speed,
    float time_seconds,
    inout float height,
    inout float2 gradient)
{
    const float frequency = kTwoPi / wavelength;
    const float2 wave_dir = normalize(dir);
    const float phase = dot(sample_xy, wave_dir) * frequency + time_seconds * phase_speed;
    height += sin(phase) * amplitude;
    gradient += wave_dir * (cos(phase) * amplitude * frequency);
}

float water_height(float2 world_xy, float time_seconds, out float2 gradient, float water_quality) {
    const float2 wind_dir = safe_wind_direction();
    const float2 cross_dir = float2(-wind_dir.y, wind_dir.x);
    const float wind_force = saturate(environment_wind.z);
    const float wind_speed = max(environment_wind.w, 0.05);
    const float weather_density = saturate(environment_weather.x);

    float height = 0.0;
    gradient = float2(0.0, 0.0);
    const float speed = wind_speed * lerp(0.55, 1.10, wind_force);
    const float amplitude = lerp(0.70, 1.35, wind_force) * lerp(1.0, 1.20, weather_density)
        * lerp(1.0, 1.25, saturate(water_quality - 1.0));
    add_height_wave(world_xy, wind_dir, 9.4, 0.90 * amplitude, 0.18 * speed, time_seconds, height, gradient);
    add_height_wave(world_xy, normalize(wind_dir + cross_dir * 0.42), 5.8, 0.38 * amplitude, -0.24 * speed, time_seconds, height, gradient);
    add_height_wave(world_xy, normalize(wind_dir - cross_dir * 0.32), 3.4, 0.16 * amplitude, 0.42 * speed, time_seconds, height, gradient);

    if (water_quality > 1.5) {
        add_height_wave(world_xy, normalize(wind_dir + cross_dir * 0.8), 1.6, 0.06 * amplitude, 1.10 * speed, time_seconds, height, gradient);
    }

    gradient *= kWaterHeightStrength;
    return height * kWaterHeightStrength;
}

VSOutput main(VSInput input) {
    VSOutput output;

    const float time_seconds = pad_alpha.y;
    const float water_quality = environment_weather.w;
    float4 world_pos = mul(model, float4(input.position, 1.0));
    float3 base_normal = normalize(mul((float3x3)normal_matrix, input.normal));
    const float horizontal_weight = saturate(abs(base_normal.z));

    float2 gradient = float2(0.0, 0.0);
    const float height = water_height(world_pos.xy, time_seconds, gradient, water_quality) * horizontal_weight;
    world_pos.z += height;
    float4 view_pos = mul(view, world_pos);

    float3 wave_normal = normalize(float3(-gradient.x * horizontal_weight, -gradient.y * horizontal_weight, 1.0));
    output.world_pos = world_pos.xyz;
    output.position  = mul(projection, view_pos);
    output.normal    = normalize(lerp(base_normal, wave_normal, horizontal_weight));
    output.texcoord  = input.texcoord;
    output.view_dir  = camera_pos.xyz - world_pos.xyz;
    output.view_depth = -view_pos.z;

    return output;
}
