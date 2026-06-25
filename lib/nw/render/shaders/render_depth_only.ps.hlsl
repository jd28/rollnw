// Depth pre-pass pixel shader. Writes no color (the pipeline disables color writes);
// its only job is to let the rasterizer populate the depth buffer. Pairs with the
// regular static/batched mesh vertex shaders so the depth it produces is bit-identical
// to the subsequent color pass (which uses a LESS_EQUAL / EQUAL depth test).
struct PSInput {
    float4 position : SV_Position;
};

void main(PSInput input) {}
