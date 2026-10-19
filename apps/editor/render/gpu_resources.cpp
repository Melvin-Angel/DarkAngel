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
namespace {
constexpr const char* cluster_compute_source = R"(
cbuffer ClusterFrame {uint4 Dimensions;uint4 Limits;float4 Camera;};
struct GpuLight {float4 PositionRange;float4 Metadata;float4 ColorIntensity;float4 DirectionType;float4 ConeFalloff;};
StructuredBuffer<GpuLight> Lights;
RWStructuredBuffer<uint> ClusterCandidates;
RWStructuredBuffer<uint> ClusterSelected;
RWStructuredBuffer<uint> ClusterCounts;
RWStructuredBuffer<uint> ClusterStats;

bool overlaps(float a_min,float a_max,float b_min,float b_max){return a_max>=b_min&&a_min<=b_max;}
[numthreads(64,1,1)]
void CSMain(uint3 dispatch_id:SV_DispatchThreadID){
    uint cluster=dispatch_id.x;
    if(cluster>=Limits.z)return;
    uint tiles_per_slice=Dimensions.x*Dimensions.y;
    uint z=cluster/tiles_per_slice;
    uint rem=cluster-z*tiles_per_slice;
    uint y=rem/Dimensions.x;
    uint x=rem-y*Dimensions.x;
    float tile_min_x=float(x)/float(Dimensions.x),tile_max_x=float(x+1)/float(Dimensions.x);
    float tile_min_y=float(y)/float(Dimensions.y),tile_max_y=float(y+1)/float(Dimensions.y);
    float log_range=log(Camera.y/Camera.x);
    float depth_min=exp(log(Camera.x)+float(z)/float(Dimensions.z)*log_range);
    float depth_max=exp(log(Camera.x)+float(z+1)/float(Dimensions.z)*log_range);
    uint candidate_count=0;
    uint base=cluster*64;
    for(uint light_index=0;light_index<Dimensions.w;++light_index){
        GpuLight light=Lights[light_index];
        if(light.Metadata.x<0.5||light.Metadata.y<0.5)continue;
        float3 position=light.PositionRange.xyz;
        float radius=max(light.PositionRange.w,.001);
        float depth=-position.z;
        if(depth+radius<Camera.x||depth-radius>Camera.y||depth<=.001||depth+radius<depth_min||depth-radius>depth_max)continue;
        float screen_depth=max(Camera.x,depth-radius);
        float tx=max(.01,Camera.w*Camera.z);
        float min_x=.5+.5*(position.x-radius)/screen_depth/tx;
        float max_x=.5+.5*(position.x+radius)/screen_depth/tx;
        float min_y=.5+.5*(position.y-radius)/screen_depth/Camera.z;
        float max_y=.5+.5*(position.y+radius)/screen_depth/Camera.z;
        if(!overlaps(min_x,max_x,tile_min_x,tile_max_x)||!overlaps(.5-max_y*.5,.5-min_y*.5,tile_min_y,tile_max_y))continue;
        if(candidate_count<Limits.x)ClusterCandidates[base+candidate_count]=light_index;
        ++candidate_count;
    }
    uint stored=min(candidate_count,Limits.x);
    ClusterCounts[cluster]=stored;
    uint selected=min(stored,Limits.y);
    for(uint i=0;i<4;++i)ClusterSelected[cluster*4+i]=i<selected?ClusterCandidates[base+i]:0xffffffff;
    if(candidate_count>0)InterlockedAdd(ClusterStats[0],1);
    if(candidate_count>Limits.x)InterlockedAdd(ClusterStats[1],1);
    InterlockedAdd(ClusterStats[2],stored);
    InterlockedAdd(ClusterStats[3],selected);
    InterlockedMax(ClusterStats[4],stored);
}
)";
}

std::string normalize_shader_source(std::string source) {
    const std::string cbuffer = "float4 Features;float4 MainLight;";
    const std::string replacement = "float4 Features;float4 Alpha;float4 MainLight;";
    for (std::size_t at = source.find(cbuffer); at != source.npos; at = source.find(cbuffer, at + replacement.size()))
        source.replace(at, cbuffer.size(), replacement);
    const std::string test = "base.a<Features.z";
    for (std::size_t at = source.find(test); at != source.npos; at = source.find(test, at + 8))
        source.replace(at, test.size(), "base.a<Alpha.x");
    return source;
}
std::string normalized_shader_source() { return normalize_shader_source(shader_source); }
std::string instanced_surface_source(std::string source) {
    const auto replace_once = [&](std::string_view from, std::string_view to) {
        const auto at = source.find(from);
        require(at != source.npos, "Instanced surface shader contract changed");
        source.replace(at, from.size(), to);
    };
    replace_once("struct VertexInput {float3 Position:ATTRIB0;float3 Normal:ATTRIB1;float2 UV:ATTRIB2;};",
                 "struct VertexInput {float3 Position:ATTRIB0;float3 Normal:ATTRIB1;float2 UV:ATTRIB2;float4 World0:ATTRIB5;float4 World1:ATTRIB6;float4 World2:ATTRIB7;float4 World3:ATTRIB8;};");
    replace_once("float4 local=float4(vertex.Position,1);result.Position=mul(local,Mvp);result.Normal=mul(vertex.Normal,(float3x3)Rotation);result.Local=mul(vertex.Position,(float3x3)Rotation);result.WorldPosition=mul(local,World).xyz;",
                 "float4 local=float4(vertex.Position,1);row_major float4x4 instance_world=float4x4(vertex.World0,vertex.World1,vertex.World2,vertex.World3);float4 world_position=mul(local,instance_world);result.Position=mul(world_position,Mvp);result.Normal=mul(vertex.Normal,(float3x3)instance_world);result.Local=mul(vertex.Position,(float3x3)instance_world);result.WorldPosition=world_position.xyz;");
    return source;
}

void ensure_gpu_clusters(IRenderDevice* device, IDeviceContext* context, GpuClusterResources& resources, unsigned tiles_x,
                         unsigned tiles_y, unsigned slices, unsigned max_candidates, unsigned shading_budget, unsigned light_capacity) {
    max_candidates = std::clamp(max_candidates, 1u, 64u);
    shading_budget = std::clamp(shading_budget, 1u, 4u);
    light_capacity = std::max(1u, light_capacity);
    const auto clusters = static_cast<std::size_t>(tiles_x) * tiles_y * slices;
    if (resources.pipeline && resources.tiles_x == tiles_x && resources.tiles_y == tiles_y && resources.slices == slices &&
        resources.max_candidates == max_candidates && resources.shading_budget == shading_budget && resources.light_capacity >= light_capacity)
        return;
    context->Flush();
    context->WaitForIdle();
    resources = {};
    resources.tiles_x = tiles_x;
    resources.tiles_y = tiles_y;
    resources.slices = slices;
    resources.max_candidates = max_candidates;
    resources.shading_budget = shading_budget;
    resources.light_capacity = light_capacity;
    resources.cluster_count = clusters;

    BufferDesc frame_desc;
    frame_desc.Name = "DarkAngel GPU cluster frame";
    frame_desc.Size = sizeof(ClusterGpuFrame);
    frame_desc.Usage = USAGE_DYNAMIC;
    frame_desc.BindFlags = BIND_UNIFORM_BUFFER;
    frame_desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    device->CreateBuffer(frame_desc, nullptr, &resources.frame);
    require(resources.frame, "GPU cluster frame buffer creation failed");

    BufferDesc lights_desc;
    lights_desc.Name = "DarkAngel GPU cluster lights";
    lights_desc.Size = static_cast<Uint64>(light_capacity) * sizeof(ClusterGpuLight);
    lights_desc.Usage = USAGE_DYNAMIC;
    lights_desc.BindFlags = BIND_SHADER_RESOURCE;
    lights_desc.Mode = BUFFER_MODE_STRUCTURED;
    lights_desc.ElementByteStride = sizeof(ClusterGpuLight);
    lights_desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    device->CreateBuffer(lights_desc, nullptr, &resources.lights);
    require(resources.lights, "GPU cluster light buffer creation failed");

    auto output_buffer = [&](const char* name, std::size_t count, RefCntAutoPtr<IBuffer>& destination) {
        BufferDesc desc;
        desc.Name = name;
        desc.Size = static_cast<Uint64>(std::max<std::size_t>(1, count)) * sizeof(std::uint32_t);
        desc.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
        desc.Mode = BUFFER_MODE_STRUCTURED;
        desc.ElementByteStride = sizeof(std::uint32_t);
        device->CreateBuffer(desc, nullptr, &destination);
        require(destination, name);
    };
    output_buffer("DarkAngel GPU cluster candidate indices", clusters * 64, resources.candidates);
    output_buffer("DarkAngel GPU cluster selected indices", clusters * 4, resources.selected);
    output_buffer("DarkAngel GPU cluster occupancy", clusters, resources.counts);
    output_buffer("DarkAngel GPU cluster statistics", 5, resources.stats);

    BufferDesc readback;
    readback.Name = "DarkAngel GPU cluster statistics readback";
    readback.Size = 5 * sizeof(std::uint32_t);
    readback.Usage = USAGE_STAGING;
    readback.CPUAccessFlags = CPU_ACCESS_READ;
    device->CreateBuffer(readback, nullptr, &resources.stats_readback);
    require(resources.stats_readback, "GPU cluster statistics readback creation failed");

    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.ShaderCompiler = SHADER_COMPILER_DXC;
    shader.HLSLVersion = {6, 0};
    shader.Source = cluster_compute_source;
    shader.Desc.Name = "DarkAngel clustered light assignment CS";
    shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    shader.EntryPoint = "CSMain";
    RefCntAutoPtr<IShader> compute_shader;
    device->CreateShader(shader, &compute_shader);
    require(compute_shader, "GPU cluster compute shader compilation failed");

    ComputePipelineStateCreateInfo pipeline_info;
    pipeline_info.PSODesc.Name = "DarkAngel GPU clustered light assignment";
    pipeline_info.pCS = compute_shader;
    ShaderResourceVariableDesc variables[] = {{SHADER_TYPE_COMPUTE, "ClusterFrame", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
                                              {SHADER_TYPE_COMPUTE, "Lights", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
                                              {SHADER_TYPE_COMPUTE, "ClusterCandidates", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
                                              {SHADER_TYPE_COMPUTE, "ClusterSelected", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
                                              {SHADER_TYPE_COMPUTE, "ClusterCounts", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
                                              {SHADER_TYPE_COMPUTE, "ClusterStats", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    pipeline_info.PSODesc.ResourceLayout.Variables = variables;
    pipeline_info.PSODesc.ResourceLayout.NumVariables = _countof(variables);
    device->CreateComputePipelineState(pipeline_info, &resources.pipeline);
    require(resources.pipeline, "GPU cluster compute pipeline creation failed");
    resources.pipeline->CreateShaderResourceBinding(&resources.binding, true);
    require(resources.binding, "GPU cluster compute binding creation failed");
}

void dispatch_gpu_clusters(IDeviceContext* context, GpuClusterResources& resources, std::span<const ClusterGpuLight> lights,
                           const ClusterGpuFrame& frame) {
    std::vector<ClusterGpuLight> padded(std::max<std::size_t>(1, lights.size()));
    if (!lights.empty())
        std::memcpy(padded.data(), lights.data(), lights.size_bytes());
    void* mapped{};
    context->MapBuffer(resources.lights, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
    require(mapped, "GPU cluster light mapping failed");
    std::memcpy(mapped, padded.data(), padded.size() * sizeof(ClusterGpuLight));
    context->UnmapBuffer(resources.lights, MAP_WRITE);
    mapped = nullptr;
    context->MapBuffer(resources.frame, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
    require(mapped, "GPU cluster frame mapping failed");
    std::memcpy(mapped, &frame, sizeof(frame));
    context->UnmapBuffer(resources.frame, MAP_WRITE);
    const std::uint32_t zero_stats[5]{};
    context->UpdateBuffer(resources.stats, 0, sizeof(zero_stats), zero_stats, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto set = [&](const char* name, IBufferView* view) {
        auto* variable = resources.binding->GetVariableByName(SHADER_TYPE_COMPUTE, name);
        require(variable, name);
        variable->Set(view);
    };
    auto set_buffer = [&](const char* name, IBuffer* buffer, BUFFER_VIEW_TYPE type) {
        set(name, buffer->GetDefaultView(type));
    };
    auto* frame_variable = resources.binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ClusterFrame");
    require(frame_variable, "GPU cluster frame contract changed");
    frame_variable->Set(resources.frame);
    set_buffer("Lights", resources.lights, BUFFER_VIEW_SHADER_RESOURCE);
    set_buffer("ClusterCandidates", resources.candidates, BUFFER_VIEW_UNORDERED_ACCESS);
    set_buffer("ClusterSelected", resources.selected, BUFFER_VIEW_UNORDERED_ACCESS);
    set_buffer("ClusterCounts", resources.counts, BUFFER_VIEW_UNORDERED_ACCESS);
    set_buffer("ClusterStats", resources.stats, BUFFER_VIEW_UNORDERED_ACCESS);
    context->SetPipelineState(resources.pipeline);
    context->CommitShaderResources(resources.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(DispatchComputeAttribs{static_cast<Uint32>((resources.cluster_count + 63) / 64), 1, 1});
    context->CopyBuffer(resources.stats, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, resources.stats_readback, 0, 5 * sizeof(std::uint32_t),
                        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    resources.readback_pending = true;
}

bool read_gpu_cluster_stats(IDeviceContext* context, GpuClusterResources& resources, rendering::ClusterGpuStats& result) {
    if (!resources.readback_pending)
        return false;
    void* mapped{};
    context->MapBuffer(resources.stats_readback, MAP_READ, MAP_FLAG_DO_NOT_WAIT, mapped);
    if (!mapped)
        return false;
    std::uint32_t values[5]{};
    std::memcpy(values, mapped, sizeof(values));
    context->UnmapBuffer(resources.stats_readback, MAP_READ);
    result.ready = true;
    result.non_empty = values[0];
    result.overflowed = values[1];
    result.candidate_links = values[2];
    result.selected_links = values[3];
    result.max_occupancy = values[4];
    resources.readback_pending = false;
    return true;
}

void bind_gpu_clusters(IShaderResourceBinding* binding, const GpuClusterResources& resources) {
    if (!binding || !resources.lights || !resources.selected)
        return;
    auto* lights = binding->GetVariableByName(SHADER_TYPE_PIXEL, "ClusterLights");
    auto* selected = binding->GetVariableByName(SHADER_TYPE_PIXEL, "ClusterSelected");
    require(lights && selected, "Graphics clustered-light resource contract changed");
    lights->Set(resources.lights->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
    selected->Set(resources.selected->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
}

RefCntAutoPtr<IPipelineState> pipeline(IRenderDevice* device, IBuffer* constants, TEXTURE_FORMAT color, TEXTURE_FORMAT depth, bool cull,
                                       const char* source, IBuffer* bones, bool blend, bool instanced, bool outline_targets) {
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.ShaderCompiler = SHADER_COMPILER_DXC;
    shader.HLSLVersion = {6, 0};
    shader.Desc.UseCombinedTextureSamplers = true;
    std::string transformed = source ? source : normalized_shader_source();
    if (instanced) transformed = instanced_surface_source(std::move(transformed));
    if (outline_targets) {
        if (instanced) {
            const auto replace = [&](std::string_view from, std::string_view to) {
                const auto at = transformed.find(from);
                require(at != transformed.npos, "Instanced outline shader contract changed");
                transformed.replace(at, from.size(), to);
            };
            replace("float4 World3:ATTRIB8;", "float4 World3:ATTRIB8;float ObjectId:ATTRIB9;");
            replace("struct VertexOutput {", "struct VertexOutput {nointerpolation float ObjectId:TEXCOORD6;");
            replace("result.UV=vertex.UV;return result;", "result.UV=vertex.UV;result.ObjectId=vertex.ObjectId;return result;");
        }
        const auto entry = transformed.find("PSMain(");
        require(entry != transformed.npos, "Outline surface entry point missing");
        transformed.replace(entry, 7, "SurfaceMain(");
        transformed += R"(
struct OutlineOutput {float4 Info:SV_TARGET0;float ObjectId:SV_TARGET1;};
OutlineOutput PSMain(VertexOutput vertex) {
    OutlineOutput result;
    result.Info=SurfaceMain(vertex);
    result.ObjectId=OUTLINE_OBJECT_ID;
    return result;
}
)";
        const auto id = transformed.find("OUTLINE_OBJECT_ID");
        transformed.replace(id, 17, instanced ? "vertex.ObjectId" : "Outline.y");
    }
    shader.Source = transformed.c_str();
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
    if (outline_targets) {
        graphics.NumRenderTargets = 2;
        graphics.RTVFormats[1] = TEX_FORMAT_R32_FLOAT;
    }
    graphics.DSVFormat = depth;
    graphics.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode = cull ? CULL_MODE_BACK : CULL_MODE_NONE;
    graphics.RasterizerDesc.FrontCounterClockwise = true;
    graphics.DepthStencilDesc.DepthEnable = true;
    // The same geometry PSO is reused for the outline-information pass after
    // the visible scene has populated depth. Equal-depth fragments must pass
    // so the pass can record normals/object groups for the compositor.
    graphics.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
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
    LayoutElement instance_layout[] = {{5, 1, 4, VT_FLOAT32, false, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1},
                                       {6, 1, 4, VT_FLOAT32, false, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1},
                                       {7, 1, 4, VT_FLOAT32, false, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1},
                                       {8, 1, 4, VT_FLOAT32, false, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1}};
    LayoutElement full_layout[] = {{0, 0, 3, VT_FLOAT32, false}, {1, 0, 3, VT_FLOAT32, false}, {2, 0, 2, VT_FLOAT32, false},
                                   instance_layout[0], instance_layout[1], instance_layout[2], instance_layout[3],
                                   {9, 1, 1, VT_FLOAT32, false, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1}};
    if (instanced) { graphics.InputLayout.LayoutElements = full_layout; graphics.InputLayout.NumElements = outline_targets ? 8 : 7; }
    else graphics.InputLayout.NumElements = bones ? 5 : 3;
    ShaderResourceVariableDesc variables[] = {{SHADER_TYPE_PIXEL, "Albedo", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "NormalMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "SurfaceMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "EmissiveMap", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "DissolveMask", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "TerrainLayer1", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "TerrainLayer2", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
                                              {SHADER_TYPE_PIXEL, "ShadowMap", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
                                              {SHADER_TYPE_PIXEL, "ClusterLights", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
                                              {SHADER_TYPE_PIXEL, "ClusterSelected", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
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
                                       {SHADER_TYPE_PIXEL, "TerrainLayer2", sampler},
                                       {SHADER_TYPE_PIXEL, "ShadowMap", [&]{SamplerDesc comparison=sampler;comparison.MinFilter=FILTER_TYPE_COMPARISON_LINEAR;comparison.MagFilter=FILTER_TYPE_COMPARISON_LINEAR;comparison.MipFilter=FILTER_TYPE_COMPARISON_LINEAR;comparison.AddressU=TEXTURE_ADDRESS_CLAMP;comparison.AddressV=TEXTURE_ADDRESS_CLAMP;comparison.ComparisonFunc=COMPARISON_FUNC_LESS_EQUAL;return comparison;}()}};
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
cbuffer PostFrame {float4 Exposure;float4 Bloom;float4 TintFxaa;float4 GradeLift;float4 GradeGamma;float4 GradeGain;float4 GradeParams;float4 Texel;float4 Outline;float4 OutlineParams;float4 OutlineFade;float4 SkyZenith;float4 SkyHorizon;float4 SkyGround;float4 SkySun;float4 SkySunColor;float4 SkyMode;float4 SkyCloud;row_major float4x4 InverseProjection;row_major float4x4 InverseView;};
Texture2D SceneColor;SamplerState SceneColor_sampler;
Texture2D OutlineInfo;SamplerState OutlineInfo_sampler;
Texture2D OutlineObjectId;SamplerState OutlineObjectId_sampler;
Texture2D SceneDepth;SamplerState SceneDepth_sampler;
Texture2D SkyTexture;SamplerState SkyTexture_sampler;
TextureCube SkyCube;SamplerState SkyCube_sampler;
struct VertexOutput {float4 Position:SV_POSITION;float2 UV:TEXCOORD0;};
VertexOutput VSMain(uint id:SV_VertexID){float2 positions[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};float2 uv[3]={float2(0,1),float2(0,-1),float2(2,1)};VertexOutput result;result.Position=float4(positions[id],0,1);result.UV=uv[id];return result;}
float luminance(float3 v){return dot(v,float3(.2126,.7152,.0722));}
float3 hdr(float2 uv){return max(SceneColor.Sample(SceneColor_sampler,uv).rgb,0);}
float3 tone(float3 value){float exposure=exp2(Exposure.x+Exposure.y);float3 color=1-exp(-value*exposure);float l=luminance(color);color=lerp(l.xxx,color,Exposure.w);color=(color-.5)*Exposure.z+.5;color=max(color+GradeLift.rgb,0);if(any(GradeGamma.rgb!=1))color=pow(color,1/max(GradeGamma.rgb,.001));color*=GradeGain.rgb;float temperature=clamp(GradeParams.x,-1,1);color*=float3(1+.08*temperature,1,1-.08*temperature);return saturate(color*TintFxaa.rgb);}
float outline_coverage(float2 uv){
    if(Outline.w<=0)return 0;
    int2 pixel=int2(uv/Texel.xy);
    int2 last_pixel=int2(round(1/Texel.xy))-1;
    // IDs and geometry must not interpolate across object boundaries.
    float4 center=OutlineInfo.Load(int3(pixel,0));
    float depth=SceneDepth.Load(int3(pixel,0)).r;
    float center_id=OutlineObjectId.Load(int3(pixel,0)).r;
    float2 depth_gradient=float2(ddx(depth),ddy(depth));
    if(center_id<=0&&depth>=1)return 0;
    // Reject fully faded pixels before gathering the neighbourhood.
    float4 position=mul(float4(uv.x*2-1,(uv.y-.5)/OutlineFade.z,depth,1),InverseProjection);
    float distance=length(position.xyz/max(abs(position.w),1e-7));
    float fade=1-saturate((distance-OutlineFade.x)/max(OutlineFade.y-OutlineFade.x,.001));
    if(fade<=0)return 0;
    // Treat thickness as a continuous pixel radius. Quantizing this to an
    // integer made small slider changes jump between visibly different widths.
    float thickness=clamp(OutlineParams.x,.25,8);
    int radius=(int)clamp(ceil(thickness+.5),1,8);
    // Do not select the single strongest neighbor: at moving silhouettes that
    // makes one sub-pixel depth/normal sample switch the whole pixel on/off.
    // Accumulate soft evidence from the neighborhood instead, so contours
    // change continuously as they cross pixel centers.
    float object_transmittance=1,detail_transmittance=1;
    [loop]for(int y=-radius;y<=radius;++y)[loop]for(int x=-radius;x<=radius;++x){
        if(x==0&&y==0)continue;
        float distance_squared=float(x*x+y*y);
        float outer=thickness+1;
        float distance_coverage=1-smoothstep(thickness*thickness,outer*outer,distance_squared);
        // The square loop contains samples outside the circular filter. They
        // have zero weight, so do not fetch three textures for them.
        if(distance_coverage<=0)continue;
        int2 sample_pixel=clamp(pixel+int2(x,y),int2(0,0),last_pixel);
        float4 other=OutlineInfo.Load(int3(sample_pixel,0));
        float other_id=OutlineObjectId.Load(int3(sample_pixel,0)).r;
        float other_depth=SceneDepth.Load(int3(sample_pixel,0)).r;
        float normal_delta=length(other.rgb-center.rgb);
        // Projected depth is affine on a plane. Remove that slope so adjacent
        // coplanar terrain/path pieces do not acquire a grid of object-ID ink.
        float expected_depth=depth+dot(depth_gradient,float2(sample_pixel-pixel));
        // At an occlusion boundary a derivative may belong to the other
        // surface. It must never invent a larger break than the raw depth.
        float depth_delta=min(abs(other_depth-depth),abs(other_depth-expected_depth))/max(depth,.001);
        // Object identity is the authoritative silhouette/boundary signal.
        // Adjacent coplanar pieces are one continuous surface. Their different
        // IDs must not ink a tile grid after fixing the old ID-pass routing.
        bool surface_break=other_id<=0||normal_delta>.08||depth_delta>2e-6;
        float object_edge=(center_id>0&&abs(other_id-center_id)>.5&&surface_break)?1:0;
        // Depth and normal only contribute restrained secondary detail. These
        // are intentionally less sensitive than the dedicated object mask.
        float normal_edge=smoothstep(.2,.42,normal_delta)*OutlineParams.z*.04;
        float depth_edge=smoothstep(.002,.012,depth_delta*OutlineParams.y*.01);
        float object_sample=saturate(object_edge*distance_coverage);
        float detail_sample=saturate(max(depth_edge,normal_edge)*distance_coverage);
        float spatial_weight=1/(1+distance_squared*.45);
        object_transmittance*=1-object_sample*spatial_weight;
        detail_transmittance*=1-detail_sample*spatial_weight;
    }
    // Fade controls are metres, not nonlinear depth-buffer values. Read the
    // exact projection for this viewport, including previews and split Play.
    float object_coverage=1-object_transmittance;
    float detail_coverage=(1-detail_transmittance)*.2;
    return saturate(max(object_coverage,detail_coverage))*fade*saturate(Outline.w);
}
float hash12(float2 p){p=frac(p*float2(123.34,345.45));p+=dot(p,p+34.345);return frac(p.x*p.y);}
float value_noise(float2 p){float2 cell=floor(p),local=frac(p);local=local*local*(3-2*local);float a=hash12(cell),b=hash12(cell+float2(1,0)),c=hash12(cell+float2(0,1)),d=hash12(cell+float2(1,1));return lerp(lerp(a,b,local.x),lerp(c,d,local.x),local.y);}
float cloud_noise(float2 p){float result=0,weight=.5;[unroll]for(uint octave=0;octave<4;++octave){result+=value_noise(p)*weight;p=p*2.03+float2(17.1,9.2);weight*=.5;}return result;}
float3 procedural_clouds(float2 uv,float height,float3 sky_color){
    float seed=SkyCloud.y;
    // Wide, low-frequency noise makes the clouds read as hand-painted streaks;
    // a second, tighter field breaks the silhouettes into the small torn shapes
    // used by the authored sunset reference.
    float2 broad_uv=float2(uv.x*4.8+height*1.7+seed*.013,height*2.7+seed*.007);
    float broad=cloud_noise(broad_uv);
    float detail=cloud_noise(broad_uv*1.75+float2(4.7,11.3));
    float wisps=saturate(broad*.72+detail*.28);
    float coverage=smoothstep(.48,.61,wisps);
    float breakup=smoothstep(.30,.58,cloud_noise(broad_uv*2.35+float2(5.4,13.7)));
    float streaks=smoothstep(.36,.60,value_noise(float2(broad_uv.x*1.55,broad_uv.y*.62)+float2(9.2,3.1)));
    float2 puffy_uv=float2(uv.x*10.5+height*3.1+seed*.021,height*7.2+seed*.011);
    float puffy=smoothstep(.53,.66,cloud_noise(puffy_uv));
    float puffy_breakup=smoothstep(.34,.62,cloud_noise(puffy_uv*2.15+float2(12.4,6.8)));
    float horizon_fade=smoothstep(.06,.18,height);
    float upper_fade=1-smoothstep(.94,1,height)*.42;
    float broad_density=coverage*lerp(.52,1,breakup)*lerp(.70,1,streaks);
    float puffy_density=puffy*lerp(.35,1,puffy_breakup);
    float density=saturate((broad_density*.72+puffy_density*.72)*horizon_fade*upper_fade*saturate(SkyCloud.x*1.35));
    float highlight=smoothstep(.43,.64,cloud_noise(broad_uv*.72+float2(8.1,2.6)));
    float3 cloud_shadow=lerp(float3(.28,.16,.30),float3(.58,.28,.32),saturate(height*1.3));
    float3 cloud_light=lerp(float3(1.00,.62,.49),float3(1.16,.94,.70),saturate(height));
    float3 cloud=lerp(cloud_shadow,cloud_light,highlight*.90);
    return lerp(sky_color,cloud,density);
}
float2 direction_to_uv(float3 direction){float phi=atan2(direction.z,direction.x);return float2(frac(phi/6.2831853+.5),acos(clamp(direction.y,-1,1))/3.14159265);}
float3 uv_to_direction(float2 uv){float phi=(uv.x-.5)*6.2831853,theta=uv.y*3.14159265;return float3(sin(theta)*cos(phi),cos(theta),sin(theta)*sin(phi));}
float3 world_view_ray(float2 uv){float4 view=mul(float4(uv.x*2-1,(uv.y*2-1)/OutlineFade.z,1,1),InverseProjection);float3 view_direction=normalize(view.xyz/max(abs(view.w),1e-7));return normalize(mul(float4(view_direction,0),InverseView).xyz);}
float3 imported_sky(float3 world_direction){float c=cos(SkyMode.w),s=sin(SkyMode.w);float3 direction=float3(c*world_direction.x-s*world_direction.z,world_direction.y,s*world_direction.x+c*world_direction.z);if(SkyMode.y>.5)return SkyCube.Sample(SkyCube_sampler,direction).rgb;float2 uv=direction_to_uv(world_direction);uv.x=frac(uv.x+SkyMode.w/6.2831853);return SkyTexture.Sample(SkyTexture_sampler,uv).rgb;}
float3 sky(float3 world_direction){world_direction=normalize(world_direction);if(SkyMode.x>1.5)return SkyTexture.Sample(SkyTexture_sampler,direction_to_uv(world_direction)).rgb;if(SkyMode.x>.5)return imported_sky(world_direction)*SkyMode.z;float2 uv=direction_to_uv(world_direction);float height=saturate(world_direction.y*.5+.5);float3 lower=lerp(SkyGround.rgb,SkyHorizon.rgb,smoothstep(0,.45,height));float3 result=lerp(lower,SkyZenith.rgb,smoothstep(.35,1,height));if(SkyCloud.x>.001)result=procedural_clouds(uv,height,result);float sun_dot=clamp(dot(world_direction,normalize(SkySun.xyz)),-1,1);float sun_distance=acos(sun_dot);float disc=1-smoothstep(SkySun.w*.62,SkySun.w*.76,sun_distance);float glow=(1-smoothstep(0,max(.001,SkySun.w*3.5),sun_distance))*.18;return result+SkySunColor.rgb*(disc+glow)*SkySunColor.a;}
// Only untouched clear-depth pixels are sky. Perspective depth near 1 is also
// valid distant geometry, particularly with the Scene camera's .05 m near plane.
// The edge mask is opacity, independent of ink colour: adding black cannot
// darken the scene. Composite after FXAA so it cannot average the ink away.
float4 PSMain(VertexOutput vertex):SV_TARGET {if(SkyMode.x<-.5)return float4(sky(uv_to_direction(vertex.UV)),1);float depth=SceneDepth.Load(int3(uint2(vertex.Position.xy),0)).r;float3 source=depth>=1.0&&(SkyMode.x>.5||SkySun.w>0)?sky(world_view_ray(vertex.UV)):hdr(vertex.UV);float3 bloom=0;if(Bloom.z>0){float2 radius=Texel.xy*max(Bloom.w,1);float3 samples[5]={source,hdr(vertex.UV+float2(radius.x,0)),hdr(vertex.UV-float2(radius.x,0)),hdr(vertex.UV+float2(0,radius.y)),hdr(vertex.UV-float2(0,radius.y))};[unroll]for(uint i=0;i<5;++i){float l=luminance(samples[i]);float threshold=max(Bloom.x,0);float soft=max(Bloom.y,.001);float weight=smoothstep(threshold,threshold+soft,l);bloom+=samples[i]*weight;}bloom=bloom*(Bloom.z/5);source+=bloom;}float3 color=tone(source);if(TintFxaa.a>0){float3 left=tone(hdr(vertex.UV-float2(Texel.x,0))),right=tone(hdr(vertex.UV+float2(Texel.x,0))),up=tone(hdr(vertex.UV-float2(0,Texel.y))),down=tone(hdr(vertex.UV+float2(0,Texel.y)));float range=max(max(luminance(left),luminance(right)),max(luminance(up),luminance(down)))-min(min(luminance(left),luminance(right)),min(luminance(up),luminance(down)));if(range>.08)color=(color+left+right+up+down)/5;}color=lerp(color,Outline.rgb,outline_coverage(vertex.UV));return float4(color,1);}
)";
    ShaderCreateInfo shader;shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_HLSL;shader.ShaderCompiler=SHADER_COMPILER_DXC;shader.HLSLVersion={6,0};shader.Desc.UseCombinedTextureSamplers=true;shader.Source=source;RefCntAutoPtr<IShader> vs,ps;shader.Desc.Name="DarkAngel post VS";shader.Desc.ShaderType=SHADER_TYPE_VERTEX;shader.EntryPoint="VSMain";device->CreateShader(shader,&vs);shader.Desc.Name="DarkAngel post PS";shader.Desc.ShaderType=SHADER_TYPE_PIXEL;shader.EntryPoint="PSMain";device->CreateShader(shader,&ps);require(vs&&ps,"Post-process shader compilation failed");
    // Viewport resize and workspace changes replace these textures. Dynamic
    // bindings follow the current targets; mutable bindings retain the first
    // texture and leave the displayed image frozen while cameras keep moving.
    GraphicsPipelineStateCreateInfo ci;ci.PSODesc.Name="DarkAngel HDR post-process";ci.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;ci.pVS=vs;ci.pPS=ps;auto& graphics=ci.GraphicsPipeline;graphics.NumRenderTargets=1;graphics.RTVFormats[0]=color;graphics.DSVFormat=TEX_FORMAT_UNKNOWN;graphics.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;graphics.RasterizerDesc.CullMode=CULL_MODE_NONE;graphics.DepthStencilDesc.DepthEnable=false;graphics.InputLayout.NumElements=0;ShaderResourceVariableDesc variables[]={{SHADER_TYPE_PIXEL,"SceneColor",SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},{SHADER_TYPE_PIXEL,"OutlineInfo",SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},{SHADER_TYPE_PIXEL,"OutlineObjectId",SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},{SHADER_TYPE_PIXEL,"SceneDepth",SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},{SHADER_TYPE_PIXEL,"SkyTexture",SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},{SHADER_TYPE_PIXEL,"SkyCube",SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};ci.PSODesc.ResourceLayout.Variables=variables;ci.PSODesc.ResourceLayout.NumVariables=_countof(variables);SamplerDesc sampler;sampler.MinFilter=FILTER_TYPE_LINEAR;sampler.MagFilter=FILTER_TYPE_LINEAR;sampler.MipFilter=FILTER_TYPE_LINEAR;sampler.AddressU=TEXTURE_ADDRESS_CLAMP;sampler.AddressV=TEXTURE_ADDRESS_CLAMP;ImmutableSamplerDesc immutables[]={{SHADER_TYPE_PIXEL,"SceneColor",sampler},{SHADER_TYPE_PIXEL,"OutlineInfo",sampler},{SHADER_TYPE_PIXEL,"OutlineObjectId",sampler},{SHADER_TYPE_PIXEL,"SceneDepth",sampler},{SHADER_TYPE_PIXEL,"SkyTexture",sampler},{SHADER_TYPE_PIXEL,"SkyCube",sampler}};ci.PSODesc.ResourceLayout.ImmutableSamplers=immutables;ci.PSODesc.ResourceLayout.NumImmutableSamplers=_countof(immutables);RefCntAutoPtr<IPipelineState> result;device->CreateGraphicsPipelineState(ci,&result);require(result,"Post-process pipeline creation failed");auto* pixel_frame=result->GetStaticVariableByName(SHADER_TYPE_PIXEL,"PostFrame");require(pixel_frame,"Post-process constant contract changed");pixel_frame->Set(constants);return result;
}
RefCntAutoPtr<IPipelineState> shadow_pipeline(IRenderDevice* device, IBuffer* constants, TEXTURE_FORMAT depth, bool cull, const char* source, IBuffer* bones, bool instanced) {
    static constexpr const char* depth_source = R"(
cbuffer Frame {row_major float4x4 Mvp;};
struct VertexInput {float3 Position:ATTRIB0;float3 Normal:ATTRIB1;float2 UV:ATTRIB2;};
struct VertexOutput {float4 Position:SV_POSITION;};
VertexOutput VSMain(VertexInput vertex){VertexOutput result;result.Position=mul(float4(vertex.Position,1),Mvp);return result;}
float4 PSMain(VertexOutput vertex):SV_TARGET{return 1;}
)";
    const std::string instanced_depth=R"(cbuffer Frame {row_major float4x4 Mvp;}; struct VertexInput {float3 Position:ATTRIB0;float3 Normal:ATTRIB1;float2 UV:ATTRIB2;float4 World0:ATTRIB5;float4 World1:ATTRIB6;float4 World2:ATTRIB7;float4 World3:ATTRIB8;}; struct VertexOutput {float4 Position:SV_POSITION;}; VertexOutput VSMain(VertexInput vertex){VertexOutput result;row_major float4x4 world=float4x4(vertex.World0,vertex.World1,vertex.World2,vertex.World3);result.Position=mul(mul(float4(vertex.Position,1),world),Mvp);return result;} float4 PSMain(VertexOutput vertex):SV_TARGET{return 1;})";
    const auto transformed=instanced&&source?instanced_surface_source(source):std::string{};
    const char* shader_source_text=instanced?(source?transformed.c_str():instanced_depth.c_str()):(source?source:depth_source);ShaderCreateInfo shader;shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_HLSL;shader.ShaderCompiler=SHADER_COMPILER_DXC;shader.HLSLVersion={6,0};shader.Desc.UseCombinedTextureSamplers=true;shader.Source=shader_source_text;RefCntAutoPtr<IShader> vs,ps;shader.Desc.Name="DarkAngel shadow VS";shader.Desc.ShaderType=SHADER_TYPE_VERTEX;shader.EntryPoint="VSMain";device->CreateShader(shader,&vs);require(vs,"Shadow shader compilation failed");if(!source){shader.Desc.Name="DarkAngel shadow PS";shader.Desc.ShaderType=SHADER_TYPE_PIXEL;shader.EntryPoint="PSMain";device->CreateShader(shader,&ps);require(ps,"Shadow pixel shader compilation failed");}
    GraphicsPipelineStateCreateInfo ci;ci.PSODesc.Name="DarkAngel cascaded shadow caster";ci.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;ci.pVS=vs;ci.pPS=ps;auto& graphics=ci.GraphicsPipeline;graphics.NumRenderTargets=1;graphics.RTVFormats[0]=TEX_FORMAT_RGBA8_UNORM;graphics.DSVFormat=depth;graphics.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;graphics.RasterizerDesc.CullMode=cull?CULL_MODE_BACK:CULL_MODE_NONE;graphics.RasterizerDesc.FrontCounterClockwise=true;graphics.DepthStencilDesc.DepthEnable=true;graphics.DepthStencilDesc.DepthWriteEnable=true;graphics.DepthStencilDesc.DepthFunc=COMPARISON_FUNC_LESS;LayoutElement layout[]={{0,0,3,VT_FLOAT32,false},{1,0,3,VT_FLOAT32,false},{2,0,2,VT_FLOAT32,false},{3,0,4,VT_UINT32,false},{4,0,4,VT_FLOAT32,false}};LayoutElement instance_layout[]={{5,1,4,VT_FLOAT32,false,INPUT_ELEMENT_FREQUENCY_PER_INSTANCE,1},{6,1,4,VT_FLOAT32,false,INPUT_ELEMENT_FREQUENCY_PER_INSTANCE,1},{7,1,4,VT_FLOAT32,false,INPUT_ELEMENT_FREQUENCY_PER_INSTANCE,1},{8,1,4,VT_FLOAT32,false,INPUT_ELEMENT_FREQUENCY_PER_INSTANCE,1}};LayoutElement instanced_layout[]={{0,0,3,VT_FLOAT32,false},{1,0,3,VT_FLOAT32,false},{2,0,2,VT_FLOAT32,false},instance_layout[0],instance_layout[1],instance_layout[2],instance_layout[3]};graphics.InputLayout.LayoutElements=instanced?instanced_layout:layout;graphics.InputLayout.NumElements=instanced?7:(bones?5:3);RefCntAutoPtr<IPipelineState> result;device->CreateGraphicsPipelineState(ci,&result);require(result,"Shadow pipeline creation failed");auto* frame=result->GetStaticVariableByName(SHADER_TYPE_VERTEX,"Frame");require(frame,"Shadow Frame contract changed");frame->Set(constants);if(bones){auto* skeleton=result->GetStaticVariableByName(SHADER_TYPE_VERTEX,"Skeleton");require(skeleton,"Shadow skeleton contract missing");skeleton->Set(bones);}return result;
}
RefCntAutoPtr<IPipelineState> alpha_shadow_pipeline(IRenderDevice* device, IBuffer* constants, TEXTURE_FORMAT depth, bool cull, const char* source, IBuffer* bones, bool instanced) {
    const auto normalized = normalized_shader_source();
    return pipeline(device, constants, TEX_FORMAT_RGBA8_UNORM, depth, cull, source ? source : normalized.c_str(), bones, false, instanced);
}
RefCntAutoPtr<ITexture> texture(IRenderDevice* device, const CookedTexture& cooked) {
    require(!cooked.mips.empty(), "Missing texture mips");
    TextureDesc desc;
    desc.Name = cooked.srgb ? "Cooked DarkAngel sRGB color" : "Cooked DarkAngel linear data";
    desc.Type = cooked.layers == 6 ? RESOURCE_DIM_TEX_CUBE : RESOURCE_DIM_TEX_2D;
    desc.ArraySize = cooked.layers;
    desc.Width = cooked.mips[0].width;
    desc.Height = cooked.mips[0].height;
    desc.MipLevels = static_cast<Uint32>(cooked.mips.size());
    // Colour textures are sampled with sRGB decoding; data and normal maps (R5 A3) are linear.
    desc.Format = cooked.hdr ? TEX_FORMAT_RGBA16_FLOAT : (cooked.srgb ? TEX_FORMAT_RGBA8_UNORM_SRGB : TEX_FORMAT_RGBA8_UNORM);
    desc.Usage = USAGE_IMMUTABLE;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    std::vector<TextureSubResData> subresources;
    const auto bytes_per_pixel = cooked.hdr ? 8u : 4u;
    for (std::uint32_t layer = 0; layer < cooked.layers; ++layer)
        for (const auto& mip : cooked.mips) {
            TextureSubResData data;
            data.pData = mip.rgba.data() + static_cast<std::size_t>(layer) * mip.width * mip.height * bytes_per_pixel;
            data.Stride = mip.width * bytes_per_pixel;
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
    std::cout << "Viewport foreground pixels=" << changed << " capture=" << path.string()
              << " viewport_rect=" << left << ',' << top << ',' << right << ',' << bottom << '\n';
}
void apply_material(Constants& constants, const CookedMaterial& material) {
    constants.surface = {material.roughness, material.metallic, material.occlusion_strength, material.normal_scale};
    constants.emission = {material.emissive[0], material.emissive[1], material.emissive[2], 0};
    const unsigned bits = (material.has_normal ? 1u : 0u) | (material.has_surface ? 2u : 0u) | (material.has_emissive_texture ? 4u : 0u) | (material.triplanar ? 8u : 0u) | (material.has_dissolve_texture ? 16u : 0u) | (material.has_terrain_layer1 ? 32u : 0u) | (material.has_terrain_layer2 ? 64u : 0u);
    const float family=material.family==material_family_toon_character?2.f:material.family==material_family_toon_terrain?3.f:material.family==material_family_toon_standard?1.f:0.f;
    constants.features = {static_cast<float>(bits), static_cast<float>(material.alpha_mode), family, material.triplanar_scale};
    constants.alpha = {material.alpha_cutoff, 0, 0, 0};
    constants.toon_bands={material.toon_thresholds[0],material.toon_thresholds[1],material.toon_thresholds[2],material.toon_softness};constants.shadow_tint={material.shadow_tint[0],material.shadow_tint[1],material.shadow_tint[2],1};constants.ambient={material.ambient_color[0]*material.ambient_strength,material.ambient_color[1]*material.ambient_strength,material.ambient_color[2]*material.ambient_strength,1};
    constants.cluster_viewport.z=material.local_light_strength;
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
UploadedMaterial upload_material(IRenderDevice* device,const RuntimeMaterial& material,IPipelineState* pso,IPipelineState* cull,IPipelineState* skin,IPipelineState* skin_cull,
                                 IPipelineState* shadow,IPipelineState* shadow_cull,IPipelineState* skin_shadow,IPipelineState* skin_shadow_cull) {
    UploadedMaterial out;out.source=material;CookedTexture white;white.mips.push_back({1,1,{255,255,255,255}});out.fallback=texture(device,white);
    for(const auto& [id,cooked]:material.textures)out.textures.emplace(id,texture(device,cooked));
    auto bind=[&](Material& destination,IPipelineState* double_sided,IPipelineState* single_sided,IPipelineState* shadow_double_sided,IPipelineState* shadow_single_sided){destination.value=material.value;auto* chosen=material.value.double_sided?double_sided:single_sided;require(chosen,"Missing material pipeline");chosen->CreateShaderResourceBinding(&destination.binding,true);require(destination.binding,"Native material binding failed");
        bind_surface(destination.binding,material.value,out.textures,out.fallback);if(shadow_double_sided||shadow_single_sided){auto* shadow_chosen=material.value.double_sided?shadow_double_sided:shadow_single_sided;require(shadow_chosen,"Missing alpha shadow material pipeline");shadow_chosen->CreateShaderResourceBinding(&destination.shadow_binding,true);require(destination.shadow_binding,"Alpha shadow material binding failed");bind_surface(destination.shadow_binding,material.value,out.textures,out.fallback);}};
    bind(out.surface,pso,cull,shadow,shadow_cull);if(skin)bind(out.skinned,skin,skin_cull,skin_shadow,skin_shadow_cull);return out;
}
UploadedModel upload(IRenderDevice* device, const RuntimeModel& model, IPipelineState* pso, IPipelineState* pso_cull,IPipelineState* shadow,IPipelineState* shadow_cull) {
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
        if (shadow || shadow_cull) {
            auto* shadow_chosen = material.double_sided ? shadow : shadow_cull;
            require(shadow_chosen, "Alpha shadow material pipeline missing");
            shadow_chosen->CreateShaderResourceBinding(&gpu.shadow_binding, true);
            require(gpu.shadow_binding, "Alpha shadow material binding creation failed");
            bind_surface(gpu.shadow_binding, material, out.textures, out.fallback);
        }
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
        return normalized_shader_source();
    require(in.tellg() >= 0 && in.tellg() <= 256 * 1024, "Shader source byte limit");
    in.seekg(0);
    auto source = normalize_shader_source({std::istreambuf_iterator<char>(in), {}});
    // Older project shader overrides predate the R7 GPU light contract. Keep them loadable, but do not silently
    // advertise GPU-driven shading while the override lacks the shared cluster resources; the built-in contract is
    // the safe source of truth until the override is explicitly upgraded.
    return source.find("ClusterLights") == source.npos ? normalized_shader_source() : source;
}

#ifdef DAE_ANIMATION
std::string skin_shader() {
    std::string source = normalized_shader_source();
    auto replace = [&](std::string_view from, std::string_view to) {
        auto at = source.find(from);
        require(at != source.npos, "Native skin shader template mismatch");
        source.replace(at, from.size(), to);
    };
    replace("struct VertexInput", "cbuffer Skeleton {row_major float4x4 Bones[256];};\nstruct VertexInput");
    replace("float2 UV:ATTRIB2;", "float2 UV:ATTRIB2;uint4 Joints:ATTRIB3;float4 Weights:ATTRIB4;");
    replace("result.Position=mul(local,Mvp);result.Normal=mul(vertex.Normal,(float3x3)Rotation);result.Local=mul(vertex.Position,(float3x3)Rotation);",
            "float4 position=0;float3 normal=0;[unroll]for(uint "
            "i=0;i<4;++i){position+=mul(float4(vertex.Position,1),Bones[vertex.Joints[i]])*vertex.Weights[i];normal+=mul(vertex.Normal,("
            "float3x3)Bones[vertex.Joints[i]])*vertex.Weights[i];}result.Position=mul(position,Mvp);result.Normal=mul(normal,(float3x3)"
            "Rotation);result.Local=mul(position.xyz,(float3x3)Rotation);");
    replace("result.WorldPosition=mul(local,World).xyz;", "result.WorldPosition=mul(position,World).xyz;");
    return source;
}
UploadedSkin upload_skin(IRenderDevice* device, RuntimeSkinnedModel model, IPipelineState* pso, IPipelineState* cull,
                         const AnimationClip* clip, double tick, bool reference, IPipelineState* shadow, IPipelineState* shadow_cull) {
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
    material.family = material_family_toon_character;
    material.double_sided = true;
    material.roughness = .85f;
    material.has_texture = model.textured;
    material.texture = model.id;
    visible.materials.push_back(material);
    if (model.textured)
        visible.textures.emplace_back(model.id, model.texture);
    // Every skinned primitive keeps its stable mesh slot as the draw material key. Assigned primitives use their
    // native material instance; unassigned primitives get an atlas-backed fallback with the same slot identity so
    // scene-local material bindings can replace them without modifying the shared imported skin.
    for (const auto& skin : model.meshes) {
        if (skin.material)
            visible.materials.push_back(*skin.material);
        else {
            auto fallback = material;
            fallback.id = skin.id;
            visible.materials.push_back(fallback);
        }
    }
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
        mesh.parts.push_back({skin.id, 0, static_cast<unsigned>(mesh.indices.size())});
        visible.meshes.push_back(std::move(mesh));
    }
    UploadedSkin result;
    result.gpu = upload(device, visible, pso, cull, shadow, shadow_cull);
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
