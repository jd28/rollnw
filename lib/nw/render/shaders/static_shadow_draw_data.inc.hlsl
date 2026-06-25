// Per-draw data for batched NWN static shadow submission.
// Layout must match nw::render::nwn::PreparedStaticShadowDrawConstants.

struct StaticShadowDrawData {
    float4x4 model;
    uint4 texture_flags;
    float4 alpha_params;
};

StructuredBuffer<StaticShadowDrawData> g_static_shadow_draws : register(t3, space0);
