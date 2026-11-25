// Shared bindless sampled-texture table and linear sampler contract.
// Vulkan keeps the unbounded descriptor-buffer array at (set=1, binding=2/3).
// Metal exposes the same table as a Tier-2 argument buffer at [[buffer(8)]],
// where slots 0..4095 hold MTLResourceID texture handles and slot 4096 holds
// the linear sampler handle (slot 0 is reserved as kInvalidBindlessTextureIndex).
#pragma once

#ifdef NW_SHADER_METAL
struct NwBindlessTextures {
    Texture2D<float4> textures[4096];
    SamplerState linear_sampler;
};

[[vk::binding(8, 1)]] ParameterBlock<NwBindlessTextures> g_nw_bindless : register(b8, space1);
#define g_textures g_nw_bindless.textures
#define g_sampler g_nw_bindless.linear_sampler
#else
[[vk::binding(2, 1)]] Texture2D<float4> g_textures[] : register(t2, space1);
[[vk::binding(3, 1)]] SamplerState g_sampler : register(s3, space1);
#endif
