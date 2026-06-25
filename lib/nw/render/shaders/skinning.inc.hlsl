// Shared GPU skinning helpers. The g_bones buffer stays declared per-shader
// because its register differs between pipelines.

float4x4 blend_bone_matrices(StructuredBuffer<float4x4> bones, uint4 indices, float4 weights) {
    return bones[indices.x] * weights.x +
        bones[indices.y] * weights.y +
        bones[indices.z] * weights.z +
        bones[indices.w] * weights.w;
}
