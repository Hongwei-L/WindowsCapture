// shaders.hlsl
Texture2D inputTex : register(t0);
SamplerState linearSampler : register(s0);

cbuffer ScaleBuffer : register(b0)
{
    float scaleX, scaleY;
    float offsetX, offsetY;
};

struct VSInput {
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 1.0);
    output.uv = input.uv;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    //float2 uv = (input.uv - float2(offsetX, offsetY)) / float2(scaleX, scaleY);
    float2 uv = input.uv / float2(scaleX, scaleY) - float2(offsetX, offsetY) ;
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) return float4(0,0,0,1);
    
    return inputTex.Sample(linearSampler, uv);
    //return float4(0, 0, 1, 1);
}


