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
#ifdef DAE_ANIMATION
#include <darkangel/animation.hpp>
#include <darkangel/animation_assets.hpp>
#endif
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace darkangel::editor_app {
struct ViewArea;
namespace render {
using namespace Diligent;
// The editor's stylized prop shading and the device resources made from cooked models and skins.
// The Frame constants of the shared surface shader (R5 A3): surface (roughness, metallic, occlusion strength, normal scale),
// emission (rgb) and features (bits: 1 normal map, 2 surface map, 4 emissive map; alpha mode; alpha cutoff).
struct Constants {
    DirectX::XMFLOAT4X4 mvp, rotation;
    DirectX::XMFLOAT4 color, surface;
    DirectX::XMFLOAT4 emission{0, 0, 0, 0}, features{0, 0, 0, 0};
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
constexpr const char* shader_source = R"(
cbuffer Frame {row_major float4x4 Mvp;row_major float4x4 Rotation;float4 Tint;float4 Surface;float4 Emission;float4 Features;};
struct VertexInput {float3 Position:ATTRIB0;float3 Normal:ATTRIB1;float2 UV:ATTRIB2;};
struct VertexOutput {float4 Position:SV_POSITION;float3 Normal:TEXCOORD0;float2 UV:TEXCOORD1;float3 Local:TEXCOORD2;};
VertexOutput VSMain(VertexInput vertex){VertexOutput result;result.Position=mul(float4(vertex.Position,1),Mvp);result.Normal=mul(vertex.Normal,(float3x3)Rotation);result.Local=mul(vertex.Position,(float3x3)Rotation);result.UV=vertex.UV;return result;}
Texture2D Albedo;SamplerState Albedo_sampler;
Texture2D NormalMap;SamplerState NormalMap_sampler;
Texture2D SurfaceMap;SamplerState SurfaceMap_sampler;
Texture2D EmissiveMap;SamplerState EmissiveMap_sampler;
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
    if(Features.y==1&&base.a<Features.z)discard;
    float3 normal=normalize(vertex.Normal);
    if(features&1u){float3 mapped=NormalMap.Sample(NormalMap_sampler,vertex.UV).xyz*2-1;mapped.xy*=Surface.w;normal=MappedNormal(normal,vertex.Local,vertex.UV,normalize(mapped));}
    float roughness=Surface.x,metallic=Surface.y,occlusion=1;
    if(features&2u){float3 packed=SurfaceMap.Sample(SurfaceMap_sampler,vertex.UV).rgb;occlusion=lerp(1,packed.r,Surface.z);roughness*=packed.g;metallic*=packed.b;}
    float3 light=normalize(float3(.4,.7,.8));
    float diffuse=saturate(dot(normal,light));float bands=floor(diffuse*3.999)/3;
    float specular=pow(saturate(dot(normal,normalize(light+float3(0,0,1)))),lerp(40,4,roughness));
    float3 emissive=Emission.rgb;if(features&4u)emissive*=EmissiveMap.Sample(EmissiveMap_sampler,vertex.UV).rgb;
    float3 color=base.rgb;
    return float4(color*(.22*occlusion+.78*bands)+specular*.08*lerp(float3(1,1,1),color,metallic)+emissive,Features.y==2?base.a:1);
}
)";
// blend: the transparent variant (alpha blending, depth test without depth writes) of the same layout, so a material's binding
// serves both.
RefCntAutoPtr<IPipelineState> pipeline(IRenderDevice* device, IBuffer* constants, TEXTURE_FORMAT color, TEXTURE_FORMAT depth, bool cull,
                                       const char* source = shader_source, IBuffer* bones = nullptr, bool blend = false);
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
