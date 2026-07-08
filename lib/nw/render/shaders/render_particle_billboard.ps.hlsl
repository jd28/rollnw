Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

cbuffer ParticleDrawConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
    uint texture_index;
    uint alpha_cutout;
    uint additive_like;
    uint additive_alpha_gated;
    float alpha_cutout_threshold;
    float additive_intensity;
    float alpha_intensity;
    float4 fog_color;
    float2 fog_range;
    float fog_amount;
    uint fog_enabled;
};

struct PSInput {
    float4 position      : SV_Position;
    float2 texcoord      : TEXCOORD0;
    float4 color         : COLOR0;
    float view_distance  : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target
{
    float4 texel = g_textures[texture_index].Sample(g_sampler, input.texcoord);
    float4 color = texel * input.color;
    float source_alpha = texel.a * input.color.a;

    if (alpha_cutout != 0 && color.a < alpha_cutout_threshold) {
        discard;
    }

    if (additive_like != 0) {
        // Preserve the authored texture hue only for additive particle classes that
        // explicitly rely on alpha-gated additive presentation (for example, projectile
        // trails). Ordinary additive billboards such as flares should still tint normally.
        float tint_chroma = max(abs(input.color.r - input.color.g), max(abs(input.color.g - input.color.b), abs(input.color.b - input.color.r)));
        float texel_chroma = max(abs(texel.r - texel.g), max(abs(texel.g - texel.b), abs(texel.b - texel.r)));
        if (additive_alpha_gated != 0 && tint_chroma <= 0.02f && texel_chroma >= 0.08f) {
            color.rgb = texel.rgb;
        }

        color.rgb *= source_alpha * additive_intensity;
        color.a = source_alpha;
    } else if (alpha_cutout == 0) {
        color.rgb *= input.color.a * alpha_intensity;
        color.a = source_alpha * alpha_intensity;
    }

    if (fog_enabled != 0) {
        float fog_start = min(fog_range.x, fog_range.y);
        float fog_end = max(fog_range.x, fog_range.y);
        float fog_t = saturate((input.view_distance - fog_start) / max(fog_end - fog_start, 1.0e-4));
        float fog_factor = 1.0 - pow(1.0 - fog_t, 1.0 + saturate(fog_amount) * 3.0);
        if (alpha_cutout == 0) {
            color.rgb = lerp(color.rgb, fog_color.rgb * color.a, fog_factor);
            if (additive_like == 0) {
                float alpha_scale = 1.0 - fog_factor * 0.35;
                color.rgb *= alpha_scale;
                color.a *= alpha_scale;
            }
        } else {
            color.rgb = lerp(color.rgb, fog_color.rgb, fog_factor);
        }
    }

    return color;
}
