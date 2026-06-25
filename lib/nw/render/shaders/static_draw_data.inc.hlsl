// Per-draw data for batched NWN static mesh submission.
// Layout must match nw::render::nwn::PreparedStaticDrawConstants.

struct StaticDrawData {
    float4x4 model;
    float4x4 normal_matrix;
    float4 color_key_threshold;
    float4 pad_alpha;
    uint4 texture_flags;
    float4 alpha_params;
    uint4 plt_colors0;
    uint4 plt_colors1;
    uint4 plt_colors2;
    float4 emissive_color;
    float4 material_params;
    uint4 material_texture_indices;
};

StructuredBuffer<StaticDrawData> g_static_draws : register(t3, space0);
