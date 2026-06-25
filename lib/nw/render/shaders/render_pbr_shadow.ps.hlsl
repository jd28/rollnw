// Depth-only shadow pixel shader for glTF PBR primitives.
// SurfaceConstants layout must match nw::render::SurfaceConstants exactly.

Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

cbuffer SurfaceConstants : register(b4) {
    float4 sc_albedo;
    float  sc_roughness;
    float  sc_metallic;
    float  sc_specular_strength;
    float  sc_normal_scale;
    float  sc_occlusion_strength;
    float  sc_ibl_strength;
    float  sc_exposure;
    float  sc_pad0;
    float4 sc_emissive;
    uint   sc_albedo_index;
    uint   sc_normal_index;
    uint   sc_surface_index;
    uint   sc_emissive_index;
    uint   sc_alpha_mode;
    float  sc_alpha_cutoff;
    uint   sc_double_sided;
    uint   sc_plt_enabled;
    uint4  sc_plt_colors0;
    uint4  sc_plt_colors1;
    uint4  sc_plt_colors2;
};

struct PSInput {
    float4 position   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float3 normal     : TEXCOORD1;
    float2 texcoord   : TEXCOORD2;
    float3 view_dir   : TEXCOORD3;
    float3 tangent    : TEXCOORD4;
    float3 bitangent  : TEXCOORD5;
    float view_depth   : TEXCOORD6;
};

float sample_albedo_alpha(float2 texcoord) {
    if (sc_plt_enabled == 0u) {
        return g_textures[NonUniformResourceIndex(sc_albedo_index)].Sample(g_sampler, texcoord).a;
    }

    uint tex_idx = NonUniformResourceIndex(sc_albedo_index);
    uint width = 0;
    uint height = 0;
    g_textures[tex_idx].GetDimensions(width, height);
    if (width == 0 || height == 0) {
        return 0.0;
    }

    uint2 pixel = min(uint2(texcoord * float2(width, height)), uint2(width - 1, height - 1));
    return g_textures[tex_idx].Load(int3(pixel, 0)).a;
}

void main(PSInput input) {
    if (sc_alpha_mode == 1u) {
        const float alpha = sample_albedo_alpha(input.texcoord);
        clip(alpha - sc_alpha_cutoff);
    }
}
