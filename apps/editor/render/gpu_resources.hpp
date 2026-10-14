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
struct Constants {
    DirectX::XMFLOAT4X4 mvp, rotation;
    DirectX::XMFLOAT4 color, surface;
};
struct GpuMesh {
    RefCntAutoPtr<IBuffer> vertices, indices;
    std::vector<MeshPart> parts;
    std::array<float, 4> color{1, 1, 1, 1};
};
struct Material {
    CookedMaterial value;
    RefCntAutoPtr<IShaderResourceBinding> binding;
};
struct UploadedModel {
    std::vector<GpuMesh> meshes;
    std::map<AssetId, RefCntAutoPtr<ITexture>> textures;
    std::map<AssetId, Material> materials;
    RefCntAutoPtr<ITexture> fallback;
    std::array<float, 3> center{}, half{};
    float radius{};
};
constexpr const char* shader_source = R"(
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
)";
RefCntAutoPtr<IPipelineState> pipeline(IRenderDevice* device, IBuffer* constants, TEXTURE_FORMAT color, TEXTURE_FORMAT depth, bool cull,
                                       const char* source = shader_source, IBuffer* bones = nullptr);
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
