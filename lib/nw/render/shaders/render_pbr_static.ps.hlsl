// PBR pixel shader — GGX Cook-Torrance, 3-point studio lighting.
// SceneConstants layout must match renderer.hpp exactly.
// SurfaceConstants layout must match nw::render::SurfaceConstants exactly.

Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

cbuffer SceneConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
    float4x4 model;
    float4x4 normal_matrix;
    uint texture_index;      uint alpha_cutout; float alpha_cutout_threshold; float _pad_tex;
    float2 color_key_rg;     float color_key_b; float color_key_threshold;
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
    uint plt_enabled;         uint ibl_diffuse_texture_index; uint ibl_specular_texture_index; uint ibl_brdf_lut_texture_index;
    uint4 plt_colors0;
    uint4 plt_colors1;
    uint4 plt_colors2;
};

cbuffer SurfaceConstants : register(b4) {
    float4 sc_albedo;
    float  sc_roughness;
    float  sc_metallic;
    float  sc_normal_scale;
    float  sc_occlusion_strength;
    float  sc_ibl_strength;
    float  sc_exposure;
    float2 sc_pad0;
    float4 sc_emissive;
    uint   sc_albedo_index;
    uint   sc_normal_index;
    uint   sc_surface_index;
    uint   sc_emissive_index;
    uint   sc_alpha_mode;
    float  sc_alpha_cutoff;
    uint   sc_double_sided;
    uint   sc_pad1;
};

struct PSInput {
    float4 position   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float3 normal     : TEXCOORD1;
    float2 texcoord   : TEXCOORD2;
    float3 view_dir   : TEXCOORD3;
    float3 tangent    : TEXCOORD4;
    float3 bitangent  : TEXCOORD5;
};

static const float PI = 3.14159265358979;

float2 env_latlong_uv(float3 dir)
{
    dir = normalize(dir);
    float phi = atan2(dir.y, dir.x);
    float theta = acos(clamp(dir.z, -1.0, 1.0));
    return float2(phi / (2.0 * PI) + 0.5, theta / PI);
}

float3 fallback_env(float3 dir, float roughness)
{
    float hemi = dir.z * 0.5 + 0.5;
    float3 sky = float3(0.80, 0.86, 0.96);
    float3 ground = float3(0.08, 0.07, 0.06);
    float3 env = lerp(ground, sky, hemi);
    float gloss = 1.0 - roughness;
    env *= lerp(1.05, 0.22, roughness);
    env *= lerp(0.85, 1.35, gloss * gloss);
    return env;
}

float3 sample_env_specular(float3 dir, float roughness)
{
    if (ibl_specular_texture_index == 0u) {
        return fallback_env(dir, roughness);
    }

    uint width = 0;
    uint height = 0;
    uint levels = 1;
    g_textures[NonUniformResourceIndex(ibl_specular_texture_index)].GetDimensions(0, width, height, levels);
    float max_mip = max(float(levels) - 1.0, 0.0);
    float spec_mip = saturate(roughness) * max_mip;
    return g_textures[NonUniformResourceIndex(ibl_specular_texture_index)].SampleLevel(
        g_sampler, env_latlong_uv(dir), spec_mip).rgb;
}

float3 sample_env_diffuse(float3 dir)
{
    if (ibl_diffuse_texture_index == 0u) {
        float hemi = dir.z * 0.5 + 0.5;
        float3 sky = float3(0.34, 0.38, 0.46);
        float3 ground = float3(0.05, 0.05, 0.05);
        return lerp(ground, sky, hemi);
    }

    return g_textures[NonUniformResourceIndex(ibl_diffuse_texture_index)].SampleLevel(
        g_sampler, env_latlong_uv(dir), 0.0).rgb;
}

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float G_Smith(float NdotV, float NdotL, float a) {
    float k = (a + 1.0) * (a + 1.0) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

float3 F_Schlick(float VdotH, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

float3 env_brdf_approx(float3 F0, float roughness, float NdotV)
{
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

float2 sample_brdf_lut(float NdotV, float roughness)
{
    if (ibl_brdf_lut_texture_index == 0u) {
        return float2(-1.0, -1.0);
    }

    return g_textures[NonUniformResourceIndex(ibl_brdf_lut_texture_index)].SampleLevel(
        g_sampler, float2(saturate(NdotV), saturate(roughness)), 0.0).rg;
}

float3 eval_env_diffuse(float3 N, float3 albedo_lin, float occlusion, float metallic) {
    float3 env = sample_env_diffuse(N);
    return env * albedo_lin * (1.0 - metallic) * occlusion * sc_ibl_strength;
}

float3 eval_env_specular(float3 N, float3 V, float3 F0, float roughness, float metallic, float occlusion) {
    float3 R = reflect(-V, N);
    float3 env = sample_env_specular(R, roughness);

    float NdotV = max(dot(N, V), 1e-4);
    float2 brdf = sample_brdf_lut(NdotV, roughness);
    float3 env_brdf = brdf.x >= 0.0
        ? (F0 * brdf.x + brdf.y)
        : env_brdf_approx(F0, roughness, NdotV);
    return env * env_brdf * occlusion * sc_ibl_strength;
}

float3 eval_light(float3 N, float3 V, float3 L,
                  float3 light_color, float intensity,
                  float3 albedo_lin, float3 F0, float a, float metallic) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return (float3)0;

    float3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float VdotH = max(dot(V, H), 0.0);

    float D = D_GGX(NdotH, a);
    float G = G_Smith(NdotV, NdotL, a);
    float3 F = F_Schlick(VdotH, F0);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    float3 kd = (1.0 - F) * (1.0 - metallic);

    return (kd * albedo_lin / PI + specular) * light_color * intensity * NdotL;
}

float3 tonemap_aces_fitted(float3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float4 main(PSInput input) : SV_Target {
    float3 V = normalize(input.view_dir);
    float3 geom_N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);
    if (sc_double_sided != 0u && dot(geom_N, V) < 0.0) {
        geom_N = -geom_N;
        T = -T;
        B = -B;
    }

    float3 normal_sample = g_textures[NonUniformResourceIndex(sc_normal_index)].Sample(g_sampler, input.texcoord).xyz;
    normal_sample = normal_sample * 2.0 - 1.0;
    normal_sample.xy *= sc_normal_scale;
    normal_sample.z = sqrt(saturate(1.0 - dot(normal_sample.xy, normal_sample.xy)));
    float3 N = normalize(normal_sample.x * T + normal_sample.y * B + normal_sample.z * geom_N);

    float4 albedo_sample = g_textures[NonUniformResourceIndex(sc_albedo_index)].Sample(g_sampler, input.texcoord) * sc_albedo;
    if (sc_alpha_mode == 1u) clip(albedo_sample.a - sc_alpha_cutoff);
    float3 albedo_lin = albedo_sample.rgb;

    float4 surface_sample = g_textures[NonUniformResourceIndex(sc_surface_index)].Sample(g_sampler, input.texcoord);
    float occlusion = lerp(1.0, surface_sample.r, saturate(sc_occlusion_strength));
    float roughness = clamp(sc_roughness * surface_sample.g, 0.05, 1.0);
    float a = roughness * roughness;
    float m = sc_metallic * surface_sample.b;
    float3 emissive_sample = g_textures[NonUniformResourceIndex(sc_emissive_index)].Sample(g_sampler, input.texcoord).rgb;

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo_lin, m);
    float3 Lo = (float3)0;
    Lo += eval_light(N, V, normalize(-key_dir_intensity.xyz), key_color.xyz, key_dir_intensity.w, albedo_lin, F0, a, m);
    Lo += eval_light(N, V, normalize(-fill_dir_intensity.xyz), fill_color.xyz, fill_dir_intensity.w, albedo_lin, F0, a, m);
    Lo += eval_light(N, V, normalize(-rim_dir_intensity.xyz), rim_color.xyz, rim_dir_intensity.w, albedo_lin, F0, a, m);

    float3 env_diffuse = eval_env_diffuse(N, albedo_lin, occlusion, m);
    float3 env_specular = eval_env_specular(N, V, F0, roughness, m, occlusion);
    float3 ambient_diffuse = albedo_lin * ambient.xyz * occlusion * (1.0 - m);
    float3 final_color = ambient_diffuse + env_diffuse + env_specular + Lo + sc_emissive.rgb * emissive_sample;

    if (fog_enabled != 0) {
        float scene_distance = length(input.view_dir);
        float fog_start = min(fog_range.x, fog_range.y);
        float fog_end = max(fog_range.x, fog_range.y);
        float fog_dist = max(scene_distance - fog_start, 0.0);
        float fog_span = max(fog_end - fog_start, 1e-4);
        float fog_norm = fog_dist / fog_span;
        float fog_density = lerp(2.5, 8.0, saturate(fog_amount));
        float transmittance = exp(-fog_density * pow(fog_norm, 1.35));
        final_color = final_color * transmittance + fog_color.rgb * (1.0 - transmittance);
    }

    final_color *= sc_exposure;
    final_color = tonemap_aces_fitted(final_color);
    final_color = pow(saturate(final_color), 1.0 / 2.2);

    float out_alpha = sc_alpha_mode == 2u ? albedo_sample.a : 1.0;
    return float4(final_color, out_alpha);
}
