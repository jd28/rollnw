// Static shadow-map vertex shader.

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

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent  : TANGENT;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.position = mul(projection, mul(view, world_pos));
    output.texcoord = input.texcoord;
    return output;
}
