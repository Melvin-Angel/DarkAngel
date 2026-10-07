
cbuffer Frame {row_major float4x4 Mvp;row_major float4x4 Rotation;float4 Tint;float4 Surface;};
struct VertexInput {float3 Position:ATTRIB0;float3 Normal:ATTRIB1;float2 UV:ATTRIB2;};
struct VertexOutput {float4 Position:SV_POSITION;float3 Normal:TEXCOORD0;float2 UV:TEXCOORD1;};
VertexOutput VSMain(VertexInput vertex){VertexOutput result;result.Position=mul(float4(vertex.Position,1),Mvp);result.Normal=mul(vertex.Normal,(float3x3)Rotation);result.UV=vertex.UV;return result;}
Texture2D Albedo;SamplerState Albedo_sampler;
float4 PSMain(VertexOutput vertex):SV_TARGET {
    float3 normal=normalize(vertex.Normal);float3 light=normalize(float3(.4,.7,.8));
    float diffuse=saturate(dot(normal,light));float bands=floor(diffuse*3.999)/3;
    float specular=pow(saturate(dot(normal,normalize(light+float3(0,0,1)))),lerp(40,4,Surface.x));
    float3 color=Albedo.Sample(Albedo_sampler,vertex.UV).rgb*Tint.rgb;
    return float4(color*(.22+.78*bands)+specular*.08*lerp(float3(1,1,1),color,Surface.y),1);
}
