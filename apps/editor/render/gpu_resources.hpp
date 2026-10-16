#pragma once
#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>
#include <DeviceContext.h>
#include <Buffer.h>
#include <Texture.h>
#include <PipelineState.h>
#include <ShaderResourceBinding.h>
#include <DirectXMath.h>
#include <darkangel/assets.hpp>
#include <darkangel/rendering.hpp>
#ifdef DAE_ANIMATION
#include <darkangel/animation.hpp>
#include <darkangel/animation_assets.hpp>
#endif
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <span>

namespace darkangel::editor_app {
struct ViewArea;
namespace render {
using namespace Diligent;
// The editor's stylized prop shading and the device resources made from cooked models and skins.
// The Frame constants of the shared surface shader (R5 A3): surface (roughness, metallic, occlusion strength, normal scale),
// emission (rgb) and features (bits: 1 normal map, 2 surface map, 4 emissive map; alpha mode; alpha cutoff).
struct Constants {
    DirectX::XMFLOAT4X4 mvp, rotation, world;
    DirectX::XMFLOAT4 color, surface;
    DirectX::XMFLOAT4 emission{0, 0, 0, 0}, features{0, 0, 0, 0}, alpha{.5f, 0, 0, 0};
    DirectX::XMFLOAT4 main_light{.4f, .7f, .8f, 1}, main_color{1, 1, 1, 1}, ambient{.22f, .25f, .34f, 1};
    DirectX::XMFLOAT4 toon_bands{.18f, .52f, .82f, .035f}, shadow_tint{.22f, .25f, .38f, 1};
    DirectX::XMFLOAT4 local_positions[4]{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
    DirectX::XMFLOAT4 local_colors[4]{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
    DirectX::XMFLOAT4 local_directions[4]{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
    DirectX::XMFLOAT4 local_cones[4]{{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}};
    DirectX::XMFLOAT4 character_hit{1,1,1,0},character_status{1,1,1,0},character_params{0,0,0,.05f};
    DirectX::XMFLOAT4 terrain_params{4,.35f,.75f,.5f};
    DirectX::XMFLOAT4 fog_color{.35f,.42f,.52f,0},fog_params{20,180,.015f,0};
    DirectX::XMFLOAT4 shadow_splits{.2f,.45f,.7f,1};
    DirectX::XMFLOAT4 shadow_params{.001f,.002f,0,0};
    DirectX::XMFLOAT4X4 shadow_matrices[4]{};
    DirectX::XMFLOAT4 outline{};
};
struct PostConstants {
    DirectX::XMFLOAT4 exposure{0,0,1,1};
    DirectX::XMFLOAT4 bloom{1,.5f,.35f,4};
    DirectX::XMFLOAT4 tint_fxaa{1,1,1,1};
    DirectX::XMFLOAT4 texel{};
    DirectX::XMFLOAT4 outline{.025f,.015f,.04f,1};
    DirectX::XMFLOAT4 outline_params{1,80,1.5f,1};
    DirectX::XMFLOAT4 outline_fade{20,180,0,0};
    DirectX::XMFLOAT4 sky_zenith{.06f,.10f,.22f,0};
    DirectX::XMFLOAT4 sky_horizon{.42f,.30f,.24f,0};
    DirectX::XMFLOAT4 sky_ground{.025f,.02f,.03f,0};
    DirectX::XMFLOAT4 sky_sun{-.35f,.7f,-.55f,0};
    DirectX::XMFLOAT4 sky_sun_color{1,.75f,.45f,0};
};
// Sets a draw's surface, emission and feature constants from its material (colour is set by the caller, which may tint it).
void apply_material(Constants&, const CookedMaterial&);
// Binds a material's textures to a binding of the surface pipelines: Albedo is required; the optional maps are bound when the
// shader declares them (an older game shader may not) and fall back to `fallback`, which the feature bits never sample.
void bind_surface(IShaderResourceBinding*, const CookedMaterial&, const std::map<AssetId, RefCntAutoPtr<ITexture>>&, ITexture* fallback);
struct GpuMesh {
    RefCntAutoPtr<IBuffer> vertices, indices;
    std::vector<MeshPart> parts;
    std::array<float, 4> color{1, 1, 1, 1};
};
struct Material {
    CookedMaterial value;
    RefCntAutoPtr<IShaderResourceBinding> binding;
};
// A native instance's bindings/textures, independent of model geometry. The same textures serve static and skin pipelines.
struct UploadedMaterial {
    RuntimeMaterial source;
    Material surface,skinned;
    std::map<AssetId,RefCntAutoPtr<ITexture>> textures;
    RefCntAutoPtr<ITexture> fallback;
};
UploadedMaterial upload_material(IRenderDevice*,const RuntimeMaterial&,IPipelineState*,IPipelineState*,IPipelineState* skin=nullptr,IPipelineState* skin_cull=nullptr);
struct UploadedModel {
    std::vector<GpuMesh> meshes;
    std::map<AssetId, RefCntAutoPtr<ITexture>> textures;
    std::map<AssetId, Material> materials;
    RefCntAutoPtr<ITexture> fallback;
    std::array<float, 3> center{}, half{};
    float radius{};
};
struct ClusterGpuLight { DirectX::XMFLOAT4 position_range{}; DirectX::XMFLOAT4 metadata{}; };
struct ClusterGpuFrame { std::array<std::uint32_t, 4> dimensions{}; std::array<std::uint32_t, 4> limits{}; DirectX::XMFLOAT4 camera{}; };
struct GpuClusterResources {
    RefCntAutoPtr<IBuffer> frame, lights, candidates, selected, counts, stats, stats_readback;
    RefCntAutoPtr<IPipelineState> pipeline;
    RefCntAutoPtr<IShaderResourceBinding> binding;
    unsigned tiles_x{}, tiles_y{}, slices{}, max_candidates{}, shading_budget{}, light_capacity{};
    std::size_t cluster_count{};
    bool readback_pending{};
};
void ensure_gpu_clusters(IRenderDevice*, IDeviceContext*, GpuClusterResources&, unsigned tiles_x, unsigned tiles_y, unsigned slices,
                         unsigned max_candidates, unsigned shading_budget, unsigned light_capacity);
void dispatch_gpu_clusters(IDeviceContext*, GpuClusterResources&, std::span<const ClusterGpuLight>, const ClusterGpuFrame&);
bool read_gpu_cluster_stats(IDeviceContext*, GpuClusterResources&, rendering::ClusterGpuStats&);
constexpr const char* shader_source = R"(
    cbuffer Frame {row_major float4x4 Mvp;row_major float4x4 Rotation;row_major float4x4 World;float4 Tint;float4 Surface;float4 Emission;float4 Features;float4 MainLight;float4 MainColor;float4 Ambient;float4 ToonBands;float4 ShadowTint;float4 LocalPositions[4];float4 LocalColors[4];float4 LocalDirections[4];float4 LocalCones[4];float4 CharacterHit;float4 CharacterStatus;float4 CharacterParams;float4 TerrainParams;float4 FogColor;float4 FogParams;float4 ShadowSplits;float4 ShadowParams;row_major float4x4 ShadowMatrices[4];float4 Outline;};
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
// Features: x feature bits (1 normal map, 2 surface map, 4 emissive map), y alpha mode, z toon family, w triplanar scale.
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
    float3 color=base.rgb*(Ambient.rgb*occlusion+MainColor.rgb*MainColor.a*bands);[unroll]for(uint i=0;i<4;++i){float3 delta=LocalPositions[i].xyz-vertex.Local;float d=length(delta);float attenuation=saturate(1-d/max(LocalPositions[i].w,.001));float spot=1;if(LocalDirections[i].w>0){float cone=dot(-normalize(delta),normalize(LocalDirections[i].xyz));spot=smoothstep(LocalCones[i].x,LocalCones[i].y,cone);spot=pow(spot,max(LocalCones[i].z,.1));}color+=base.rgb*LocalColors[i].rgb*LocalColors[i].a*attenuation*attenuation*spot;}
    if(Features.z==3){float3 coordinates=vertex.Local*Features.w;float3 l1=TerrainLayer1.Sample(TerrainLayer1_sampler,coordinates.xz).rgb,l2=TerrainLayer2.Sample(TerrainLayer2_sampler,coordinates.yz).rgb;float slope=saturate((abs(normal.y)-TerrainParams.y)/max(TerrainParams.z-TerrainParams.y,.001));if(features&32u)color=lerp(color,base.rgb*l1,slope);if(features&64u)color=lerp(color,base.rgb*l2,saturate(TerrainParams.w+(1-slope)*.5));}
    if(Features.z==2){float hit=saturate(CharacterParams.x*CharacterHit.a),status=saturate(CharacterParams.y*CharacterStatus.a);color=lerp(color,CharacterHit.rgb,hit);color=lerp(color,CharacterStatus.rgb,status);if(features&16u){float noise=DissolveMask.Sample(DissolveMask_sampler,vertex.UV).r;float edge=saturate((noise-CharacterParams.z)/max(CharacterParams.w,.001));if(noise<CharacterParams.z)discard;color=lerp(CharacterHit.rgb,color,edge);}}
    if(Outline.x>0) return float4(normalize(normal)*.5+.5,Outline.y);
    if(ShadowParams.z>0){float depth=saturate(vertex.Position.z/max(vertex.Position.w,.0001));uint cascade_count=max(1,(uint)ShadowParams.z);uint cascade=min(cascade_count-1,depth<ShadowSplits.x?0:depth<ShadowSplits.y?1:depth<ShadowSplits.z?2:3);float4 shadow_position=mul(float4(vertex.WorldPosition,1),ShadowMatrices[cascade]);shadow_position.xyz/=max(shadow_position.w,.0001);float2 uv=shadow_position.xy*.5+.5;float shadow=0;[unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x)shadow+=ShadowMap.SampleCmpLevelZero(ShadowMap_sampler,float3(uv+float2(x,y)*ShadowParams.ww,cascade),shadow_position.z-ShadowParams.x-ShadowParams.y);shadow/=9;color*=lerp(ShadowTint.rgb,1,shadow);}
    if(FogColor.a>0){float distance=max(vertex.Position.z/max(vertex.Position.w,.0001),0);float fog=FogParams.w>0?1-exp(-distance*FogParams.z):saturate((distance-FogParams.x)/max(FogParams.y-FogParams.x,.001));color=lerp(color,FogColor.rgb,saturate(fog));}
    return float4(color+specular*.08*lerp(float3(1,1,1),color,metallic)+emissive,Features.y==2?base.a:1);
}
)";
// blend: the transparent variant (alpha blending, depth test without depth writes) of the same layout, so a material's binding
// serves both.
RefCntAutoPtr<IPipelineState> pipeline(IRenderDevice* device, IBuffer* constants, TEXTURE_FORMAT color, TEXTURE_FORMAT depth, bool cull,
                                       const char* source = shader_source, IBuffer* bones = nullptr, bool blend = false);
RefCntAutoPtr<IPipelineState> post_pipeline(IRenderDevice*, IBuffer*, TEXTURE_FORMAT color);
RefCntAutoPtr<IPipelineState> shadow_pipeline(IRenderDevice*, IBuffer*, TEXTURE_FORMAT depth, bool cull, const char* source = nullptr, IBuffer* bones = nullptr);
RefCntAutoPtr<IPipelineState> alpha_shadow_pipeline(IRenderDevice*, IBuffer*, TEXTURE_FORMAT depth, bool cull, const char* source = nullptr, IBuffer* bones = nullptr);
RefCntAutoPtr<ITexture> texture(IRenderDevice* device, const CookedTexture& cooked);
// Reads the viewport back, requires foreground geometry inside the viewport area and writes a PNG.
void capture(IRenderDevice* device, IDeviceContext* context, ITexture* source, const std::filesystem::path& path, const ViewArea& viewport);
UploadedModel upload(IRenderDevice* device, const RuntimeModel& model, IPipelineState* pso, IPipelineState* pso_cull);
// The game's prop shader source when present, otherwise the built-in one.
std::string shader_text();
#ifdef DAE_ANIMATION
std::string skin_shader();
struct UploadedSkin {
    RuntimeSkinnedModel model;
    UploadedModel gpu;
    std::unique_ptr<RigPose> pose;
};
UploadedSkin upload_skin(IRenderDevice* device, RuntimeSkinnedModel model, IPipelineState* pso, IPipelineState* cull,
                         const AnimationClip* clip = nullptr, double tick = 0, bool reference = false);
void upload_palette(IDeviceContext* context, IBuffer* buffer, UploadedSkin& skin, const AnimationClip* clip, double tick,
                    const std::vector<JointMatrix>* gameplay = nullptr);
#endif
} // namespace render
} // namespace darkangel::editor_app
