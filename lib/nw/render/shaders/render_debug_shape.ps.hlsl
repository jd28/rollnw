struct PSInput {
    float4 position : SV_Position;
    float4 color    : COLOR0;
};

float4 main(PSInput input) : SV_Target
{
    float4 color = input.color;
    color.rgb *= color.a;
    return color;
}
