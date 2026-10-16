#define NOMINMAX
#define UNICODE
#include <Windows.h>
#include <wincodec.h>
#include "gpu_resources.hpp"
#include "../editor_ui.hpp"
#include <darkangel/check.hpp>
#include <DirectXTex.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace darkangel::editor_app::render {
RefCntAutoPtr<IPipelineState> pipeline(IRenderDevice* device, IBuffer* constants, TEXTURE_FORMAT color, TEXTURE_FORMAT depth, bool cull,
                                       const char* source, IBuffer* bones, bool blend) {
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.ShaderCompiler = SHADER_COMPILER_DXC;
    shader.HLSLVersion = {6, 0};
    shader.Desc.UseCombinedTextureSamplers = true;
    shader.Source = source;
    RefCntAutoPtr<IShader> vs, ps;
    shader.Desc.Name = "DarkAngel stylized prop VS";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.EntryPoint = "VSMain";
    device->CreateShader(shader, &vs);
    shader.Desc.Name = "DarkAngel stylized prop PS";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.EntryPoint = "PSMain";
    device->CreateShader(shader, &ps);
    require(vs && ps, "Pinned DXC shader compilation failed");
    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = "DarkAngel stylized static prop";
    ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    ci.pVS = vs;
    ci.pPS = ps;
    auto& graphics = ci.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = color;
    graphics.DSVFormat = depth;
    graphics.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode = cull ? CULL_MODE_BACK : CULL_MODE_NONE;
    graphics.RasterizerDesc.FrontCounterClockwise = true;
    graphics.DepthStencilDesc.DepthEnable = true;
    graphics.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS;
    if (blend) {
        graphics.DepthStencilDesc.DepthWriteEnable = false;
        auto& target = graphics.BlendDesc.RenderTargets[0];
        target.BlendEnable = true;
        target.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
        target.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
        target.SrcBlendAlpha = BLEND_FACTOR_ONE;
        target.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
    }
    LayoutElement layout[] = {{0, 0, 3, VT_FLOAT32, false},
                              {1, 0, 3, VT_FLOAT32, false},
                              {2, 0, 2, VT_FLOAT32, false},
                              {3, 0, 4, VT_UINT32, false},
                              {4, 0, 4, VT_FLOAT32, false}};
    graphics.InputLayout.LayoutElements = layout;
    graphics.InputLayout.NumElements = bones ? 5 : 3;
    ShaderResourceVariableDesc variables[] = {{SHADER_TYPE_PIXEL, "Albedo", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "NormalMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "SurfaceMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "EmissiveMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "DissolveMask", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "TerrainLayer1", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "TerrainLayer2", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};
    ci.PSODesc.ResourceLayout.Variables = variables;
    ci.PSODesc.ResourceLayout.NumVariables = _countof(variables);
    SamplerDesc sampler;
    sampler.MinFilter = FILTER_TYPE_LINEAR;
    sampler.MagFilter = FILTER_TYPE_LINEAR;
    sampler.MipFilter = FILTER_TYPE_LINEAR;
    sampler.AddressU = TEXTURE_ADDRESS_WRAP;
    sampler.AddressV = TEXTURE_ADDRESS_WRAP;
    ImmutableSamplerDesc samplers[] = {{SHADER_TYPE_PIXEL, "Albedo", sampler},
                                       {SHADER_TYPE_PIXEL, "NormalMap", sampler},
                                       {SHADER_TYPE_PIXEL, "SurfaceMap", sampler},
                                       {SHADER_TYPE_PIXEL, "EmissiveMap", sampler},
                                       {SHADER_TYPE_PIXEL, "DissolveMask", sampler},
                                       {SHADER_TYPE_PIXEL, "TerrainLayer1", sampler},
                                       {SHADER_TYPE_PIXEL, "TerrainLayer2", sampler}};
    ci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
    ci.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);
    RefCntAutoPtr<IPipelineState> result;
    device->CreateGraphicsPipelineState(ci, &result);
    require(result, "Pipeline creation failed");
    auto* vertex_frame = result->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Frame");
    auto* pixel_frame = result->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Frame");
    require(vertex_frame && pixel_frame, "Shader changed the declared Frame resource contract");
    vertex_frame->Set(constants);
    pixel_frame->Set(constants);
    if (bones) {
        auto* skeleton = result->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Skeleton");
        require(skeleton, "Missing GPU skin palette contract");
        skeleton->Set(bones);
    }
    return result;
}
RefCntAutoPtr<IPipelineState> post_pipeline(IRenderDevice* device, IBuffer* constants, TEXTURE_FORMAT color) {
    static constexpr const char* source = R"(
cbuffer PostFrame {float4 Exposure;float4 Bloom;float4 TintFxaa;float4 Texel;};
Texture2D SceneColor;SamplerState SceneColor_sampler;
struct VertexOutput {float4 Position:SV_POSITION;float2 UV:TEXCOORD0;};
VertexOutput VSMain(uint id:SV_VertexID){float2 positions[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};float2 uv[3]={float2(0,1),float2(0,-1),float2(2,1)};VertexOutput result;result.Position=float4(positions[id],0,1);result.UV=uv[id];return result;}
float luminance(float3 v){return dot(v,float3(.2126,.7152,.0722));}
float3 hdr(float2 uv){return max(SceneColor.Sample(SceneColor_sampler,uv).rgb,0);}
float3 tone(float3 value){float exposure=exp2(Exposure.x+Exposure.y);float3 color=1-exp(-value*exposure);float l=luminance(color);color=lerp(l.xxx,color,Exposure.w);color=(color-.5)*Exposure.z+.5;return saturate(color*TintFxaa.rgb);}
float4 PSMain(VertexOutput vertex):SV_TARGET {float3 source=hdr(vertex.UV);float3 bloom=0;if(Bloom.z>0){float2 radius=Texel.xy*max(Bloom.w,1);float3 samples[5]={source,hdr(vertex.UV+float2(radius.x,0)),hdr(vertex.UV-float2(radius.x,0)),hdr(vertex.UV+float2(0,radius.y)),hdr(vertex.UV-float2(0,radius.y))};[unroll]for(uint i=0;i<5;++i){float l=luminance(samples[i]);float threshold=max(Bloom.x,0);float soft=max(Bloom.y,.001);float weight=smoothstep(threshold,threshold+soft,l);bloom+=samples[i]*weight;}bloom=bloom*(Bloom.z/5);source+=bloom;}float3 color=tone(source);if(TintFxaa.a>0){float3 left=tone(hdr(vertex.UV-float2(Texel.x,0))),right=tone(hdr(vertex.UV+float2(Texel.x,0))),up=tone(hdr(vertex.UV+float2(0,Texel.y))),down=tone(hdr(vertex.UV-float2(0,Texel.y)));float range=max(max(luminance(left),luminance(right)),max(luminance(up),luminance(down)))-min(min(luminance(left),luminance(right)),min(luminance(up),luminance(down)));if(range>.08)color=(color+left+right+up+down)/5;}return float4(color,1);}
)";
    ShaderCreateInfo shader;shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_HLSL;shader.ShaderCompiler=SHADER_COMPILER_DXC;shader.HLSLVersion={6,0};shader.Desc.UseCombinedTextureSamplers=true;shader.Source=source;RefCntAutoPtr<IShader> vs,ps;shader.Desc.Name="DarkAngel post VS";shader.Desc.ShaderType=SHADER_TYPE_VERTEX;shader.EntryPoint="VSMain";device->CreateShader(shader,&vs);shader.Desc.Name="DarkAngel post PS";shader.Desc.ShaderType=SHADER_TYPE_PIXEL;shader.EntryPoint="PSMain";device->CreateShader(shader,&ps);require(vs&&ps,"Post-process shader compilation failed");
    GraphicsPipelineStateCreateInfo ci;ci.PSODesc.Name="DarkAngel HDR post-process";ci.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;ci.pVS=vs;ci.pPS=ps;auto& graphics=ci.GraphicsPipeline;graphics.NumRenderTargets=1;graphics.RTVFormats[0]=color;graphics.DSVFormat=TEX_FORMAT_UNKNOWN;graphics.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;graphics.RasterizerDesc.CullMode=CULL_MODE_NONE;graphics.DepthStencilDesc.DepthEnable=false;graphics.InputLayout.NumElements=0;ShaderResourceVariableDesc variable{SHADER_TYPE_PIXEL,"SceneColor",SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE};ci.PSODesc.ResourceLayout.Variables=&variable;ci.PSODesc.ResourceLayout.NumVariables=1;SamplerDesc sampler;sampler.MinFilter=FILTER_TYPE_LINEAR;sampler.MagFilter=FILTER_TYPE_LINEAR;sampler.MipFilter=FILTER_TYPE_LINEAR;sampler.AddressU=TEXTURE_ADDRESS_CLAMP;sampler.AddressV=TEXTURE_ADDRESS_CLAMP;ImmutableSamplerDesc immutable{SHADER_TYPE_PIXEL,"SceneColor",sampler};ci.PSODesc.ResourceLayout.ImmutableSamplers=&immutable;ci.PSODesc.ResourceLayout.NumImmutableSamplers=1;RefCntAutoPtr<IPipelineState> result;device->CreateGraphicsPipelineState(ci,&result);require(result,"Post-process pipeline creation failed");auto* pixel_frame=result->GetStaticVariableByName(SHADER_TYPE_PIXEL,"PostFrame");require(pixel_frame,"Post-process constant contract changed");pixel_frame->Set(constants);return result;
}
RefCntAutoPtr<ITexture> texture(IRenderDevice* device, const CookedTexture& cooked) {
    require(!cooked.mips.empty(), "Missing texture mips");
    TextureDesc desc;
    desc.Name = cooked.srgb ? "Cooked DarkAngel sRGB color" : "Cooked DarkAngel linear data";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = cooked.mips[0].width;
    desc.Height = cooked.mips[0].height;
    desc.MipLevels = static_cast<Uint32>(cooked.mips.size());
    // Colour textures are sampled with sRGB decoding; data and normal maps (R5 A3) are linear.
    desc.Format = cooked.srgb ? TEX_FORMAT_RGBA8_UNORM_SRGB : TEX_FORMAT_RGBA8_UNORM;
    desc.Usage = USAGE_IMMUTABLE;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    std::vector<TextureSubResData> subresources;
    for (const auto& mip : cooked.mips) {
        TextureSubResData data;
        data.pData = mip.rgba.data();
        data.Stride = mip.width * 4;
        subresources.push_back(data);
    }
    TextureData data;
    data.pSubResources = subresources.data();
    data.NumSubresources = static_cast<Uint32>(subresources.size());
    RefCntAutoPtr<ITexture> result;
    device->CreateTexture(desc, &data, &result);
    require(result, "Cooked texture upload failed");
    return result;
}
void capture(IRenderDevice* device, IDeviceContext* context, ITexture* source, const std::filesystem::path& path,
             const darkangel::editor_app::ViewArea& viewport) {
    TextureDesc desc = source->GetDesc();
    desc.Name = "DarkAngel capture readback";
    desc.BindFlags = BIND_NONE;
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    require(staging, "Readback texture creation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    CopyTextureAttribs copy{source, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    context->CopyTexture(copy);
    context->Flush();
    context->WaitForIdle();
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    require(mapped.pData, "GPU readback failed");
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + static_cast<std::size_t>(y) * desc.Width * 4,
                    static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.Stride, desc.Width * 4);
    context->UnmapTextureSubresource(staging, 0, 0);
    // The viewport area excludes the inspector; UI pixels alone cannot satisfy this gate.
    std::size_t changed{};
    auto left = static_cast<unsigned>(std::clamp(viewport.x, 0.f, static_cast<float>(desc.Width - 1))),
         top = static_cast<unsigned>(std::clamp(viewport.y, 0.f, static_cast<float>(desc.Height - 1)));
    auto right = static_cast<unsigned>(std::clamp(viewport.x + viewport.width, 0.f, static_cast<float>(desc.Width))),
         bottom = static_cast<unsigned>(std::clamp(viewport.y + viewport.height, 0.f, static_cast<float>(desc.Height)));
    require(right > left + 40 && bottom > top + 40, "Viewport too small for capture gate");
    auto ref = (static_cast<std::size_t>(top + 10) * desc.Width + left + 10) * 4;
    for (unsigned y = top + 20; y + 20 < bottom; ++y)
        for (unsigned x = left + 20; x + 20 < right; ++x) {
            auto at = (static_cast<std::size_t>(y) * desc.Width + x) * 4;
            unsigned delta{};
            for (unsigned c = 0; c < 3; ++c)
                delta += static_cast<unsigned>(std::abs(static_cast<int>(pixels[at + c]) - pixels[ref + c]));
            if (delta > 40)
                ++changed;
        }
    require(changed > 2000, "Viewport did not draw foreground geometry");
    std::filesystem::create_directories(path.parent_path());
    DirectX::Image image{desc.Width,    desc.Height,  DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<std::size_t>(desc.Width) * 4,
                         pixels.size(), pixels.data()}; // Encoded in memory and then written, trying again for two seconds: another program
                                                        // (a thumbnailer, an indexer) may briefly hold a capture that was just replaced.
    DirectX::Blob encoded;
    require(SUCCEEDED(DirectX::SaveToWICMemory(image, DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, encoded)), "PNG capture failed");
    bool written = false;
    for (unsigned attempt = 0; attempt < 40 && !written; ++attempt) {
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (out) {
                out.write(reinterpret_cast<const char*>(encoded.GetBufferPointer()), static_cast<std::streamsize>(encoded.GetBufferSize()));
                out.close();
                written = !out.fail();
            }
        }
        if (!written)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    require(written, "The capture could not be written: its file is held by another program");
    std::cout << "Viewport foreground pixels=" << changed << " capture=" << path.string() << '\n';
}
void apply_material(Constants& constants, const CookedMaterial& material) {
    constants.surface = {material.roughness, material.metallic, material.occlusion_strength, material.normal_scale};
    constants.emission = {material.emissive[0], material.emissive[1], material.emissive[2], 0};
    const unsigned bits = (material.has_normal ? 1u : 0u) | (material.has_surface ? 2u : 0u) | (material.has_emissive_texture ? 4u : 0u) | (material.triplanar ? 8u : 0u) | (material.has_dissolve_texture ? 16u : 0u) | (material.has_terrain_layer1 ? 32u : 0u) | (material.has_terrain_layer2 ? 64u : 0u);
    const float family=material.family==material_family_toon_character?2.f:material.family==material_family_toon_terrain?3.f:material.family==material_family_toon_standard?1.f:0.f;
    constants.features = {static_cast<float>(bits), static_cast<float>(material.alpha_mode), family, material.triplanar_scale};
    constants.toon_bands={material.toon_thresholds[0],material.toon_thresholds[1],material.toon_thresholds[2],material.toon_softness};constants.shadow_tint={material.shadow_tint[0],material.shadow_tint[1],material.shadow_tint[2],1};constants.ambient={material.ambient_color[0]*material.ambient_strength,material.ambient_color[1]*material.ambient_strength,material.ambient_color[2]*material.ambient_strength,1};
    constants.character_hit={material.hit_flash_color[0],material.hit_flash_color[1],material.hit_flash_color[2],material.hit_flash_color[3]};constants.character_status={material.status_color[0],material.status_color[1],material.status_color[2],material.status_color[3]};constants.character_params={material.hit_flash_strength,material.status_strength,material.dissolve,material.dissolve_edge_width};constants.terrain_params={material.terrain_blend_sharpness,material.terrain_slope_start,material.terrain_slope_end,material.terrain_height_blend};
}
void bind_surface(IShaderResourceBinding* binding, const CookedMaterial& material, const std::map<AssetId, RefCntAutoPtr<ITexture>>& textures,
                  ITexture* fallback) {
    auto view = [&](bool used, AssetId id) {
        return (used ? textures.at(id).RawPtr() : fallback)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    };
    auto* albedo = binding->GetVariableByName(SHADER_TYPE_PIXEL, "Albedo");
    require(albedo, "Shader changed the declared Albedo resource contract");
    albedo->Set(view(material.has_texture, material.texture));
    if (auto* normal = binding->GetVariableByName(SHADER_TYPE_PIXEL, "NormalMap"))
        normal->Set(view(material.has_normal, material.normal_texture));
    else
        require(!material.has_normal, "The prop shader does not declare NormalMap, which this material needs");
    if (auto* surface = binding->GetVariableByName(SHADER_TYPE_PIXEL, "SurfaceMap"))
        surface->Set(view(material.has_surface, material.surface_texture));
    else
        require(!material.has_surface, "The prop shader does not declare SurfaceMap, which this material needs");
    if (auto* emissive = binding->GetVariableByName(SHADER_TYPE_PIXEL, "EmissiveMap"))
        emissive->Set(view(material.has_emissive_texture, material.emissive_texture));
    else
        require(!material.has_emissive_texture, "The prop shader does not declare EmissiveMap, which this material needs");
    if (auto* dissolve = binding->GetVariableByName(SHADER_TYPE_PIXEL, "DissolveMask"))
        dissolve->Set(view(material.has_dissolve_texture, material.dissolve_texture));
    else
        require(!material.has_dissolve_texture, "The prop shader does not declare DissolveMask, which this material needs");
    if (auto* layer1 = binding->GetVariableByName(SHADER_TYPE_PIXEL, "TerrainLayer1"))
        layer1->Set(view(material.has_terrain_layer1, material.terrain_layer1));
    else
        require(!material.has_terrain_layer1, "The prop shader does not declare TerrainLayer1, which this material needs");
    if (auto* layer2 = binding->GetVariableByName(SHADER_TYPE_PIXEL, "TerrainLayer2"))
        layer2->Set(view(material.has_terrain_layer2, material.terrain_layer2));
    else
        require(!material.has_terrain_layer2, "The prop shader does not declare TerrainLayer2, which this material needs");
}
UploadedMaterial upload_material(IRenderDevice* device,const RuntimeMaterial& material,IPipelineState* pso,IPipelineState* cull,IPipelineState* skin,IPipelineState* skin_cull) {
    UploadedMaterial out;out.source=material;CookedTexture white;white.mips.push_back({1,1,{255,255,255,255}});out.fallback=texture(device,white);
    for(const auto& [id,cooked]:material.textures)out.textures.emplace(id,texture(device,cooked));
    auto bind=[&](Material& destination,IPipelineState* double_sided,IPipelineState* single_sided){destination.value=material.value;auto* chosen=material.value.double_sided?double_sided:single_sided;require(chosen,"Missing material pipeline");chosen->CreateShaderResourceBinding(&destination.binding,true);require(destination.binding,"Native material binding failed");
        bind_surface(destination.binding,material.value,out.textures,out.fallback);};
    bind(out.surface,pso,cull);if(skin)bind(out.skinned,skin,skin_cull);return out;
}
UploadedModel upload(IRenderDevice* device, const RuntimeModel& model, IPipelineState* pso, IPipelineState* pso_cull) {
    require(!model.meshes.empty(), "Empty cooked model");
    UploadedModel out;
    auto minimum = model.meshes[0].minimum, maximum = model.meshes[0].maximum;
    for (const auto& mesh : model.meshes) {
        GpuMesh gpu;
        gpu.parts = mesh.parts;
        BufferDesc desc;
        desc.Name = "Cooked static vertices";
        desc.Usage = USAGE_IMMUTABLE;
        desc.BindFlags = BIND_VERTEX_BUFFER;
        desc.Size = mesh.vertices.size() * sizeof(Vertex);
        BufferData data{mesh.vertices.data(), desc.Size};
        device->CreateBuffer(desc, &data, &gpu.vertices);
        desc.Name = "Cooked static indices";
        desc.BindFlags = BIND_INDEX_BUFFER;
        desc.Size = mesh.indices.size() * sizeof(std::uint32_t);
        data = {mesh.indices.data(), desc.Size};
        device->CreateBuffer(desc, &data, &gpu.indices);
        require(gpu.vertices && gpu.indices, "Mesh upload failed");
        out.meshes.push_back(std::move(gpu));
        for (unsigned c = 0; c < 3; ++c) {
            minimum[c] = std::min(minimum[c], mesh.minimum[c]);
            maximum[c] = std::max(maximum[c], mesh.maximum[c]);
        }
    }
    CookedTexture white;
    white.mips.push_back({1, 1, {255, 255, 255, 255}});
    out.fallback = texture(device, white);
    for (const auto& [id, cooked] : model.textures)
        out.textures.emplace(id, texture(device, cooked));
    for (const auto& material : model.materials) {
        Material gpu;
        gpu.value = material;
        auto* chosen = material.double_sided ? pso : pso_cull;
        chosen->CreateShaderResourceBinding(&gpu.binding, true);
        require(gpu.binding, "Material binding creation failed");
        bind_surface(gpu.binding, material, out.textures, out.fallback);
        out.materials.emplace(material.id, std::move(gpu));
    }
    for (unsigned c = 0; c < 3; ++c) {
        out.center[c] = (minimum[c] + maximum[c]) * .5f;
        out.half[c] = (maximum[c] - minimum[c]) * .5f;
        out.radius = std::max(out.radius, out.half[c]);
    }
    require(out.radius > 1e-6f, "Model has zero bounds");
    return out;
}
std::string shader_text() {
    std::ifstream in("games/AshenRoots/shaders/stylized_prop.hlsl", std::ios::binary | std::ios::ate);
    if (!in)
        return shader_source;
    require(in.tellg() >= 0 && in.tellg() <= 256 * 1024, "Shader source byte limit");
    in.seekg(0);
    return {std::istreambuf_iterator<char>(in), {}};
}

#ifdef DAE_ANIMATION
std::string skin_shader() {
    std::string source = shader_source;
    auto replace = [&](std::string_view from, std::string_view to) {
        auto at = source.find(from);
        require(at != source.npos, "Native skin shader template mismatch");
        source.replace(at, from.size(), to);
    };
    replace("struct VertexInput", "cbuffer Skeleton {row_major float4x4 Bones[256];};\nstruct VertexInput");
    replace("float2 UV:ATTRIB2;", "float2 UV:ATTRIB2;uint4 Joints:ATTRIB3;float4 Weights:ATTRIB4;");
    replace("result.Position=mul(float4(vertex.Position,1),Mvp);result.Normal=mul(vertex.Normal,(float3x3)Rotation);result.Local=mul(vertex.Position,(float3x3)Rotation);",
            "float4 position=0;float3 normal=0;[unroll]for(uint "
            "i=0;i<4;++i){position+=mul(float4(vertex.Position,1),Bones[vertex.Joints[i]])*vertex.Weights[i];normal+=mul(vertex.Normal,("
            "float3x3)Bones[vertex.Joints[i]])*vertex.Weights[i];}result.Position=mul(position,Mvp);result.Normal=mul(normal,(float3x3)"
            "Rotation);result.Local=mul(position.xyz,(float3x3)Rotation);");
    return source;
}
UploadedSkin upload_skin(IRenderDevice* device, RuntimeSkinnedModel model, IPipelineState* pso, IPipelineState* cull,
                         const AnimationClip* clip, double tick, bool reference) {
    RigPose reference_pose(model.rig.definition, model.rig.archive);
    const auto& sampled = clip ? reference_pose.sample(*clip, tick) : reference_pose.rest_pose();
    std::vector<std::array<float, 16>> matrices(sampled.size());
    for (unsigned joint = 0; joint < sampled.size(); ++joint)
        for (unsigned col = 0; col < 4; ++col)
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned k = 0; k < 4; ++k)
                    matrices[joint][col * 4 + row] += sampled[joint].values[k * 4 + row] * model.inverse_bind[joint][col * 4 + k];
    static_assert(sizeof(SkinnedVertex) == 64);
    require(model.inverse_bind.size() <= 256, "GPU skin joint limit");
    RuntimeModel visible;
    visible.id = model.id;
    CookedMaterial material;
    material.id = model.id;
    material.double_sided = true;
    material.roughness = .85f;
    material.has_texture = model.textured;
    material.texture = model.id;
    visible.materials.push_back(material);
    if (model.textured)
        visible.textures.emplace_back(model.id, model.texture);
    // A primitive with an assigned material instance draws with it (R5 A3 skinned parity); the others share the atlas.
    for (const auto& skin : model.meshes)
        if (skin.material)
            visible.materials.push_back(*skin.material);
    for (const auto& texture : model.textures)
        visible.textures.push_back(texture);
    for (const auto& skin : model.meshes) {
        CookedMesh mesh;
        mesh.indices = skin.indices;
        mesh.minimum = {1e9f, 1e9f, 1e9f};
        mesh.maximum = {-1e9f, -1e9f, -1e9f};
        for (const auto& vertex : skin.vertices) {
            auto geometry = vertex.geometry;
            if (reference) {
                geometry.position = {};
                geometry.normal = {};
                for (unsigned influence = 0; influence < 4; ++influence) {
                    const auto& matrix = matrices[vertex.joints[influence]];
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        float position = matrix[12 + axis], normal = 0;
                        for (unsigned component = 0; component < 3; ++component) {
                            position += matrix[component * 4 + axis] * vertex.geometry.position[component];
                            normal += matrix[component * 4 + axis] * vertex.geometry.normal[component];
                        }
                        geometry.position[axis] += position * vertex.weights[influence];
                        geometry.normal[axis] += normal * vertex.weights[influence];
                    }
                }
            }
            mesh.vertices.push_back(geometry);
            for (unsigned c = 0; c < 3; ++c) {
                mesh.minimum[c] = std::min(mesh.minimum[c], vertex.geometry.position[c]);
                mesh.maximum[c] = std::max(mesh.maximum[c], vertex.geometry.position[c]);
            }
        }
        mesh.parts.push_back({skin.material ? skin.material->id : model.id, 0, static_cast<unsigned>(mesh.indices.size())});
        visible.meshes.push_back(std::move(mesh));
    }
    UploadedSkin result;
    result.gpu = upload(device, visible, pso, cull);
    for (unsigned index = 0; index < model.meshes.size(); ++index)
        result.gpu.meshes[index].color = model.meshes[index].material ? std::array<float, 4>{1, 1, 1, 1} : model.meshes[index].color;
    if (!reference)
        for (unsigned index = 0; index < model.meshes.size(); ++index) {
            const auto& mesh = model.meshes[index];
            BufferDesc desc;
            desc.Name = "Canonical four-influence skin vertices";
            desc.Usage = USAGE_IMMUTABLE;
            desc.BindFlags = BIND_VERTEX_BUFFER;
            desc.Size = mesh.vertices.size() * sizeof(SkinnedVertex);
            BufferData data{mesh.vertices.data(), desc.Size};
            device->CreateBuffer(desc, &data, &result.gpu.meshes[index].vertices);
            require(result.gpu.meshes[index].vertices, "GPU skin vertex upload failed");
        }
    result.pose = std::make_unique<RigPose>(model.rig.definition, model.rig.archive);
    result.model = std::move(model);
    return result;
}
void upload_palette(IDeviceContext* context, IBuffer* buffer, UploadedSkin& skin, const AnimationClip* clip, double tick,
                    const std::vector<JointMatrix>* gameplay) {
    const auto& matrices = gameplay ? *gameplay : (clip ? skin.pose->sample(*clip, tick) : skin.pose->rest_pose());
    require(matrices.size() == skin.model.inverse_bind.size(), "Gameplay pose skin layout mismatch");
    std::array<std::array<float, 16>, 256> palette{};
    for (unsigned joint = 0; joint < matrices.size(); ++joint)
        for (unsigned col = 0; col < 4; ++col)
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned k = 0; k < 4; ++k)
                    palette[joint][col * 4 + row] += matrices[joint].values[k * 4 + row] * skin.model.inverse_bind[joint][col * 4 + k];
    void* mapped{};
    context->MapBuffer(buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
    require(mapped, "GPU skin palette map failed");
    std::memcpy(mapped, palette.data(), sizeof(palette));
    context->UnmapBuffer(buffer, MAP_WRITE);
}
#endif
} // namespace darkangel::editor_app::render
