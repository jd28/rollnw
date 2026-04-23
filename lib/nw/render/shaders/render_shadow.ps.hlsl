Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

cbuffer SceneConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
    float4x4 model;
    float4x4 normal_matrix;
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

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

void main(PSInput input) {
    if (alpha_cutout != 0) {
        float alpha = g_textures[NonUniformResourceIndex(texture_index)].Sample(g_sampler, input.texcoord).a;
        clip(alpha - alpha_cutout_threshold);
    }
}
