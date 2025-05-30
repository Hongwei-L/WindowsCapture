// shaders.hlsl
Texture2D inputTex : register(t0);
SamplerState linearSampler : register(s0);

cbuffer ScaleBuffer : register(b0)
{
    float scaleX, scaleY;
    float offsetX, offsetY;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    float2 pos[4] = { float2(-1,-1), float2(-1,1), float2(1,-1), float2(1,1) };
    float2 uv[4] = { float2(0,1), float2(0,0), float2(1,1), float2(1,0) };

    VSOut o;
    o.pos = float4(pos[id], 0, 1);
    o.uv = uv[id];
    return o;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float2 uv = (input.uv - float2(offsetX, offsetY)) / float2(scaleX, scaleY);
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) return float4(0,0,0,1);
    return inputTex.Sample(linearSampler, uv);
}
