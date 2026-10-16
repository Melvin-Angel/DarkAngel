cbuffer Frame {row_major float4x4 Mvp;row_major float4x4 Rotation;row_major float4x4 World;float4 Tint;float4 Surface;float4 Emission;float4 Features;float4 MainLight;float4 MainColor;float4 Ambient;float4 ToonBands;float4 ShadowTint;float4 LocalPositions[4];float4 LocalColors[4];float4 CharacterHit;float4 CharacterStatus;float4 CharacterParams;float4 TerrainParams;float4 FogColor;float4 FogParams;float4 ShadowSplits;float4 ShadowParams;row_major float4x4 ShadowMatrices[4];};
struct VertexInput {float3 Position:ATTRIB0;float3 Normal:ATTRIB1;float2 UV:ATTRIB2;};
struct VertexOutput {float4 Position:SV_POSITION;float3 Normal:TEXCOORD0;float2 UV:TEXCOORD1;float3 Local:TEXCOORD2;float3 WorldPosition:TEXCOORD3;};
VertexOutput VSMain(VertexInput vertex){VertexOutput result;result.Position=mul(float4(vertex.Position,1),Mvp);result.Normal=mul(vertex.Normal,(float3x3)Rotation);result.Local=mul(vertex.Position,(float3x3)Rotation);result.WorldPosition=mul(float4(vertex.Position,1),World).xyz;result.UV=vertex.UV;return result;}
Texture2D Albedo;SamplerState Albedo_sampler;
Texture2D NormalMap;SamplerState NormalMap_sampler;
Texture2D SurfaceMap;SamplerState SurfaceMap_sampler;
Texture2D EmissiveMap;SamplerState EmissiveMap_sampler;
Texture2D DissolveMask;SamplerState DissolveMask_sampler;
Texture2D TerrainLayer1;SamplerState TerrainLayer1_sampler;
Texture2D TerrainLayer2;SamplerState TerrainLayer2_sampler;
Texture2DArray ShadowMap;SamplerComparisonState ShadowMap_sampler;
// Surface: x roughness, y metallic, z occlusion strength, w normal scale. Emission: rgb emissive colour.
// Features: x feature bits (1 normal map, 2 surface map, 4 emissive map), y alpha mode (0 opaque, 1 cutout, 2 transparent), z cutoff.
// A feature a material does not use is never sampled.
float3 MappedNormal(float3 normal,float3 local,float2 uv,float3 tangent_space){
    // Cotangent frame from screen derivatives: no vertex tangents are needed. glTF normal maps are +Y up in texture space.
    float3 dp1=ddx(local),dp2=ddy(local);float2 duv1=ddx(uv),duv2=ddy(uv);float3 dp2perp=cross(dp2,normal),dp1perp=cross(normal,dp1);
    float3 t=dp2perp*duv1.x+dp1perp*duv2.x,b=dp2perp*duv1.y+dp1perp*duv2.y;float scale=rsqrt(max(max(dot(t,t),dot(b,b)),1e-20));
    return normalize(tangent_space.x*t*scale-tangent_space.y*b*scale+tangent_space.z*normal);
}
float4 PSMain(VertexOutput vertex):SV_TARGET {
    uint features=(uint)Features.x;
    float4 base=Albedo.Sample(Albedo_sampler,vertex.UV)*Tint;
    if(features&8u){float3 weights=abs(normalize(vertex.Normal));weights/=max(weights.x+weights.y+weights.z,1e-4);float3 coordinates=vertex.Local*Features.w;float4 yz=Albedo.Sample(Albedo_sampler,coordinates.yz),xz=Albedo.Sample(Albedo_sampler,coordinates.xz),xy=Albedo.Sample(Albedo_sampler,coordinates.xy);base=(yz*weights.x+xz*weights.y+xy*weights.z)*Tint;}
    if(Features.y==1&&base.a<Features.z)discard;
    float3 normal=normalize(vertex.Normal);
    if(features&1u){float3 mapped=NormalMap.Sample(NormalMap_sampler,vertex.UV).xyz*2-1;mapped.xy*=Surface.w;normal=MappedNormal(normal,vertex.Local,vertex.UV,normalize(mapped));}
    float roughness=Surface.x,metallic=Surface.y,occlusion=1;
    if(features&2u){float3 packed=SurfaceMap.Sample(SurfaceMap_sampler,vertex.UV).rgb;occlusion=lerp(1,packed.r,Surface.z);roughness*=packed.g;metallic*=packed.b;}
    float3 light=normalize(MainLight.xyz);float diffuse=saturate(dot(normal,light));float bands=lerp(0,1,smoothstep(ToonBands.x-ToonBands.w,ToonBands.x+ToonBands.w,diffuse));bands+=lerp(0,1,smoothstep(ToonBands.y-ToonBands.w,ToonBands.y+ToonBands.w,diffuse));bands+=lerp(0,1,smoothstep(ToonBands.z-ToonBands.w,ToonBands.z+ToonBands.w,diffuse));bands/=3;
    float specular=pow(saturate(dot(normal,normalize(light+float3(0,0,1)))),lerp(40,4,roughness));
    float3 emissive=Emission.rgb;if(features&4u)emissive*=EmissiveMap.Sample(EmissiveMap_sampler,vertex.UV).rgb;
    float3 color=base.rgb*(Ambient.rgb*occlusion+MainColor.rgb*MainColor.a*bands);[unroll]for(uint i=0;i<4;++i){float3 delta=LocalPositions[i].xyz-vertex.Local;float d=length(delta);float attenuation=saturate(1-d/max(LocalPositions[i].w,.001));color+=base.rgb*LocalColors[i].rgb*LocalColors[i].a*attenuation*attenuation;}
    if(Features.z==3){float3 coordinates=vertex.Local*Features.w;float3 l1=TerrainLayer1.Sample(TerrainLayer1_sampler,coordinates.xz).rgb,l2=TerrainLayer2.Sample(TerrainLayer2_sampler,coordinates.yz).rgb;float slope=saturate((abs(normal.y)-TerrainParams.y)/max(TerrainParams.z-TerrainParams.y,.001));if(features&32u)color=lerp(color,base.rgb*l1,slope);if(features&64u)color=lerp(color,base.rgb*l2,saturate(TerrainParams.w+(1-slope)*.5));}
    if(Features.z==2){float hit=saturate(CharacterParams.x*CharacterHit.a),status=saturate(CharacterParams.y*CharacterStatus.a);color=lerp(color,CharacterHit.rgb,hit);color=lerp(color,CharacterStatus.rgb,status);if(features&16u){float noise=DissolveMask.Sample(DissolveMask_sampler,vertex.UV).r;float edge=saturate((noise-CharacterParams.z)/max(CharacterParams.w,.001));if(noise<CharacterParams.z)discard;color=lerp(CharacterHit.rgb,color,edge);}}
    if(ShadowParams.z>0){float depth=saturate(vertex.Position.z/max(vertex.Position.w,.0001));uint cascade=depth<ShadowSplits.x?0:depth<ShadowSplits.y?1:depth<ShadowSplits.z?2:3;float4 shadow_position=mul(float4(vertex.WorldPosition,1),ShadowMatrices[cascade]);shadow_position.xyz/=max(shadow_position.w,.0001);float2 uv=shadow_position.xy*.5+.5;float shadow=0;[unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x)shadow+=ShadowMap.SampleCmpLevelZero(ShadowMap_sampler,float3(uv+float2(x,y)*ShadowParams.ww,cascade),shadow_position.z-ShadowParams.x);shadow/=9;color*=lerp(ShadowTint.rgb,1,shadow);}
    if(FogColor.a>0){float distance=max(vertex.Position.z/max(vertex.Position.w,.0001),0);float fog=FogParams.w>0?1-exp(-distance*FogParams.z):saturate((distance-FogParams.x)/max(FogParams.y-FogParams.x,.001));color=lerp(color,FogColor.rgb,saturate(fog));}
    return float4(color+specular*.08*lerp(float3(1,1,1),color,metallic)+emissive,Features.y==2?base.a:1);
}
