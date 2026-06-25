// NWN water pixel shader.

Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

#include "scene_constants.inc.hlsl"
#include "forward_plus.inc.hlsl"
#include "shadow.inc.hlsl"

struct PSInput {
    float4 position  : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 texcoord  : TEXCOORD2;
    float3 view_dir  : TEXCOORD3;
    float view_depth  : TEXCOORD4;
};

float3 lambert(float3 N, float3 L, float3 color) {
    return color * max(dot(N, L), 0.0);
}

static const float kTwoPi = 6.28318530718;
static const float kWaterNormalStrength = 0.062;

float2 hash22(float2 p) {
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float hash(float2 p) {
    return hash22(p).x;
}

float2 rain_ripples(float2 world_xy, float time_seconds, float weather_density, float weather_type, float water_quality) {
    if (water_quality < 0.5 || abs(weather_type - 1.0) > 0.5 || weather_density < 0.05) {
        return float2(0.0, 0.0);
    }
    const float density = weather_density * lerp(0.7, 1.3, saturate(water_quality - 1.0));
    const float2 scaled = world_xy * 1.2;
    const float2 cell = floor(scaled);
    const float2 frac_pos = frac(scaled);
    float2 ripple_grad = float2(0.0, 0.0);
    const float ripple_intensity = 0.030 * density;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const float2 neighbor = cell + float2(x, y);
            const float2 center = hash22(neighbor);
            const float2 delta = frac_pos - center - float2(x, y);
            const float dist = length(delta);
            const float h = hash(neighbor);
            const float t = frac(time_seconds * (1.4 + h * 1.6) + h * 10.0);
            const float radius = t * 0.55;
            const float w = exp(-t * 3.0) * sin((dist - radius) * 52.0) * smoothstep(0.6, 0.0, dist);
            ripple_grad += normalize(delta + 1.0e-4) * w * ripple_intensity;
        }
    }
    return ripple_grad;
}

void add_normal_wave(
    float2 sample_xy,
    float2 dir,
    float wavelength,
    float amplitude,
    float phase_speed,
    float time_seconds,
    inout float2 gradient)
{
    const float frequency = kTwoPi / wavelength;
    const float2 wave_dir = normalize(dir);
    const float phase = dot(sample_xy, wave_dir) * frequency + time_seconds * phase_speed;
    gradient += wave_dir * (cos(phase) * amplitude * frequency);
}

float3 water_surface_normal(float2 world_xy, float time_seconds, float3 base_normal, float water_quality) {
    const float2 wind_dir = safe_wind_direction();
    const float2 cross_dir = float2(-wind_dir.y, wind_dir.x);
    const float wind_force = saturate(environment_wind.z);
    const float wind_speed = max(environment_wind.w, 0.05);
    const float weather_density = saturate(environment_weather.x);
    const float weather_type = environment_weather.y;

    float2 gradient = float2(0.0, 0.0);
    const float speed = wind_speed * lerp(0.60, 1.15, wind_force);
    const float amplitude = lerp(0.75, 1.45, wind_force) * lerp(1.0, 1.25, weather_density);
    add_normal_wave(world_xy, wind_dir, 8.8, 0.82 * amplitude, 0.20 * speed, time_seconds, gradient);
    add_normal_wave(world_xy, normalize(wind_dir + cross_dir * 0.45), 5.1, 0.42 * amplitude, -0.28 * speed, time_seconds, gradient);
    add_normal_wave(world_xy, normalize(wind_dir - cross_dir * 0.35), 2.7, 0.16 * amplitude, 0.55 * speed, time_seconds, gradient);
    add_normal_wave(world_xy, normalize(cross_dir + wind_dir * 0.25), 1.55, 0.055 * amplitude, -0.95 * speed, time_seconds, gradient);

    if (water_quality > 1.5) {
        add_normal_wave(world_xy, normalize(wind_dir + cross_dir * 0.8), 0.95, 0.025 * amplitude, 1.35 * speed, time_seconds, gradient);
    }

    gradient += rain_ripples(world_xy, time_seconds, weather_density, weather_type, water_quality);

    gradient *= kWaterNormalStrength;
    float3 ripple = normalize(float3(-gradient.x, -gradient.y, 1.0));
    return normalize(lerp(base_normal, ripple, saturate(abs(base_normal.z)) * 0.82));
}

float schlick_fresnel(float ior1, float ior2, float3 V, float3 N) {
    const float incident = saturate(dot(V, N));
    const float reflectance = (ior2 - ior1) / (ior2 + ior1);
    const float r0 = reflectance * reflectance;
    return r0 + (1.0 - r0) * pow(1.0 - incident, 5.0);
}

float wind_sheen(float2 world_xy, float time_seconds, float2 wind_dir, float wind_force, float wind_speed,
    float weather_density, float horizontal_weight)
{
    const float2 cross_dir = float2(-wind_dir.y, wind_dir.x);
    const float2 p = float2(dot(world_xy, wind_dir), dot(world_xy, cross_dir));
    const float speed = wind_speed * lerp(0.65, 1.25, wind_force);
    const float crest_a = 0.5 + 0.5 * sin(p.x * 1.85 + p.y * 0.18 + time_seconds * 0.95 * speed);
    const float crest_b = 0.5 + 0.5 * sin(p.x * 2.70 - p.y * 0.34 - time_seconds * 1.25 * speed);
    const float broken_highlight = pow(saturate(crest_a * crest_b), 6.0);
    return broken_highlight * lerp(0.016, 0.066, wind_force) * lerp(1.0, 0.62, weather_density) * horizontal_weight;
}

float water_detail_noise(float2 world_xy, float time_seconds, float2 wind_dir, float wind_force) {
    const float2 cross_dir = float2(-wind_dir.y, wind_dir.x);
    const float2 p = float2(dot(world_xy, wind_dir), dot(world_xy, cross_dir));
    const float speed = lerp(0.55, 1.05, wind_force);
    float detail = 0.5 + 0.5 * sin(p.x * 0.54 + p.y * 0.11 + time_seconds * 0.20 * speed);
    detail = detail * 0.62 + (0.5 + 0.5 * sin(p.x * 0.88 - p.y * 0.30 - time_seconds * 0.27 * speed)) * 0.38;
    return saturate(detail);
}

float water_foam(float2 world_xy, float time_seconds, float3 N, float2 wind_dir, float wind_force,
    float wind_speed, float weather_density, float horizontal_weight, float water_quality)
{
    if (water_quality < 0.5 || horizontal_weight < 0.01) {
        return 0.0;
    }
    const float crest = saturate(length(N.xy) * 3.0 - 0.35);
    const float sheen = wind_sheen(world_xy, time_seconds, wind_dir, wind_force, wind_speed,
        weather_density, horizontal_weight);
    const float foam = saturate(crest * lerp(0.25, 1.0, wind_force) + sheen * 3.0)
        * lerp(0.4, 1.0, weather_density)
        * lerp(0.5, 1.0, saturate(water_quality - 1.0));
    return foam;
}

float3 analytical_sky_color(float3 R, float3 L_key) {
    float horizon = saturate(1.0 - abs(R.z));
    float upward = saturate(R.z * 0.5 + 0.5);

    float3 horizon_color = fog_enabled != 0
        ? lerp(fill_color.xyz, fog_color.xyz, 0.45)
        : fill_color.xyz;
    float3 zenith_color = ambient.xyz * 5.5 + float3(0.05, 0.12, 0.18);
    float3 sky_color = lerp(horizon_color, zenith_color, upward);
    sky_color += horizon * fill_color.xyz * fill_dir_intensity.w * 0.35;

    float sun_core = pow(saturate(dot(R, L_key)), 180.0);
    float sun_glow = pow(saturate(dot(R, L_key)), 18.0);
    sky_color += key_color.xyz * key_dir_intensity.w * (sun_core * 2.8 + sun_glow * 0.16);
    return sky_color;
}

float water_shadow_visibility(float3 world_pos, float3 N, float3 L, float scene_distance) {
    const float visibility = shadow_directional_raw_visibility(
        world_pos, N, L, scene_distance,
        0.0020, 0.0010, 0.00055, 0.00025,
        0.035, 0.025,
        1.25, 0.50);
    const float surface_weight = saturate(abs(N.z));
    const float influence = saturate(shadow_strength * 0.32 * surface_weight);
    return lerp(1.0 - influence, 1.0, visibility);
}

void accumulate_water_local_light(
    inout float3 diffuse,
    inout float3 specular,
    float3 world_pos,
    float3 N,
    float3 V,
    float rough_weather,
    float4 pos_radius,
    float4 color_intensity,
    float4 light_params)
{
    bool ambient_local_light = light_params.x >= 0.5;
    float radius = max(pos_radius.w, 1.0e-3);
    float3 light_delta = pos_radius.xyz - world_pos;
    if (ambient_local_light) {
        light_delta.z *= saturate(light_params.y);
    }

    float dist_sq = dot(light_delta, light_delta);
    float radius_sq = radius * radius;
    if (dist_sq >= radius_sq) {
        return;
    }

    float falloff = saturate(dist_sq / radius_sq);
    float attenuation = saturate(1.0 - falloff);
    attenuation *= attenuation;
    float intensity = color_intensity.w * attenuation;
    if (ambient_local_light) {
        diffuse += color_intensity.xyz * intensity * 0.18;
        return;
    }

    float3 L = light_delta * rsqrt(max(dist_sq, 1.0e-5));
    float wrapped_diffuse = saturate(dot(N, L) * 0.35 + 0.65);
    diffuse += color_intensity.xyz * wrapped_diffuse * intensity * 0.16;

    float spec_power = lerp(92.0, 48.0, rough_weather);
    float spec = pow(max(dot(reflect(-L, N), V), 0.0), spec_power);
    specular += color_intensity.xyz * spec * intensity * lerp(0.46, 0.26, rough_weather);
}

void accumulate_water_forward_plus_lights(
    inout float3 diffuse,
    inout float3 specular,
    float4 screen_position,
    float view_depth,
    float3 world_pos,
    float3 N,
    float3 V,
    float rough_weather)
{
    if (forward_plus_enabled()) {
        const ForwardPlusLightRange light_range = forward_plus_light_range(screen_position, view_depth);
        [loop]
        for (uint i = 0; i < light_range.count; ++i) {
            ForwardPlusLightData light;
            if (!forward_plus_load_light(light_range, i, light)) {
                continue;
            }
            accumulate_water_local_light(
                diffuse, specular, world_pos, N, V, rough_weather,
                light.position_radius, light.color_intensity, light.params);
        }
    }
}

float4 main(PSInput input) : SV_Target {
    const float time_seconds = pad_alpha.y;
    const float opacity = saturate(pad_alpha.x);
    const float2 wind_dir = safe_wind_direction();
    const float wind_force = saturate(environment_wind.z);
    const float wind_speed = max(environment_wind.w, 0.05);
    const float weather_density = saturate(environment_weather.x);
    const float water_quality = environment_weather.w;

    const uint tex_idx = NonUniformResourceIndex(texture_index);
    float4 texel = g_textures[tex_idx].Sample(g_sampler, input.texcoord);
    float3 flow_a = g_textures[tex_idx].Sample(g_sampler,
        input.texcoord + wind_dir * (0.0035 + wind_force * 0.0045) * wind_speed * time_seconds).rgb;
    const float texture_luma = dot(flow_a, float3(0.299, 0.587, 0.114));
    const float texture_detail = saturate((texture_luma - 0.5) * 0.12 + 0.5);

    float3 N = water_surface_normal(input.world_pos.xy, time_seconds, normalize(input.normal), water_quality);
    float3 V = normalize(input.view_dir);
    const float scene_distance = length(input.view_dir);
    const float view_alignment = saturate(dot(N, V));
    const float horizontal_weight = saturate(abs(input.normal.z));
    const float broad_detail = water_detail_noise(input.world_pos.xy, time_seconds, wind_dir, wind_force) * horizontal_weight;
    const float3 absorption_color = float3(0.58, 0.88, 0.92);
    const float optical_path = lerp(0.22, 1.25, 1.0 - view_alignment);
    const float3 transmittance = pow(absorption_color, optical_path * 1.15);
    const float absorption = 1.0 - dot(transmittance, float3(0.299, 0.587, 0.114));
    const float3 shallow_tint = float3(0.050, 0.30, 0.34);
    const float3 deep_tint = float3(0.010, 0.055, 0.125);
    const float depth_bias = saturate(absorption * 0.92 + (1.0 - broad_detail) * 0.12);
    const float3 water_tint = lerp(shallow_tint, deep_tint, depth_bias);

    float3 lighting = ambient.xyz;
    float3 L_key = normalize(-key_dir_intensity.xyz);
    float3 L_fill = normalize(-fill_dir_intensity.xyz);
    const float key_shadow = water_shadow_visibility(input.world_pos, N, L_key, scene_distance);
    lighting += lambert(N, L_key, key_color.xyz) * key_dir_intensity.w * key_shadow;
    lighting += lambert(N, L_fill, fill_color.xyz) * fill_dir_intensity.w * 0.65;

    const float fresnel = schlick_fresnel(1.0, 1.333, V, N);
    const float slope_reflection = saturate(length(N.xy) * 0.42);
    const float3 R = normalize(reflect(-V, N));
    const float3 sky_reflection = analytical_sky_color(R, L_key);
    const float rough_weather = saturate(weather_density * 0.48 + wind_force * 0.16);
    const float key_spec = pow(max(dot(reflect(-L_key, N), V), 0.0), lerp(120.0, 72.0, rough_weather));
    const float fill_spec = pow(max(dot(reflect(-L_fill, N), V), 0.0), lerp(80.0, 52.0, rough_weather));
    const float sheen = wind_sheen(input.world_pos.xy, time_seconds, wind_dir, wind_force, wind_speed,
        weather_density, horizontal_weight) * lerp(0.72, 1.0, key_shadow);
    const float foam = water_foam(input.world_pos.xy, time_seconds, N, wind_dir, wind_force, wind_speed,
        weather_density, horizontal_weight, water_quality);
    float3 local_light_diffuse = float3(0.0, 0.0, 0.0);
    float3 local_light_specular = float3(0.0, 0.0, 0.0);
    accumulate_water_forward_plus_lights(
        local_light_diffuse, local_light_specular, input.position, input.view_depth,
        input.world_pos, N, V, rough_weather);
    float3 final_color = water_tint * (ambient.xyz * 2.0 + lighting * 0.30);
    final_color += water_tint * local_light_diffuse;
    final_color *= lerp(0.93, 1.11, broad_detail);
    final_color *= lerp(0.985, 1.015, texture_detail);
    final_color *= lerp(1.0, 0.88, weather_density);
    final_color = lerp(final_color, sky_reflection,
        saturate((fresnel * 1.16 + slope_reflection * 0.58) * lerp(1.0, 0.62, rough_weather)));
    final_color = lerp(final_color, sky_reflection, sheen * 0.07);
    final_color += sky_reflection * sheen * 0.035;
    final_color += key_color.xyz * key_spec * key_dir_intensity.w * key_shadow * lerp(0.42, 0.24, rough_weather);
    final_color += fill_color.xyz * fill_spec * fill_dir_intensity.w * lerp(0.30, 0.18, rough_weather);
    final_color += local_light_specular;
    final_color = lerp(final_color, float3(0.92, 0.96, 1.0), foam * 0.65);

    if (fog_enabled != 0) {
        const float fog_start = min(fog_range.x, fog_range.y);
        const float fog_end = max(fog_range.x, fog_range.y);
        const float fog_distance = max(scene_distance - fog_start, 0.0);
        const float fog_range_span = max(fog_end - fog_start, 1.0e-4);
        const float fog_normalized = fog_distance / fog_range_span;
        const float fog_density = lerp(2.5, 8.0, saturate(fog_amount));
        const float fog_extinction = pow(fog_normalized, 1.35);
        const float transmittance = exp(-fog_density * fog_extinction);
        final_color = final_color * transmittance + fog_color.rgb * (1.0 - transmittance);
    }

    final_color = final_color / (final_color + float3(1.0, 1.0, 1.0));
    final_color = pow(saturate(final_color), 1.0 / 2.2);

    if (debug_mode != 0) {
        final_color = lerp(final_color, float3(0.15, 0.65, 0.95), 0.65);
    }
    final_color = forward_plus_apply_debug(final_color, input.position, input.view_depth, 0.72);

    // Legacy NWN water is self-composited by the water shader. Keep the target opaque so
    // transparent tile water does not expose the viewport clear color when no bottom mesh exists.
    const float output_alpha = saturate(opacity);
    return float4(final_color * output_alpha, output_alpha);
}
