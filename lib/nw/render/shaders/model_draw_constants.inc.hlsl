// Model draw protocol v1. Layout matches ModelDrawConstants in model_renderer.hpp.
cbuffer ModelDrawConstants : register(b4) {
    float4x4 draw_model;
    float4x4 draw_normal_matrix;
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
    float4 sc_color_key_threshold;
    uint4  sc_plt_colors0;
    uint4  sc_plt_colors1;
    uint4  sc_plt_colors2;
};
