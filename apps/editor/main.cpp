#define NOMINMAX
#define UNICODE
#include <Windows.h>
#include <wincodec.h>
#include <EngineFactoryD3D12.h>
#include <EngineFactoryVk.h>
#include <RenderDevice.h>
#include <DeviceContext.h>
#include <SwapChain.h>
#include <Texture.h>
#include <Buffer.h>
#include <PipelineState.h>
#include <ShaderResourceBinding.h>
#include <RefCntAutoPtr.hpp>
#include <ImGuiImplWin32.hpp>
#include <imgui.h>
#include <DirectXMath.h>
#include <DirectXTex.h>
#include <darkangel/assets.hpp>
#include <darkangel/spline.hpp>
#ifdef DAE_ANIMATION
#include <darkangel/animation_assets.hpp>
#include <nlohmann/json.hpp>
#endif
#include <darkangel/editor_service.hpp>
#include "editor_ui.hpp"
#include "editor_options.hpp"
#ifdef DAE_CHARACTER_SCENE
#include "exercises/script_runner.hpp"
#include "exercises/authoring_sources.hpp"
#endif
#include "editor_theme.hpp"
#include "gameplay_debug.hpp"
#include "cue_debug.hpp"
#include "vfx_cue_sink.hpp"
#ifdef DAE_AUDIO
#include "audio_cue_sink.hpp"
#include "primitive_cue_sink.hpp"
#include <darkangel/primitive_assets.hpp>
#include <darkangel/audio_director.hpp>
#include <deque>
#endif
#include "native_input.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#ifdef DAE_CHARACTER_SCENE
#include "character_preview.hpp"
#ifdef DAE_EDITOR_GNS
#include "network_play.hpp"
#endif
#ifdef DAE_HUD
#include "hud_overlay.hpp"
#include "world_ui_presenter.hpp"
#include "runtime_inventory.hpp"
#include "runtime_altar.hpp"
#include "play_saves.hpp"
#include "exercises/world_place_exercise.hpp"
#include "scene_collision.hpp"
#include <darkangel/hud.hpp>
#include <ashen_roots/royal_hud.hpp>
#endif
#include <darkangel/actor_assets.hpp>
#include <darkangel/cue_assets.hpp>
#endif
#ifdef DAE_AGENT_ENDPOINTS
#include <darkangel/editor_rpc.hpp>
#endif
#include <ImGuizmo.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <cmath>
#include <stdexcept>
#include <vector>

using namespace Diligent;
#ifdef DAE_NAVIGATION
#include <darkangel/navigation.hpp>
#endif
#ifdef DAE_BEHAVIOR
#include <darkangel/behavior_tree.hpp>
#endif
using namespace darkangel;
namespace {
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
struct ComposerPackageFile {std::filesystem::path path;~ComposerPackageFile(){std::error_code ignored;std::filesystem::remove(path,ignored);}};
struct ComApartment {ComApartment(){require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Editor COM initialization failed");}~ComApartment(){DirectX::SetWICFactory(nullptr);CoUninitialize();}};
#ifdef DAE_CHARACTER_SCENE
// The editor's scene provider: Play's own preparation, for the scene the party travels to.
struct EditorSceneProvider final:darkangel::SceneProvider {
    std::function<void(AssetId)> on_begin;std::function<darkangel::SceneLoadStatus()> on_poll;std::function<std::unique_ptr<darkangel::PreparedScene>()> on_take;std::function<void()> on_cancel;
    void begin(AssetId scene)override{on_begin(scene);}
    darkangel::SceneLoadStatus poll()override{return on_poll();}
    std::unique_ptr<darkangel::PreparedScene> take()override{return on_take();}
    void cancel()override{on_cancel();}
};
#endif
darkangel::editor_app::NativeInput* window_input{};ImGuiImplWin32* window_gui{};unsigned resize_width{},resize_height{};
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    if(window_input)window_input->message(message,wparam,lparam);
    if(window_gui && window_gui->Win32_ProcHandler(window,message,wparam,lparam))return 1;
    if(message==WM_SIZE){resize_width=LOWORD(lparam);resize_height=HIWORD(lparam);return 0;}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,wparam,lparam);
}
struct Constants {DirectX::XMFLOAT4X4 mvp,rotation;DirectX::XMFLOAT4 color,surface;};
struct GpuMesh {RefCntAutoPtr<IBuffer> vertices,indices;std::vector<MeshPart> parts;std::array<float,4> color{1,1,1,1};};
constexpr const char* shader_source=R"(
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
RefCntAutoPtr<IPipelineState> pipeline(IRenderDevice* device,IBuffer* constants,TEXTURE_FORMAT color,TEXTURE_FORMAT depth,bool cull,const char* source=shader_source,IBuffer* bones=nullptr){
    ShaderCreateInfo shader;shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_HLSL;shader.ShaderCompiler=SHADER_COMPILER_DXC;shader.HLSLVersion={6,0};shader.Desc.UseCombinedTextureSamplers=true;shader.Source=source;
    RefCntAutoPtr<IShader> vs,ps;shader.Desc.Name="DarkAngel stylized prop VS";shader.Desc.ShaderType=SHADER_TYPE_VERTEX;shader.EntryPoint="VSMain";device->CreateShader(shader,&vs);
    shader.Desc.Name="DarkAngel stylized prop PS";shader.Desc.ShaderType=SHADER_TYPE_PIXEL;shader.EntryPoint="PSMain";device->CreateShader(shader,&ps);require(vs && ps,"Pinned DXC shader compilation failed");
    GraphicsPipelineStateCreateInfo ci;ci.PSODesc.Name="DarkAngel stylized static prop";ci.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;ci.pVS=vs;ci.pPS=ps;
    auto& graphics=ci.GraphicsPipeline;graphics.NumRenderTargets=1;graphics.RTVFormats[0]=color;graphics.DSVFormat=depth;graphics.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode=cull?CULL_MODE_BACK:CULL_MODE_NONE;graphics.RasterizerDesc.FrontCounterClockwise=true;graphics.DepthStencilDesc.DepthEnable=true;graphics.DepthStencilDesc.DepthFunc=COMPARISON_FUNC_LESS;
    LayoutElement layout[]={{0,0,3,VT_FLOAT32,false},{1,0,3,VT_FLOAT32,false},{2,0,2,VT_FLOAT32,false},{3,0,4,VT_UINT32,false},{4,0,4,VT_FLOAT32,false}};graphics.InputLayout.LayoutElements=layout;graphics.InputLayout.NumElements=bones?5:3;
    ShaderResourceVariableDesc variables[]={{SHADER_TYPE_PIXEL,"Albedo",SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};ci.PSODesc.ResourceLayout.Variables=variables;ci.PSODesc.ResourceLayout.NumVariables=1;
    SamplerDesc sampler;sampler.MinFilter=FILTER_TYPE_LINEAR;sampler.MagFilter=FILTER_TYPE_LINEAR;sampler.MipFilter=FILTER_TYPE_LINEAR;sampler.AddressU=TEXTURE_ADDRESS_WRAP;sampler.AddressV=TEXTURE_ADDRESS_WRAP;
    ImmutableSamplerDesc samplers[]={{SHADER_TYPE_PIXEL,"Albedo",sampler}};ci.PSODesc.ResourceLayout.ImmutableSamplers=samplers;ci.PSODesc.ResourceLayout.NumImmutableSamplers=1;
    RefCntAutoPtr<IPipelineState> result;device->CreateGraphicsPipelineState(ci,&result);require(result,"Pipeline creation failed");
    auto* vertex_frame=result->GetStaticVariableByName(SHADER_TYPE_VERTEX,"Frame");auto* pixel_frame=result->GetStaticVariableByName(SHADER_TYPE_PIXEL,"Frame");require(vertex_frame && pixel_frame,"Shader changed the declared Frame resource contract");vertex_frame->Set(constants);pixel_frame->Set(constants);if(bones){auto* skeleton=result->GetStaticVariableByName(SHADER_TYPE_VERTEX,"Skeleton");require(skeleton,"Missing GPU skin palette contract");skeleton->Set(bones);}return result;
}
RefCntAutoPtr<ITexture> texture(IRenderDevice* device,const CookedTexture& cooked){require(!cooked.mips.empty(),"Missing texture mips");TextureDesc desc;desc.Name="Cooked DarkAngel sRGB color";desc.Type=RESOURCE_DIM_TEX_2D;desc.Width=cooked.mips[0].width;desc.Height=cooked.mips[0].height;desc.MipLevels=static_cast<Uint32>(cooked.mips.size());desc.Format=TEX_FORMAT_RGBA8_UNORM_SRGB;desc.Usage=USAGE_IMMUTABLE;desc.BindFlags=BIND_SHADER_RESOURCE;
    std::vector<TextureSubResData> subresources;for(const auto& mip:cooked.mips){TextureSubResData data;data.pData=mip.rgba.data();data.Stride=mip.width*4;subresources.push_back(data);}TextureData data;data.pSubResources=subresources.data();data.NumSubresources=static_cast<Uint32>(subresources.size());RefCntAutoPtr<ITexture> result;device->CreateTexture(desc,&data,&result);require(result,"Cooked texture upload failed");return result;
}
void capture(IRenderDevice* device,IDeviceContext* context,ITexture* source,const std::filesystem::path& path,const darkangel::editor_app::ViewArea& viewport){
    TextureDesc desc=source->GetDesc();desc.Name="DarkAngel capture readback";desc.BindFlags=BIND_NONE;desc.Usage=USAGE_STAGING;desc.CPUAccessFlags=CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;device->CreateTexture(desc,nullptr,&staging);require(staging,"Readback texture creation failed");context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);CopyTextureAttribs copy{source,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,staging,RESOURCE_STATE_TRANSITION_MODE_TRANSITION};context->CopyTexture(copy);context->Flush();context->WaitForIdle();
    MappedTextureSubresource mapped;context->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);require(mapped.pData,"GPU readback failed");
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(desc.Width)*desc.Height*4);for(unsigned y=0;y<desc.Height;++y)std::memcpy(pixels.data()+static_cast<std::size_t>(y)*desc.Width*4,static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.Stride,desc.Width*4);context->UnmapTextureSubresource(staging,0,0);
    // The viewport area excludes the inspector; UI pixels alone cannot satisfy this gate.
    std::size_t changed{};auto left=static_cast<unsigned>(std::clamp(viewport.x,0.f,static_cast<float>(desc.Width-1))),top=static_cast<unsigned>(std::clamp(viewport.y,0.f,static_cast<float>(desc.Height-1)));auto right=static_cast<unsigned>(std::clamp(viewport.x+viewport.width,0.f,static_cast<float>(desc.Width))),bottom=static_cast<unsigned>(std::clamp(viewport.y+viewport.height,0.f,static_cast<float>(desc.Height)));require(right>left+40 && bottom>top+40,"Viewport too small for capture gate");auto ref=(static_cast<std::size_t>(top+10)*desc.Width+left+10)*4;
    for(unsigned y=top+20;y+20<bottom;++y)for(unsigned x=left+20;x+20<right;++x){auto at=(static_cast<std::size_t>(y)*desc.Width+x)*4;unsigned delta{};for(unsigned c=0;c<3;++c)delta+=static_cast<unsigned>(std::abs(static_cast<int>(pixels[at+c])-pixels[ref+c]));if(delta>40)++changed;}
    require(changed>2000,"Viewport did not draw foreground geometry");std::filesystem::create_directories(path.parent_path());DirectX::Image image{desc.Width,desc.Height,DXGI_FORMAT_R8G8B8A8_UNORM,static_cast<std::size_t>(desc.Width)*4,pixels.size(),pixels.data()};// Encoded in memory and then written, trying again for two seconds: another program (a thumbnailer, an indexer) may briefly hold a capture that was just replaced.
    DirectX::Blob encoded;require(SUCCEEDED(DirectX::SaveToWICMemory(image,DirectX::WIC_FLAGS_NONE,GUID_ContainerFormatPng,encoded)),"PNG capture failed");
    bool written=false;for(unsigned attempt=0;attempt<40&&!written;++attempt){{std::ofstream out(path,std::ios::binary|std::ios::trunc);if(out){out.write(reinterpret_cast<const char*>(encoded.GetBufferPointer()),static_cast<std::streamsize>(encoded.GetBufferSize()));out.close();written=!out.fail();}}if(!written)std::this_thread::sleep_for(std::chrono::milliseconds(50));}
    require(written,"The capture could not be written: its file is held by another program");std::cout<<"Viewport foreground pixels="<<changed<<" capture="<<path.string()<<'\n';
}
struct Material {CookedMaterial value;RefCntAutoPtr<IShaderResourceBinding> binding;};
struct UploadedModel {
    std::vector<GpuMesh> meshes;std::map<AssetId,RefCntAutoPtr<ITexture>> textures;
    std::map<AssetId,Material> materials;RefCntAutoPtr<ITexture> fallback;
    std::array<float,3> center{},half{};float radius{};
};
UploadedModel upload(IRenderDevice* device,const RuntimeModel& model,IPipelineState* pso,IPipelineState* pso_cull){
    require(!model.meshes.empty(),"Empty cooked model");UploadedModel out;auto minimum=model.meshes[0].minimum,maximum=model.meshes[0].maximum;
    for(const auto& mesh:model.meshes){GpuMesh gpu;gpu.parts=mesh.parts;BufferDesc desc;desc.Name="Cooked static vertices";desc.Usage=USAGE_IMMUTABLE;desc.BindFlags=BIND_VERTEX_BUFFER;desc.Size=mesh.vertices.size()*sizeof(Vertex);BufferData data{mesh.vertices.data(),desc.Size};device->CreateBuffer(desc,&data,&gpu.vertices);desc.Name="Cooked static indices";desc.BindFlags=BIND_INDEX_BUFFER;desc.Size=mesh.indices.size()*sizeof(std::uint32_t);data={mesh.indices.data(),desc.Size};device->CreateBuffer(desc,&data,&gpu.indices);require(gpu.vertices && gpu.indices,"Mesh upload failed");out.meshes.push_back(std::move(gpu));for(unsigned c=0;c<3;++c){minimum[c]=std::min(minimum[c],mesh.minimum[c]);maximum[c]=std::max(maximum[c],mesh.maximum[c]);}}
    CookedTexture white;white.mips.push_back({1,1,{255,255,255,255}});out.fallback=texture(device,white);for(const auto& [id,cooked]:model.textures)out.textures.emplace(id,texture(device,cooked));
    for(const auto& material:model.materials){Material gpu;gpu.value=material;auto* chosen=material.double_sided?pso:pso_cull;chosen->CreateShaderResourceBinding(&gpu.binding,true);require(gpu.binding,"Material binding creation failed");auto* variable=gpu.binding->GetVariableByName(SHADER_TYPE_PIXEL,"Albedo");require(variable,"Shader changed the declared Albedo resource contract");auto* view=(material.has_texture?out.textures.at(material.texture):out.fallback)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);variable->Set(view);out.materials.emplace(material.id,std::move(gpu));}
    for(unsigned c=0;c<3;++c){out.center[c]=(minimum[c]+maximum[c])*.5f;out.half[c]=(maximum[c]-minimum[c])*.5f;out.radius=std::max(out.radius,out.half[c]);}require(out.radius>1e-6f,"Model has zero bounds");return out;
}
std::string shader_text(){std::ifstream in("games/AshenRoots/shaders/stylized_prop.hlsl",std::ios::binary|std::ios::ate);if(!in)return shader_source;require(in.tellg()>=0 && in.tellg()<=256*1024,"Shader source byte limit");in.seekg(0);return {std::istreambuf_iterator<char>(in),{}};}

#ifdef DAE_ANIMATION
std::string skin_shader(){std::string source=shader_source;auto replace=[&](std::string_view from,std::string_view to){auto at=source.find(from);require(at!=source.npos,"Native skin shader template mismatch");source.replace(at,from.size(),to);};
    replace("struct VertexInput", "cbuffer Skeleton {row_major float4x4 Bones[256];};\nstruct VertexInput");
    replace("float2 UV:ATTRIB2;", "float2 UV:ATTRIB2;uint4 Joints:ATTRIB3;float4 Weights:ATTRIB4;");
    replace("result.Position=mul(float4(vertex.Position,1),Mvp);result.Normal=mul(vertex.Normal,(float3x3)Rotation);", "float4 position=0;float3 normal=0;[unroll]for(uint i=0;i<4;++i){position+=mul(float4(vertex.Position,1),Bones[vertex.Joints[i]])*vertex.Weights[i];normal+=mul(vertex.Normal,(float3x3)Bones[vertex.Joints[i]])*vertex.Weights[i];}result.Position=mul(position,Mvp);result.Normal=mul(normal,(float3x3)Rotation);");return source;
}
struct UploadedSkin {RuntimeSkinnedModel model;UploadedModel gpu;std::unique_ptr<RigPose> pose;};
UploadedSkin upload_skin(IRenderDevice* device,RuntimeSkinnedModel model,IPipelineState* pso,IPipelineState* cull,const AnimationClip* clip=nullptr,double tick=0,bool reference=false){
    RigPose reference_pose(model.rig.definition,model.rig.archive);const auto& sampled=clip?reference_pose.sample(*clip,tick):reference_pose.rest_pose();std::vector<std::array<float,16>> matrices(sampled.size());for(unsigned joint=0;joint<sampled.size();++joint)for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)for(unsigned k=0;k<4;++k)matrices[joint][col*4+row]+=sampled[joint].values[k*4+row]*model.inverse_bind[joint][col*4+k];
    static_assert(sizeof(SkinnedVertex)==64);require(model.inverse_bind.size()<=256,"GPU skin joint limit");RuntimeModel visible;visible.id=model.id;
    CookedMaterial material;material.id=model.id;material.double_sided=true;material.roughness=.85f;material.has_texture=model.textured;material.texture=model.id;visible.materials.push_back(material);if(model.textured)visible.textures.emplace_back(model.id,model.texture);
    for(const auto& skin:model.meshes){CookedMesh mesh;mesh.indices=skin.indices;mesh.minimum={1e9f,1e9f,1e9f};mesh.maximum={-1e9f,-1e9f,-1e9f};for(const auto& vertex:skin.vertices){auto geometry=vertex.geometry;if(reference){geometry.position={};geometry.normal={};for(unsigned influence=0;influence<4;++influence){const auto& matrix=matrices[vertex.joints[influence]];for(unsigned axis=0;axis<3;++axis){float position=matrix[12+axis],normal=0;for(unsigned component=0;component<3;++component){position+=matrix[component*4+axis]*vertex.geometry.position[component];normal+=matrix[component*4+axis]*vertex.geometry.normal[component];}geometry.position[axis]+=position*vertex.weights[influence];geometry.normal[axis]+=normal*vertex.weights[influence];}}}mesh.vertices.push_back(geometry);for(unsigned c=0;c<3;++c){mesh.minimum[c]=std::min(mesh.minimum[c],vertex.geometry.position[c]);mesh.maximum[c]=std::max(mesh.maximum[c],vertex.geometry.position[c]);}}mesh.parts.push_back({model.id,0,static_cast<unsigned>(mesh.indices.size())});visible.meshes.push_back(std::move(mesh));}
    UploadedSkin result;result.gpu=upload(device,visible,pso,cull);for(unsigned index=0;index<model.meshes.size();++index)result.gpu.meshes[index].color=model.meshes[index].color;if(!reference)for(unsigned index=0;index<model.meshes.size();++index){const auto& mesh=model.meshes[index];BufferDesc desc;desc.Name="Canonical four-influence skin vertices";desc.Usage=USAGE_IMMUTABLE;desc.BindFlags=BIND_VERTEX_BUFFER;desc.Size=mesh.vertices.size()*sizeof(SkinnedVertex);BufferData data{mesh.vertices.data(),desc.Size};device->CreateBuffer(desc,&data,&result.gpu.meshes[index].vertices);require(result.gpu.meshes[index].vertices,"GPU skin vertex upload failed");}
    result.pose=std::make_unique<RigPose>(model.rig.definition,model.rig.archive);result.model=std::move(model);return result;
}
void upload_palette(IDeviceContext* context,IBuffer* buffer,UploadedSkin& skin,const AnimationClip* clip,double tick,const std::vector<JointMatrix>* gameplay=nullptr){const auto& matrices=gameplay?*gameplay:(clip?skin.pose->sample(*clip,tick):skin.pose->rest_pose());require(matrices.size()==skin.model.inverse_bind.size(),"Gameplay pose skin layout mismatch");std::array<std::array<float,16>,256> palette{};for(unsigned joint=0;joint<matrices.size();++joint)for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)for(unsigned k=0;k<4;++k)palette[joint][col*4+row]+=matrices[joint].values[k*4+row]*skin.model.inverse_bind[joint][col*4+k];void* mapped{};context->MapBuffer(buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"GPU skin palette map failed");std::memcpy(mapped,palette.data(),sizeof(palette));context->UnmapBuffer(buffer,MAP_WRITE);}
#endif

}
int main(int argc,char** argv){try{
    auto args=darkangel::editor_app::parse_editor_options(argc,argv);auto id=AssetId::parse(args.model);ModelStore store;auto lease=store.prepare(args.registry,args.cas,id);store.publish(lease);
    ComApartment apartment;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DarkAngelM2Editor";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);require(RegisterClassW(&wc),"Window class registration failed");
    RECT rect{0,0,static_cast<LONG>(args.width),static_cast<LONG>(args.height)};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);HWND window=CreateWindowW(wc.lpszClassName,L"DarkAngel Editor",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,wc.hInstance,nullptr);require(window,"Editor window creation failed");
    RefCntAutoPtr<IRenderDevice> device;RefCntAutoPtr<IDeviceContext> context;RefCntAutoPtr<ISwapChain> swap;
    RECT initial_client{};require(GetClientRect(window,&initial_client)&&initial_client.right>0&&initial_client.bottom>0,"Editor client size unavailable");SwapChainDesc swap_desc;swap_desc.Width=static_cast<unsigned>(initial_client.right);swap_desc.Height=static_cast<unsigned>(initial_client.bottom);swap_desc.ColorBufferFormat=TEX_FORMAT_RGBA8_UNORM_SRGB;swap_desc.DepthBufferFormat=TEX_FORMAT_D32_FLOAT;NativeWindow native(window);
    if(args.backend=="d3d12"){auto* factory=GetEngineFactoryD3D12();EngineD3D12CreateInfo ci;ci.pDxCompilerPath=DAE_DXC_DLL;ci.EnableValidation=true;factory->CreateDeviceAndContextsD3D12(ci,&device,&context);require(device && context,"D3D12 device initialization failed");factory->CreateSwapChainD3D12(device,context,swap_desc,FullScreenModeDesc{},native,&swap);}
    else{auto* factory=GetEngineFactoryVk();EngineVkCreateInfo ci;ci.pDxCompilerPath=DAE_DXC_DLL;ci.EnableValidation=true;factory->CreateDeviceAndContextsVk(ci,&device,&context);require(device && context,"Vulkan device initialization failed");factory->CreateSwapChainVk(device,context,swap_desc,native,&swap);}require(swap,"Swap chain creation failed");
    auto gui=ImGuiImplWin32::Create(ImGuiDiligentCreateInfo{device,swap->GetDesc()},window);window_gui=gui.get();ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_DockingEnable;ImGui::GetIO().IniFilename=nullptr;
    darkangel::editor_app::initialize_theme(GetDpiForWindow(window)/96.f);if(!args.hidden)ShowWindow(window,SW_SHOW);
    BufferDesc cb;cb.Name="DarkAngel frame constants";cb.Size=sizeof(Constants);cb.Usage=USAGE_DYNAMIC;cb.BindFlags=BIND_UNIFORM_BUFFER;cb.CPUAccessFlags=CPU_ACCESS_WRITE;RefCntAutoPtr<IBuffer> constant_buffer;device->CreateBuffer(cb,nullptr,&constant_buffer);require(constant_buffer,"Constant buffer creation failed");
    auto source=shader_text();auto pso=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,false,source.c_str());auto pso_cull=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,true,source.c_str());
    // A projectile in flight is drawn as a small arrow-like solid built here, not from an asset.
    UploadedModel projectile_gpu;std::size_t projectiles_seen{};
    {RuntimeModel shape;shape.id=AssetId::random();CookedMaterial material;material.id=shape.id;material.color={3.2f,2.3f,.6f,1.f};material.roughness=.4f;material.double_sided=true;shape.materials.push_back(material);
        CookedMesh mesh;const float half=.45f,girth=.085f;const std::array<std::array<float,3>,6> points{{{0,0,half},{0,0,-half},{girth,0,0},{0,girth,0},{-girth,0,0},{0,-girth,0}}};
        for(const auto& point:points){const float length=std::sqrt(point[0]*point[0]+point[1]*point[1]+point[2]*point[2]);mesh.vertices.push_back({point,{point[0]/length,point[1]/length,point[2]/length},{0,0}});}
        mesh.indices={0,2,3,0,3,4,0,4,5,0,5,2,1,3,2,1,4,3,1,5,4,1,2,5};mesh.minimum={-girth,-girth,-half};mesh.maximum={girth,girth,half};mesh.parts.push_back({shape.id,0,static_cast<std::uint32_t>(mesh.indices.size())});shape.meshes.push_back(std::move(mesh));
        projectile_gpu=upload(device,shape,pso,pso_cull);}
    auto gpu=upload(device,*lease.model,pso,pso_cull);darkangel::editor_app::Controller editor(id,gpu.radius);darkangel::editor_app::Shell shell;if(!args.sources.empty())editor.configure_assets(args.sources,args.asset_cache);shell.select_workspace(args.character_workspace?darkangel::editor_app::Workspace::Character:args.npc_workspace?darkangel::editor_app::Workspace::Npc:args.player_workspace?darkangel::editor_app::Workspace::Player:args.animation_workspace?darkangel::editor_app::Workspace::Animation:args.ability_workspace?darkangel::editor_app::Workspace::Ability:args.assets_workspace?darkangel::editor_app::Workspace::Assets:(args.game_workspace||args.exercise_character||args.exercise_combat||args.exercise_heavy)?darkangel::editor_app::Workspace::Game:darkangel::editor_app::Workspace::Scene);if(args.exercise_reaction){shell.focus_gameplay_inspector=true;shell.select_effect_debug=true;}if(args.exercise_camera_preview){shell.camera_preview=1;shell.camera_preview_yaw=25;shell.camera_preview_pitch=10;}if(args.tools_workspace)shell.open_tool(darkangel::editor_app::ToolView::InputBindings);if(args.navigation_tool)shell.open_tool(darkangel::editor_app::ToolView::Navigation);if(args.settings_workspace)shell.select_workspace(darkangel::editor_app::Workspace::Settings);if(!args.import_preview.empty())shell.open_import(args.import_preview);std::size_t import_count=editor.asset_inventory.size();
    std::unique_ptr<InputManager> inputs;std::unique_ptr<darkangel::editor_app::NativeInput> native_input;
    AssetId selected_input;
#ifdef DAE_ANIMATION
    // A Player not yet in the start registry is packaged when Play prepares its resources; with a project mount that is not an error.
    if(!args.player_asset.empty())try{selected_input=load_cooked_player(args.registry,args.cas,AssetId::parse(args.player_asset)).input.id;}catch(const std::exception&){if(args.sources.empty())throw;}
    else if(args.character_clips.starts_with("player:"))selected_input=load_cooked_player(args.registry,args.cas,AssetId::parse(args.character_clips.substr(7))).input.id;
    else if(args.character_clips.starts_with("kit:"))selected_input=load_cooked_combat_kit(args.registry,args.cas,AssetId::parse(args.character_clips.substr(4))).input.id;
#endif
    {std::ifstream file(args.registry);auto registry=nlohmann::json::parse(file);for(const auto& record:registry.at("assets"))if(record.at("kind")=="input"&&(selected_input==AssetId{}||record.at("id")==selected_input.text())){require(!inputs,"Editor package has multiple input profiles; select a Player or Combat Kit explicitly");inputs=std::make_unique<InputManager>(load_cooked_input(args.registry,args.cas,AssetId::parse(record.at("id").get<std::string>())));}if(inputs){auto controls=inputs->controls();native_input=std::make_unique<darkangel::editor_app::NativeInput>(controls);window_input=native_input.get();shell.input=inputs.get();}}
    std::map<AssetId,UploadedModel> scene_models;
    // Models worn by the active Play actor (mask visuals); released when Play stops.
    std::map<AssetId,UploadedModel> worn_models;std::set<AssetId> worn_failed;
#ifdef DAE_ANIMATION
    RefCntAutoPtr<IBuffer> bone_buffer;cb.Name="Canonical human skin palette";cb.Size=256*16*sizeof(float);device->CreateBuffer(cb,nullptr,&bone_buffer);require(bone_buffer,"Skin palette buffer creation failed");auto skin_source=skin_shader();auto skin_pso=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,false,skin_source.c_str(),bone_buffer);auto skin_cull=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,true,skin_source.c_str(),bone_buffer);std::map<AssetId,UploadedSkin> scene_skins;std::unique_ptr<AnimationClip> diagnostic_clip;if(!args.pose_clip.empty()){auto cooked=load_cooked_clip(args.registry,args.cas,AssetId::parse(args.pose_clip));diagnostic_clip=std::make_unique<AnimationClip>(std::move(cooked.definition),cooked.archive);}
    auto is_skin=[&](AssetId asset){std::ifstream input(args.registry);auto registry=nlohmann::json::parse(input);for(const auto& record:registry.at("assets"))if(record.at("id")==asset.text())return record.at("kind")=="human-binding";return false;};
#endif
    editor.prepare_model=[&](AssetId requested){if(requested==id||scene_models.contains(requested))return;require(scene_models.size()<63,"Editor model resource limit");
#ifdef DAE_ANIMATION
        if(scene_skins.contains(requested))return;if(is_skin(requested)){require(scene_skins.size()<4,"Editor skin resource limit");scene_skins.emplace(requested,upload_skin(device,load_cooked_skinned_model(args.registry,args.cas,requested),args.skin_reference?pso.RawPtr():skin_pso.RawPtr(),args.skin_reference?pso_cull.RawPtr():skin_cull.RawPtr(),diagnostic_clip.get(),args.pose_tick,args.skin_reference));return;}
#endif
        auto cooked=load_cooked_model(args.registry,args.cas,requested);auto resources=upload(device,cooked,pso,pso_cull);scene_models.emplace(requested,std::move(resources));};
    if(!args.scene.empty())editor.open(args.scene);const auto import_baseline=editor.document->world().serialize();
#ifdef DAE_CHARACTER_SCENE
    auto scene_role=[&](const std::string& value){if(value.empty())return StableId{};auto id=StableId::parse(value);if(editor.document->world().valid(editor.document->world().find(id)))return id;StableId resolved;for(const auto& origin:editor.document->plan().origins)if(origin.local_path.ends_with("/"+id.text())){require(!resolved,"Ambiguous character scene role; use a placed identity");resolved=origin.object;}require(bool(resolved),"Character scene role missing");return resolved;};
    std::optional<UploadedSkin> authoring_skin;CharacterDefinition authoring_character_definition;
    std::unique_ptr<darkangel::editor_app::GraphPosePreview> graph_pose;std::unique_ptr<darkangel::editor_app::ComposerPosePreview> composer_pose;std::vector<JointMatrix> composer_matrices;
    // Presentation cues of the Play session: the cooked cue assets, the player the session drives, and two recording sinks that stand in
    // for the visual and the audio service until they exist. Declared before the session, which must not outlive its cue player.
    std::optional<std::vector<darkangel::CueDefinition>> play_cues;std::size_t play_cue_closure{};darkangel::editor_app::RecordingCueSink cue_visual{true},cue_audio{false};bool cue_debug_selected{};
    // Visual effects: one service on this device, the effect asset behind each cue visual, and the sink that plays them. The recording sink stays behind it for the Cues tab.
    std::unique_ptr<darkangel::VfxService> vfx;std::map<std::string,AssetId> play_effects;std::map<std::string,std::string> play_effect_generations;darkangel::editor_app::PrimitiveCueSink cue_primitives{&cue_visual};std::map<AssetId,darkangel::TrailDefinition> play_trails;std::map<AssetId,darkangel::DecalDefinition> play_decals;darkangel::editor_app::VfxCueSink cue_effects{&cue_primitives};std::uint64_t vfx_tick{};std::string vfx_failed;
    try{vfx=std::make_unique<darkangel::VfxService>(device,context,int(swap_desc.ColorBufferFormat),int(swap_desc.DepthBufferFormat));}catch(const std::exception& error){vfx_failed=error.what();editor.log(std::string("Visual effects unavailable: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Warning);}
#ifdef DAE_AUDIO
    // Sounds: one audio service for the process, made when the first Play needs it. A hidden run and --no-audio use the silent driver. The recording sink stays behind it for the Cues tab.
    std::unique_ptr<darkangel::AudioService> audio;std::map<std::string,AssetId> play_sounds;darkangel::editor_app::AudioCueSink cue_sounds{&cue_audio};std::string audio_failed;bool audio_tried{};
    // Music and ambience: one director while a Play session or a profile audition lasts. The mixer is the project's audio/project.damix,
    // read from its open draft when there is one so that a slider moved in the Audio tab is heard at once.
    std::unique_ptr<darkangel::AudioDirector> audio_director;std::map<AssetId,darkangel::AudioProfile> play_profiles;std::map<StableId,AssetId> encounter_profiles;std::optional<darkangel::AudioMix> audio_applied;bool audio_in_play{};std::map<AssetId,std::string> audio_generations;
    struct AudioTab {AssetId sound,profile;std::uint64_t audition{};bool profile_audition{};char sound_name[49]{"new_sound"},profile_name[49]{"new_profile"},import_path[512]{};std::string error;darkangel::AudioMix file_mix;std::filesystem::file_time_type file_seen{};bool file_known{};} audio_tab;
    const std::string audio_mix_path="audio/project.damix";
    auto project_mix=[&]()->darkangel::AudioMix{if(!editor.assets)return {};
        try{for(const auto& asset:editor.asset_inventory)if(asset.path==audio_mix_path){if(const auto draft=editor.authoring.drafts.find(asset.id);draft!=editor.authoring.drafts.end())return darkangel::decode_audio_mix(draft->second.value.dump()).mix;}
            const auto file=editor.assets->source_root()/audio_mix_path;std::error_code ignored;const auto written=std::filesystem::last_write_time(file,ignored);if(ignored){audio_tab.file_known=false;return {};}
            if(!audio_tab.file_known||written!=audio_tab.file_seen){std::ifstream in(file,std::ios::binary);const std::string text{std::istreambuf_iterator<char>(in),{}};audio_tab.file_mix=darkangel::decode_audio_mix(text).mix;audio_tab.file_seen=written;audio_tab.file_known=true;}
            return audio_tab.file_mix;}
        catch(const std::exception&){return audio_tab.file_known?audio_tab.file_mix:darkangel::AudioMix{};}};
    // The service is made when something first needs to be heard, and made again when the mixer's limiter or ducking changed and nothing is playing.
    auto ensure_audio=[&]{if(!editor.assets)return false;const auto mix=project_mix();
        if(audio&&!mix.same_structure(audio->started_mix())&&!audio_in_play&&!audio_director&&audio->stats().playing==0){cue_sounds.bind(nullptr,nullptr,{});audio.reset();audio_tried=false;audio_applied.reset();}
        if(!audio&&!audio_tried){audio_tried=true;try{darkangel::AudioOptions options;options.device=!args.hidden&&!args.no_audio;options.work=editor.assets->cas_path().parent_path();options.mix=mix;audio=std::make_unique<darkangel::AudioService>(options);audio_applied=mix;audio_failed.clear();}catch(const std::exception& error){audio_failed=error.what();editor.log(std::string("Audio unavailable: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Warning);}}
        return bool(audio);};
    // Cooks and loads sounds by identity: WAVs (default settings), sound definitions, and whole profiles with the definitions they carry. Returns the profiles.
    auto load_audio=[&](std::vector<AssetId> wanted){std::map<AssetId,darkangel::AudioProfile> profiles;if(!audio||!editor.assets)return profiles;std::map<AssetId,std::string> paths;for(const auto& asset:editor.assets->assets())paths[asset.id]=asset.path;
        std::vector<AssetId> roots;for(const auto id:wanted)if(paths.contains(id)&&std::find(roots.begin(),roots.end(),id)==roots.end())roots.push_back(id);if(roots.empty())return profiles;require(roots.size()<=64,"At most 64 sounds and profiles are loaded at once");
        const auto registry=editor.assets->cas_path().parent_path()/("audio-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);
        std::deque<darkangel::CookedSound> waves;std::deque<darkangel::CookedSoundDefinition> definitions;std::vector<darkangel::AudioSound> sounds;std::set<AssetId> listed;
        const auto define=[&](darkangel::CookedSoundDefinition cooked){if(!listed.insert(cooked.definition.id).second)return;definitions.push_back(std::move(cooked));const auto& held=definitions.back();sounds.push_back({held.definition.id,held.generation,&held.wave,held.definition.settings});};
        for(const auto id:roots){const auto extension=std::filesystem::path(paths.at(id)).extension();
            if(extension==".wav"){if(!listed.insert(id).second)continue;waves.push_back(darkangel::load_cooked_sound(registry,editor.assets->cas_path(),id));sounds.push_back({id,waves.back().generation,&waves.back(),{}});}
            else if(extension==".dasound")define(darkangel::load_cooked_sound_definition(registry,editor.assets->cas_path(),id));
            else if(extension==".daaudioprofile"){auto cooked=darkangel::load_cooked_audio_profile(registry,editor.assets->cas_path(),id);for(auto& held:cooked.sounds)define(std::move(held));profiles[id]=std::move(cooked.profile);}}
        for(const auto& sound:sounds)audio_generations[sound.id]=sound.generation;audio->load(sounds);return profiles;};
#endif
    // The VFX tab: the selected effect is cooked and played in a loop in the preview, and cooked again whenever its source file changes on disk
    // (that is how an edit saved in Effekseer shows up). A refusal by the cooker is shown as it is.
    struct VfxPreview {AssetId selected;std::string path,error,generation;std::filesystem::file_time_type seen{};bool cooked{};std::uint64_t handle{};float yaw{.6f},distance{4.5f},height{1.f};char name[49]{"new_effect"};AssetId cue,primitive;char cue_name[49]{"new_cue"},primitive_name[49]{"new_trail"};std::string cue_error;double idle{};std::size_t textures{};unsigned cooks{};} vfx_preview;
    auto vfx_effects=[&]{std::vector<darkangel::AssetInfo> found;for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();if(extension==".efkproj"||extension==".efkefc")found.push_back(asset);}return found;};
    auto vfx_cook=[&]{auto& preview=vfx_preview;preview.cooked=false;preview.error.clear();if(!vfx||!editor.assets||preview.selected==AssetId{})return;if(preview.handle){vfx->kill(preview.handle);preview.handle=0;}
        try{std::error_code ignored;preview.seen=std::filesystem::last_write_time(editor.assets->source_root()/preview.path,ignored);const auto registry=editor.assets->cas_path().parent_path()/("vfx-preview-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};const std::array roots{preview.selected};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);
            const auto cooked=darkangel::load_cooked_vfx(registry,editor.assets->cas_path(),preview.selected);vfx->load(cooked);preview.generation=cooked.generation;preview.textures=cooked.textures.size();preview.cooked=true;++preview.cooks;preview.idle=0;}
        catch(const std::exception& error){preview.error=error.what();}};
    auto vfx_select=[&](const darkangel::AssetInfo& asset){vfx_preview.selected=asset.id;vfx_preview.path=asset.path;vfx_cook();};
    // Cues in the VFX tab: the project's cue sources as ordinary native drafts (Undo, Redo, Save All), edited in a small form.
    auto vfx_cues=[&]{auto& preview=vfx_preview;const auto red=[](const std::string& text){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1.f,.45f,.35f,1.f));ImGui::TextWrapped("%s",text.c_str());ImGui::PopStyleColor();};
        ImGui::SeparatorText("Cues");ImGui::TextWrapped("A cue is what gameplay asks for by key: an effect, a sound or both, played on the actor.");
        std::vector<darkangel::AssetInfo> cues,sounds,trail_assets,decal_assets;for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();if(extension==".dacue")cues.push_back(asset);else if(extension==".wav"||extension==".dasound")sounds.push_back(asset);else if(extension==".datrail")trail_assets.push_back(asset);else if(extension==".dadecal")decal_assets.push_back(asset);}
        if(cues.empty())ImGui::TextDisabled("This project has no cues yet.");
        for(const auto& asset:cues){const auto draft=editor.authoring.drafts.find(asset.id);const bool dirty=draft!=editor.authoring.drafts.end()&&draft->second.dirty();if(ImGui::Selectable((asset.path+(dirty?" *":"")+"##cue").c_str(),asset.id==preview.cue)){preview.cue=asset.id;preview.cue_error.clear();}}
        ImGui::SetNextItemWidth(220);ImGui::InputText("##cue-name",preview.cue_name,sizeof(preview.cue_name));ImGui::SameLine();
        if(ImGui::Button("New cue")){try{preview.cue=editor.authoring.create_cue(*editor.assets,preview.cue_name);editor.refresh_assets();preview.cue_error.clear();}catch(const std::exception& error){preview.cue_error=error.what();}}
        if(preview.cue!=AssetId{}){darkangel::editor_app::NativeDraft* draft{};try{draft=&editor.authoring.open(*editor.assets,preview.cue);}catch(const std::exception& error){preview.cue_error=error.what();preview.cue=AssetId{};}
            if(draft){auto& value=draft->value;ImGui::SeparatorText("Selected cue");ImGui::TextWrapped("%s",draft->asset.path.c_str());
                const auto field=[&](const char* label,const char* name){char buffer[65]{};const auto current=value.value(name,std::string{});std::snprintf(buffer,sizeof(buffer),"%s",current.c_str());if(ImGui::InputText(label,buffer,sizeof(buffer)))value[name]=std::string(buffer);};
                field("Name##cue","name");field("Key##cue","key");
                const auto pick=[&](const char* label,const char* name,const std::vector<darkangel::AssetInfo>& choices,bool effect){const auto current=value.value(name,std::string{});if(ImGui::BeginCombo(label,current.empty()?"(none)":current.c_str())){if(ImGui::Selectable("(none)",current.empty()))value[name]="";for(const auto& choice:choices)if(ImGui::Selectable(choice.path.c_str(),choice.path==current)){value[name]=choice.path;if(effect)vfx_select(choice);}ImGui::EndCombo();}};
                pick("Effect##cue","visual",vfx_effects(),true);pick("Sound##cue","audio",sounds,false);
                // A trail and a decal are optional fields of the cue: absent when none is chosen.
                const auto optional=[&](const char* label,const char* name,const std::vector<darkangel::AssetInfo>& choices){const auto current=value.value(name,std::string{});if(ImGui::BeginCombo(label,current.empty()?"(none)":current.c_str())){if(ImGui::Selectable("(none)",current.empty()))value.erase(name);for(const auto& choice:choices)if(ImGui::Selectable(choice.path.c_str(),choice.path==current)){value[name]=choice.path;preview.primitive=choice.id;}ImGui::EndCombo();}};
                optional("Weapon trail##cue","trail",trail_assets);optional("Decal##cue","decal",decal_assets);
                int tail=int(value.value("tail_ticks",30u));if(ImGui::InputInt("Tail (ticks)##cue",&tail))value["tail_ticks"]=unsigned(std::clamp(tail,0,600));
                bool predictable=value.value("predictable",false);if(ImGui::Checkbox("Starts on prediction (the owner's own cue)",&predictable))value["predictable"]=predictable;
                std::string problem;try{darkangel::decode_cue(value.dump());}catch(const std::exception& error){problem=error.what();}if(!problem.empty())red(problem);
                ImGui::BeginDisabled(!draft->dirty()||!problem.empty());if(ImGui::Button("Save cue")){try{const auto path=draft->asset.path;editor.authoring.save(*editor.assets,preview.cue);editor.refresh_assets();play_cues.reset();preview.cue_error.clear();editor.log("Saved "+path);}catch(const std::exception& error){preview.cue_error=error.what();}}ImGui::EndDisabled();
                if(args.exercise_cue_form)ImGui::SetScrollHereY(1.f);
                try{editor.authoring.record_changes(*editor.assets,"Edit cue",ImGui::IsAnyItemActive());}catch(const std::exception& error){preview.cue_error=error.what();}}}
        if(!preview.cue_error.empty())red(preview.cue_error);
        // Weapon trails and decals: native, small, and edited here because a cue is what names them.
        ImGui::SeparatorText("Trails and decals");ImGui::TextWrapped("A weapon trail is a strip swept by two points on a socket while its cue is live. A decal is a soft disc laid under the actor when its cue starts.");
        for(const auto* list:{&trail_assets,&decal_assets})for(const auto& asset:*list){const auto draft=editor.authoring.drafts.find(asset.id);const bool dirty=draft!=editor.authoring.drafts.end()&&draft->second.dirty();if(ImGui::Selectable((asset.path+(dirty?" *":"")+"##primitive").c_str(),asset.id==preview.primitive)){preview.primitive=asset.id;preview.cue_error.clear();}}
        ImGui::SetNextItemWidth(160);ImGui::InputText("##primitive-name",preview.primitive_name,sizeof(preview.primitive_name));ImGui::SameLine();
        if(ImGui::Button("New trail")){try{preview.primitive=editor.authoring.create_trail(*editor.assets,preview.primitive_name);editor.refresh_assets();preview.cue_error.clear();}catch(const std::exception& error){preview.cue_error=error.what();}}ImGui::SameLine();
        if(ImGui::Button("New decal")){try{preview.primitive=editor.authoring.create_decal(*editor.assets,preview.primitive_name);editor.refresh_assets();preview.cue_error.clear();}catch(const std::exception& error){preview.cue_error=error.what();}}
        if(preview.primitive!=AssetId{}){darkangel::editor_app::NativeDraft* draft{};try{draft=&editor.authoring.open(*editor.assets,preview.primitive);}catch(const std::exception& error){preview.cue_error=error.what();preview.primitive=AssetId{};}
            if(draft){auto& value=draft->value;const bool trail=value.value("kind",std::string{})=="trail";ImGui::SeparatorText(trail?"Selected trail":"Selected decal");ImGui::TextWrapped("%s",draft->asset.path.c_str());
                const auto text=[&](const char* label,const char* name){char buffer[65]{};std::snprintf(buffer,sizeof(buffer),"%s",value.value(name,std::string{}).c_str());if(ImGui::InputText(label,buffer,sizeof(buffer)))value[name]=std::string(buffer);};
                const auto floats=[&](const char* label,const char* name,int count,float least,float most){float numbers[4]{};for(int index=0;index<count;++index)numbers[index]=value[name].is_array()&&value[name].size()==std::size_t(count)&&value[name][index].is_number()?value[name][index].get<float>():0.f;bool changed=count==3?ImGui::DragFloat3(label,numbers,.01f,least,most,"%.2f"):ImGui::ColorEdit4(label,numbers);if(changed){value[name]=nlohmann::json::array();for(int index=0;index<count;++index)value[name].push_back(double(std::round(std::clamp(numbers[index],least,most)*1000)/1000));}};
                const auto whole=[&](const char* label,const char* name,int least,int most){int number=int(value.value(name,unsigned(least)));if(ImGui::InputInt(label,&number))value[name]=unsigned(std::clamp(number,least,most));};
                text("Name##primitive","name");
                if(trail){text("Socket or joint##primitive","socket");floats("Base offset (m)##primitive","base",3,-5,5);floats("Tip offset (m)##primitive","tip",3,-5,5);whole("Lifetime (ticks)##primitive","lifetime_ticks",1,120);whole("Points##primitive","max_points",2,64);floats("Colour##primitive","color",4,0,1);}
                else{float size=value.value("size",1.f);if(ImGui::SliderFloat("Size (m)##primitive",&size,.05f,10,"%.2f"))value["size"]=double(std::round(size*100)/100);whole("Lifetime (ticks)##primitive","lifetime_ticks",1,3600);whole("Fade (ticks)##primitive","fade_ticks",0,3600);floats("Colour##primitive","color",4,0,1);}
                std::string problem;try{if(trail)darkangel::decode_trail(value.dump());else darkangel::decode_decal(value.dump());}catch(const std::exception& error){problem=error.what();}if(!problem.empty())red(problem);
                ImGui::BeginDisabled(!draft->dirty()||!problem.empty());if(ImGui::Button("Save##primitive")){try{const auto path=draft->asset.path;editor.authoring.save(*editor.assets,preview.primitive);editor.refresh_assets();play_cues.reset();preview.cue_error.clear();editor.log("Saved "+path);}catch(const std::exception& error){preview.cue_error=error.what();}}ImGui::EndDisabled();
                try{editor.authoring.record_changes(*editor.assets,"Edit trail or decal",ImGui::IsAnyItemActive());}catch(const std::exception& error){preview.cue_error=error.what();}}}};
    if(editor.assets)shell.draw_vfx_panel=[&]{auto& preview=vfx_preview;
        ImGui::TextWrapped("Effects are built in Effekseer. Pick one to preview it as the game draws it; \"Edit in Effekseer\" opens it there, and saving it there updates the preview.");
        if(!vfx){ImGui::TextWrapped("Visual effects are unavailable: %s",vfx_failed.c_str());return;}
        ImGui::SeparatorText("Effects");const auto effects=vfx_effects();if(effects.empty())ImGui::TextDisabled("This project has no effects yet.");for(const auto& asset:effects)if(ImGui::Selectable(asset.path.c_str(),asset.id==preview.selected))vfx_select(asset);
        ImGui::SeparatorText("New effect");ImGui::SetNextItemWidth(220);ImGui::InputText("##vfx-name",preview.name,sizeof(preview.name));ImGui::SameLine();
        {const std::string name(preview.name);const bool valid=!name.empty()&&name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==name.npos;const auto root=editor.assets->source_root();const auto relative="vfx/"+name+".efkproj";const auto starter=root/"vfx/placeholder/hit_spark.efkproj";
            ImGui::BeginDisabled(!valid||std::filesystem::exists(root/relative)||!std::filesystem::exists(starter));if(ImGui::Button("Create")){try{std::filesystem::create_directories(root/"vfx/Texture");if(!std::filesystem::exists(root/"vfx/Texture/glow.png"))std::filesystem::copy_file(root/"vfx/placeholder/Texture/glow.png",root/"vfx/Texture/glow.png");std::filesystem::copy_file(starter,root/relative);editor.assets->adopt(relative);editor.refresh_assets();for(const auto& asset:vfx_effects())if(asset.path==relative)vfx_select(asset);editor.log("Created "+relative+" from the placeholder spark. Open it in Effekseer to build the effect.");}catch(const std::exception& error){preview.error=error.what();}}ImGui::EndDisabled();
            if(!valid)ImGui::TextDisabled("Lowercase letters, digits and underscores.");else if(std::filesystem::exists(root/relative))ImGui::TextDisabled("An effect with this name exists.");}
        if(preview.selected==AssetId{}){vfx_cues();return;}ImGui::SeparatorText("Selected");ImGui::TextWrapped("%s",preview.path.c_str());
        if(ImGui::Button("Edit in Effekseer")){const auto tool=darkangel::effekseer_tool();const auto file=(editor.assets->source_root()/preview.path).make_preferred();if(!std::filesystem::is_regular_file(tool))preview.error="The Effekseer tool is missing from .tools";else{const std::wstring arguments=L"\""+file.wstring()+L"\"";const auto started=reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr,L"open",tool.wstring().c_str(),arguments.c_str(),tool.parent_path().wstring().c_str(),SW_SHOWNORMAL));if(started<=32)preview.error="Effekseer could not be started";else editor.log("Opened "+preview.path+" in Effekseer. Save it there to update the preview.");}}
        ImGui::SameLine();if(ImGui::Button("Cook again"))vfx_cook();ImGui::SameLine();if(ImGui::Button("Restart")&&preview.cooked){if(preview.handle)vfx->kill(preview.handle);preview.handle=0;preview.idle=1;}
        if(!preview.error.empty()){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1.f,.45f,.35f,1.f));ImGui::TextWrapped("Not cooked: %s",preview.error.c_str());ImGui::PopStyleColor();ImGui::TextWrapped("The preview keeps the last version that cooked.");}
        else if(preview.cooked)ImGui::TextWrapped("Cooked %u time(s) this session; %zu texture(s); product %.12s.",preview.cooks,preview.textures,preview.generation.c_str());
        {const auto drawn=vfx->stats();ImGui::Text("%zu particle(s), %u quad(s) in %u draw(s)",drawn.instances,drawn.quads,drawn.draws);if(drawn.unsupported)ImGui::TextWrapped("%u batch(es) skipped: they need shading the renderer does not have.",drawn.unsupported);}
        ImGui::SeparatorText("Preview camera");ImGui::SliderFloat("Turn",&preview.yaw,-3.14f,3.14f);ImGui::SliderFloat("Distance",&preview.distance,1.f,20.f);ImGui::SliderFloat("Height",&preview.height,0.f,3.f);vfx_cues();};
    if(args.vfx_workspace){shell.select_workspace(darkangel::editor_app::Workspace::Vfx);if(editor.assets)editor.refresh_assets();for(const auto& asset:vfx_effects())if(asset.path==args.vfx_select)vfx_select(asset);}
    std::unique_ptr<darkangel::CuePlayer> cue_player;
    // Presentation-only camera brain for the Play actor; rebuilt for each Play session.
    // Further local players of this Play, the second one's camera, and the script that drives it when one was given:
    // steps "walk-to:x,z", "interact" and "wait:frames", separated by ';'.
    std::unique_ptr<CameraDirector> director_second;std::vector<StableId> companions;darkangel::editor_app::exercises::ScriptRunner companion_script(args.input_script),owner_script(args.player_script);
    auto& script=companion_script.steps();auto& player_script=owner_script.steps();auto& script_at=companion_script.cursor();auto& player_script_at=owner_script.cursor();
    auto& script_interactions=companion_script.interactions();auto& player_script_interactions=owner_script.interactions();
    darkangel::editor_app::exercises::ScriptSignals script_signals;auto& script_capture=script_signals.capture;auto& script_captured=script_signals.captured;auto& script_quit=script_signals.quit;
    std::uint64_t guest_arrived_epoch{};require(args.players>=1&&args.players<=4,"--players is 1 to 4");shell.players=int(args.players);
    std::unique_ptr<CameraDirector> director;std::vector<std::string> camera_held;std::vector<TagId> camera_tags;float camera_fov=DirectX::XM_PIDIV4;
    // Play's scene session belongs to the transition coordinator, which replaces it when the party travels; character is the
    // running session. The world ledger lives as long as one Play. While a Play travels, the editor's document, Play resources
    // and uploaded models are those of globals_scene, and the other scenes' documents (the authoring one among them) are parked.
    darkangel::WorldLedger play_ledger;EditorSceneProvider scene_provider;std::unique_ptr<darkangel::editor_app::PlaySaves> play_saves;std::function<void(std::string)> save_play;
#ifdef DAE_EDITOR_GNS
    std::unique_ptr<darkangel::editor_app::EditorPartyHost> party_host;std::unique_ptr<darkangel::editor_app::EditorPartyGuest> party_guest;
#endif
    std::unique_ptr<darkangel::SceneTransitionCoordinator> travel;CharacterSceneSession* character{};StableId character_player;
    using SpawnController=std::function<std::shared_ptr<darkangel::ActorController>(const darkangel::editor_app::PreparedSpawn&,const darkangel::CollisionDefinition&)>;
    std::function<void(const SpawnPlan&)> attach_session;std::function<SpawnController()> make_controller;std::function<void(AssetId,std::unique_ptr<darkangel::EditorDocument>,const std::filesystem::path&)> enter_scene;
    AssetId globals_scene,authoring_scene;std::filesystem::path authoring_path;std::map<AssetId,std::unique_ptr<darkangel::EditorDocument>> parked_documents;std::map<AssetId,std::filesystem::path> parked_paths;bool travel_preparing=false,travel_was_busy=false;
    struct TravelLoad {AssetId scene;unsigned step{};bool parked{};std::filesystem::path path;std::unique_ptr<darkangel::EditorDocument> document;std::unique_ptr<darkangel::PreparedScene> prepared;} travel_load;std::unique_ptr<darkangel::SceneSession::Prepared> travel_preview;
    unsigned travel_arrivals{},travel_failures{},travel_overlay_frames{};std::string travel_failure;StableId travel_placed_entry,travel_placed_trigger;std::unique_ptr<darkangel::editor_app::CharacterPreviewResources> character_resources;CharacterSceneInput character_input;std::vector<InputEvent> combat_events;
#ifdef DAE_AUDIO
    if(editor.assets)shell.draw_audio_panel=[&]{auto& tab=audio_tab;const auto red=[](const std::string& text){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1.f,.45f,.35f,1.f));ImGui::TextWrapped("%s",text.c_str());ImGui::PopStyleColor();};
        const auto guarded=[&](auto operation){try{operation();}catch(const std::exception& error){tab.error=error.what();}};const auto act=[&](auto operation){try{operation();tab.error.clear();}catch(const std::exception& error){tab.error=error.what();}};
        const auto root=editor.assets->source_root();const bool playing=bool(character);const char* bus_names[]{"master","music","ambient","player","npc","ui"};const char* bus_labels[]{"Master","Music","Ambient","Player","NPC","UI"};
        std::vector<darkangel::AssetInfo> waves,definitions,profiles;AssetId mix_id;for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();if(extension==".wav")waves.push_back(asset);else if(extension==".dasound")definitions.push_back(asset);else if(extension==".daaudioprofile")profiles.push_back(asset);if(asset.path==audio_mix_path)mix_id=asset.id;}
        const auto path_of=[&](const std::string& id)->std::string{if(id.empty())return "(none)";for(const auto& asset:editor.asset_inventory)if(asset.id.text()==id)return asset.path;return "(missing) "+id;};
        const auto dirty=[&](AssetId id){const auto draft=editor.authoring.drafts.find(id);return draft!=editor.authoring.drafts.end()&&draft->second.dirty();};
        const auto save_draft=[&](AssetId id){const auto path=editor.authoring.drafts.at(id).asset.path;editor.authoring.save(*editor.assets,id);editor.refresh_assets();play_cues.reset();editor.log("Saved "+path);};
        if(!audio_failed.empty())red("Audio is unavailable: "+audio_failed);if(!tab.error.empty())red(tab.error);
        if(audio){const auto heard=audio->stats();ImGui::TextDisabled("Audio engine: %s driver, %zu sound(s) loaded, %zu playing.%s",heard.driver.c_str(),heard.sounds,heard.playing,project_mix().same_structure(audio->started_mix())?"":" The limiter or ducking changed: it applies when audio restarts, the next time something is played while nothing else is.");}
        else ImGui::TextDisabled("The audio engine starts the first time something is played.");
        const float column=std::max(260.f,(ImGui::GetContentRegionAvail().x-16)/3);
        // ---- Sounds
        ImGui::BeginChild("Audio sounds",{column,0},ImGuiChildFlags_Borders);ImGui::SeparatorText("Sounds");
        ImGui::TextWrapped("WAV files, and sound definitions that say how a WAV is played: bus, volume, pitch, looping, positioned or flat, range and one effect.");
        ImGui::SetNextItemWidth(-90);ImGui::InputTextWithHint("##audio-import","Path of a .wav file to import",tab.import_path,sizeof(tab.import_path));ImGui::SameLine();
        if(ImGui::Button("Import"))act([&]{const std::filesystem::path from(tab.import_path);require(std::filesystem::is_regular_file(from)&&from.extension()==".wav","Give the path of an existing .wav file");const auto stem=from.stem().string();require(!stem.empty()&&stem.size()<=48&&stem.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==stem.npos,"The file name may hold letters, digits, '_' and '-'");
            const auto relative="audio/"+stem+".wav";require(!std::filesystem::exists(root/relative),"A sound with this name is already in the project");std::filesystem::create_directories(root/"audio");std::filesystem::copy_file(from,root/relative);try{editor.assets->adopt(relative);editor.assets->cook(relative);}catch(...){std::error_code ignored;std::filesystem::remove(root/relative,ignored);std::filesystem::remove(root/(relative+".daimport"),ignored);throw;}
            editor.refresh_assets();for(const auto& asset:editor.asset_inventory)if(asset.path==relative)tab.sound=asset.id;tab.import_path[0]=0;editor.log("Imported "+relative);});
        for(const auto& asset:waves)if(ImGui::Selectable((asset.path+"##wave").c_str(),asset.id==tab.sound))tab.sound=asset.id;
        for(const auto& asset:definitions)if(ImGui::Selectable((asset.path+(dirty(asset.id)?" *":"")+"##definition").c_str(),asset.id==tab.sound))tab.sound=asset.id;
        // Audition plays through the real service. A definition is played as its draft has it, saved or not.
        const auto audition=[&](AssetId wave,AssetId as,const darkangel::SoundSettings& settings,const std::string& settings_text){if(!ensure_audio())throw std::runtime_error(audio_failed.empty()?"Audio is unavailable":audio_failed);std::string wave_path;for(const auto& asset:editor.asset_inventory)if(asset.id==wave)wave_path=asset.path;require(!wave_path.empty(),"The sound's WAV is not in the project");
            const auto registry=editor.assets->cas_path().parent_path()/("audition-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};const std::array roots{wave};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);const auto cooked=darkangel::load_cooked_sound(registry,editor.assets->cas_path(),wave);
            const std::array<darkangel::AudioSound,1> one{{{as,darkangel::sha256(cooked.generation+settings_text),&cooked,settings}}};audio->load(one);if(tab.audition)audio->stop(tab.audition,0);audio->listener({0,0,0},{0,0,1},{0,1,0});tab.audition=audio->play(as,{0,0,1.5f});require(tab.audition!=0,"The sound did not start");};
        if(tab.sound!=AssetId{}){const auto selected=std::find_if(editor.asset_inventory.begin(),editor.asset_inventory.end(),[&](const auto& asset){return asset.id==tab.sound;});
            if(selected==editor.asset_inventory.end())tab.sound=AssetId{};
            else if(std::filesystem::path(selected->path).extension()==".wav"){ImGui::SeparatorText("Selected WAV");ImGui::TextWrapped("%s",selected->path.c_str());
                if(ImGui::Button("Play##wave"))act([&]{audition(selected->id,selected->id,{},"plain");});ImGui::SameLine();if(ImGui::Button("Stop##wave")&&audio&&tab.audition){audio->stop(tab.audition,.05);tab.audition=0;}
                ImGui::SetNextItemWidth(180);ImGui::InputText("##definition-name",tab.sound_name,sizeof(tab.sound_name));ImGui::SameLine();if(ImGui::Button("New definition"))act([&]{const auto wave=*selected;tab.sound=editor.authoring.create_sound_definition(*editor.assets,wave.id,wave.path,tab.sound_name);editor.refresh_assets();});}
            else guarded([&]{auto& draft=editor.authoring.open(*editor.assets,tab.sound);auto& value=draft.value;ImGui::SeparatorText("Selected definition");ImGui::TextWrapped("%s",draft.asset.path.c_str());ImGui::TextDisabled("WAV: %s",path_of(value.value("wave",std::string{})).c_str());
                {char buffer[65]{};std::snprintf(buffer,sizeof(buffer),"%s",value.value("name",std::string{}).c_str());if(ImGui::InputText("Name##sound",buffer,sizeof(buffer)))value["name"]=std::string(buffer);}
                {const auto current=value.value("bus",std::string("player"));if(ImGui::BeginCombo("Bus##sound",current.c_str())){for(int bus=1;bus<6;++bus)if(ImGui::Selectable(bus_labels[bus],current==bus_names[bus]))value["bus"]=bus_names[bus];ImGui::EndCombo();}}
                const auto slider=[&](const char* label,nlohmann::json& field,float least,float most,const char* format="%.2f"){float number=field.is_number()?field.get<float>():least;if(ImGui::SliderFloat(label,&number,least,most,format))field=double(std::round(number*1000)/1000);};
                slider("Volume##sound",value["gain"],0,4);slider("Pitch##sound",value["pitch"],.25f,4);{bool loop=value.value("loop",false);if(ImGui::Checkbox("Loops",&loop))value["loop"]=loop;}ImGui::SameLine();{bool positioned=value.value("positioned",true);if(ImGui::Checkbox("Positioned in the world",&positioned))value["positioned"]=positioned;}
                if(value.value("positioned",true))slider("Silent beyond (m)##sound",value["range"],1,500,"%.0f");
                {auto& effect=value["effect"];const auto current=effect.value("kind",std::string("none"));const auto kinds=darkangel::sound_effect_kinds();const auto known=std::find_if(kinds.begin(),kinds.end(),[&](const auto& kind){return current==kind.kind;});
                    if(ImGui::BeginCombo("Effect##sound",known!=kinds.end()?known->label:current.c_str())){for(const auto& kind:kinds)if(ImGui::Selectable(kind.label,current==kind.kind)){effect={{"kind",kind.kind},{"parameters",nlohmann::json::object()}};for(const auto& parameter:kind.parameters)effect["parameters"][parameter.name]=double(parameter.initial);}ImGui::EndCombo();}
                    if(known!=kinds.end())for(const auto& parameter:known->parameters){if(!effect["parameters"].contains(parameter.name))effect["parameters"][parameter.name]=double(parameter.initial);slider((std::string(parameter.name)+"##effect").c_str(),effect["parameters"][parameter.name],parameter.least,parameter.most);}}
                std::string problem;darkangel::SoundDefinitionAsset decoded;try{decoded=darkangel::decode_sound_definition(value.dump());}catch(const std::exception& error){problem=error.what();}if(!problem.empty())red(problem);
                ImGui::BeginDisabled(!problem.empty());if(ImGui::Button("Play##definition")){const auto text=value.dump();act([&]{audition(decoded.wave,decoded.id,decoded.settings,text);});}ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Stop##definition")&&audio&&tab.audition){audio->stop(tab.audition,.05);tab.audition=0;}
                ImGui::SameLine();ImGui::BeginDisabled(!draft.dirty()||!problem.empty());if(ImGui::Button("Save##definition")){const auto id=tab.sound;act([&]{save_draft(id);});}ImGui::EndDisabled();});}
        ImGui::EndChild();ImGui::SameLine();
        // ---- Mixer
        ImGui::BeginChild("Audio mixer",{column,0},ImGuiChildFlags_Borders);ImGui::SeparatorText("Mixer");
        if(mix_id==AssetId{}){ImGui::TextWrapped("This project has no mixer yet. Without one every bus plays at full volume.");if(ImGui::Button("Create the project mixer"))act([&]{editor.authoring.create_audio_mix(*editor.assets);editor.refresh_assets();});}
        else guarded([&]{auto& draft=editor.authoring.open(*editor.assets,mix_id);auto& value=draft.value;ImGui::TextWrapped("Volumes and mutes are heard at once, also during Play. Master is over the other five.");
            for(int bus=0;bus<6;++bus){auto& entry=value["buses"][bus_names[bus]];float volume=entry.value("volume",1.f);ImGui::SetNextItemWidth(170);if(ImGui::SliderFloat((std::string(bus_labels[bus])+"##volume").c_str(),&volume,0,2,"%.2f"))entry["volume"]=double(std::round(volume*100)/100);ImGui::SameLine();bool mute=entry.value("mute",false);if(ImGui::Checkbox((std::string("Mute##")+bus_names[bus]).c_str(),&mute))entry["mute"]=mute;}
            ImGui::SeparatorText("Loudness");{bool limit=value.value("limit_each_sound",false);if(ImGui::Checkbox("Limit each sound to",&limit))value["limit_each_sound"]=limit;ImGui::SameLine();float threshold=value.value("threshold_db",-1.f);ImGui::SetNextItemWidth(120);if(ImGui::SliderFloat("dB##limit",&threshold,-40,0,"%.1f"))value["threshold_db"]=double(std::round(threshold*10)/10);}
            ImGui::TextDisabled("The audio engine limits sounds one by one, not their sum.");
            ImGui::SeparatorText("Ducking");ImGui::TextWrapped("While a sound plays on the first bus, the second is lowered.");auto& ducking=value["ducking"];
            const auto bus_combo=[&](const char* label,nlohmann::json& field){const auto current=field.is_string()?field.get<std::string>():std::string("music");ImGui::SetNextItemWidth(90);if(ImGui::BeginCombo(label,current.c_str())){for(int bus=1;bus<6;++bus)if(ImGui::Selectable(bus_names[bus],current==bus_names[bus]))field=bus_names[bus];ImGui::EndCombo();}};
            for(unsigned index=0;index<ducking.size();++index){ImGui::PushID(int(index));auto& rule=ducking[index];bus_combo("##when",rule["when"]);ImGui::SameLine();ImGui::TextUnformatted("lowers");ImGui::SameLine();bus_combo("##lower",rule["lower"]);ImGui::SameLine();float to=rule.value("to",.5f);ImGui::SetNextItemWidth(70);if(ImGui::SliderFloat("to",&to,0,1,"%.2f"))rule["to"]=double(std::round(to*100)/100);ImGui::SameLine();const bool remove=ImGui::SmallButton("Remove");ImGui::PopID();if(remove){ducking.erase(index);break;}}
            if(ducking.size()<8&&ImGui::Button("Add a ducking rule"))ducking.push_back({{"when","ui"},{"lower","music"},{"to",.5},{"fade_in",.2},{"fade_out",.5}});
            std::string problem;try{darkangel::decode_audio_mix(value.dump());}catch(const std::exception& error){problem=error.what();}if(!problem.empty())red(problem);
            ImGui::BeginDisabled(!draft.dirty()||!problem.empty());if(ImGui::Button("Save##mixer"))act([&]{save_draft(mix_id);});ImGui::EndDisabled();});
        ImGui::EndChild();ImGui::SameLine();
        // ---- Scene audio
        ImGui::BeginChild("Audio profiles",{0,0},ImGuiChildFlags_Borders);ImGui::SeparatorText("Scene audio");
        const auto scene_profile=darkangel::editor_app::scene_audio_profile(editor);ImGui::TextWrapped("This scene uses: %s",path_of(scene_profile==AssetId{}?std::string{}:scene_profile.text()).c_str());
        ImGui::TextWrapped("A profile is music, ambient loops and bus offsets. The scene's starts with Play; an encounter can layer its own while it runs (set in the encounter's Inspector).");
        for(const auto& asset:profiles)if(ImGui::Selectable((asset.path+(dirty(asset.id)?" *":"")+(asset.id==scene_profile?"  (this scene)":"")+"##profile").c_str(),asset.id==tab.profile))tab.profile=asset.id;
        ImGui::SetNextItemWidth(180);ImGui::InputText("##profile-name",tab.profile_name,sizeof(tab.profile_name));ImGui::SameLine();if(ImGui::Button("New profile"))act([&]{tab.profile=editor.authoring.create_audio_profile(*editor.assets,tab.profile_name);editor.refresh_assets();});
        if(tab.profile!=AssetId{})guarded([&]{auto& draft=editor.authoring.open(*editor.assets,tab.profile);auto& value=draft.value;const auto before=value;ImGui::SeparatorText("Selected profile");ImGui::TextWrapped("%s",draft.asset.path.c_str());
            {char buffer[65]{};std::snprintf(buffer,sizeof(buffer),"%s",value.value("name",std::string{}).c_str());if(ImGui::InputText("Name##profile",buffer,sizeof(buffer)))value["name"]=std::string(buffer);}
            const auto slider=[&](const char* label,nlohmann::json& field,float least,float most,const char* format="%.2f"){float number=field.is_number()?field.get<float>():least;ImGui::SetNextItemWidth(170);if(ImGui::SliderFloat(label,&number,least,most,format))field=double(std::round(number*100)/100);};
            {const auto current=value["music"].is_string()?value["music"].get<std::string>():std::string{};if(ImGui::BeginCombo("Music##profile",path_of(current).c_str())){if(ImGui::Selectable("(none)",current.empty()))value["music"]=nullptr;for(const auto& asset:definitions)if(ImGui::Selectable(asset.path.c_str(),asset.id.text()==current))value["music"]=asset.id.text();ImGui::EndCombo();}}
            slider("Music volume##profile",value["music_volume"],0,2);ImGui::SeparatorText("Ambient loops");auto& ambient=value["ambient"];
            for(unsigned index=0;index<ambient.size();++index){ImGui::PushID(int(index));ImGui::TextWrapped("%s",path_of(ambient[index].value("sound",std::string{})).c_str());slider("Volume",ambient[index]["volume"],0,2);ImGui::SameLine();const bool remove=ImGui::SmallButton("Remove");ImGui::PopID();if(remove){ambient.erase(index);break;}}
            if(ambient.size()<8&&ImGui::BeginCombo("##add-ambient","Add an ambient loop")){for(const auto& asset:definitions){const bool listed=std::any_of(ambient.begin(),ambient.end(),[&](const auto& loop){return loop.value("sound",std::string{})==asset.id.text();});if(ImGui::Selectable(asset.path.c_str(),false,listed?ImGuiSelectableFlags_Disabled:0))ambient.push_back({{"sound",asset.id.text()},{"volume",1.0}});}ImGui::EndCombo();}
            ImGui::SeparatorText("Bus offsets (dB)");for(int bus=0;bus<6;++bus)slider((std::string(bus_labels[bus])+"##offset").c_str(),value["bus_offsets_db"][bus_names[bus]],-60,12,"%.1f");slider("Crossfade (s)##profile",value["crossfade_seconds"],0,30,"%.1f");
            // The locators of the sounds it names are kept with the names.
            if(value!=before){nlohmann::json located=nlohmann::json::object();const auto locate=[&](const nlohmann::json& id){if(!id.is_string())return;for(const auto& asset:definitions)if(asset.id.text()==id.get<std::string>())located[asset.id.text()]=asset.path;};locate(value["music"]);for(const auto& loop:value["ambient"])locate(loop["sound"]);value["sources"]={{"sounds",located}};}
            std::string problem;try{darkangel::decode_audio_profile(value.dump());}catch(const std::exception& error){problem=error.what();}if(!problem.empty())red(problem);
            const bool unsaved=draft.dirty();ImGui::BeginDisabled(!unsaved||!problem.empty());if(ImGui::Button("Save##profile")){const auto id=tab.profile;act([&]{save_draft(id);});}ImGui::EndDisabled();
            ImGui::SameLine();ImGui::BeginDisabled(playing||unsaved||tab.profile==scene_profile);if(ImGui::Button("Use for this scene"))act([&]{darkangel::editor_app::set_scene_audio_profile(editor,tab.profile);});ImGui::EndDisabled();
            ImGui::SameLine();ImGui::BeginDisabled(playing||scene_profile==AssetId{});if(ImGui::Button("Scene uses none"))act([&]{darkangel::editor_app::set_scene_audio_profile(editor,AssetId{});});ImGui::EndDisabled();
            // Audition of the whole profile, as saved.
            ImGui::BeginDisabled(playing||unsaved);if(ImGui::Button(tab.profile_audition?"Restart audition":"Audition"))act([&]{const auto id=tab.profile;if(!ensure_audio())throw std::runtime_error(audio_failed.empty()?"Audio is unavailable":audio_failed);audio_director.reset();auto loaded=load_audio({id});require(loaded.contains(id),"The profile did not cook");audio_director=std::make_unique<darkangel::AudioDirector>(*audio,project_mix());audio_applied=project_mix();audio_director->scene(loaded.at(id));tab.profile_audition=true;});ImGui::EndDisabled();
            ImGui::SameLine();ImGui::BeginDisabled(playing||!tab.profile_audition);if(ImGui::Button("Stop audition")){audio_director.reset();tab.profile_audition=false;}ImGui::EndDisabled();
            if(unsaved)ImGui::TextDisabled("Save the profile to audition it or to use it for the scene.");});
        ImGui::EndChild();
        try{editor.authoring.record_changes(*editor.assets,"Edit audio",ImGui::IsAnyItemActive());}catch(const std::exception& error){tab.error=error.what();}};
    if(args.audio_workspace){shell.select_workspace(darkangel::editor_app::Workspace::Audio);if(editor.assets)editor.refresh_assets();}
#endif
    // Navigation and behaviour facts of the latest Play session, for readouts.
    unsigned navigation_polygons{};std::string behavior_running;
    // Spawn points whose NPC definition is in a cooked registry. A startup registry may predate the scene's spawn points; a prepared Play packages them all.
    auto registry_spawns=[&](const std::filesystem::path& path){std::ifstream input(path);auto registry=nlohmann::json::parse(input);std::set<std::string> known;for(const auto& record:registry.at("assets"))if(record.at("kind")=="npc")known.insert(record.at("id").get<std::string>());std::map<StableId,AssetId> result;for(const auto& [point,npc]:editor.document->plan().spawns)if(known.contains(npc.text())&&!editor.document->plan().spawn_owners.contains(point))result.emplace(point,npc);return result;};
    // NPC definitions the scene's encounters can spawn, and those of them a cooked registry holds.
    auto encounter_npcs=[&]{std::set<AssetId> result;for(const auto& [object,json]:editor.document->plan().encounters){try{for(auto npc:darkangel::decode_encounter(json).npcs())result.insert(npc);}catch(const std::exception&){}}return result;};
    auto registry_encounter_npcs=[&](const std::filesystem::path& path){std::ifstream input(path);auto registry=nlohmann::json::parse(input);std::set<std::string> known;for(const auto& record:registry.at("assets"))if(record.at("kind")=="npc")known.insert(record.at("id").get<std::string>());std::set<AssetId> result;for(auto npc:encounter_npcs())if(known.contains(npc.text()))result.insert(npc);return result;};
    // Exercises act on the scene's first spawned actor.
    auto first_actor=[&]{require(character_resources&&!character_resources->spawns.empty(),"This exercise needs a scene with an NPC spawn point");return character_resources->spawns.front().actor;};
    // HUD target: a presentation choice that writes no gameplay state and is separate from soft lock and from any lock-on.
    // The soft-locked actor if there is one, else the nearest live hostile in front of the camera; a target that stops
    // qualifying stays shown until another one does.
    std::optional<StableId> hud_target;
    auto choose_hud_target=[&]{
        if(!character||!character_resources||!character_resources->combat){hud_target.reset();return hud_target;}
#ifdef DAE_EDITOR_GNS
        // A networked client that is catching up on a new world revision holds no observer state to aim at.
        if((party_host||party_guest)&&!character->bootstrapped()){hud_target.reset();return hud_target;}
#endif
        if(auto locked=character->soft_lock();locked&&character->observer_if_ready(*locked,double(character->motor().tick))){hud_target=locked;return hud_target;}
        const auto& combat=*character_resources->combat;const auto& self=character->motor();const double yaw=director?director->yaw():self.yaw,forward_x=std::sin(yaw),forward_z=std::cos(yaw);
        std::optional<StableId> best;double nearest{};
        // Every actor the session holds now, enemies an encounter brought in included.
        for(const auto id:character->actors()){const auto* actor=character->actor(id);if(!actor||actor->faction==combat.faction)continue;auto handle=character->presentation().find(id);if(!character->presentation().valid(handle)||character->presentation().read(handle).health.current<=0)continue;
            const auto observed=character->observer_if_ready(id,double(self.tick));if(!observed)continue;const auto at=observed->frame.motor.position;const double dx=at.x-self.position.x,dz=at.z-self.position.z,distance=std::hypot(dx,dz);if(dx*forward_x+dz*forward_z<=0||distance>40)continue;if(!best||distance<nearest){best=id;nearest=distance;}}
        hud_target=best;return hud_target;};
#ifdef DAE_NAVIGATION
    // Tools > Navigation: an explicit bake of the scene's cooked collision that Play reuses while the collision is unchanged.
    struct NavigationBake {darkangel::NavigationSettings settings,baked_settings;std::shared_ptr<const darkangel::NavigationMesh> mesh;std::string key,error;double milliseconds{};std::size_t triangles{};unsigned reused{};bool shown{};std::vector<darkangel::NavigationMesh::Edge> outline;} navigation;
    auto navigation_key=[](const darkangel::CollisionDefinition& collision){std::string key=std::to_string(collision.boxes.size());for(const auto& mesh:collision.meshes)key+=':'+std::to_string(mesh.id)+':'+(mesh.data?mesh.data->signature:std::string{});return key;};
    auto same_settings=[](const darkangel::NavigationSettings& a,const darkangel::NavigationSettings& b){return a.cell_size==b.cell_size&&a.cell_height==b.cell_height&&a.agent_height==b.agent_height&&a.agent_radius==b.agent_radius&&a.agent_climb==b.agent_climb&&a.slope_degrees==b.slope_degrees;};
    auto bake_navigation=[&]{
        navigation.error.clear();
        try{require(character_resources&&character_resources->collision,"Navigation baking needs a scene with cooked collision (--character-collision)");const auto& collision=*character_resources->collision;const auto start=std::chrono::steady_clock::now();auto mesh=std::make_shared<const darkangel::NavigationMesh>(collision,navigation.settings);
            navigation.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();navigation.triangles=0;for(const auto& box:collision.boxes)if(!box.moving&&!box.dynamic&&!box.sensor)navigation.triangles+=12;for(const auto& source:collision.meshes)if(source.data)navigation.triangles+=source.data->triangles.size();
            navigation.mesh=std::move(mesh);navigation.outline=navigation.mesh->outline();navigation.key=navigation_key(collision);navigation.baked_settings=navigation.settings;}
        catch(const std::exception& error){navigation.error=error.what();}
    };
    // Play asks here: the bake when it matches the session's collision, otherwise a mesh built on demand with the current settings.
    auto navigation_for=[&](const darkangel::CollisionDefinition& collision){if(navigation.mesh&&navigation.key==navigation_key(collision)){++navigation.reused;return navigation.mesh;}return std::make_shared<const darkangel::NavigationMesh>(collision,navigation.settings);};
    // The project's save slot lives beside its asset cache. Saving is the host's: a capture of the ledger and inventories at a safe point.
    if(editor.assets)play_saves=std::make_unique<darkangel::editor_app::PlaySaves>(editor.assets->cas_path().parent_path()/"saves"/"project.dasave");
    shell.draw_saves_tool=[&]{if(!play_saves){ImGui::TextDisabled("Saves need a project with an asset cache.");return;}play_saves->draw(character&&travel&&!travel->busy(),[&]{if(save_play)save_play("Saved from Tools > Saves");});};
    shell.draw_navigation_tool=[&]{
        ImGui::TextWrapped("Bakes one navigation mesh from the scene's cooked static collision. NPC controllers use it to find paths; movement and collision stay with the motor. While the collision is unchanged, Play uses this bake; otherwise it builds a mesh on demand with these settings.");
        ImGui::SeparatorText("Agent and voxel settings");auto field=[](const char* label,double& value,double low,double high){ImGui::SetNextItemWidth(160);if(ImGui::InputDouble(label,&value,0,0,"%.2f"))value=std::clamp(value,low,high);};
        field("Cell size (m)",navigation.settings.cell_size,.05,2);field("Cell height (m)",navigation.settings.cell_height,.05,2);field("Agent height (m)",navigation.settings.agent_height,.5,4);field("Agent radius (m)",navigation.settings.agent_radius,.1,2);field("Step height (m)",navigation.settings.agent_climb,0,1);field("Walkable slope (deg)",navigation.settings.slope_degrees,1,89);
        if(ImGui::Button("Use defaults"))navigation.settings={};
        ImGui::SeparatorText("Bake");const bool available=character_resources&&character_resources->collision;ImGui::BeginDisabled(!available);if(ImGui::Button("Bake navigation mesh"))bake_navigation();ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!navigation.mesh);if(ImGui::Button("Clear bake")){navigation.mesh.reset();navigation.outline.clear();navigation.key.clear();}ImGui::EndDisabled();
        ImGui::Checkbox("Show in the viewports",&navigation.shown);if(ImGui::IsItemHovered())ImGui::SetTooltip("Draws the baked mesh's polygon edges over the Scene and Game viewports; the mesh's border is brighter.");if(navigation.shown&&!navigation.mesh){ImGui::SameLine();ImGui::TextDisabled("Nothing to show until a mesh is baked.");}
        if(!available)ImGui::TextDisabled("This session has no cooked scene collision to bake.");
        if(!navigation.error.empty())ImGui::TextColored({1.f,.55f,.3f,1.f},"Bake failed: %s",navigation.error.c_str());
        if(navigation.mesh){const bool current=available&&navigation.key==navigation_key(*character_resources->collision);ImGui::Text("Polygons: %u",navigation.mesh->polygons());ImGui::Text("Source triangles: %zu",navigation.triangles);ImGui::Text("Bake time: %.1f ms",navigation.milliseconds);ImGui::Text("Used by Play sessions: %u",navigation.reused);
            if(!current)ImGui::TextColored({1.f,.55f,.3f,1.f},"Stale: the scene collision changed since this bake. Bake again.");else if(!same_settings(navigation.settings,navigation.baked_settings))ImGui::TextColored({1.f,.55f,.3f,1.f},"Settings changed since this bake. Bake again to apply them.");else ImGui::TextDisabled("Current: the next Play session uses this bake.");}
        else ImGui::TextDisabled("Not baked. Play builds a mesh on demand when an NPC needs one.");
    };
#endif
#ifdef DAE_HUD
    // Runtime HUD: the game's RmlUi document over the Game viewport, fed by a native view model of confirmed
    // state. Its geometry goes into the viewport's draw list; font atlases are device textures.
    std::map<ImTextureID,RefCntAutoPtr<ITexture>> hud_textures;
    darkangel::editor_app::ImGuiHudRenderer hud_renderer([&](const unsigned char* rgba,int width,int height){TextureDesc desc;desc.Name="HUD font atlas";desc.Type=RESOURCE_DIM_TEX_2D;desc.Width=static_cast<Uint32>(width);desc.Height=static_cast<Uint32>(height);desc.MipLevels=1;desc.Format=TEX_FORMAT_RGBA8_UNORM;desc.Usage=USAGE_IMMUTABLE;desc.BindFlags=BIND_SHADER_RESOURCE;TextureSubResData level;level.pData=rgba;level.Stride=static_cast<Uint64>(width)*4;TextureData data;data.pSubResources=&level;data.NumSubresources=1;RefCntAutoPtr<ITexture> texture;device->CreateTexture(desc,&data,&texture);require(texture,"HUD texture creation failed");auto id=reinterpret_cast<ImTextureID>(texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));hud_textures.emplace(id,std::move(texture));return id;},[&](ImTextureID id){hud_textures.erase(id);});
    std::unique_ptr<darkangel::Hud> hud;bool hud_failed=false,hud_second_drawn=false;
    // Debug view of NPC senses over the Scene and Game viewports. While authoring: the radii and cones of the selected spawn
    // point's NPC, from its draft. In Play: the same for every actor ticked in the Gameplay Inspector (or whose spawn point is
    // selected) at its current place, with a line from its eye to each actor it perceives. Reads the host's own state; draws only.
    std::set<StableId> senses_shown;bool senses_all=false,world_debug=false;unsigned senses_drawn=0,volumes_drawn=0,navigation_drawn=0,interactions_asked=0,splines_drawn=0,scatter_drawn=0;bool interact_pressed=false,interact_held=false,interaction_offered=false;std::uint64_t senses_noticed=0;double senses_noticed_z=0;bool senses_early=false;
    // Viewport picking: the first scene object whose bounds the segment meets. A model uses its cooked bounds under the object's
    // transform; a character or a spawn point uses a standing body's box.
    shell.pick_object=[&](const std::array<double,3>& from,const std::array<double,3>& to)->StableId{
        using namespace DirectX;StableId best;float nearest=2;
        auto test=[&](StableId object,const std::array<float,3>& center,const std::array<float,3>& half){auto entity=editor.displayed().find(object);if(!editor.displayed().valid(entity))return;const auto pose=editor.displayed().read(entity).transform;
            const auto inverse=XMMatrixInverse(nullptr,XMMatrixScaling(float(pose.scale),float(pose.scale),float(pose.scale))*XMMatrixRotationRollPitchYaw(float(pose.pitch),float(pose.yaw),float(pose.roll))*XMMatrixTranslation(float(pose.x),float(pose.y),float(pose.z)));
            XMFLOAT3 a,b;XMStoreFloat3(&a,XMVector3TransformCoord(XMVectorSet(float(from[0]),float(from[1]),float(from[2]),1),inverse));XMStoreFloat3(&b,XMVector3TransformCoord(XMVectorSet(float(to[0]),float(to[1]),float(to[2]),1),inverse));
            const float start[3]={a.x,a.y,a.z},delta[3]={b.x-a.x,b.y-a.y,b.z-a.z};float enter=0,leave=1;
            for(unsigned axis=0;axis<3;++axis){const float reach=std::max(half[axis],.05f),low=center[axis]-reach,high=center[axis]+reach;if(std::abs(delta[axis])<1e-9f){if(start[axis]<low||start[axis]>high)return;continue;}float first=(low-start[axis])/delta[axis],second=(high-start[axis])/delta[axis];if(first>second)std::swap(first,second);enter=std::max(enter,first);leave=std::min(leave,second);if(enter>leave)return;}
            if(enter<nearest){nearest=enter;best=object;}};
        const std::array<float,3> body_center{0,.9f,0},body_half{.4f,.9f,.4f};
        for(const auto& [object,model]:editor.document->plan().models){if(model==id)test(object,gpu.center,gpu.half);else if(auto found=scene_models.find(model);found!=scene_models.end())test(object,found->second.center,found->second.half);else test(object,body_center,body_half);}
        for(const auto& [object,npc]:editor.document->plan().spawns)test(object,body_center,body_half);
        // A volume is picked only where no solid object smaller than it is under the cursor, so things inside a volume stay clickable.
        const StableId solid=best;const float solid_at=nearest;float solid_size=1e9f;if(solid){solid_size=1.f;if(auto model=editor.document->plan().models.find(solid);model!=editor.document->plan().models.end()){if(model->second==id)solid_size=gpu.radius;else if(auto found=scene_models.find(model->second);found!=scene_models.end())solid_size=found->second.radius;}auto entity=editor.displayed().find(solid);if(editor.displayed().valid(entity))solid_size*=float(editor.displayed().read(entity).transform.scale);}
        StableId volume;float volume_size=1e9f;auto offer=[&](StableId object,const std::array<float,3>& half){best={};nearest=2;test(object,{0,0,0},half);const float size=std::max({half[0],half[1],half[2]});if(best&&size<volume_size){volume=best;volume_size=size;}};
        for(const auto& [object,info]:editor.document->plan().volumes)offer(object,{float(info.half[0]),float(info.half[1]),float(info.half[2])});
        for(const auto& [object,json]:editor.document->plan().interactables)if(!editor.document->plan().models.contains(object))offer(object,{.4f,.4f,.4f});
        for(const auto& [object,info]:editor.document->plan().spline_points)offer(object,{.3f,.3f,.3f});for(const auto& [object,json]:editor.document->plan().splines)offer(object,{.3f,.3f,.3f});
        for(const auto& [object,json]:editor.document->plan().triggers){try{const auto trigger=darkangel::decode_trigger(json);offer(object,{float(trigger.half.x),float(trigger.half.y),float(trigger.half.z)});}catch(const std::exception&){}}
        for(const auto& [object,json]:editor.document->plan().transitions){try{const auto transition=darkangel::decode_transition(json);offer(object,{float(transition.half.x),float(transition.half.y),float(transition.half.z)});}catch(const std::exception&){}}
        for(const auto& [object,json]:editor.document->plan().entries)offer(object,{.4f,.4f,.4f});
        for(const auto& [object,json]:editor.document->plan().pickups)if(!editor.document->plan().models.contains(object))offer(object,{.3f,.3f,.3f});
        for(const auto& [object,json]:editor.document->plan().altars)if(!editor.document->plan().models.contains(object))offer(object,{.5f,.5f,.5f});
        for(const auto& [object,json]:editor.document->plan().movers){try{const auto mover=darkangel::decode_mover(json);test(object,{0,float(mover.half.y),0},{float(mover.half.x),float(mover.half.y),float(mover.half.z)});}catch(const std::exception&){}}
        for(const auto& [object,json]:editor.document->plan().encounters){best={};nearest=2;test(object,{0,.5f,0},{.4f,.4f,.4f});if(best){volume=best;volume_size=.4f;}}
        best=solid;nearest=solid_at;if(volume&&(!solid||solid_size>volume_size))best=volume;
        return best;};
    // Placement surface: rays against the scene's cooked static collision, in a query world built when the collision changes.
    std::unique_ptr<darkangel::PhysicsWorld> place_world;const darkangel::CollisionDefinition* place_collision=nullptr;
    shell.place_surface=[&](const std::array<double,3>& from,const std::array<double,3>& to)->std::optional<std::array<double,3>>{
        if(!character_resources||!character_resources->collision)return std::nullopt;
        if(place_collision!=character_resources->collision.get()){place_world=std::make_unique<darkangel::PhysicsWorld>();place_world->load_scene(*character_resources->collision);place_collision=character_resources->collision.get();}
        const auto found=place_world->query_ray({from[0],from[1],from[2]},{to[0]-from[0],to[1]-from[1],to[2]-from[2]},{darkangel::StaticCollision,0});const darkangel::CollisionHit* nearest=nullptr;for(const auto& hit:found.hits)if(!nearest||hit.fraction<nearest->fraction)nearest=&hit;
        if(!nearest)return std::nullopt;return std::array<double,3>{nearest->point.x,nearest->point.y,nearest->point.z};};
    // Splines of the scene as they stand now (a point being dragged included): the curve through each spline's points in order.
    struct BuiltSpline {StableId id;darkangel::SplineDefinition definition;std::vector<darkangel::MotorVec> points;std::optional<darkangel::Spline> curve;};
    auto build_splines=[&]{std::vector<BuiltSpline> result;if(!editor.document)return result;const auto& plan=editor.document->plan();
        for(const auto& [object,json]:plan.splines){BuiltSpline built;built.id=object;try{built.definition=darkangel::decode_spline(json);}catch(const std::exception&){continue;}
            std::vector<std::pair<unsigned,StableId>> order;for(const auto& [point,info]:plan.spline_points)if(info.spline==object)order.push_back({info.index,point});std::sort(order.begin(),order.end());
            for(const auto& [index,point]:order){auto entity=editor.displayed().find(point);if(!editor.displayed().valid(entity))continue;auto pose=editor.displayed().read(entity).transform;if(shell.draft&&editor.selected==point)pose=*shell.draft;built.points.push_back({pose.x,pose.y,pose.z});}
            if(built.points.size()>=2&&built.points.size()<=darkangel::spline_point_limit){try{built.curve.emplace(built.points,built.definition.closed);}catch(const std::exception&){}}
            result.push_back(std::move(built));}
        return result;};
    shell.debug_overlay=[&](ImDrawList* draw,darkangel::editor_app::ViewArea area){
        using namespace DirectX;using darkangel::MotorVec;senses_drawn=0;volumes_drawn=0;splines_drawn=0;if(!shell.scene_camera||!draw)return;const auto matrix=XMLoadFloat4x4(&shell.scene_view)*XMLoadFloat4x4(&shell.scene_projection);
        auto project=[&](const MotorVec& point,ImVec2& out){XMFLOAT4 clip;XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(float(point.x),float(point.y),float(point.z),1),matrix));if(clip.w<=.05f)return false;out={area.x+(clip.x/clip.w*.5f+.5f)*area.width,area.y+(.5f-clip.y/clip.w*.5f)*area.height};return true;};
        auto line=[&](const MotorVec& a,const MotorVec& b,ImU32 colour,float width=1.5f){ImVec2 p,q;if(project(a,p)&&project(b,q))draw->AddLine(p,q,colour,width);};
#ifdef DAE_NAVIGATION
        // The baked navigation mesh: polygon edges a little above the surface, the border brighter.
        navigation_drawn=0;if(navigation.shown&&navigation.mesh)for(const auto& edge:navigation.outline){ImVec2 p,q;if(project({edge.from.x,edge.from.y+.05,edge.from.z},p)&&project({edge.to.x,edge.to.y+.05,edge.to.z},q)){draw->AddLine(p,q,edge.border?IM_COL32(80,220,255,255):IM_COL32(80,220,255,110),edge.border?2.f:1.f);++navigation_drawn;}}
#endif
        auto ring=[&](const MotorVec& centre,double radius,ImU32 colour){constexpr unsigned steps=48;for(unsigned i=0;i<steps;++i){const double a=6.283185307179586*i/steps,b=6.283185307179586*(i+1)/steps;line({centre.x+std::sin(a)*radius,centre.y,centre.z+std::cos(a)*radius},{centre.x+std::sin(b)*radius,centre.y,centre.z+std::cos(b)*radius},colour);}};
        auto shape=[&](const darkangel::SensesDefinition& senses,const MotorVec& foot,double yaw,bool alert){
            ++senses_drawn;const MotorVec eye{foot.x,foot.y+senses.eye_height,foot.z};const ImU32 vision=alert?IM_COL32(255,80,60,255):IM_COL32(90,230,120,255);constexpr double radians=3.141592653589793/180;
            auto toward=[&](double aside,double above,double range){const double a=yaw+aside,c=std::cos(above);return MotorVec{eye.x+std::sin(a)*c*range,eye.y+std::sin(above)*range,eye.z+std::cos(a)*c*range};};
            for(const auto& sensor:senses.sensors){
                if(sensor.kind==darkangel::SensorKind::Distance)ring({foot.x,foot.y+.05,foot.z},sensor.range,IM_COL32(255,200,60,255));
                else if(sensor.kind==darkangel::SensorKind::LineOfSight)ring(eye,sensor.range,IM_COL32(80,200,255,255));
                else{const double h=sensor.horizontal*radians,v=sensor.vertical*radians;constexpr unsigned steps=24;
                    // Three arcs at the far end (bottom, level, top), the two side edges, and the facing line.
                    for(double above:{-v,0.,v})for(unsigned i=0;i<steps;++i)line(toward(-h+2*h*i/steps,above,sensor.range),toward(-h+2*h*(i+1)/steps,above,sensor.range),vision,above==0?2.f:1.f);
                    for(double aside:{-h,h}){for(double above:{-v,0.,v})line(eye,toward(aside,above,sensor.range),vision,above==0?2.f:1.f);for(unsigned i=0;i<8;++i)line(toward(aside,-v+2*v*i/8,sensor.range),toward(aside,-v+2*v*(i+1)/8,sensor.range),vision,1.f);}
                    line(eye,toward(0,0,sensor.range),vision,1.f);}
            }
            if(senses.everything){ImVec2 at;if(project({eye.x,eye.y+.5,eye.z},at))draw->AddText(at,IM_COL32(255,255,255,255),"sees everything");}
        };
        auto placed=[&](StableId object,MotorVec& foot,double& yaw){auto entity=editor.displayed().find(object);if(!editor.displayed().valid(entity))return false;const auto transform=editor.displayed().read(entity).transform;foot={transform.x,transform.y,transform.z};yaw=transform.yaw;return true;};
        draw->PushClipRect({area.x,area.y},{area.x+area.width,area.y+area.height},true);
        if(character&&character_resources&&editor.preview.active()){
            for(const auto& spawn:character_resources->spawns){if(!senses_all&&!senses_shown.contains(spawn.actor)&&editor.selected!=spawn.point&&editor.selected!=spawn.actor)continue;const auto* perception=character->perception(spawn.actor);MotorVec foot;double yaw{};if(!perception||!placed(spawn.actor,foot,yaw))continue;
                bool alert=false;const MotorVec eye{foot.x,foot.y+perception->senses().eye_height,foot.z};
                for(const auto& other:perception->actors()){alert|=other.detected;line(eye,{other.motor.position.x,other.motor.position.y+darkangel::perception_body_height,other.motor.position.z},other.detected?(other.visible?IM_COL32(255,60,60,255):IM_COL32(255,160,40,255)):other.sensed?IM_COL32(255,240,90,255):IM_COL32(160,160,160,255),other.detected?2.5f:1.f+float(other.awareness)*1.5f);}
                shape(perception->senses(),foot,yaw,alert);}
        }else if(auto spawn=editor.document->plan().spawns.find(editor.selected);spawn!=editor.document->plan().spawns.end()){
            try{const auto& source=editor.authoring.open(*editor.assets,spawn->second).value;MotorVec foot;double yaw{};if(source.contains("senses")&&placed(spawn->first,foot,yaw))shape(darkangel::decode_npc_senses(source.at("senses").dump()),foot,yaw,false);}catch(const std::exception&){}
        }
        // Encounter volumes, trigger volumes and encounter markers: always while authoring, on request in Play.
        if(!editor.preview.active()||world_debug){const auto& plan=editor.document->plan();auto pose_of=[&](StableId object,darkangel::Transform& out){auto entity=editor.displayed().find(object);if(!editor.displayed().valid(entity))return false;out=editor.displayed().read(entity).transform;return true;};darkangel::Transform at,to;
            auto box=[&](const darkangel::Transform& pose,const std::array<double,3>& half,ImU32 colour,float width){const double c=std::cos(pose.yaw),s=std::sin(pose.yaw);auto corner=[&](int index){const double x=(index&1?1:-1)*half[0]*pose.scale,y=(index&2?1:-1)*half[1]*pose.scale,z=(index&4?1:-1)*half[2]*pose.scale;return MotorVec{pose.x+c*x+s*z,pose.y+y,pose.z-s*x+c*z};};for(int index=0;index<8;++index)for(int bit:{1,2,4})if(!(index&bit))line(corner(index),corner(index|bit),colour,width);};
            StableId focus=editor.selected;if(auto volume=plan.volumes.find(focus);volume!=plan.volumes.end())focus=volume->second.encounter;else if(auto point=plan.spawn_owners.find(focus);point!=plan.spawn_owners.end())focus=point->second;
            for(const auto& [object,volume]:plan.volumes)if(pose_of(object,at)){const bool lit=volume.encounter==focus||object==editor.selected;box(at,volume.half,lit?IM_COL32(120,220,255,255):IM_COL32(70,140,170,170),object==editor.selected?2.5f:lit?1.8f:1.f);++volumes_drawn;if(lit&&pose_of(volume.encounter,to))line({at.x,at.y,at.z},{to.x,to.y+.5,to.z},IM_COL32(120,220,255,160),1.f);}
            for(const auto& [point,owner]:plan.spawn_owners)if(owner==focus&&pose_of(point,at)&&pose_of(owner,to))line({at.x,at.y+.9,at.z},{to.x,to.y+.5,to.z},IM_COL32(120,220,255,220),1.5f);
            // A spline: its curve and its points.
            for(const auto& built:build_splines()){const bool lit=built.id==editor.selected||(plan.spline_points.contains(editor.selected)&&plan.spline_points.at(editor.selected).spline==built.id);const ImU32 colour=IM_COL32(255,120,220,lit?255:190);
                if(built.curve){const auto& path=built.curve->polyline();for(std::size_t piece=0;piece+1<path.size();++piece)line({path[piece].x,path[piece].y+.05,path[piece].z},{path[piece+1].x,path[piece+1].y+.05,path[piece+1].z},colour,lit?2.5f:1.6f);++splines_drawn;}
                for(const auto& point:built.points){darkangel::Transform marker;marker.x=point.x;marker.y=point.y;marker.z=point.z;marker.scale=1;box(marker,{.15,.15,.15},colour,1.2f);}}
            // An interactable: its reach as a ring on the ground and a small marker box.
            for(const auto& [object,json]:plan.interactables)if(pose_of(object,at)){try{const auto item=darkangel::decode_interactable(json);const ImU32 colour=IM_COL32(120,255,160,object==editor.selected?255:190);ring({at.x,at.y+.05,at.z},item.radius,colour);box(at,{.25,.25,.25},colour,object==editor.selected?2.5f:1.2f);++volumes_drawn;}catch(const std::exception&){}}
            // Party travel: a transition's box, and an entry point as a ring with a mark the way the party will face.
            for(const auto& [object,json]:plan.transitions)if(pose_of(object,at)){try{const auto transition=darkangel::decode_transition(json);box(at,{transition.half.x,transition.half.y,transition.half.z},IM_COL32(190,130,255,object==editor.selected?255:190),object==editor.selected?2.5f:1.2f);if(transition.activation==darkangel::TransitionActivation::Interact)ring({at.x,at.y+.05,at.z},transition.radius,IM_COL32(190,130,255,170));ImVec2 label;if(project({at.x,at.y+transition.half.y+.3,at.z},label))draw->AddText(label,IM_COL32(230,210,255,255),"Scene transition");++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.entries)if(pose_of(object,at)){const ImU32 colour=IM_COL32(120,200,255,object==editor.selected?255:200);ring({at.x,at.y+.05,at.z},.6,colour);darkangel::Transform facing=at;facing.x+=std::sin(at.yaw)*.9;facing.z+=std::cos(at.yaw)*.9;facing.scale=1;box(facing,{.12,.12,.12},colour,2.f);ImVec2 label;if(project({at.x,at.y+1.,at.z},label)){std::string name="Entry";try{name="Entry: "+darkangel::decode_entry(json).name;}catch(const std::exception&){}draw->AddText(label,IM_COL32(210,235,255,255),name.c_str());}++volumes_drawn;}
            for(const auto& [object,json]:plan.pickups)if(pose_of(object,at)){try{const auto pickup=darkangel::decode_pickup(json);const ImU32 colour=IM_COL32(255,220,90,object==editor.selected?255:200);ring({at.x,at.y+.05,at.z},pickup.radius,colour);darkangel::Transform marker=at;marker.y+=.3;marker.scale=1;box(marker,{.2,.2,.2},colour,object==editor.selected?2.5f:1.4f);ImVec2 label;if(project({at.x,at.y+.8,at.z},label))draw->AddText(label,colour,("Pickup x"+std::to_string(pickup.quantity)).c_str());++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.altars)if(pose_of(object,at)){try{const auto altar=darkangel::decode_altar(json);const ImU32 colour=IM_COL32(255,150,110,object==editor.selected?255:210);ring({at.x,at.y+.05,at.z},altar.radius,colour);darkangel::Transform marker=at;marker.y+=.5;marker.scale=1;box(marker,{.35,.5,.35},colour,object==editor.selected?2.5f:1.6f);const auto arrival=darkangel::altar_arrival(altar,{at.x,at.y,at.z},at.yaw);ring({arrival.x,arrival.y+.05,arrival.z},.5,IM_COL32(120,200,255,200));line({at.x,at.y+.1,at.z},{arrival.x,arrival.y+.1,arrival.z},IM_COL32(120,200,255,160),1.f);ImVec2 label;if(project({at.x,at.y+1.3,at.z},label))draw->AddText(label,colour,("Altar: "+altar.name).c_str());++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.movers)if(pose_of(object,at)){try{const auto mover=darkangel::decode_mover(json);const ImU32 colour=IM_COL32(150,230,230,object==editor.selected?255:200);darkangel::Transform start=at;start.y+=mover.half.y*at.scale;box(start,{mover.half.x,mover.half.y,mover.half.z},colour,object==editor.selected?2.5f:1.4f);
                const double c=std::cos(at.yaw),s=std::sin(at.yaw);darkangel::Transform end=start;end.x+=(c*mover.offset.x+s*mover.offset.z)*at.scale;end.y+=mover.offset.y*at.scale;end.z+=(-s*mover.offset.x+c*mover.offset.z)*at.scale;box(end,{mover.half.x,mover.half.y,mover.half.z},IM_COL32(150,230,230,110),1.f);line({start.x,start.y,start.z},{end.x,end.y,end.z},IM_COL32(150,230,230,160),1.f);
                ImVec2 label;if(project({start.x,start.y+mover.half.y*at.scale+.3,start.z},label))draw->AddText(label,colour,mover.door?(mover.locked?"Door (locked)":"Door"):"Platform");++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.triggers)if(pose_of(object,at)){try{const auto trigger=darkangel::decode_trigger(json);box(at,{trigger.half.x,trigger.half.y,trigger.half.z},IM_COL32(255,170,60,object==editor.selected?255:180),object==editor.selected?2.5f:1.2f);++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.encounters)if(pose_of(object,at)){darkangel::Transform marker=at;marker.y+=.5;marker.scale=1;box(marker,{.4,.4,.4},object==focus?IM_COL32(255,255,255,255):IM_COL32(120,220,255,230),2.f);ImVec2 label;if(project({at.x,at.y+1.3,at.z},label))draw->AddText(label,IM_COL32(255,255,255,255),"Encounter");}
        }
        draw->PopClipRect();
    };
    darkangel::editor_app::RuntimeInventory runtime_inventory;darkangel::editor_app::RuntimeAltar runtime_altar;
    shell.game_overlay=[&](ImDrawList* draw,darkangel::editor_app::ViewArea area){
        if(!character||!character_resources||!character_resources->combat||!character->ability()){hud.reset();return;}
        if(hud_failed)return;
        try{
            const int width=std::max(1,int(area.width)),height=std::max(1,int(area.height));
            if(!hud)hud=std::make_unique<darkangel::Hud>(hud_renderer,std::filesystem::path(DAE_GAME_UI_DIR)/"hud.rml","C:/Windows/Fonts/segoeui.ttf",width,height);
            hud->resize(width,height);const auto& combat=*character_resources->combat;std::optional<darkangel::Health> target;const auto shown=choose_hud_target();if(shown){auto handle=character->presentation().find(*shown);if(character->presentation().valid(handle))target=character->presentation().read(handle).health;}
            auto hud_model=ashen_roots::royal_hud(*character->ability(),combat.attributes,target,darkangel::editor_app::RuntimeInventory::name(*character_resources,character->ability()));hud_model.target_locked=character->soft_lock().has_value();if(shown)if(const auto* actor=character->actor(*shown))hud_model.target_name=actor->name;hud_model.crosshair=director&&director->rig().cameras[director->live()].facing==CameraFacing::Camera;const auto offered_now=character->interaction_prompt();if(offered_now)hud_model.prompt=offered_now->prompt;if(const auto* owner=character->client(character_player))darkangel::editor_app::party_lifecycle_hud(hud_model,*owner,offered_now,interact_held);if(travel&&travel->busy()){const auto status=travel->status();hud_model.loading=true;hud_model.loading_text=status.text.substr(0,96);hud_model.loading_progress=status.progress;++travel_overlay_frames;}if(!hud_model.loading&&shell.scene_camera)if(const auto* viewer=character->client(character_player)){std::vector<PartyMember> roster;
#ifdef DAE_EDITOR_GNS
            if(party_host)roster=party_host->roster();else if(party_guest)roster=party_guest->roster();
#endif
            hud_model.world_markers=darkangel::editor_app::character_world_ui(*character,*viewer,darkangel::editor_app::world_ui_camera(shell.scene_view,shell.scene_projection,area.width,area.height),roster);}hud->update(hud_model);runtime_inventory.frame(*hud,character->client(character_player),*character_resources,area.x,area.y,area.width,area.height,ImGui::IsWindowFocused(),hud_model.loading);runtime_altar.frame(*hud,character->client(character_player),character->hosting(),ImGui::IsWindowFocused(),hud_model.loading);shell.inventory_open=runtime_inventory.open()||runtime_altar.open();hud_renderer.begin(draw,{area.x,area.y},{area.width,area.height});hud->render();hud_renderer.end();
        }catch(const std::exception& error){hud_renderer.end();hud.reset();hud_failed=true;editor.log(std::string("Runtime HUD unavailable: ")+error.what());}
    };
    // The second local player's HUD, from its own client: its attributes, mask and what it is offered. RmlUi allows one
    // Hud at a time, so the first viewport's Hud is laid out again with the second player's values and drawn here.
    shell.game_overlay_second=[&](ImDrawList* draw,darkangel::editor_app::ViewArea area){
        if(!character||character->players().size()<2||!character_resources||!character_resources->combat||!hud||hud_failed)return;
        const auto* second=character->client(character->players()[1]);if(!second||!second->ability())return;
        try{
            hud->resize(std::max(1,int(area.width)),std::max(1,int(area.height)));auto hud_model=ashen_roots::royal_hud(*second->ability(),character_resources->combat->attributes,std::optional<darkangel::Health>{},darkangel::editor_app::RuntimeInventory::name(*character_resources,second->ability()));hud_model.target_locked=second->soft_lock().has_value();if(const auto offered=second->interaction_prompt())hud_model.prompt=offered->prompt;if(travel&&travel->busy()){const auto status=travel->status();hud_model.loading=true;hud_model.loading_text=status.text.substr(0,96);hud_model.loading_progress=status.progress;}if(!hud_model.loading)hud_model.world_markers=darkangel::editor_app::character_world_ui(*character,*second,darkangel::editor_app::world_ui_camera(shell.game_view_second,shell.game_projection_second,area.width,area.height));hud->update(hud_model);hud_renderer.begin(draw,{area.x,area.y},{area.width,area.height});hud->render();hud_renderer.end();hud_second_drawn=true;
        }catch(const std::exception& error){hud_renderer.end();hud.reset();hud_failed=true;editor.log(std::string("Runtime HUD unavailable: ")+error.what());}
    };
#endif
    if(!args.character_clips.empty()){require(inputs!=nullptr,"Character controls require a cooked input profile");const char* required[]={"move.forward","move.lateral","look.turn","walk","jump"};for(unsigned action=1;action<=5;++action){auto definition=std::find_if(inputs->profile().actions.begin(),inputs->profile().actions.end(),[&](const auto& candidate){return candidate.id==action;});require(definition!=inputs->profile().actions.end()&&definition->name==required[action-1]&&definition->kind==(action<=3?InputActionKind::Axis:InputActionKind::Button),"Character input semantic contract");}require(scene_skins.size()==1,"Character preset requires one loaded skin");character_resources=std::make_unique<darkangel::editor_app::CharacterPreviewResources>(darkangel::editor_app::load_character_preview(args.registry,args.cas,args.character_clips,scene_skins.begin()->second.model,scene_role(args.character_player),registry_spawns(args.registry),args.character_collision.empty()?AssetId{}:AssetId::parse(args.character_collision),registry_encounter_npcs(args.registry)));shell.native_controls=true;if(args.character_clips.starts_with("kit:"))editor.authoring_kit=AssetId::parse(args.character_clips.substr(4));
    // A party guest launched after its host prepared Play with a Player gets "player:<id>": it selects that Player, so it can prepare later scenes too.
    else if(args.character_clips.starts_with("player:")&&editor.assets){editor.authoring_player=AssetId::parse(args.character_clips.substr(7));editor.authoring_kit=AssetId::parse(editor.authoring.open(*editor.assets,editor.authoring_player).value.at("loadout").at("kit").get<std::string>());}if(character_resources->spawns.size()!=editor.document->plan().spawns.size())editor.log("Some spawn points reference NPCs outside the loaded registry; they join when Play prepares the scene from the project sources.",darkangel::editor_app::ConsoleSeverity::Warning);
        // Every cue asset of the project, cooked and loaded from its frozen product. Cues are not yet in the Play package through the actions and
        // effects that name them, so they are packaged on their own; a session started from a cooked registry alone uses the cues in that registry.
        auto load_play_cues=[&]{std::vector<darkangel::CueDefinition> cues;try{
                play_cue_closure=0;if(editor.assets){editor.assets->scan();std::set<AssetId> held;std::set<std::string> keys;
                    // Cues the Play package reached through the effects that bind them come first. The project's other cues answer the keys nothing names by asset (action cue blocks, footsteps).
                    if(!args.registry.empty()&&std::filesystem::is_regular_file(args.registry)){try{for(auto& cue:darkangel::load_cooked_cues(args.registry,args.cas)){held.insert(cue.id);keys.insert(cue.key);cues.push_back(std::move(cue));}}catch(const std::exception& error){cues.clear();held.clear();keys.clear();editor.log(std::string("The Play package's cues could not be read: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Warning);}}
                    play_cue_closure=cues.size();std::vector<AssetId> roots;for(const auto& asset:editor.assets->assets())if(std::filesystem::path(asset.path).extension()==".dacue"&&!held.contains(asset.id))roots.push_back(asset.id);
                    if(roots.size()>64){editor.log("This project has more than 64 cue assets outside the Play package; Play uses the first 64 of them.",darkangel::editor_app::ConsoleSeverity::Warning);roots.resize(64);}
                    if(!roots.empty()){const auto registry=editor.assets->cas_path().parent_path()/("cues-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);for(auto& cue:darkangel::load_cooked_cues(registry,editor.assets->cas_path())){if(keys.insert(cue.key).second)cues.push_back(std::move(cue));else editor.log("Two cues answer to the key "+cue.key+"; "+cue.name+" is not used.",darkangel::editor_app::ConsoleSeverity::Warning);}}}
                else cues=darkangel::load_cooked_cues(args.registry,args.cas);
                // The weapon trails and decals the cues name, by the identities in the cue products.
                play_trails.clear();play_decals.clear();if(editor.assets){std::vector<AssetId> primitives;for(const auto& cue:cues)for(const auto id:{cue.trail_asset,cue.decal_asset})if(id!=AssetId{}&&std::find(primitives.begin(),primitives.end(),id)==primitives.end())primitives.push_back(id);
                    if(!primitives.empty()&&primitives.size()<=64){const auto registry=editor.assets->cas_path().parent_path()/("primitives-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};darkangel::editor_app::package_authoring(*editor.assets,primitives,registry);for(auto& trail:darkangel::load_cooked_trails(registry,editor.assets->cas_path()))play_trails[trail.id]=std::move(trail);for(auto& decal:darkangel::load_cooked_decals(registry,editor.assets->cas_path()))play_decals[decal.id]=std::move(decal);}}
                // A cue's visual names an effect source by its path; each is cooked, packaged and loaded into the service.
                play_effects.clear();if(vfx&&editor.assets){std::vector<AssetId> effects;std::vector<std::string> names;for(const auto& cue:cues){if(cue.visual.empty()||std::find(names.begin(),names.end(),cue.visual)!=names.end())continue;bool found=false;for(const auto& asset:editor.assets->assets())if(cue.visual_asset!=AssetId{}?asset.id==cue.visual_asset:asset.path==cue.visual){effects.push_back(asset.id);names.push_back(cue.visual);found=true;}if(!found)editor.log("Cue "+cue.name+" names a visual effect that is not in the project: "+cue.visual,darkangel::editor_app::ConsoleSeverity::Warning);}
#ifdef DAE_AUDIO
                    // A cue's audio names a sound source by its path, the same way.
                    play_sounds.clear();if(ensure_audio()){std::vector<AssetId> sounds;for(const auto& cue:cues){if(cue.audio.empty()||play_sounds.contains(cue.audio))continue;AssetId found=cue.audio_asset;if(found==AssetId{})for(const auto& asset:editor.assets->assets())if(asset.path==cue.audio)found=asset.id;
                            if(found==AssetId{}){editor.log("Cue "+cue.name+" names a sound that is not in the project: "+cue.audio,darkangel::editor_app::ConsoleSeverity::Warning);continue;}sounds.push_back(found);play_sounds[cue.audio]=found;}
                        load_audio(sounds);}
#endif
                    if(!effects.empty()&&effects.size()<=64){const auto registry=editor.assets->cas_path().parent_path()/("vfx-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};darkangel::editor_app::package_authoring(*editor.assets,effects,registry);play_effect_generations.clear();for(std::size_t index=0;index<effects.size();++index){const auto cooked=darkangel::load_cooked_vfx(registry,editor.assets->cas_path(),effects[index]);vfx->load(cooked);play_effects[names[index]]=effects[index];play_effect_generations[names[index]]=cooked.generation.substr(0,12);}}}
            }catch(const std::exception& error){cues.clear();editor.log(std::string("Cues are unavailable for this Play: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Warning);}
            return cues;};
        editor.prepare_gameplay=[&,load_play_cues](const SpawnPlan& plan,const World& world){navigation_polygons=0;behavior_running.clear();hud_target.reset();// Every scene session of this Play gets its own controller source: behaviours run on the navigation mesh of the collision that session simulates.
            make_controller=[&]{SpawnController controller;
#ifdef DAE_BEHAVIOR
            // A spawned NPC with an authored behaviour runs its tree on the navigation mesh of this session's collision; one without a behaviour stays passive.
            // Held by the controller source itself: an encounter asks it for controllers long after this preparation has returned.
            auto mesh_slot=std::make_shared<std::shared_ptr<const darkangel::NavigationMesh>>();
            controller=[&,mesh_slot](const darkangel::editor_app::PreparedSpawn& spawn,const darkangel::CollisionDefinition& collision)->std::shared_ptr<darkangel::ActorController>{auto& session_mesh=*mesh_slot;if(!spawn.behavior)return {};if(!session_mesh)session_mesh=navigation_for(collision);navigation_polygons=session_mesh->polygons();if(behavior_running.empty())behavior_running=spawn.behavior->name;return darkangel::make_behavior_controller(*spawn.behavior,session_mesh,spawn.tuning);};
#endif
            return controller;};
#ifdef DAE_EDITOR_GNS
            if(!args.join.empty())make_controller=[] {return SpawnController{};};
#endif
            auto controller=make_controller();
            companions.clear();if(character_resources->combat)for(int seat=1;seat<shell.players;++seat)companions.push_back(darkangel::party_member(unsigned(seat)));director_second.reset();script_at=0;script_interactions=0;
            auto first_scene=std::make_unique<darkangel::PreparedScene>(darkangel::editor_app::prepare_character_scene(plan,world,*character_resources,[&](AssetId asset){return load_cooked_model(args.registry,args.cas,asset);},controller));first_scene->scene=editor.document->root();character_player=first_scene->player;
            play_ledger=darkangel::WorldLedger{};parked_documents.clear();parked_paths.clear();globals_scene=authoring_scene=editor.document->root();authoring_path=editor.path;travel_preview.reset();travel_load=TravelLoad{};travel_arrivals=travel_failures=travel_overlay_frames=0;travel_failure.clear();travel_was_busy=false;player_script_at=0;player_script_interactions=0;
            travel=std::make_unique<darkangel::SceneTransitionCoordinator>(scene_provider,play_ledger,std::vector<StableId>(companions.begin(),companions.end()));if(args.travel_hold>=0)travel->minimum_seconds=args.travel_hold;
#ifdef DAE_EDITOR_GNS
            travel->inventory_profile=darkangel::editor_app::local_player_profile(args.local_profile==4?0:args.local_profile);
            const bool network_host=args.join.empty()&&!args.exercise_two_players&&(shell.players>1||!args.listen.empty());
            if(network_host){party_host=std::make_unique<darkangel::editor_app::EditorPartyHost>(darkangel::editor_app::local_player_profile(args.local_profile==4?0:args.local_profile),unsigned(shell.players),args.listen);
                travel->build_session=[&](darkangel::PreparedScene& scene,StableId entry,const darkangel::SceneStart& start,std::span<const darkangel::ScenePlayer> others){return party_host->build(scene,entry,start,others);};
                travel->ready=[&](CharacterSceneSession& session){return party_host->ready(session);};travel->resumed=[&]{party_host->resume();};travel->cancelled=[&](std::string_view reason){party_host->cancel(reason);};travel->construction_text=[&]{return party_host->status();};travel->party_size=[&]{return party_host->members();};}
#endif
            // What hangs off a session is dropped before the coordinator destroys it, and built again for the next one.
            travel->unloading=[&]{
#ifdef DAE_HUD
                runtime_inventory.reset();runtime_altar.reset();shell.inventory_open=false;
#endif
                hud_target.reset();if(character)character->present_cues(nullptr);cue_player.reset();cue_effects.bind(nullptr,nullptr,{});cue_primitives.bind(nullptr,nullptr,{});
#ifdef DAE_AUDIO
                cue_sounds.bind(nullptr,nullptr,{});audio_director.reset();
#endif
                director.reset();director_second.reset();character=nullptr;};
            travel->loaded=[&]{character=travel->session();character_player=travel->scene()->player;
                // The Preview world (scripts) of the scene the editor's document now is.
                if(travel_preview&&travel->scene()->scene==globals_scene){editor.preview.retire();editor.preview.activate(std::move(travel_preview));}
                attach_session(editor.document->plan());};
            #ifdef DAE_EDITOR_GNS
            if(!args.join.empty()){
                const bool local=args.join.starts_with("127.0.0.1:");const auto slot=args.local_profile==4?(local?args.player:0):args.local_profile;
                party_guest=std::make_unique<darkangel::editor_app::EditorPartyGuest>(darkangel::editor_app::local_player_profile(slot),args.player,args.join,scene_provider,args.party_mismatch);
                party_guest->unloading=travel->unloading;party_guest->loaded=[&]{character=party_guest->session();character_player=party_guest->owner();if(travel_preview){editor.preview.retire();editor.preview.activate(std::move(travel_preview));}attach_session(editor.document->plan());};
                party_guest->initial(std::move(first_scene));travel.reset();shell.client_only=true;
            }else
#endif
            {// Play from the save slot: its ledger and inventories, and the party at its checkpoint (in this scene, or travelling there).
                std::optional<darkangel::WorldDestination> checkpoint;
                if(play_saves&&play_saves->play_from_slot){std::string warning;try{if(auto loaded=play_saves->load(warning)){play_ledger=std::move(loaded->ledger);travel->restore_inventory(std::move(loaded->inventories));checkpoint=darkangel::read_checkpoint(play_ledger);editor.log("Play starts from the save slot.");}else editor.log("The save slot is empty; Play starts fresh.");}catch(const std::exception& error){editor.log(std::string("The save could not be loaded: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Error);}if(!warning.empty())editor.log(warning,darkangel::editor_app::ConsoleSeverity::Warning);}
                const bool here=checkpoint&&checkpoint->scene==first_scene->scene;
                travel->start(std::move(first_scene),here?checkpoint->altar:StableId{});character=travel->session();if(checkpoint&&!here&&!travel->request({checkpoint->scene,checkpoint->altar,"Play from save"}))editor.log("The saved checkpoint's scene could not be entered.",darkangel::editor_app::ConsoleSeverity::Warning);}
            save_play=[&](std::string reason){if(!play_saves||!travel||!travel->scene()||!character||!character->hosting())return;character->capture();auto* inventory=travel->inventory();
#ifdef DAE_EDITOR_GNS
                if(!inventory&&party_host)inventory=party_host->inventory();
#endif
                if(play_saves->save(darkangel::editor_app::encode_play_save(play_ledger,inventory,travel->scene()->scene,{}),std::move(reason)))editor.log("Saving the game.");};
            attach_session=[&,load_play_cues](const SpawnPlan& plan){
            // An effect cue names its cue asset; an action cue block only has a key.
            cue_player.reset();cue_effects.bind(nullptr,nullptr,{});cue_primitives.bind(nullptr,nullptr,{});
#ifdef DAE_AUDIO
            cue_sounds.bind(nullptr,nullptr,{});
#endif
            cue_visual.clear();cue_audio.clear();if(character_resources->combat){if(!play_cues)play_cues=load_play_cues();cue_player=std::make_unique<darkangel::CuePlayer>([&](std::string_view key,AssetId cue)->std::optional<darkangel::CueDefinition>{for(const auto& known:*play_cues)if(cue!=AssetId{}&&known.id==cue)return known;for(const auto& known:*play_cues)if(known.key==key)return known;return std::nullopt;},&cue_effects,
#ifdef DAE_AUDIO
                    &cue_sounds
#else
                    &cue_audio
#endif
                    );character->present_cues(cue_player.get());
                cue_primitives.bind(&play_trails,&play_decals,[&](std::uint64_t network)->std::optional<std::array<float,3>>{if(!character)return std::nullopt;const auto identity=character->identity(network);if(!identity)return std::nullopt;const auto handle=character->presentation().find(*identity);if(!character->presentation().valid(handle))return std::nullopt;const auto& at=character->presentation().read(handle).transform;return std::array<float,3>{float(at.x),float(at.y),float(at.z)};});
                cue_effects.bind(vfx.get(),&play_effects,[&](std::uint64_t network)->std::optional<std::array<float,3>>{if(!character)return std::nullopt;const auto identity=character->identity(network);if(!identity)return std::nullopt;const auto handle=character->presentation().find(*identity);if(!character->presentation().valid(handle))return std::nullopt;const auto& at=character->presentation().read(handle).transform;return std::array<float,3>{float(at.x),float(at.y)+1.f,float(at.z)};});vfx_tick=0;
#ifdef DAE_AUDIO
                cue_sounds.bind(audio.get(),&play_sounds,[&](std::uint64_t network)->std::optional<std::array<float,3>>{if(!character)return std::nullopt;const auto identity=character->identity(network);if(!identity)return std::nullopt;const auto handle=character->presentation().find(*identity);if(!character->presentation().valid(handle))return std::nullopt;const auto& at=character->presentation().read(handle).transform;return std::array<float,3>{float(at.x),float(at.y)+1.f,float(at.z)};});
                // The scene's audio profile starts with Play; an encounter's is layered over it while the encounter runs.
                audio_director.reset();play_profiles.clear();encounter_profiles.clear();audio_tab.profile_audition=false;audio_in_play=true;
                if(audio){std::vector<AssetId> wanted;for(const auto& [object,profile]:plan.audio_profiles)wanted.push_back(profile);for(const auto& [object,component]:plan.encounters){try{if(const auto profile=darkangel::decode_encounter(component).audio_profile;profile!=AssetId{}){wanted.push_back(profile);encounter_profiles[object]=profile;}}catch(const std::exception&){}}
                    try{play_profiles=load_audio(wanted);for(const auto profile:wanted)if(!play_profiles.contains(profile))editor.log("An audio profile named by the scene is not in the project: "+profile.text(),darkangel::editor_app::ConsoleSeverity::Warning);}catch(const std::exception& error){editor.log(std::string("Audio profiles are unavailable for this Play: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Warning);}
                    audio_director=std::make_unique<darkangel::AudioDirector>(*audio,project_mix());audio_applied=project_mix();if(!plan.audio_profiles.empty())if(const auto found=play_profiles.find(plan.audio_profiles.begin()->second);found!=play_profiles.end())audio_director->scene(found->second);}
#endif
            }
            };if(character)attach_session(plan);
#ifdef DAE_EDITOR_GNS
            if(party_host){std::vector<std::string> child{"--registry",args.registry.string(),"--cas",args.cas.string(),"--model",args.model,"--scene",editor.path.string(),"--backend",args.backend,"--character-clips",args.character_clips,"--game-workspace"};
                if(!args.sources.empty())child.insert(child.end(),{"--sources",args.sources.string(),"--asset-cache",args.asset_cache.string()});if(!args.character_collision.empty())child.insert(child.end(),{"--character-collision",args.character_collision});if(!args.character_player.empty())child.insert(child.end(),{"--character-player",args.character_player});if(!args.player_asset.empty())child.insert(child.end(),{"--player-asset",args.player_asset});if(!args.input_script.empty())child.insert(child.end(),{"--player-script",args.input_script});if(args.hidden)child.push_back("--hidden");if(args.no_audio)child.push_back("--no-audio");if(args.party_mismatch)child.push_back("--party-mismatch");
                for(unsigned seat=1;seat<unsigned(shell.players);++seat){auto arguments=child;if(!args.capture.empty())arguments.insert(arguments.end(),{"--capture",(args.capture.parent_path()/(args.capture.stem().string()+"-player-"+std::to_string(seat)+args.capture.extension().string())).string()});if(!args.party_report.empty())arguments.insert(arguments.end(),{"--party-report",(args.party_report.parent_path()/(args.party_report.stem().string()+"-player-"+std::to_string(seat)+".json")).string()});party_host->launch(seat,std::move(arguments),std::filesystem::path(".cache")/"party"/("player-"+std::to_string(seat)+".log"));}editor.log("Party host listening at "+party_host->address());}
#endif
            editor.log(character_resources->combat?"Native Loopback combat Play: light attack uses the cooked kit, action and authoritative target damage. Click Game for input.":"Native Loopback character Play: locomotion and solid box collision proxies. Use Game to play; click its viewport for input.");};
        shell.draw_gameplay_inspector=[&]{
            if(!character||!character_resources->combat){ImGui::TextDisabled("Start combat Play to inspect actor state.");return;}
            const auto& content=*character_resources->combat;auto confirmed=character->ability(),predicted=character->predicted_ability();if(!confirmed)return;
            ImGui::Text("Confirmed tick %llu | predicted tick %llu",static_cast<unsigned long long>(confirmed->tick),static_cast<unsigned long long>(predicted?predicted->tick:0));ImGui::TextWrapped("Kit %s",content.kit->id.text().c_str());if(!content.actor_generation.empty())ImGui::Text("Player generation %.12s",content.actor_generation.c_str());
            if(ImGui::BeginTabBar("Gameplay inspector sections")){if(ImGui::BeginTabItem("Player")){AttributeSet spawn(content.attributes);if(ImGui::BeginTable("Gameplay attribute state",4,ImGuiTableFlags_BordersInnerV)){ImGui::TableSetupColumn("Attribute");ImGui::TableSetupColumn("Spawn");ImGui::TableSetupColumn("Confirmed");ImGui::TableSetupColumn("Predicted");ImGui::TableHeadersRow();for(const auto& definition:content.attributes){ImGui::TableNextColumn();ImGui::TextUnformatted(definition.name.c_str());ImGui::TableNextColumn();ImGui::Text("%.2f",spawn.value(definition.id));ImGui::TableNextColumn();if(auto value=darkangel::editor_app::debug_attribute(confirmed,definition.id))ImGui::Text("%.2f",*value);else ImGui::TextDisabled("--");ImGui::TableNextColumn();if(auto value=darkangel::editor_app::debug_attribute(predicted,definition.id))ImGui::Text("%.2f",*value);else ImGui::TextDisabled("--");}ImGui::EndTable();}
            ImGui::SeparatorText("Action and outcomes");ImGui::TextUnformatted(confirmed->active?"Presentation: confirmed ability action":"Presentation: locomotion graph");ImGui::Text("Graph phase %.3f",character->graph().phase);for(const auto& layer:character->graph_inputs().span()){auto id=layer.clip->definition().id;std::string name=id.text();for(const auto& asset:editor.asset_inventory)if(asset.id==id){name=std::filesystem::path(asset.path).filename().string();break;}ImGui::TextWrapped("Locomotion: %s / weight %.3f / tick %.2f",name.c_str(),layer.weight,layer.tick);}if(confirmed->action)ImGui::Text("Composer clock %.2f ticks",double(confirmed->action->clock)/action_tick_units);
            for(const auto& cooldown:confirmed->cooldowns)if(cooldown.second>confirmed->tick)ImGui::Text("Cooldown group %u: %llu ticks",cooldown.first,static_cast<unsigned long long>(cooldown.second-confirmed->tick));
            if(confirmed->reservation)for(const auto& cost:confirmed->reservation->costs)ImGui::Text("Reserved attribute %u: %.2f",cost.attribute,cost.amount);
            unsigned displayed=0;for(auto it=confirmed->operations.rbegin();it!=confirmed->operations.rend()&&displayed<8;++it,++displayed)ImGui::TextWrapped("Operation %llu: %s%s",static_cast<unsigned long long>(it->operation),darkangel::editor_app::ability_failure_text(it->failure),it->committed?" (committed)":"");
            if(confirmed->active&&ImGui::Button("Inspect active ability asset")){try{shell.open_native_asset(editor,confirmed->ability);}catch(const std::exception& error){editor.log(error.what());}}
            ImGui::SeparatorText("Player tags");auto player_effects=character->effects(character_player);const EffectFrame* owner_frame=player_effects&&player_effects->current()?&*player_effects->current():nullptr;
            if(content.tags)for(auto tag:confirmed->tags.values){const auto& definitions=content.tags->definitions();auto found=std::find_if(definitions.begin(),definitions.end(),[&](const auto& definition){return definition.id==tag;});if(found==definitions.end())continue;ImGui::TextUnformatted(found->name.c_str());auto sources=darkangel::editor_app::tag_debug_sources(tag,*content.tags,confirmed,owner_frame,content.abilities,content.effects);for(const auto& source:sources)if(source.action)ImGui::Text("  Action activation %llu / block %u",static_cast<unsigned long long>(source.activation),source.block);else ImGui::Text("  Effect %llu / source actor %llu / epoch %llu",static_cast<unsigned long long>(source.effect),static_cast<unsigned long long>(source.source_network),static_cast<unsigned long long>(source.session_epoch));if(sources.empty())ImGui::TextDisabled("  Contributor not exposed in received state");}
            ImGui::EndTabItem();}auto effects_selected=shell.select_effect_debug?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;shell.select_effect_debug=false;if(ImGui::BeginTabItem("World")){const auto& world=character->world_events();ImGui::Checkbox("Draw encounter and trigger volumes in the viewports",&world_debug);
                static char event_name[49]="";ImGui::SetNextItemWidth(220);ImGui::InputText("##world-event",event_name,sizeof(event_name));ImGui::SameLine();ImGui::BeginDisabled(!darkangel::world_name_valid(event_name));if(ImGui::Button("Emit world event"))character->emit_world_event(event_name);ImGui::EndDisabled();
                ImGui::SeparatorText("World tags");if(world.tags().empty())ImGui::TextDisabled("None set.");for(const auto& tag:world.tags())ImGui::BulletText("%s",tag.c_str());
                ImGui::SeparatorText("Encounters");if(character->encounters().empty())ImGui::TextDisabled("This scene has no encounters. Place one from the Scene tab's Place panel.");
                for(auto encounter:character->encounters()){const auto status=*character->encounter(encounter);std::string name="Encounter";for(const auto& origin:editor.document->plan().origins)if(origin.object==encounter)name=origin.name;const char* phase=status.phase==darkangel::EncounterPhase::Waiting?"waiting":status.phase==darkangel::EncounterPhase::Running?"running":"complete";
                    if(status.waves)ImGui::Text("%s: %s, wave %u of %u, %u of %u spawned, %u alive",name.c_str(),phase,status.wave,status.waves,status.spawned,status.total,status.alive);else ImGui::Text("%s: %s, %u of %u spawned, %u alive",name.c_str(),phase,status.spawned,status.total,status.alive);
                    for(auto actor:character->actors())if(character->actor_encounter(actor)==encounter){auto handle=character->presentation().find(actor);const auto* config=character->actor(actor);if(character->presentation().valid(handle)&&config){const auto health=character->presentation().read(handle).health;ImGui::Text("   %s: Health %.0f / %.0f",config->name.c_str(),health.current,health.maximum);}}}
                ImGui::SeparatorText("Interactables");if(editor.document->plan().interactables.empty())ImGui::TextDisabled("None in this scene.");
                for(const auto& [object,json]:editor.document->plan().interactables)if(const auto used=character->interactable_used(object)){std::string name="Interactable";for(const auto& origin:editor.document->plan().origins)if(origin.object==object)name=origin.name;ImGui::Text("%s: used %u time(s)",name.c_str(),*used);}
                runtime_inventory.inspector(*character,character_player,*character_resources);
            if(const auto* outcome=character->last_interaction())ImGui::Text("Latest request: tick %llu, %s",static_cast<unsigned long long>(outcome->tick),darkangel::interaction_result_text(outcome->result));
                ImGui::SeparatorText("Trigger volumes");if(editor.document->plan().triggers.empty())ImGui::TextDisabled("None in this scene.");
                for(const auto& [object,json]:editor.document->plan().triggers)if(const auto fired=character->trigger_fired(object)){std::string name="Trigger volume";for(const auto& origin:editor.document->plan().origins)if(origin.object==object)name=origin.name;ImGui::Text("%s: fired %u time(s)",name.c_str(),*fired);}
                ImGui::SeparatorText("World events (latest last)");if(world.log().empty())ImGui::TextDisabled("None yet.");for(const auto& event:world.log())ImGui::Text("tick %llu  %s",static_cast<unsigned long long>(event.tick),event.name.c_str());
                ImGui::EndTabItem();}
            const auto cues_selected=cue_debug_selected?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;cue_debug_selected=false;if(ImGui::BeginTabItem("Cues",nullptr,cues_selected)){
                if(!cue_player||!play_cues)ImGui::TextDisabled("This session has no cue player.");else{
                    ImGui::TextWrapped("%zu cue asset(s) loaded. Visual cues play as effects and audio cues as sounds; the lists below are what each side was told.",play_cues->size());
                    ImGui::Text("Live %zu | keys without a cue asset %llu | dropped %llu",cue_player->live(),static_cast<unsigned long long>(cue_player->unresolved()),static_cast<unsigned long long>(cue_player->dropped()));
                    const auto who=[&](std::uint64_t network){std::string name="Actor "+std::to_string(network);if(const auto identity=character->identity(network)){if(*identity==character_player)name="Player";else if(const auto* known=character->actor(*identity))if(!known->name.empty())name=known->name;}return name;};
                    if(vfx){const auto drawn=vfx->stats();ImGui::Text("Visual effects: %zu loaded, %zu playing, %zu particles, %u quads in %u draws",drawn.effects,drawn.playing,drawn.instances,drawn.quads,drawn.draws);ImGui::Text("Trails: %zu live, %zu points. Decals: %zu. %u quad(s) of both drawn.",cue_primitives.trails.live(),cue_primitives.trails.points(),cue_primitives.decals.live(),drawn.primitives);if(drawn.unsupported)ImGui::TextWrapped("%u batch(es) skipped: they need shading the renderer does not have.",drawn.unsupported);if(cue_effects.missing())ImGui::TextWrapped("%u cue(s) named a visual effect that is not loaded.",cue_effects.missing());}else ImGui::TextWrapped("Visual effects are unavailable: %s",vfx_failed.c_str());
#ifdef DAE_AUDIO
                    if(audio){const auto heard=audio->stats();ImGui::Text("Audio (%s): %zu sound(s) loaded, %zu playing, %llu started",heard.device?"device":"silent driver",heard.sounds,heard.playing,static_cast<unsigned long long>(heard.started));if(cue_sounds.missing())ImGui::TextWrapped("%u cue(s) named a sound that is not loaded.",cue_sounds.missing());}else if(!audio_failed.empty())ImGui::TextWrapped("Audio is unavailable: %s",audio_failed.c_str());
#endif
                    ImGui::SeparatorText("Live cues");const auto live=cue_player->instances();if(live.empty())ImGui::TextDisabled("None.");
                    for(const auto& cue:live){if(cue.stopping_until)ImGui::TextWrapped("%s on %s: %s, started tick %llu, fading until tick %llu",cue.key.c_str(),who(cue.network).c_str(),cue.action?"action":"effect",static_cast<unsigned long long>(cue.started),static_cast<unsigned long long>(cue.stopping_until));else ImGui::TextWrapped("%s on %s: %s, started tick %llu",cue.key.c_str(),who(cue.network).c_str(),cue.action?"action":"effect",static_cast<unsigned long long>(cue.started));}
                    ImGui::SeparatorText("Visual (latest last)");if(cue_visual.log().empty())ImGui::TextDisabled("Nothing yet.");for(const auto& line:cue_visual.log())ImGui::TextWrapped("%s",line.c_str());
                    ImGui::SeparatorText("Audio (latest last)");if(cue_audio.log().empty())ImGui::TextDisabled("Nothing yet.");for(const auto& line:cue_audio.log())ImGui::TextWrapped("%s",line.c_str());}
                ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Actors",nullptr,effects_selected)){
                auto presented=[&](const char* label,StableId actor){if(auto death=character->death(actor)){ImGui::TextWrapped("%s: Health 0 - death timeline at tick %.0f of %u%s",label,*death,content.death->duration/action_tick_units,*death>=double(content.death->duration/action_tick_units-1)?" (final pose held)":"");return;}if(auto reaction=character->reaction(actor))ImGui::TextWrapped("%s: effect %llu shows its reaction timeline at tick %.0f of %u (priority %u)%s",label,static_cast<unsigned long long>(reaction->key.handle),reaction->tick,reaction->action->duration/action_tick_units,reaction->action->priority,reaction->suppressed==ReactionSuppression::ActiveAction?" - waiting: the actor's own action is active":reaction->suppressed==ReactionSuppression::Dead?" - suppressed: Health is 0":"");else ImGui::TextDisabled("%s: no current reaction",label);};
                ImGui::SeparatorText("Reaction presentation");presented("Player",character_resources->player);ImGui::TextDisabled("Chosen from current public effect instances: highest timeline priority, then latest onset. Presentation only.");
                if(character->actors().empty())ImGui::TextDisabled(content.encounters.empty()?"This scene has no NPC spawn points. Add one in the Scene tab.":"No actors yet: this scene's encounters have not brought any in.");
                auto actor_name=[&](StableId id){std::string name=id==character_resources->player?"Player":"another actor";if(const auto* known=character->actor(id))if(!known->name.empty())name=known->name;return name;};
                // One section per server-driven actor the session holds now, in simulation order: spawn-point NPCs and the enemies encounters brought in.
                for(const auto id:character->actors()){const auto* config=character->actor(id);if(!config)continue;const auto& actor=*config;const darkangel::editor_app::PreparedSpawn* prepared=nullptr;for(std::size_t index=0;index<content.actors.size()&&index<character_resources->spawns.size();++index)if(content.actors[index].id==id)prepared=&character_resources->spawns[index];
                    const auto owner=character->actor_encounter(id);if(!prepared&&owner)for(const auto& [npc,made]:character_resources->encounter_npcs)if(made.actor.generation==actor.generation&&made.actor.name==actor.name)prepared=&made.spawn;
                    ImGui::PushID(actor.id.text().c_str());ImGui::SeparatorText(((actor.name.empty()?std::string("Actor"):actor.name)+(owner?" (encounter)":"")).c_str());
                    auto handle=character->presentation().find(actor.id);if(character->presentation().valid(handle)){auto health=character->presentation().read(handle).health;ImGui::Text("Health %.0f / %.0f | faction %s",health.current,health.maximum,actor.faction.c_str());}
                    if(prepared&&prepared->behavior)ImGui::Text("Behaviour: %s",prepared->behavior->name.c_str());else ImGui::TextDisabled("No behaviour: this actor never acts.");
                    {const auto decision=character->controller_status(actor.id);if(!decision.empty())ImGui::TextWrapped("Decision: %s",decision.c_str());}
                    if(auto chosen=character->controller_target(actor.id))ImGui::Text("Chosen target: %s",actor_name(*chosen).c_str());
                    // What the actor's senses give its controller, as authority holds it.
                    if(const auto* perception=character->perception(actor.id)){const auto& senses=perception->senses();bool shown=senses_shown.contains(actor.id);if(ImGui::Checkbox("Draw senses in the viewports",&shown)){if(shown)senses_shown.insert(actor.id);else senses_shown.erase(actor.id);}
                        if(senses.everything)ImGui::TextUnformatted("Senses: sees everything");else if(senses.sensors.empty())ImGui::TextDisabled("Senses: none, this actor perceives nothing.");else ImGui::Text("Senses: %u sensor(s), eye at %.2f m, aware after %.2f s, forgets after %.1f s",unsigned(senses.sensors.size()),senses.eye_height,senses.awareness_seconds,senses.forget_seconds);
                        const auto now=character->motor().tick;auto self=character->presentation().find(actor.id);darkangel::Transform here{};if(character->presentation().valid(self))here=character->presentation().read(self).transform;
                        if(perception->actors().empty())ImGui::TextDisabled("Perceives nobody.");
                        for(const auto& other:perception->actors()){const std::string name=actor_name(other.id);const double dx=other.motor.position.x-here.x,dy=other.motor.position.y-here.y,dz=other.motor.position.z-here.z,distance=std::sqrt(dx*dx+dy*dy+dz*dz);
                            if(other.detected)ImGui::Text("  Detects %s at %.1f m, for %.1f s, %s",name.c_str(),distance,double(now>other.since?now-other.since:0)/60,other.visible?"line of sight":"no line of sight");else if(other.seen)ImGui::Text("  Lost %s %.1f s ago, last known %.1f m away (awareness %.0f%%)",name.c_str(),double(now>other.seen?now-other.seen:0)/60,distance,other.awareness*100);else ImGui::Text("  Becoming aware of %s: %.0f%%%s",name.c_str(),other.awareness*100,other.sensed?"":" (fading)");}}
                    if(auto receipt=character->controller_receipt(actor.id)){if(receipt->failure==darkangel::AbilityFailure::None)ImGui::Text("Last request: tick %llu started activation %llu",static_cast<unsigned long long>(receipt->tick),static_cast<unsigned long long>(receipt->handle.activation));else ImGui::Text("Last request: tick %llu refused (%s)",static_cast<unsigned long long>(receipt->tick),receipt->failure==darkangel::AbilityFailure::Resources?"not enough Stamina":receipt->failure==darkangel::AbilityFailure::Cooldown?"cooldown":receipt->failure==darkangel::AbilityFailure::TagRequirements?"blocked by a status":"not available");}
                    presented("Presentation",actor.id);
                    auto actor_effects=character->effects(actor.id);if(actor_effects&&actor_effects->current()){const auto& frame=*actor_effects->current();for(const auto& state:frame.effects){ImGui::Text("Effect %llu: %s",static_cast<unsigned long long>(state.handle),state.suppressed?"suppressed":"active");ImGui::TextWrapped("Definition %s | generation %.12s",state.definition.text().c_str(),state.generation.c_str());ImGui::Text("Source actor %llu / activation %llu",static_cast<unsigned long long>(state.credit.source_network),static_cast<unsigned long long>(state.credit.activation));if(state.end)ImGui::Text("Remaining %llu ticks | next period %llu",static_cast<unsigned long long>(state.end>frame.tick?state.end-frame.tick:0),static_cast<unsigned long long>(state.next_period));for(const auto& definition:content.effects)if(definition->id==state.definition&&definition->generation==state.generation){for(auto tag:definition->tags)for(const auto& entry:content.tags->definitions())if(entry.id==tag)ImGui::Text("  %s %s",state.suppressed?"Inactive configured tag:":"Grants",entry.name.c_str());for(const auto& modifier:definition->modifiers)ImGui::Text("  Configured modifier: attribute %u, magnitude %.2f",modifier.attribute,modifier.magnitude);}}}
                    ImGui::PopID();}
            ImGui::EndTabItem();}ImGui::EndTabBar();}
            ImGui::TextWrapped("Sources shown use available action and replicated effect state. Other contributor ownership is not exposed by aggregate tags.");
        };
        shell.gameplay_metrics=[&]{if(!character)return std::string("Character session stopped");std::ostringstream text;const auto chosen=choose_hud_target();const StableId shown=chosen?*chosen:StableId{};if(character_resources->combat){auto target=character->presentation().find(shown);if(character->presentation().valid(target)){auto health=character->presentation().read(target).health;text<<"Target Health "<<health.current<<" / "<<health.maximum<<"\n";}if(auto ability=character->predicted_ability()){for(const auto& attribute:ability->attributes)if(attribute.id==3)text<<"Stamina "<<attribute.value<<"\n";if(ability->reservation){double reserved{};for(const auto& cost:ability->reservation->costs)if(cost.attribute==3)reserved+=cost.amount;text<<"Reserved Stamina "<<reserved<<"\n";}text<<"Ability "<<(ability->active?"active":"idle")<<"\n";}if(auto status=character->effects(shown))for(const auto& cue:status->cues())text<<cue.key<<" "<<std::max(0.,(double(cue.end)-character->motor().tick)/60.)<<" s\n";if(auto reaction=character->reaction(shown))if(reaction->suppressed==ReactionSuppression::None&&!character->death(shown))text<<"Target reaction "<<reaction->tick<<" / "<<reaction->action->duration/action_tick_units<<" ticks\n";}if(character_resources->combat){if(auto death=character->death(shown))text<<"Target death "<<*death<<" / "<<character_resources->combat->death->duration/action_tick_units<<" ticks\n";}if(!darkangel::editor_app::RuntimeInventory::name(*character_resources,character->ability()).empty())text<<"Mask "<<darkangel::editor_app::RuntimeInventory::name(*character_resources,character->ability())<<"\n";if(!shell.camera_live.empty())text<<"Camera "<<shell.camera_live<<"\n";text<<"Simulation tick "<<character->motor().tick<<"\nPrediction pending "<<character->pending_prediction()<<"\nAbility pending "<<character->pending_abilities()<<"\nClock debt "<<character->debt()*1000<<" ms\nGraph phase "<<character->graph().phase;return text.str();};editor.stop_gameplay=[&]{
#ifdef DAE_HUD
            runtime_inventory.reset();runtime_altar.reset();shell.inventory_open=false;
#endif
            hud_target.reset();director.reset();shell.camera_live.clear();if(travel)travel->stop();travel.reset();
#ifdef DAE_EDITOR_GNS
            if(party_guest)party_guest->stop();party_guest.reset();if(party_host)party_host->stop();party_host.reset();
#endif
            character=nullptr;travel_preview.reset();travel_load=TravelLoad{};
            if(authoring_scene!=AssetId{}&&globals_scene!=authoring_scene)if(auto parked=parked_documents.find(authoring_scene);parked!=parked_documents.end()){editor.document=std::move(parked->second);editor.path=authoring_path;editor.selected={};}
            parked_documents.clear();parked_paths.clear();globals_scene=authoring_scene;cue_player.reset();
#ifdef DAE_AUDIO
            audio_director.reset();play_profiles.clear();encounter_profiles.clear();audio_in_play=false;
#endif
            cue_effects.bind(nullptr,nullptr,{});
#ifdef DAE_AUDIO
            cue_sounds.bind(nullptr,nullptr,{});
#endif
            shell.controls_focus=false;};editor.gameplay_view=[&]()->const World*{return character?&character->presentation():nullptr;};editor.update_gameplay=[&](double seconds){if(!character)return;
            // While the party travels its input is held.
            #ifdef DAE_EDITOR_GNS
            if(party_guest&&!party_guest->running()){character->pump();return;}if(party_host&&travel&&travel->busy()){character->pump();return;}
#endif
            // A long editor stall (cooking at Play start, a debugger) is not simulated time: the session catches up at most a quarter second.
            const double stalled=std::min(seconds,.25);if(travel&&travel->busy()){CharacterSceneInput held;held.yaw=character->predicted_motor().yaw;character->advance(stalled,held);}else character->advance(stalled,character_input);};editor.step_gameplay=[&]{if(character)character->step({0,0,character->motor().yaw});};
    }
#endif

#ifdef DAE_AGENT_ENDPOINTS
std::unique_ptr<AgentEndpoint> agent;EditorDocument* agent_document{};auto poll_agent=[&]{if(args.agent_descriptor.empty())return;if(agent_document!=editor.document.get()){agent.reset();agent=std::make_unique<AgentEndpoint>(*editor.document,args.agent_project,args.agent_authoring,args.agent_descriptor);agent_document=editor.document.get();editor.log("Issued scoped agent descriptor for this authoring document.");}agent->poll();};
#endif
    auto reload_model=[&]{auto candidate=store.prepare(args.registry,args.cas,id);auto resources=upload(device,*candidate.model,pso,pso_cull);std::map<AssetId,UploadedModel> other;for(const auto& [asset,unused]:scene_models)other.emplace(asset,upload(device,load_cooked_model(args.registry,args.cas,asset),pso,pso_cull));
#ifdef DAE_ANIMATION
        std::map<AssetId,UploadedSkin> skins;for(const auto& [asset,unused]:scene_skins)skins.emplace(asset,upload_skin(device,load_cooked_skinned_model(args.registry,args.cas,asset),args.skin_reference?pso.RawPtr():skin_pso.RawPtr(),args.skin_reference?pso_cull.RawPtr():skin_cull.RawPtr(),diagnostic_clip.get(),args.pose_tick,args.skin_reference));
#endif
#ifdef DAE_CHARACTER_SCENE
        std::unique_ptr<darkangel::editor_app::CharacterPreviewResources> preview_candidate;if(character_resources){require(skins.size()==1,"Character reload requires one skin");preview_candidate=std::make_unique<darkangel::editor_app::CharacterPreviewResources>(darkangel::editor_app::load_character_preview(args.registry,args.cas,args.character_clips,skins.begin()->second.model,scene_role(args.character_player),registry_spawns(args.registry),args.character_collision.empty()?AssetId{}:AssetId::parse(args.character_collision),registry_encounter_npcs(args.registry)));}
#endif
#ifdef DAE_CHARACTER_SCENE
        std::unique_ptr<InputManager> input_candidate;std::unique_ptr<darkangel::editor_app::NativeInput> native_candidate;
        if(preview_candidate&&preview_candidate->combat){const auto& profile=preview_candidate->combat->input;const char* semantics[]={"move.forward","move.lateral","look.turn","walk","jump"};for(unsigned i=1;i<=5;++i){auto action=std::find_if(profile.actions.begin(),profile.actions.end(),[&](const auto& a){return a.id==i;});require(action!=profile.actions.end()&&action->name==semantics[i-1]&&action->kind==(i<=3?InputActionKind::Axis:InputActionKind::Button),"Play kit input semantic contract mismatch");}input_candidate=std::make_unique<InputManager>(profile);auto controls=input_candidate->controls();native_candidate=std::make_unique<darkangel::editor_app::NativeInput>(controls);}
        if(preview_candidate){auto probe=darkangel::editor_app::prepare_character_preview(editor.document->plan(),editor.document->world(),*preview_candidate,[&](AssetId asset){return load_cooked_model(args.registry,args.cas,asset);});}
#endif
        context->Flush();context->WaitForIdle();scene_models=std::move(other);
#ifdef DAE_ANIMATION
        scene_skins=std::move(skins);
#ifdef DAE_CHARACTER_SCENE
        graph_pose.reset();shell.graph_preview_asset={};composer_pose.reset();composer_matrices.clear();shell.composer_clip={};
#endif
#endif

#ifdef DAE_CHARACTER_SCENE
        if(character&&!travel_preparing)editor.stop();if(preview_candidate)character_resources=std::move(preview_candidate);
        if(input_candidate){inputs=std::move(input_candidate);native_input=std::move(native_candidate);window_input=native_input.get();shell.input=inputs.get();shell.input_events.clear();combat_events.clear();shell.controls_focus=false;}

#endif
        store.publish(candidate);gpu=std::move(resources);lease=store.acquire();store.collect();editor.log("Published resource generation after GPU upload; retired unpinned data at GPU idle.");};
#ifdef DAE_CHARACTER_SCENE
    // Bakes (when bake) and returns the open scene's own collision, reading placed models from a registry that holds them.
    std::function<std::optional<AssetId>(const std::filesystem::path&,bool)> scene_collision_for=[&](const std::filesystem::path& registry,bool bake)->std::optional<AssetId>{
        if(!editor.assets||!character_resources||editor.path.empty())return std::nullopt;const auto root=std::filesystem::absolute(editor.assets->source_root());const auto scene_path=std::filesystem::absolute(editor.path);
        const auto& plan=editor.document->plan();if(!plan.collision_profiles.empty())return plan.collision_profiles.begin()->second;
        const auto file=darkangel::editor_app::scene_collision_file(editor.document->root(),scene_path,root);
        if(bake){const auto source=darkangel::editor_app::bake_scene_collision(file.asset,plan,editor.document->world(),character_resources->skin,[&](AssetId model){return load_cooked_model(registry,editor.assets->cas_path(),model);});
            if(!source)return std::nullopt;if(darkangel::editor_app::publish_scene_collision(file,*source))editor.log("Baked the scene's collision from its placed models: "+file.relative);editor.assets->cook(file.relative);}
        return std::filesystem::exists(file.source)?std::optional<AssetId>(file.asset):std::nullopt;};
    // Saving a scene rebakes its collision: package what it places, bake, publish.
    editor.scene_saved=[&]{if(!editor.assets||!character_resources||!editor.authoring_roots||!args.join.empty())return;auto roots=editor.authoring_roots();auto registry=editor.assets->cas_path().parent_path()/("bake-"+AssetId::random().text()+".json");
        darkangel::editor_app::package_authoring(*editor.assets,roots,registry);scene_collision_for(registry,true);std::error_code ignored;std::filesystem::remove(registry,ignored);};
    if(character_resources&&editor.assets&&editor.authoring_kit!=AssetId{}){editor.authoring_roots=[&]{std::set<AssetId> unique{id,editor.authoring_player==AssetId{}?editor.authoring_kit:editor.authoring_player};for(auto npc:encounter_npcs())unique.insert(npc);for(const auto& [point,npc]:editor.document->plan().spawns){unique.insert(npc);for(auto visual:editor.authoring.worn_visuals(*editor.assets,npc))unique.insert(visual);}if(editor.authoring_player!=AssetId{})for(auto visual:editor.authoring.worn_visuals(*editor.assets,editor.authoring_player))unique.insert(visual);for(const auto& [object,asset]:editor.document->plan().models)unique.insert(asset);for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daitem"){unique.insert(asset.id);try{const auto& item=editor.authoring.open(*editor.assets,asset.id).value;if(item.contains("properties")&&item.at("properties").contains("mask")&&item.at("properties").at("mask").is_string()){const auto& mask=editor.authoring.open(*editor.assets,AssetId::parse(item.at("properties").at("mask").get<std::string>())).value;if(mask.contains("visual")&&mask.at("visual").is_string())unique.insert(AssetId::parse(mask.at("visual").get<std::string>()));}}catch(const std::exception&){}}if(travel_preparing)for(const auto& [asset,unused]:scene_models)unique.insert(asset);if(!args.character_collision.empty())unique.insert(AssetId::parse(args.character_collision));return std::vector<AssetId>(unique.begin(),unique.end());};editor.prepare_play_resources=[&]{
        require(travel_preparing||!editor.preview.active(),"Stop Play before preparing authored resources");if(!travel_preparing)play_cues.reset();auto roots=editor.authoring_roots();
        auto candidate=editor.assets->cas_path().parent_path()/("play-"+AssetId::random().text()+".json");
#ifdef DAE_EDITOR_GNS
        if(!args.join.empty()){require(!roots.empty(),"Guest scene has no package roots");editor.assets->package(roots.front(),candidate,std::span<const AssetId>(roots).subspan(1));}
        else
#endif
        darkangel::editor_app::package_authoring(*editor.assets,roots,candidate);
        // The scene plays with its own collision, baked from its placements: a placed or moved model never leaves Play
        // with stale collision. Only the host bakes; a guest uses the file its host wrote.
        if(const auto own=scene_collision_for(candidate,args.join.empty());own&&own->text()!=args.character_collision){args.character_collision=own->text();roots.push_back(*own);
#ifdef DAE_EDITOR_GNS
            if(!args.join.empty())editor.assets->package(roots.front(),candidate,std::span<const AssetId>(roots).subspan(1));else
#endif
            darkangel::editor_app::package_authoring(*editor.assets,roots,candidate);}
        auto old_registry=args.registry,old_cas=args.cas;auto old_clips=args.character_clips;
        try{args.registry=candidate;args.cas=editor.assets->cas_path();args.character_clips=editor.authoring_player==AssetId{}?"kit:"+editor.authoring_kit.text():"player:"+editor.authoring_player.text();reload_model();if(editor.authoring_player!=AssetId{})editor.authoring_kit=character_resources->combat->kit->id;}
        catch(...){args.registry=old_registry;args.cas=old_cas;args.character_clips=old_clips;throw;}
        editor.refresh_assets();editor.log("Prepared current scene and combat kit for a fresh Play session.");
    };}
    // Scene transitions in the editor's Play. The destination becomes the scene the editor's document, Play resources and
    // uploaded models are for, prepared exactly as a fresh Play of that scene would be; the session that is still running
    // keeps what it was built from. Every scene uses the collision asset this editor was started with: a destination whose
    // placements that asset does not match fails its preparation and the party stays where it is.
    auto find_scene=[&](AssetId scene)->std::filesystem::path{if(!editor.assets)return {};std::vector<std::filesystem::path> files;std::error_code failed;for(std::filesystem::recursive_directory_iterator file(editor.assets->source_root(),failed),end;!failed&&file!=end;file.increment(failed))if(file->is_regular_file()&&file->path().extension()==".dascene")files.push_back(file->path());
        for(const auto& file:files){try{std::ifstream input(file);const auto value=nlohmann::json::parse(input);if(value.contains("root")&&value.at("root").is_string()&&value.at("root").get<std::string>()==scene.text())return file;}catch(const std::exception&){}}
        return {};};
    enter_scene=[&](AssetId scene,std::unique_ptr<darkangel::EditorDocument> document,const std::filesystem::path& path){
        require(bool(editor.prepare_play_resources),"This editor session cannot prepare another scene for Play: it was started without project sources");
        parked_documents[globals_scene]=std::move(editor.document);parked_paths[globals_scene]=editor.path;editor.document=std::move(document);editor.path=path;globals_scene=scene;editor.selected={};
        travel_preparing=true;try{editor.prepare_play_resources();}catch(...){travel_preparing=false;throw;}travel_preparing=false;
        auto scripts=editor.document->scripts();travel_preview=editor.preview.prepare(editor.document->plan(),scripts,[&](AssetId asset){if(editor.prepare_model)editor.prepare_model(asset);});};
    auto drop_load=[&]{if(travel_load.document&&travel_load.parked){parked_paths[travel_load.scene]=travel_load.path;parked_documents[travel_load.scene]=std::move(travel_load.document);}travel_load=TravelLoad{};};
    scene_provider.on_begin=[&,drop_load](AssetId scene){drop_load();travel_load.scene=scene;};
    scene_provider.on_poll=[&,find_scene]()->darkangel::SceneLoadStatus{
        auto& load=travel_load;
        try{
            if(load.step==0){
                if(auto parked=parked_documents.find(load.scene);parked!=parked_documents.end()){load.document=std::move(parked->second);load.path=parked_paths[load.scene];load.parked=true;parked_documents.erase(parked);}
                else{load.path=find_scene(load.scene);if(load.path.empty())return {darkangel::SceneLoad::Failed,0,"No scene with the identity "+load.scene.text()+" is in the project"};load.document=darkangel::EditorDocument::open(load.path);require(load.document->root()==load.scene,"The scene file names another scene");}
                load.step=1;return {darkangel::SceneLoad::Working,.25,"Opening "+load.path.filename().string()};}
            if(load.step==1){load.parked=false;enter_scene(load.scene,std::move(load.document),load.path);load.step=2;return {darkangel::SceneLoad::Working,.7,"Preparing "+load.path.filename().string()};}
            if(load.step==2){require(bool(travel_preview),"The destination has no Preview world");load.prepared=std::make_unique<darkangel::PreparedScene>(darkangel::editor_app::prepare_character_scene(travel_preview->plan,*travel_preview->world,*character_resources,[&](AssetId asset){return load_cooked_model(args.registry,args.cas,asset);},make_controller()));load.prepared->scene=load.scene;load.step=3;}
            return {darkangel::SceneLoad::Ready,1,"Entering "+load.path.filename().string()};
        }catch(const std::exception& error){return {darkangel::SceneLoad::Failed,0,error.what()};}};
    scene_provider.on_take=[&]{return std::move(travel_load.prepared);};
    scene_provider.on_cancel=[drop_load]{drop_load();};
#endif
#ifdef DAE_CHARACTER_SCENE
    if(character_resources&&editor.assets){
        shell.prepare_composer=[&](AssetId clip,unsigned duration){
            require(!editor.preview.active(),"Stop Play before preparing Composer preview");
            auto registry=editor.assets->cas_path().parent_path()/("composer-"+AssetId::random().text()+".json");
            ComposerPackageFile cleanup{registry};const std::array roots{clip};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);
            auto candidate=std::make_unique<darkangel::editor_app::ComposerPosePreview>(*character_resources,load_cooked_clip(registry,editor.assets->cas_path(),clip),duration);
            auto matrices=candidate->sample(0);composer_pose=std::move(candidate);graph_pose.reset();shell.graph_preview_asset={};shell.character_preview_asset={};composer_matrices=std::move(matrices);
            editor.log("Prepared isolated frozen clip/character pose preview; gameplay remains stopped.");
        };
        shell.prepare_action_pose=[&](AssetId action){
            require(!editor.preview.active(),"Stop Play before preparing Composer preview");auto& kit=editor.authoring.open(*editor.assets,editor.authoring_kit).value;auto graph=AssetId::parse(kit.at("locomotion_stance").get<std::string>());
            auto candidate=darkangel::editor_app::prepare_authored_action_pose(*editor.assets,editor.authoring,*character_resources,action,graph);auto matrices=candidate->sample(0);composer_pose=std::move(candidate);graph_pose.reset();shell.graph_preview_asset={};shell.character_preview_asset={};composer_matrices=std::move(matrices);editor.log("Prepared frozen action/locomotion pose preview from private drafts; gameplay stays stopped.");
        };
        ActorTagSnapshot preview_graph_tags;
        auto graph_parameters=[&](const darkangel::editor_app::GraphPosePreview& pose){GraphParameters parameters{shell.graph_speed,shell.graph_forward,shell.graph_lateral,1};if(auto dictionary=pose.tag_dictionary()){preview_graph_tags={dictionary->registry(),dictionary->generation(),{}};for(const auto& tag:shell.graph_preview_tags)preview_graph_tags.values.push_back(tag.get<TagId>());parameters.tags=&preview_graph_tags;}return parameters;};
        shell.scrub_composer=[&](double tick){require((composer_pose||graph_pose)&&!editor.preview.active(),"Pose preview unavailable during Play");if(graph_pose){require(std::isfinite(tick)&&tick>=0&&tick<=600,"Graph preview tick bounds");composer_matrices=graph_pose->sample(static_cast<unsigned>(tick),graph_parameters(*graph_pose));}else composer_matrices=composer_pose->sample(tick);};
        shell.prepare_character_pose=[&](AssetId asset,AssetId clip){
            require(!editor.preview.active(),"Stop Play before preparing Character preview");auto candidate=darkangel::editor_app::prepare_authored_character_pose(*editor.assets,editor.authoring,asset,clip);auto matrices=candidate.pose->sample(0);auto uploaded=upload_skin(device,std::move(candidate.character.skin),skin_pso.RawPtr(),skin_cull.RawPtr(),nullptr,0,false);context->Flush();context->WaitForIdle();authoring_skin=std::move(uploaded);authoring_character_definition=std::move(candidate.character.definition);composer_pose=std::move(candidate.pose);graph_pose.reset();shell.graph_preview_asset={};composer_matrices=std::move(matrices);shell.composer_clip=clip;shell.composer_duration=candidate.clip.definition.ticks*action_tick_units;shell.composer_tick=0;shell.composer_playing=false;shell.character_preview_asset=asset;shell.composer_asset={};
        };
        shell.draw_graph_info=[&]{if(!graph_pose||shell.graph_preview_asset!=shell.authored_selection)return;if(ImGui::CollapsingHeader("Native graph blend result",ImGuiTreeNodeFlags_DefaultOpen)){const auto& state=graph_pose->state();auto parameters=graph_pose->parameters();ImGui::Text("Frozen graph %.12s | tick %llu | phase %.5f",state.generation.c_str(),static_cast<unsigned long long>(state.tick),state.phase);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",state.generation.c_str());ImGui::Text("Sampled speed %.3f | forward %.3f | lateral %.3f",parameters.speed,parameters.forward,parameters.lateral);const auto& inputs=graph_pose->inputs();ImGui::Text("Root output: %u native pose layers",inputs.count);for(unsigned i=0;i<inputs.count;++i){const auto& layer=inputs.layers[i];auto id=layer.clip->definition().id;std::string name=id.text();for(const auto& asset:editor.asset_inventory)if(asset.id==id){name=std::filesystem::path(asset.path).filename().string();break;}ImGui::TextWrapped("%s | weight %.4f | clip tick %.3f",name.c_str(),layer.weight,layer.tick);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",id.text().c_str());}ImGui::TextWrapped("These are frozen native sampling results, not authored weights or authoritative gameplay decisions.");}};
        shell.prepare_graph_pose=[&](AssetId character_asset,AssetId graph_asset){require(!editor.preview.active(),"Stop Play before graph preview");auto candidate=darkangel::editor_app::prepare_authored_graph_pose(*editor.assets,editor.authoring,character_asset,graph_asset);auto matrices=candidate.pose->sample(0,graph_parameters(*candidate.pose));auto uploaded=upload_skin(device,std::move(candidate.character.skin),skin_pso.RawPtr(),skin_cull.RawPtr(),nullptr,0,false);context->Flush();context->WaitForIdle();authoring_skin=std::move(uploaded);graph_pose=std::move(candidate.pose);composer_pose.reset();composer_matrices=std::move(matrices);shell.character_preview_asset={};shell.graph_preview_asset=graph_asset;shell.composer_asset={};shell.composer_clip={};shell.composer_duration=600*action_tick_units;shell.composer_tick=0;shell.composer_playing=false;};
        shell.draw_character_info=[&]{if(!authoring_skin)return;const auto& model=authoring_skin->model;const auto& rig=model.rig.definition;ImGui::Text("%zu joints | %zu meshes",rig.joints.size(),model.meshes.size());if(ImGui::TreeNode("Skeleton")){for(unsigned i=0;i<rig.joints.size();++i)ImGui::Text("%u: %s | parent %d",i,rig.joints[i].key.c_str(),rig.joints[i].parent);ImGui::TreePop();}if(ImGui::TreeNode("Sockets")){for(const auto& [slot,joint]:rig.sockets)ImGui::Text("%s > %s",slot.c_str(),joint.c_str());ImGui::TreePop();}};
        shell.composer_info=[&]{if(graph_pose)return std::string("Graph | frozen generation ")+graph_pose->state().generation.substr(0,12)+" | fixed tick "+std::to_string(graph_pose->state().tick);std::string name="Clip";for(const auto& asset:editor.asset_inventory)if(asset.id==composer_pose->clip().id)name=std::filesystem::path(asset.path).stem().string();auto label=name+" | frozen clip "+std::string(composer_pose->generation().substr(0,12));if(auto action=composer_pose->action()){label+=" | action "+action->generation.substr(0,12);if(action->mask)label+=" | upper-body mask "+action->mask->generation.substr(0,12);}return label;};
    }
#endif
    if(args.ability_workspace&&editor.assets&&editor.authoring_kit!=AssetId{}){auto& kit=editor.authoring.open(*editor.assets,editor.authoring_kit).value;shell.authored_selection=AssetId::parse(kit["slots"][1]["ability"].get<std::string>());}
    if(!args.authoring_asset.empty())shell.authored_selection=AssetId::parse(args.authoring_asset);if(args.items_workspace){shell.select_workspace(darkangel::editor_app::Workspace::Items);if(!args.authoring_asset.empty())shell.item_selection=AssetId::parse(args.authoring_asset);}
    if(args.input_workspace){strcpy_s(shell.input_binding_filter,"attack.light");shell.input_authoring_asset=args.authoring_asset.empty()?AssetId{}:AssetId::parse(args.authoring_asset);shell.open_tool(darkangel::editor_app::ToolView::InputBindings);}
    if(!args.authoring_asset.empty()){if(args.character_workspace)shell.character_selection=AssetId::parse(args.authoring_asset);if(args.player_workspace)shell.player_selection=AssetId::parse(args.authoring_asset);if(args.npc_workspace)shell.npc_selection=AssetId::parse(args.authoring_asset);}
    if(!args.mask_editor.empty())shell.mask_editor=AssetId::parse(args.mask_editor);
    if(!args.player_asset.empty()){require(editor.assets!=nullptr,"Player selection needs a project mount");editor.authoring_player=AssetId::parse(args.player_asset);auto& player=editor.authoring.open(*editor.assets,editor.authoring_player).value;editor.authoring_kit=AssetId::parse(player.at("loadout").at("kit").get<std::string>());}

    auto reload_shader=[&](const std::string& text){auto next=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,false,text.c_str());auto next_cull=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,true,text.c_str());auto resources=upload(device,*lease.model,next,next_cull);std::map<AssetId,UploadedModel> other;for(const auto& [asset,unused]:scene_models)other.emplace(asset,upload(device,load_cooked_model(args.registry,args.cas,asset),next,next_cull));context->Flush();context->WaitForIdle();scene_models=std::move(other);gpu=std::move(resources);pso=std::move(next);pso_cull=std::move(next_cull);editor.log("Static shader candidate and material bindings committed at GPU idle.");};
    RefCntAutoPtr<ITexture> scene_color,scene_depth,scene_color_second,scene_depth_second;unsigned scene_width=700,scene_height=500;auto last=std::chrono::steady_clock::now();
#ifdef DAE_EDITOR_GNS
    nlohmann::json party_last_running,party_fullest;
    auto party_receipt=[&](unsigned frame){if(args.party_report.empty())return;
        nlohmann::json value{{"schema",1},{"process",GetCurrentProcessId()},{"role",args.join.empty()?"host":"guest"},{"frame",frame},{"scene",globals_scene.text()},{"epoch",std::uint64_t(party_guest?party_guest->epoch():party_host?party_host->epoch():0)},{"playing",bool(editor.preview.active())},{"running",bool(party_guest?party_guest->running():party_host&&travel&&!travel->busy())},{"ready",character&&character->bootstrapped()},{"players",nlohmann::json::array()},{"actors",nlohmann::json::array()},{"arrivals",travel_arrivals},{"failures",travel_failures},{"script_steps",player_script_at},{"script_count",player_script.size()},{"error",party_guest?party_guest->error():travel_failure}};
#ifdef DAE_HUD
        value["world_ui"]=nlohmann::json::array();if(hud&&!hud_failed)for(const auto& marker:hud->model().world_markers)value["world_ui"].push_back({{"label",marker.label},{"x",marker.x},{"y",marker.y},{"width",marker.width},{"health_bar",marker.health_bar},{"percent",marker.percent}});
#endif
        for(std::size_t line=editor.console.size()>12?editor.console.size()-12:0;line<editor.console.size();++line)value["console"].push_back(editor.console[line].message.substr(0,300));
        if(character&&character->bootstrapped()){value["owner"]=character_player.text();value["authority"]=character->hosting();value["tick"]=character->motor().tick;value["predicted_tick"]=character->predicted_motor().tick;const auto at=character->predicted_motor().position;value["predicted_position"]={at.x,at.y,at.z};value["pending_motor"]=character->pending_prediction();value["rebases"]=character->resynchronizations();value["client"]=character->client(character_player)->bootstrap_status();value["script_steps"]=player_script_at;
            for(const auto id:character->players()){auto handle=character->presentation().find(id);if(!character->presentation().valid(handle))continue;const auto& object=character->presentation().read(handle);value["players"].push_back({{"id",id.text()},{"position",{object.transform.x,object.transform.y,object.transform.z}},{"health",object.health.current},{"pose",character->pose(id)!=nullptr}});}
            for(const auto id:character->actors()){auto handle=character->presentation().find(id);if(!character->presentation().valid(handle))continue;const auto& object=character->presentation().read(handle);value["actors"].push_back({{"id",id.text()},{"health",object.health.current},{"pose",character->pose(id)!=nullptr}});}
            if(const auto* ability=character->ability()){value["equipped_masks"]=nlohmann::json::array();for(auto mask:ability->equipped_masks)value["equipped_masks"].push_back(mask==AssetId{}?nlohmann::json(nullptr):nlohmann::json(mask.text()));value["active_mask_slot"]=ability->loadout;value["highest_operation"]=ability->highest_operation;unsigned accepted{},refused{};for(const auto& operation:ability->operations){accepted+=operation.committed;refused+=operation.failure!=AbilityFailure::None;}value["accepted"]=accepted;value["refused"]=refused;}
            if(const auto* owner=character->client(character_player);owner&&owner->inventory()){const auto& inventory=*owner->inventory();value["inventory"]=nlohmann::json::parse(darkangel::encode_inventory(inventory.inventory));value["inventory_operation"]=inventory.highest_operation;value["inventory_pending"]=owner->inventory_pending();value["inventory_failure"]=int(inventory.receipt.failure);}
            if(const auto* outcome=character->last_interaction()){value["interaction"]=darkangel::interaction_result_text(outcome->result);value["interaction_object"]=outcome->object.text();}
            if(const auto* owner=character->client(character_player)){if(const auto* world=owner->world_state())value["world"]=nlohmann::json::parse(darkangel::encode_world_state(*world));value["downed"]=owner->downed();}
            value["scene_now"]=travel&&travel->scene()?travel->scene()->scene.text():std::string();value["arrivals"]=travel_arrivals;value["travel_failures"]=travel_failures;if(play_saves)value["save_status"]=play_saves->status();
            if(character->hosting()){value["movers"]=nlohmann::json::array();for(const auto& [id,at]:character->authority().movers())value["movers"].push_back({{"id",id.text()},{"at",{at.x,at.y,at.z}}});}
        }
        // What a stopped process last saw while it ran stays in its final receipt.
        if(value.at("running").get<bool>()&&value.contains("owner")){party_last_running=value;if(party_fullest.is_null()||value.at("players").size()>=party_fullest.at("players").size())party_fullest=value;else value["fullest"]=party_fullest;}else if(!party_last_running.is_null()){value["last_running"]=party_last_running;value["fullest"]=party_fullest;}
        if(!args.party_report.parent_path().empty())std::filesystem::create_directories(args.party_report.parent_path());const auto temporary=args.party_report.string()+".tmp";{std::ofstream file(temporary);file<<value.dump(2)<<'\n';require(bool(file),"Cannot write party diagnostic receipt");}// Something on the machine briefly holds a just-replaced evidence file; a diagnostic receipt waits for it, then skips this frame.
        bool published=false;for(unsigned attempt=0;attempt<100&&!published;++attempt){published=MoveFileExW(std::filesystem::path(temporary).c_str(),args.party_report.c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE;if(!published)std::this_thread::sleep_for(std::chrono::milliseconds(20));}if(!published)std::cerr<<"Party diagnostic receipt was busy; skipped one update"<<std::endl;
    };
#endif
    #ifdef DAE_CHARACTER_SCENE
    auto authoring_source_exercise=darkangel::editor_app::exercises::make_authoring_sources({args,editor,shell,character_resources,character,import_baseline});
#endif
    bool running=true;unsigned frame{};while(running && (!args.frames || frame<args.frames)){
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)running=false;TranslateMessage(&message);DispatchMessageW(&message);}if(!running)break;
        RECT current_client{};if(GetClientRect(window,&current_client)&&current_client.right>0&&current_client.bottom>0){resize_width=static_cast<unsigned>(current_client.right);resize_height=static_cast<unsigned>(current_client.bottom);}
        if(resize_width && resize_height&&(resize_width!=swap->GetDesc().Width||resize_height!=swap->GetDesc().Height)){context->WaitForIdle();swap->Resize(resize_width,resize_height);resize_width=resize_height=0;}
        auto now=std::chrono::steady_clock::now();auto elapsed_seconds=std::chrono::duration<double>(now-last).count();auto seconds=elapsed_seconds;last=now;seconds=std::clamp(seconds,0.,.1);
#ifdef DAE_CHARACTER_SCENE
        if(args.exercise_composer&&frame==0){
            auto& value=editor.authoring.open(*editor.assets,shell.authored_selection).value;
            auto clip=AssetId::parse(value.at("motion").at("clip").get<std::string>());
            auto duration=value.at("duration").get<unsigned>();
            shell.prepare_action_pose(shell.authored_selection);shell.composer_clip=clip;shell.composer_duration=duration;
            shell.composer_asset=shell.authored_selection;shell.composer_tick=args.composer_tick;shell.composer_playing=args.composer_playback;shell.composer_block=2;
        }
#endif
#ifdef DAE_CHARACTER_SCENE
        if(args.exercise_cues){shell.focus_gameplay_inspector=true;cue_debug_selected=true;}
        // Stands in for saving the effect in Effekseer: the source file changes on disk while the VFX tab shows it.
#ifdef DAE_AUDIO
        // The Audio tab without Play: a sound definition auditioned, a mixer volume moved in its draft, a whole profile auditioned.
        if(args.exercise_audio&&args.audio_workspace){const auto named=[&](const char* path){for(const auto& asset:editor.asset_inventory)if(asset.path==path)return asset.id;throw std::runtime_error(std::string("The audio exercise needs ")+path);};
            if(frame==3){require(ensure_audio(),"The audio exercise needs the audio service");const auto wind=named("audio/wind.dasound");load_audio({wind});audio_tab.sound=wind;audio_tab.audition=audio->play(wind,{0,0,1});require(audio_tab.audition!=0,"The auditioned sound did not start");}
            if(frame==6){auto& value=editor.authoring.open(*editor.assets,named("audio/project.damix")).value;value["buses"]["music"]["volume"]=0.3;editor.authoring.record_changes(*editor.assets,"Edit audio");}
            if(frame==9){const auto calm=named("audio/profiles/royal_calm.daaudioprofile");auto loaded=load_audio({calm});require(loaded.contains(calm),"The audio profile did not cook");audio_director=std::make_unique<darkangel::AudioDirector>(*audio,project_mix());audio_applied=project_mix();audio_director->scene(loaded.at(calm));audio_tab.profile=calm;audio_tab.profile_audition=true;}}
        // During Play: the NPC bus turned down in the mixer's draft.
        if(args.exercise_audio&&!args.audio_workspace&&frame==20&&editor.assets){for(const auto& asset:editor.asset_inventory)if(asset.path==audio_mix_path){auto& value=editor.authoring.open(*editor.assets,asset.id).value;value["buses"]["npc"]["volume"]=0.25;editor.authoring.record_changes(*editor.assets,"Edit audio");}}
#endif
        // The cue form's own path: a new cue staged as a draft, its fields set the way the form sets them, saved, and read back cooked.
        if(args.exercise_cue_form&&frame==3){require(bool(editor.assets),"The cue form exercise needs the project's sources");vfx_preview.cue=editor.authoring.create_cue(*editor.assets,"gate_"+AssetId::random().text().substr(0,8));editor.refresh_assets();}
        if(args.exercise_cue_form&&frame==6){auto& value=editor.authoring.open(*editor.assets,vfx_preview.cue).value;value["name"]="Gate cue";value["visual"]="vfx/placeholder/hit_spark.efkproj";value["audio"]="audio/placeholder/hit.wav";value["tail_ticks"]=20;editor.authoring.record_changes(*editor.assets,"Edit cue");require(editor.authoring.can_undo()&&editor.authoring.drafts.at(vfx_preview.cue).dirty(),"The cue edit is not an undoable draft");
            editor.authoring.save(*editor.assets,vfx_preview.cue);editor.refresh_assets();}
        if(args.exercise_vfx_edit&&frame==5){require(editor.assets&&!vfx_preview.path.empty(),"The VFX edit exercise needs a selected effect");std::ofstream changed(editor.assets->source_root()/vfx_preview.path,std::ios::binary|std::ios::app);changed<<"\n";require(bool(changed),"The effect source could not be changed");}
        if(args.exercise_game_debug){shell.focus_gameplay_inspector=true;shell.select_effect_debug=true;if(frame==29){auto status=character?character->effects(first_actor()):nullptr;require(status&&status->current()&&!status->current()->effects.empty(),"Game debug needs active target effect");auto& effect=status->current()->effects.front();require(effect.credit.source_network&&effect.credit.activation,"Game debug missing captured source attribution");std::cout<<"Game inspector effect="<<effect.handle<<" source="<<effect.credit.source_network<<" activation="<<effect.credit.activation<<"\n";}}
        authoring_source_exercise->frame(frame);
#endif
        if(args.select_spawn&&frame==1&&!editor.document->plan().spawns.empty())editor.selected=editor.document->plan().spawns.begin()->first;
        if(args.exercise_encounter)world_debug=true;
        // Background cooking: one uncooked source every half second while authoring in a visible editor. Gates run hidden and
        // leave their fixtures as they are, except the exercise that drops a new source into the project and waits for it.
        if(args.join.empty()&&!args.hidden&&!editor.preview.active()&&frame%30==29)editor.cook_pending(1);
        if(args.exercise_cook_sweep){static std::string dropped="characters/cook_sweep_probe.dacharacter";static AssetId probe;
            if(frame==1){require(bool(editor.assets),"Cook sweep exercise needs a project");std::string source;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".dacharacter"&&asset.generation){std::ifstream input(editor.assets->source_root()/asset.path);auto value=nlohmann::json::parse(input);probe=AssetId::random();value["asset"]=probe.text();source=value.dump(2);break;}
                require(!source.empty(),"Cook sweep exercise needs a cooked Character to copy");{std::ofstream output(editor.assets->source_root()/dropped);output<<source;}editor.refresh_assets();
                auto found=std::find_if(editor.asset_inventory.begin(),editor.asset_inventory.end(),[&](const auto& asset){return asset.id==probe;});require(found!=editor.asset_inventory.end()&&found->generation==0,"A source copied into the project was not listed as uncooked");}
            if(frame==2){const auto before=editor.cook_pending(0);require(before.waiting>=1,"The sweep did not see the uncooked source");unsigned passes=0;darkangel::editor_app::Controller::CookSweep sweep;do{sweep=editor.cook_pending(1);}while(sweep.waiting&&++passes<4096);
                auto found=std::find_if(editor.asset_inventory.begin(),editor.asset_inventory.end(),[&](const auto& asset){return asset.id==probe;});require(found!=editor.asset_inventory.end()&&found->generation!=0,"The background sweep did not cook the copied source");
                const auto again=editor.cook_pending(100);require(again.cooked==0&&again.waiting==0,"The sweep cooked something twice or retried a failure whose file had not changed");
                std::cout<<"Editor cook_sweep waiting_before="<<before.waiting<<" passes="<<passes+1<<" failed="<<again.failed<<" probe_generation="<<found->generation<<"\n";}
        }
        // Encounter authoring exercise: the scene's encounter is selected so its volumes are drawn lit, then the three gameplay items
        // are placed through the Place panel's own command: an encounter (with its first volume), a second volume for it, a trigger volume.
        if(args.select_encounter){auto& document=*editor.document;
            if(frame==1){require(!document.plan().encounters.empty(),"Encounter exercise needs a scene with an encounter");editor.selected=document.plan().encounters.begin()->first;}
            if(frame==3){shell.refresh_place_items(editor);auto item=[&](int kind){auto found=std::find_if(shell.place_items.begin(),shell.place_items.end(),[&](const auto& entry){return entry.gameplay==kind;});require(found!=shell.place_items.end(),"The Place panel has no gameplay items");return *found;};
                const auto encounters=document.plan().encounters.size(),volumes=document.plan().volumes.size(),triggers=document.plan().triggers.size();const auto kept=editor.selected;
                const auto made=shell.place(editor,item(1),-8,10);require(document.plan().encounters.size()==encounters+1&&document.plan().volumes.size()==volumes+1&&editor.selected==made,"Placing an encounter did not add it with its first volume");
                const auto extra=shell.place(editor,item(2),-8,14);require(document.plan().volumes.size()==volumes+2&&document.plan().volumes.at(extra).encounter==made,"Placing an encounter volume did not add it to the selected encounter");
                const auto gate=shell.place(editor,item(3),-8,4);require(document.plan().triggers.size()==triggers+1&&document.plan().triggers.contains(gate),"Placing a trigger volume did not add it");
                document.undo(document.revision());require(document.plan().triggers.size()==triggers,"Undo did not remove the placed trigger volume");document.redo(document.revision());require(document.plan().triggers.size()==triggers+1,"Redo did not restore the trigger volume");
                editor.selected=kept;std::cout<<"Editor encounter_place encounters="<<document.plan().encounters.size()<<" volumes="<<document.plan().volumes.size()<<" triggers="<<document.plan().triggers.size()<<" undo_redo=1\n";}
        }
        // Mouse path exercise: the Place panel and the outliner driven with real mouse and key events, one step a frame. An event
        // queued on one frame is seen by the widgets on the next, so every check waits two frames.
        if(args.exercise_place_mouse){auto& io=ImGui::GetIO();auto& document=*editor.document;static std::size_t before=0;static StableId dragged,clicked;static std::string item_key;
            auto at=[&](const std::string& key){auto found=shell.probes.find(key);require(found!=shell.probes.end(),"Mouse exercise: a control it needs was not drawn");return found->second;};
            auto move=[&](ImVec2 to){io.AddMousePosEvent(to.x,to.y);};auto button=[&](int which,bool down){io.AddMouseButtonEvent(which,down);};
            auto newest=[&](std::size_t expected,const char* error){require(document.plan().origins.size()==expected&&document.plan().spawns.contains(editor.selected),error);return editor.selected;};
            if(frame==2){shell.refresh_place_items(editor);auto npc=std::find_if(shell.place_items.begin(),shell.place_items.end(),[](const auto& item){return item.npc;});require(npc!=shell.place_items.end(),"Mouse exercise needs an NPC definition in the catalogue");item_key="place:"+npc->name;before=document.plan().origins.size();move(at(item_key));}
            // Drag from the Place panel into the viewport.
            if(frame==3)button(0,true);if(frame==4){auto from=at(item_key);move({from.x+12,from.y+4});}
            if(frame==5)move(at("viewport-drop"));if(frame==6){auto drop=at("viewport-drop");move({drop.x+3,drop.y});}if(frame==7)button(0,false);
            if(frame==10){dragged=newest(before+1,"A drag from the Place panel into the viewport did not place a spawn point");const auto placed=document.world().read(document.world().find(dragged)).transform;require(std::abs(placed.y-shell.surface_height(placed.x,placed.z))<1e-3&&(std::abs(placed.x)>1e-6||std::abs(placed.z)>1e-6),"The dropped object is not on the collision under the cursor");std::cout<<"Editor mouse drag=1 drop_x="<<placed.x<<" drop_y="<<placed.y<<" drop_z="<<placed.z<<"\n";move(at(item_key));}
            // Double-click a Place item.
            if(frame==11||frame==13)button(0,true);if(frame==12||frame==14)button(0,false);
            if(frame==17){clicked=newest(before+2,"A double-click on a Place item did not place a spawn point");require(clicked!=dragged,"The double-click did not select its new object");move(at("outliner:"+clicked.text()));}
            // Click the new object's outliner row, then press Delete.
            if(frame==18)button(0,true);if(frame==19)button(0,false);if(frame==21)io.AddKeyEvent(ImGuiKey_Delete,true);if(frame==22)io.AddKeyEvent(ImGuiKey_Delete,false);
            if(frame==25){require(document.plan().origins.size()==before+1&&!document.plan().spawns.contains(clicked)&&document.plan().spawns.contains(dragged),"The Delete key did not remove the selected object");move(at("outliner:"+dragged.text()));}
            // Right-click the other object's row and choose Delete.
            if(frame==26)button(1,true);if(frame==27)button(1,false);if(frame==30)move(at("menu-delete"));if(frame==31)button(0,true);if(frame==32)button(0,false);
            if(frame==35){require(document.plan().origins.size()==before&&!document.plan().spawns.contains(dragged),"The outliner's right-click Delete did not remove the object");std::cout<<"Editor mouse double_click=1 delete_key=1 context_delete=1 objects="<<before<<"\n";}
            // Click an object in the viewport: the remaining spawn point's body, found by projecting it to the screen.
            static StableId wanted;auto project=[&](const darkangel::Transform& pose,ImVec2& out){using namespace DirectX;XMFLOAT4 clip;XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(float(pose.x),float(pose.y)+.9f,float(pose.z),1),XMLoadFloat4x4(&shell.scene_view)*XMLoadFloat4x4(&shell.scene_projection)));require(clip.w>.05f,"Mouse exercise: the object to click is behind the camera");const auto drop=at("viewport-drop");const float width=(drop.x-shell.probes.at("viewport-origin").x)*2,height=(drop.y-shell.probes.at("viewport-origin").y)/.62f;out={shell.probes.at("viewport-origin").x+(clip.x/clip.w*.5f+.5f)*width,shell.probes.at("viewport-origin").y+(.5f-clip.y/clip.w*.5f)*height};};
            if(frame==36){require(!document.plan().spawns.empty(),"Mouse exercise needs a spawn point left to click");wanted=document.plan().spawns.begin()->first;editor.selected={};ImVec2 to;project(document.world().read(document.world().find(wanted)).transform,to);move(to);}
            if(frame==37)button(0,true);if(frame==38)button(0,false);
            if(frame==41){require(editor.selected==wanted,"A click on an object in the viewport did not select it");std::cout<<"Editor mouse viewport_select=1\n";}
        }
        // Save All exercise: one draft edit, then File > Save All with the mouse; the edit must be on disk and nothing left dirty.
        if(args.exercise_save_all){auto& io=ImGui::GetIO();static std::string original;static std::filesystem::path file;
            if(frame==3){require(shell.mask_editor!=AssetId{}&&editor.assets,"Save All exercise needs a mask draft open");auto& draft=editor.authoring.open(*editor.assets,shell.mask_editor);original=draft.value.at("name").get<std::string>();file=editor.assets->source_root()/draft.asset.path;auto value=draft.value;value["name"]=original+" saved";editor.authoring.apply(*editor.assets,shell.mask_editor,editor.authoring.revision(),value,"Rename mask");require(editor.authoring.dirty(),"Save All exercise could not make a draft edit");const auto to=shell.probes.at("menu-file");io.AddMousePosEvent(to.x,to.y);}
            if(frame==4)io.AddMouseButtonEvent(0,true);if(frame==5)io.AddMouseButtonEvent(0,false);
            if(frame==8){require(shell.probes.contains("menu-save-all"),"The File menu did not open");const auto to=shell.probes.at("menu-save-all");io.AddMousePosEvent(to.x,to.y);}
            if(frame==9)io.AddMouseButtonEvent(0,true);if(frame==10)io.AddMouseButtonEvent(0,false);
            if(frame==14){std::string on_disk;{std::ifstream saved(file);on_disk=nlohmann::json::parse(saved).at("name").get<std::string>();}require(!editor.authoring.dirty()&&on_disk==original+" saved","File > Save All did not write the gameplay draft");
                // Put the fixture's mask name back for the gates that run after this one.
                auto value=editor.authoring.open(*editor.assets,shell.mask_editor).value;value["name"]=original;editor.authoring.apply(*editor.assets,shell.mask_editor,editor.authoring.revision(),value,"Restore mask name");editor.save_gameplay();std::cout<<"Editor save_all saved=1 restored=1\n";}
        }
        // Mask Editor field exercise: click into the Display name field of the open Mask Editor and type one character.
        if(args.exercise_mask_field){auto& io=ImGui::GetIO();static std::string original;
            if(frame==3){require(shell.mask_editor!=AssetId{}&&shell.probes.contains("mask-name"),"Mask field exercise needs the Mask Editor open");original=editor.authoring.open(*editor.assets,shell.mask_editor).value.at("name").get<std::string>();const auto to=shell.probes.at("mask-name");io.AddMousePosEvent(to.x-40,to.y);}
            if(frame==4)io.AddMouseButtonEvent(0,true);if(frame==5)io.AddMouseButtonEvent(0,false);if(frame==7)io.AddInputCharactersUTF8("Z");
            if(frame==8){const auto typed=editor.authoring.open(*editor.assets,shell.mask_editor).value.at("name").get<std::string>();require(typed.size()==original.size()+1&&typed.find('Z')!=std::string::npos&&typed!=original,"Typing into the Mask Editor's name field did not edit the mask draft");ImGui::SetWindowFocus(nullptr);}
            // The field has let go of its text by now, so Undo is not typed over.
            if(frame==10){editor.authoring.record_changes(*editor.assets,"Edit mask name");require(editor.authoring.dirty()&&editor.authoring.can_undo(),"The typed mask name is not an undoable draft edit");editor.authoring.undo(*editor.assets);require(editor.authoring.open(*editor.assets,shell.mask_editor).value.at("name").get<std::string>()==original,"Undo did not restore the mask name");std::cout<<"Editor mask_field typed=1 undo=1\n";}
        }
#ifdef DAE_CHARACTER_SCENE
        // Senses exercise: the player stands for a second, then walks forward. Records when the first actor's controller first has a target.
        if(args.exercise_senses&&character&&character_resources&&!character_resources->spawns.empty()){senses_all=true;const bool aware=character->controller_target(character_resources->spawns.front().actor).has_value();if(aware&&frame<60)senses_early=true;if(aware&&!senses_noticed){senses_noticed=character->motor().tick;senses_noticed_z=character->motor().position.z;}}
#endif
        // Level design exercise: place an NPC and a model through the Place panel's own commands, delete one, undo the delete.
        if(args.exercise_place){auto& document=*editor.document;
            if(frame==1){shell.refresh_place_items(editor);require(!shell.place_items.empty(),"Place exercise needs placeable assets");const auto before=document.plan().origins.size(),spawns=document.plan().spawns.size(),models=document.plan().models.size();
                auto npc=std::find_if(shell.place_items.begin(),shell.place_items.end(),[](const auto& item){return item.npc;});auto model=std::find_if(shell.place_items.begin(),shell.place_items.end(),[&](const auto& item){return !item.npc&&std::any_of(document.plan().models.begin(),document.plan().models.end(),[&](const auto& placed){return placed.second==item.id;});});
                require(npc!=shell.place_items.end()&&model!=shell.place_items.end(),"Place exercise needs an NPC definition and a static model in the catalogue");
                const auto spawn=shell.place(editor,*npc,4,6,3.141592653589793);const auto prop=shell.place(editor,*model,-9,8);
                require(document.plan().origins.size()==before+2&&document.plan().spawns.size()==spawns+1&&document.plan().spawns.at(spawn)==npc->id&&document.plan().models.size()==models+1&&document.plan().models.at(prop)==model->id&&editor.selected==prop,"Placing an NPC and a model did not add two scene objects");
                const auto at=document.world().read(document.world().find(spawn)).transform;const bool surfaced=shell.place_surface&&shell.place_surface({4,500,6},{4,-500,6}).has_value();require(at.x==4&&at.z==6&&at.y==shell.surface_height(4,6)&&(surfaced||at.y==shell.place_height),"Placed object is not on the surface at the requested position");
                // A ray from above and to the side, as a viewport drop makes it, lands on the same collision.
                {const auto slanted=shell.place_surface?shell.place_surface({-6,10,-4},{4,at.y-10,6}):std::nullopt;require(!surfaced||(slanted&&std::abs((*slanted)[1]-at.y)<1e-3),"A slanted placement ray did not land on the scene collision");}
                std::cout<<"Editor placement on_collision="<<surfaced<<" surface_y="<<at.y<<"\n";
                shell.remove_object(editor,prop);require(document.plan().origins.size()==before+1&&!document.plan().models.contains(prop)&&editor.selected!=prop,"Deleting a placed object did not remove it");
                document.undo(document.revision());require(document.plan().origins.size()==before+2&&document.plan().models.contains(prop),"Undo did not bring the deleted object back");
                document.redo(document.revision());require(document.plan().origins.size()==before+1,"Redo did not delete it again");
                bool refused=false;try{shell.remove_object(editor,StableId{77,77});}catch(const std::exception&){refused=true;}require(refused,"Deleting an object that is not in the scene was accepted");
                std::size_t categories=0;std::string last;for(const auto& item:shell.place_items)if(item.category!=last){last=item.category;++categories;}
                std::cout<<"Place panel items="<<shell.place_items.size()<<" categories="<<categories<<" placed_npc="<<npc->name<<" placed_model="<<model->name<<" objects="<<before<<"->"<<document.plan().origins.size()<<" delete_undo_redo=1\n";}}
        if(args.exercise_workspaces){if(frame==40)shell.select_workspace(darkangel::editor_app::Workspace::Scene);if(frame==45)shell.select_workspace(darkangel::editor_app::Workspace::Assets);if(frame==50)shell.open_tool(darkangel::editor_app::ToolView::InputBindings);if(frame==55)shell.select_workspace(darkangel::editor_app::Workspace::Settings);if(frame==60)shell.select_workspace(darkangel::editor_app::Workspace::Game);require(editor.document->world().serialize()==import_baseline,"Workspace switch mutated authoring data");}
        if(!args.exercise_import.empty()){if(frame==0)shell.open_import(args.exercise_import,AssetImportType::Model);if(frame==1){shell.validate_import(editor);require(shell.import_validated(),"Native dialog validation failed");}if(frame==2){shell.commit_import(editor);require(editor.asset_inventory.size()==import_count+1&&editor.document->world().serialize()==import_baseline,"Import changed scene or failed to publish asset");std::cout<<"Native editor import dialog validated and committed without scene mutation\n";}}
        if(args.exercise && frame==1){auto baseline=editor.document->world().serialize();editor.play();editor.step();require(editor.document->world().serialize()==baseline,"Play mutated authoring data");editor.stop();auto selected=editor.selected;auto handle=editor.document->world().find(selected);PropertyChange change{selected,1,1,.2};auto command=editor.document->prepare(editor.document->revision(),{&change,1});editor.document->commit(command);editor.document->undo(editor.document->revision());editor.document->redo(editor.document->revision());editor.save(".cache/editor/AcceptanceScene.dascene");editor.open(".cache/editor/AcceptanceScene.dascene");require(!editor.document->world().valid(handle),"Open/restart reused old world handle");
            auto generation=store.acquire().number;bool failed=false;try{store.prepare(".cache/editor/missing.registry",args.cas,id);}catch(...){failed=true;}require(failed && store.acquire().number==generation,"Failed model candidate replaced active generation");reload_model();failed=false;try{reload_shader("deliberate shader syntax failure");}catch(...){failed=true;}require(failed,"Invalid shader candidate accepted");editor.log("Acceptance: Play isolation, Step, undo/redo, save/open, model generation and failed shader preservation passed.");}
#ifdef DAE_CHARACTER_SCENE
        if(args.exercise_authoring&&!args.authoring_replay&&frame==0){require(editor.assets&&editor.authoring_kit!=AssetId{},"Authoring exercise needs a project source mount and kit");auto& kit=editor.authoring.open(*editor.assets,editor.authoring_kit).value;auto ability=AssetId::parse(kit["slots"][1]["ability"].get<std::string>()),effect=AssetId::parse(kit["effects"][0].get<std::string>());for(const auto& asset:editor.asset_inventory){if(asset.path=="royal_district/combat/heavy.daability")ability=asset.id;if(asset.path=="royal_district/combat/burn.daeffect")effect=asset.id;}auto fresh=editor.authoring.duplicate(*editor.assets,ability,"gui_heavy"),burn=editor.authoring.duplicate(*editor.assets,effect,"gui_burn");auto& draft=editor.authoring.open(*editor.assets,fresh).value;draft["costs"][0]["amount"]=20;draft["melee"][0]["power"]=40;auto& status=editor.authoring.open(*editor.assets,burn).value;status["duration_ticks"]=120;status["period_ticks"]=30;editor.authoring.record_changes(*editor.assets,"Edit GUI ability/effect");auto ability_edit=draft,effect_edit=status;editor.authoring.undo(*editor.assets);require(draft!=ability_edit&&status!=effect_edit,"GUI grouped Undo failed");editor.authoring.redo(*editor.assets);require(draft==ability_edit&&status==effect_edit,"GUI grouped Redo failed");shell.select_workspace(darkangel::editor_app::Workspace::Scene);shell.select_workspace(darkangel::editor_app::Workspace::Assets);shell.select_workspace(darkangel::editor_app::Workspace::Ability);require(editor.authoring.dirty()&&draft==ability_edit&&status==effect_edit,"Workspace switching discarded gameplay drafts");editor.authoring.assign(*editor.assets,editor.authoring_kit,1,fresh);editor.authoring.bind(*editor.assets,editor.authoring_kit,fresh,2,burn,7);editor.authoring.record_changes(*editor.assets,"Author GUI heavy/effect");auto authored_kit=kit;editor.authoring.undo(*editor.assets);require(kit!=authored_kit,"GUI binding Undo did not restore composition");editor.authoring.redo(*editor.assets);require(kit==authored_kit,"GUI binding Redo did not restore composition");editor.save_gameplay();editor.refresh_assets();shell.authored_selection=fresh;shell.select_workspace(darkangel::editor_app::Workspace::Game);std::cout<<"Editor edit -> Undo -> Redo -> coordinated Save -> fresh Play; new heavy/effect, cost20 damage40 duration120 period30 Burn7\n";}
#ifdef DAE_NAVIGATION
        if(args.exercise_navigation_bake&&frame==0)bake_navigation();
        if(args.exercise_navigation_overlay&&frame==0){bake_navigation();navigation.shown=true;}
#else
        require(!args.exercise_navigation_bake,"This build has no navigation library");
#endif
        // Party travel authoring through editor commands: an entry point and a transition trigger from the Place panel's items,
        // the trigger pointed at its destination as a scene edit. The placement exercise saves the scene; the transition exercise plays it.
        if((args.exercise_transition||args.exercise_travel_place)&&frame==0){require(!args.travel_scene.empty(),"The travel exercises need --travel-scene");shell.refresh_place_items(editor);
            auto item=[&](int gameplay){auto found=std::find_if(shell.place_items.begin(),shell.place_items.end(),[&](const auto& candidate){return candidate.gameplay==gameplay;});require(found!=shell.place_items.end(),"The Place panel has no scene entry point or scene transition item");return *found;};
            const auto entries=editor.document->plan().entries.size(),transitions=editor.document->plan().transitions.size();const auto destination=AssetId::parse(args.travel_scene);const auto destination_entry=args.travel_entry.empty()?StableId{}:StableId::parse(args.travel_entry);
            if(args.exercise_travel_place){travel_placed_entry=shell.place(editor,item(7),2,7,1.5707963267948966);travel_placed_trigger=shell.place(editor,item(8),2,9);darkangel::editor_app::set_scene_transition(editor,travel_placed_trigger,destination,destination_entry,true,"Return to the gate",3);}
            else{travel_placed_entry=shell.place(editor,item(7),-4,3);travel_placed_trigger=shell.place(editor,item(8),6,3);darkangel::editor_app::set_scene_transition(editor,travel_placed_trigger,destination,destination_entry,false);}
            const auto& plan=editor.document->plan();require(plan.entries.size()==entries+1&&plan.transitions.size()==transitions+1&&plan.entries.contains(travel_placed_entry)&&darkangel::decode_transition(plan.transitions.at(travel_placed_trigger)).scene==destination,"Placing a scene entry point and a scene transition did not add them to the scene");
            if(args.exercise_travel_place){editor.save(editor.path);std::cout<<"Editor travel_place scene="<<editor.document->root().text()<<" entry="<<travel_placed_entry.text()<<" transition="<<travel_placed_trigger.text()<<" entries="<<plan.entries.size()<<" transitions="<<plan.transitions.size()<<"\n";}}
        // Gate authoring: world objects placed through the Scene tab's own paths and saved, before Play starts.
        if(!args.world_place.empty()&&frame==0){std::ifstream plan(args.world_place);require(bool(plan),"--world-place plan missing");darkangel::editor_app::place_world_plan(editor,shell,nlohmann::json::parse(plan));if(editor.prepare_play_resources)editor.prepare_play_resources();}
        if(args.play_from_save&&frame==0&&play_saves)play_saves->play_from_slot=true;
        if((args.play||args.exercise_character||args.exercise_combat||args.exercise_heavy||!args.join.empty()||!args.listen.empty())&&frame==0)editor.play();
        // The rejected-activation gate: authority will refuse the first press of this session.
        if(args.exercise_reject&&frame==0){require(bool(character),"The refusal exercise needs a Play session");character->refuse_next_activation();}
        if(args.character_clips.empty())editor.update(seconds);
#else
        editor.update(seconds);
#endif
        const auto& desc=swap->GetDesc();
        if(!scene_color || scene_color->GetDesc().Width!=scene_width || scene_color->GetDesc().Height!=scene_height){context->Flush();context->WaitForIdle();TextureDesc color;color.Name="Editor scene viewport";color.Type=RESOURCE_DIM_TEX_2D;color.Width=scene_width;color.Height=scene_height;color.Format=swap_desc.ColorBufferFormat;color.BindFlags=BIND_RENDER_TARGET|BIND_SHADER_RESOURCE;device->CreateTexture(color,nullptr,&scene_color);color.Name="Editor scene depth";color.Format=swap_desc.DepthBufferFormat;color.BindFlags=BIND_DEPTH_STENCIL;device->CreateTexture(color,nullptr,&scene_depth);require(scene_color && scene_depth,"Viewport resource creation failed");}
        // The second local player's view has its own target, sized to its viewport of the previous frame.
        if(shell.second_view){const unsigned wide=std::max(1u,static_cast<unsigned>(shell.area_second.width)),high=std::max(1u,static_cast<unsigned>(shell.area_second.height));
            if(!scene_color_second||scene_color_second->GetDesc().Width!=wide||scene_color_second->GetDesc().Height!=high){context->Flush();context->WaitForIdle();TextureDesc color;color.Name="Editor second player viewport";color.Type=RESOURCE_DIM_TEX_2D;color.Width=wide;color.Height=high;color.Format=swap_desc.ColorBufferFormat;color.BindFlags=BIND_RENDER_TARGET|BIND_SHADER_RESOURCE;device->CreateTexture(color,nullptr,&scene_color_second);color.Name="Editor second player depth";color.Format=swap_desc.DepthBufferFormat;color.BindFlags=BIND_DEPTH_STENCIL;device->CreateTexture(color,nullptr,&scene_depth_second);require(scene_color_second&&scene_depth_second,"Second viewport resource creation failed");}}
        shell.game_image_second=shell.second_view&&scene_color_second?reinterpret_cast<ImTextureID>(scene_color_second->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)):ImTextureID{};
        darkangel::editor_app::set_theme_dpi(GetDpiForWindow(window)/96.f);gui->NewFrame(desc.Width,desc.Height,desc.PreTransform);ImGuizmo::BeginFrame();auto image=reinterpret_cast<ImTextureID>(scene_color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));auto area=shell.draw(editor,desc.Width,desc.Height,args.backend.c_str(),lease.number,args.composer_playback?1./60:elapsed_seconds,image);
#ifdef DAE_CHARACTER_SCENE
        if(native_input){auto raw=native_input->poll();bool allow=shell.workspace==darkangel::editor_app::Workspace::Game&&shell.controls_focus&&!shell.controls_acquired&&editor.preview.active()&&!editor.paused;
            if(args.exercise_character||args.exercise_combat||args.exercise_heavy||args.exercise_actor_workflow||args.exercise_player_kit_play){raw.time_us=std::uint64_t(frame+1)*16667;raw.samples.clear();for(auto control:inputs->controls())raw.samples.push_back({control,((args.exercise_actor_workflow||args.exercise_player_kit_play)?(control==input_control(args.exercise_player_input?"Keyboard.K":"Mouse.Left")&&frame==5):args.exercise_interaction?((control==input_control("Keyboard.W")&&frame>=30&&frame<70)||(control==input_control("Keyboard.F")&&frame>=100&&frame<104)):args.exercise_encounter?(control==input_control("Keyboard.W")&&frame>=30&&frame<110):args.exercise_senses?(control==input_control("Keyboard.W")&&frame>=60&&frame<120):args.exercise_camera_look||args.exercise_attacked||args.exercise_two_players||args.exercise_transition?false:args.exercise_ranged?((control==input_control("Keyboard.AltLeft")&&frame>=4)||(control==input_control("Mouse.Left")&&frame>=10&&frame<14)):args.exercise_soft_lock?((control==input_control("Keyboard.D")&&frame<16)||(control==input_control("Keyboard.W")&&frame>=18&&frame<24)||(control==input_control("Mouse.Left")&&frame>=20&&frame<24)):args.exercise_mask_switch?(control==input_control("Keyboard.3")&&frame>=6&&frame<10):args.exercise_aim?(control==input_control("Keyboard.AltLeft")&&frame>=8):args.exercise_dodge?((control==input_control("Keyboard.C")&&frame>=6&&frame<12)||(control==input_control("Keyboard.D")&&frame<12)):args.exercise_heavy?(control==input_control("Mouse.Right")&&(args.exercise_death?frame%100<40:frame<40)):args.exercise_combat?(control==input_control("Mouse.Left")&&(frame==0||frame==60)):control==input_control("Keyboard.W")&&(!args.exercise_workspaces||frame!=60))?1.f:0.f,raw.time_us,false});allow=shell.workspace==darkangel::editor_app::Workspace::Game&&editor.preview.active()&&!editor.paused;}
            if(raw.overflow){allow=false;editor.log("Input queue overflow; controls cancelled until neutral.",darkangel::editor_app::ConsoleSeverity::Warning);}auto input_start=std::chrono::steady_clock::now();InputBatch batch;try{batch=inputs->update(raw.time_us,raw.samples,allow);}catch(const std::exception& error){batch=inputs->update(raw.time_us,{},false);allow=false;editor.stop();editor.log(error.what(),darkangel::editor_app::ConsoleSeverity::Error);}shell.input_processing_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-input_start).count();for(const auto& event:batch.events){if(character_resources&&character_resources->combat){bool routed=false;for(auto action:character_resources->combat->loadout_actions)routed|=action&&action==event.action;for(const auto& slot:character_resources->combat->kit->slots)routed|=slot.input_action&&slot.input_action==event.action;for(const auto& loadout:character_resources->combat->loadouts)if(loadout)for(const auto& slot:loadout->kit->slots)routed|=slot.input_action&&slot.input_action==event.action;if(routed)combat_events.push_back(event);}if(event.action==5&&event.edge==InputEdge::Pressed)shell.jump=true;if(event.action==7&&event.edge==InputEdge::Pressed)interact_pressed=true;if(event.edge!=InputEdge::Hold){shell.input_events.push_back(event);if(shell.input_events.size()>48)shell.input_events.erase(shell.input_events.begin());}}interact_held=allow&&inputs->state(7).down;if(allow){shell.forward=inputs->state(1).value;shell.lateral=inputs->state(2).value;shell.turn=inputs->state(3).value;shell.walk=inputs->state(4).down;}}
        if(!args.character_clips.empty()){character_input={};if(character){double forward=shell.forward,lateral=shell.lateral;auto length=std::hypot(forward,lateral);if(length>1){forward/=length;lateral/=length;}double scale=shell.walk?.4:1;const double look_step=(args.exercise_character||args.exercise_combat||args.exercise_heavy||args.exercise_actor_workflow||args.exercise_player_kit_play)?1./60:std::min(elapsed_seconds,.1);
            if(!director)director=std::make_unique<CameraDirector>(character_resources->cameras,character->predicted_motor().yaw);
            // Look input orbits the camera: stick axes through semantic actions, mouse while the viewport has control.
            double look_yaw=shell.turn*look_step*2,look_pitch=0;for(const auto& action:inputs->profile().actions)if(action.name=="look.pitch"&&action.kind==InputActionKind::Axis)look_pitch=inputs->state(action.id).value*look_step*1.5;
            ImVec2 mouse_delta{};if(shell.controls_focus&&!args.hidden)mouse_delta=ImGui::GetIO().MouseDelta;
            if(args.exercise_camera_look&&frame>=8&&frame<18)mouse_delta={6,-3};
            look_yaw-=mouse_delta.x*.0035;look_pitch+=mouse_delta.y*.0035;
            director->orbit(look_yaw,look_pitch);camera_held.clear();camera_tags.clear();
            for(const auto& camera:director->rig().cameras)if(!camera.input_action.empty())for(const auto& action:inputs->profile().actions)if(action.name==camera.input_action&&inputs->state(action.id).down&&std::find(camera_held.begin(),camera_held.end(),action.name)==camera_held.end())camera_held.push_back(action.name);
            if(character->ability())camera_tags=character->ability()->tags.values;
            const auto& live_camera=director->rig().cameras[director->select(camera_held,camera_tags)];const double camera_yaw=director->yaw();
            const double move_x=scale*(lateral*std::cos(camera_yaw)+forward*std::sin(camera_yaw)),move_z=scale*(forward*std::cos(camera_yaw)-lateral*std::sin(camera_yaw));
            // Free-look turns the character toward its movement; an aim camera keeps it facing with the camera.
            double yaw=character->predicted_motor().yaw,aim_pitch=0;if(live_camera.facing==CameraFacing::Camera){yaw=camera_yaw;
                // Crosshair convergence: aim from the muzzle of the kit's aimed ability at what the screen centre is on.
                double aim_range=60;MotorVec muzzle{0,1.3,.6};if(character_resources->combat)for(const auto& ability:character_resources->combat->abilities)if(ability->aim&&!ability->melee.empty()){aim_range=ability->aim->range;muzzle={ability->melee[0].offset[0],ability->melee[0].offset[1],ability->melee[0].offset[2]};break;}
                const auto& eye=director->pose();const MotorVec toward{eye.target[0]-eye.position[0],eye.target[1]-eye.position[1],eye.target[2]-eye.position[2]};
                if(std::hypot(std::hypot(toward.x,toward.y),toward.z)>1e-6){const auto solved=character->aim({eye.position[0],eye.position[1],eye.position[2]},toward,aim_range,muzzle);yaw=solved.yaw;aim_pitch=solved.pitch;}}
            else if(std::hypot(move_x,move_z)>.05){const double difference=std::remainder(std::atan2(move_x,move_z)-yaw,6.283185307179586),step=14*look_step;yaw=std::remainder(yaw+std::clamp(difference,-step,step),6.283185307179586);}
            character_input={move_x,move_z,yaw,shell.jump,false};character_input.pitch=aim_pitch;character_input.combat_events=std::move(combat_events);
            // The interact input asks for the interactable the HUD offers; the server decides.
            if(character&&character->interaction_prompt())interaction_offered=true;if(interact_pressed){interact_pressed=false;if(const auto offered=character->interaction_prompt()){character_input.interact=offered->id;++interactions_asked;}}else if(interact_held)if(const auto offered=character->interaction_prompt();offered&&offered->hold)character_input.interact=offered->id;combat_events.clear();}else combat_events.clear();
            if(shell.inventory_open&&character)character_input={0,0,character->predicted_motor().yaw};
            // Scripts pause while the party travels; await-scene waits for the party's n-th arrival.
            script_capture=false;bool travelling_now=travel&&travel->busy();
#ifdef DAE_EDITOR_GNS
            travelling_now|=party_guest&&!party_guest->running();
#endif
            auto run_script=[&character,&character_player,&character_resources,&script_signals,travelling_now,travel_arrivals,travel_failures,frame
#ifdef DAE_HUD
                ,&hud,&runtime_inventory,&runtime_altar
#endif
                ](darkangel::editor_app::exercises::ScriptRunner& runner,const darkangel::SceneClient& who,CharacterSceneInput& out){
                darkangel::editor_app::exercises::ScriptContext context{*character,who,script_signals,travelling_now,travel_arrivals,travel_failures,{}};
#ifdef DAE_HUD
                context.menu=[&hud,&runtime_inventory,&runtime_altar,&character,&character_player,&character_resources](std::string_view verb,unsigned value){
                    if(!hud)return false;if(verb.starts_with("inventory-"))return runtime_inventory.exercise(verb,value,*hud,character->client(character_player),*character_resources);
                    return runtime_altar.exercise(verb,value,*hud,character->client(character_player),character->hosting());};
#endif
                runner.frame(context,frame,out);};
            if(character&&player_script_at<player_script.size()){character_input={0,0,character->predicted_motor().yaw};run_script(owner_script,*character->client(character_player),character_input);}
            // A second local player: a script when one was given (gates), otherwise the keyboard and mouse while its viewport has control.
            if(character&&character->players().size()>1&&character->players()[1]!=character_player&&character->client(character->players()[1])){const auto second_id=character->players()[1];const auto* second=character->client(second_id);CharacterSceneInput other;other.yaw=second->predicted_motor().yaw;
                if(travelling_now){}
                else if(script_at<script.size())run_script(companion_script,*second,other);
                else if(shell.controls_player==1&&shell.controls_focus&&director_second){
                    // The focused viewport's player takes the keyboard and mouse; the first player stands still meanwhile.
                    if(!args.hidden){const auto moved=ImGui::GetIO().MouseDelta;director_second->orbit(-moved.x*.0035,moved.y*.0035);}
                    double forward=shell.forward,lateral=shell.lateral;const auto length=std::hypot(forward,lateral);if(length>1){forward/=length;lateral/=length;}const double facing=director_second->yaw();
                    other.x=lateral*std::cos(facing)+forward*std::sin(facing);other.z=forward*std::cos(facing)-lateral*std::sin(facing);if(std::hypot(other.x,other.z)>.05)other.yaw=std::atan2(other.x,other.z);
                    other.jump=character_input.jump;other.combat_events=std::move(character_input.combat_events);if(character_input.interact){character_input.interact.reset();if(const auto offered=second->interaction_prompt())other.interact=offered->id;}
                    character_input={0,0,character->predicted_motor().yaw};}
                character->input(second_id,std::move(other));}
#ifdef DAE_EDITOR_GNS
            if(party_host){party_host->pump(std::min(elapsed_seconds,.1),character,travel&&travel->busy()&&travel->status().phase<darkangel::TransitionPhase::Swapping);for(const auto& drop:party_host->drain_drops()){std::cout<<"Party host dropped seat="<<drop.seat<<" reason="<<drop.reason<<std::endl;editor.log("Player "+std::to_string(drop.seat)+": "+drop.reason,darkangel::editor_app::ConsoleSeverity::Warning);}}
            if(party_guest){party_guest->pump();shell.client_status=party_guest->status();
                // A guest counts its entries into a scene as the host counts the party's arrivals.
                if(party_guest->running()&&party_guest->epoch()!=guest_arrived_epoch){guest_arrived_epoch=party_guest->epoch();++travel_arrivals;}if(party_guest->finished())PostMessageW(window,WM_CLOSE,0,0);}
#endif
#ifdef DAE_EDITOR_GNS
            if(frame%60==0)party_receipt(frame);
            // A scripted party run does not idle after its Play was stopped by an error.
            if(!args.party_report.empty()&&travel_arrivals&&!editor.preview.active()&&(!player_script.empty()||!script.empty())){party_receipt(frame);throw std::runtime_error("Party Play stopped: "+(editor.console.empty()?std::string("no reason given"):editor.console.back().message));}
#endif
            editor.update((args.exercise_character||args.exercise_combat||args.exercise_heavy||args.exercise_actor_workflow||args.exercise_player_kit_play)?1./60:elapsed_seconds);
            // Party travel: the coordinator takes what the session's triggers raised and advances a running transition one phase a frame.
            if(travel&&editor.preview.active()&&!editor.paused){
                try{travel->update(args.exercise_heavy?1./60:std::min(elapsed_seconds,.1));}catch(const std::exception& error){editor.log(std::string("Scene transition: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Error);}
                if(travel_was_busy&&!travel->busy()){if(const auto& ended=travel->outcome()){if(ended->arrived){++travel_arrivals;
                        // A scripted walk that carried a player into the transition ends with it.
                        if(player_script_at<player_script.size()&&player_script[player_script_at].verb=="walk-to")++player_script_at;if(script_at<script.size()&&script[script_at].verb=="walk-to")++script_at;editor.log("The party arrived in "+editor.path.filename().string()+".");if(ended->save&&save_play)save_play("Rested at an altar");}else{++travel_failures;travel_failure=ended->failure;editor.log("The party did not travel: "+ended->failure,darkangel::editor_app::ConsoleSeverity::Warning);}}}
                travel_was_busy=travel->busy();
                // The save worker reports when a write has finished; the console says what happened.
                if(play_saves){const auto before=play_saves->status();play_saves->poll();if(play_saves->status()!=before&&!play_saves->status().starts_with("Saving"))editor.log(play_saves->status());}
                if(travel->fatal()){const auto why=travel->outcome()?travel->outcome()->failure:std::string("Play cannot continue");editor.stop();editor.log(why,darkangel::editor_app::ConsoleSeverity::Error);}
                // A transition that did not arrive can leave the editor prepared for the destination: it follows the party back.
                else if(!travel->busy()&&travel->scene()&&globals_scene!=travel->scene()->scene){const auto back=travel->scene()->scene;
                    try{auto parked=parked_documents.find(back);require(parked!=parked_documents.end(),"The scene the party is in is no longer open");auto document=std::move(parked->second);const auto file=parked_paths[back];parked_documents.erase(parked);enter_scene(back,std::move(document),file);editor.preview.retire();editor.preview.activate(std::move(travel_preview));travel->unloading();character=travel->session();attach_session(editor.document->plan());}
                    catch(const std::exception& error){const std::string why=error.what();editor.stop();editor.log("The scene the party is in could not be shown again: "+why,darkangel::editor_app::ConsoleSeverity::Error);}}}
            }
#endif

#ifdef DAE_AGENT_ENDPOINTS
poll_agent();
#endif
scene_width=std::max(1u,static_cast<unsigned>(area.width));scene_height=std::max(1u,static_cast<unsigned>(area.height));
        if(shell.reload_model){try{reload_model();}catch(const std::exception& error){context->Flush();context->WaitForIdle();editor.log(error.what(),darkangel::editor_app::ConsoleSeverity::Error);}shell.reload_model=false;}
        if(shell.reload_shader){try{reload_shader(shader_text());}catch(const std::exception& error){context->Flush();context->WaitForIdle();editor.log(error.what(),darkangel::editor_app::ConsoleSeverity::Error);}shell.reload_shader=false;}
        auto* rtv=scene_color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);auto* dsv=scene_depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);context->SetRenderTargets(1,&rtv,dsv,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);const float background[]={.015f,.025f,.045f,1};context->ClearRenderTarget(rtv,background,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearDepthStencil(dsv,CLEAR_DEPTH_FLAG,1,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport;viewport.Width=static_cast<float>(scene_color->GetDesc().Width);viewport.Height=static_cast<float>(scene_color->GetDesc().Height);context->SetViewports(1,&viewport,scene_color->GetDesc().Width,scene_color->GetDesc().Height);
        using namespace DirectX;auto view=XMMatrixLookAtRH(XMVectorSet(0,gpu.radius*.2f,gpu.radius*std::max(3.8f,2.4f/(.4142f*viewport.Width/viewport.Height)),1),XMVectorZero(),XMVectorSet(0,1,0,0));if(!args.scene.empty())view=XMMatrixLookAtRH(XMVectorSet(26,24,34,1),XMVectorSet(0,1,0,1),XMVectorSet(0,1,0,0));if(args.camera_close)view=XMMatrixLookAtRH(XMVectorSet(4,2.5f,8,1),XMVectorSet(0,1.3f,3,1),XMVectorSet(0,1,0,0));
#ifdef DAE_CHARACTER_SCENE
        camera_fov=XM_PIDIV4;if(!character)director.reset();
        if(character&&shell.workspace==darkangel::editor_app::Workspace::Game){const auto& state=character->motor();if(!director)director=std::make_unique<CameraDirector>(character_resources->cameras,state.yaw);
            const auto& camera=director->update((args.exercise_character||args.exercise_combat||args.exercise_heavy||args.exercise_actor_workflow||args.exercise_player_kit_play)?1./60:std::min(elapsed_seconds,.1),CameraTargets{{state.position.x,state.position.y,state.position.z},std::nullopt},camera_held,camera_tags);shell.camera_live=director->rig().cameras[director->live()].name;
            view=XMMatrixLookAtRH(XMVectorSet(float(camera.position[0]),float(camera.position[1]),float(camera.position[2]),1),XMVectorSet(float(camera.target[0]),float(camera.target[1]),float(camera.target[2]),1),XMVectorSet(0,1,0,0));camera_fov=float(camera.fov_degrees)*XM_PI/180.f;}
        else if(shell.workspace==darkangel::editor_app::Workspace::Player&&shell.camera_preview_visible&&shell.camera_preview_rig&&shell.camera_preview>=0&&shell.camera_preview<static_cast<int>(shell.camera_preview_rig->cameras.size())){
            // Authoring preview: one drafted virtual camera around the scene's player placement.
            CameraTargets targets;if(character_resources){auto entity=editor.displayed().find(character_resources->player);if(editor.displayed().valid(entity)){auto placed=editor.displayed().read(entity).transform;targets.follow={placed.x,placed.y,placed.z};}}
            CameraDirector preview(*shell.camera_preview_rig,shell.camera_preview_yaw*XM_PI/180.,shell.camera_preview_pitch);auto camera=preview.solve(static_cast<unsigned>(shell.camera_preview),targets);
            view=XMMatrixLookAtRH(XMVectorSet(float(camera.position[0]),float(camera.position[1]),float(camera.position[2]),1),XMVectorSet(float(camera.target[0]),float(camera.target[1]),float(camera.target[2]),1),XMVectorSet(0,1,0,0));camera_fov=float(camera.fov_degrees)*XM_PI/180.f;}
#endif
#ifdef DAE_CHARACTER_SCENE
        const bool character_render=shell.workspace==darkangel::editor_app::Workspace::Character&&authoring_skin&&shell.character_preview_asset==shell.character_selection;
        const bool graph_render=shell.workspace==darkangel::editor_app::Workspace::Animation&&authoring_skin&&graph_pose&&shell.graph_preview_asset==shell.authored_selection;
        const bool composer_render=shell.composer_visible&&(composer_pose||graph_pose)&&!editor.preview.active()&&(shell.workspace==darkangel::editor_app::Workspace::Ability||shell.workspace==darkangel::editor_app::Workspace::Animation||character_render);
        if(composer_render){float horizontal=shell.composer_distance*std::cos(shell.composer_pitch);view=XMMatrixLookAtRH(XMVectorSet(horizontal*std::sin(shell.composer_yaw),1+shell.composer_distance*std::sin(shell.composer_pitch),horizontal*std::cos(shell.composer_yaw),1),XMVectorSet(0,1.0f,0,1),XMVectorSet(0,1,0,0));}
#endif
        if(shell.workspace==darkangel::editor_app::Workspace::Vfx){const auto& preview=vfx_preview;view=XMMatrixLookAtRH(XMVectorSet(std::sin(preview.yaw)*preview.distance,preview.height+preview.distance*.25f,std::cos(preview.yaw)*preview.distance,1),XMVectorSet(0,preview.height,0,1),XMVectorSet(0,1,0,0));camera_fov=XM_PIDIV4;}
        auto projection=XMMatrixPerspectiveFovRH(camera_fov,viewport.Width/viewport.Height,gpu.radius*.01f,gpu.radius*40);XMFLOAT4X4 view_matrix,projection_matrix;XMStoreFloat4x4(&view_matrix,view);XMStoreFloat4x4(&projection_matrix,projection);shell.scene_view=view_matrix;shell.scene_projection=projection_matrix;shell.scene_camera=true;if(!args.hidden)shell.gizmo(editor,view_matrix,projection_matrix);
        // Placed models, then the characters of spawn points: at the spawn point while authoring, as the spawned actor in Play.
        std::vector<std::pair<StableId,AssetId>> drawn_objects(editor.document->plan().models.begin(),editor.document->plan().models.end());
        // Scatter: models along splines, derived each frame from the points as they stand. They are not scene objects.
        std::map<StableId,darkangel::Transform> scatter_poses;scatter_drawn=0;
        for(const auto& built:build_splines())if(built.curve&&built.definition.scatter){const auto model=built.definition.scatter->model;if(model!=id&&!scene_models.contains(model)&&editor.prepare_model){try{editor.prepare_model(model);}catch(const std::exception&){}}if(model!=id&&!scene_models.contains(model))continue;
            unsigned index=0;for(const auto& instance:darkangel::scatter_along(*built.curve,*built.definition.scatter,built.definition.closed)){darkangel::Transform pose;pose.x=instance.position.x;pose.y=instance.position.y;pose.z=instance.position.z;pose.yaw=instance.yaw;pose.scale=1;const auto derived=darkangel::scatter_instance(built.id,index++);scatter_poses.emplace(derived,pose);drawn_objects.emplace_back(derived,model);++scatter_drawn;}}
#ifdef DAE_CHARACTER_SCENE
        if(character_resources&&scene_skins.contains(character_resources->skin)){for(const auto& [point,npc]:editor.document->plan().spawns)drawn_objects.emplace_back(point,character_resources->skin);for(const auto& spawn:character_resources->spawns)drawn_objects.emplace_back(spawn.actor,spawn.skin);if(character)for(auto actor:character->actors())if(character->actor_encounter(actor))drawn_objects.emplace_back(actor,character_resources->skin);if(character)for(const auto member:character->players())if(member!=character_resources->player)drawn_objects.emplace_back(member,character_resources->skin);}
#endif
#ifdef DAE_CHARACTER_SCENE
        using SceneViewer=darkangel::SceneClient;
#else
        struct SceneViewer;
#endif
        // One pass over the scene's objects for a view. viewer: the client whose world and poses are shown; null for the editor's own.
        auto draw_objects=[&](const XMMATRIX& view,const XMMATRIX& projection,const SceneViewer* viewer){
        const darkangel::World* shown_world=&editor.displayed();(void)viewer;
#ifdef DAE_CHARACTER_SCENE
        if(viewer)shown_world=&viewer->presentation();
#endif
        std::vector<std::pair<StableId,darkangel::MotorVec>> mover_centers;const darkangel::WorldStateView* world_view=nullptr;const darkangel::CharacterSceneCombat* world_content=nullptr;
#ifdef DAE_CHARACTER_SCENE
        if(character&&editor.preview.active())if(const auto* viewer=character->client(character_player)){mover_centers=viewer->movers();world_view=viewer->world_state();world_content=viewer->combat();}
#endif
        for(const auto& [object_id,model_id]:drawn_objects){auto entity=shown_world->find(object_id);const auto scattered=scatter_poses.find(object_id);if(!shown_world->valid(entity)&&scattered==scatter_poses.end())continue;
#ifdef DAE_CHARACTER_SCENE
            if(composer_render&&(model_id!=character_resources->skin||(character_resources->player&&object_id!=character_resources->player)))continue;
#endif
            auto pose=scattered!=scatter_poses.end()?scattered->second:shown_world->read(entity).transform;
            if(world_view&&std::find(world_view->collected.begin(),world_view->collected.end(),object_id)!=world_view->collected.end())continue;
            if(world_content)for(const auto& [mover,center]:mover_centers)if(mover==object_id)for(const auto& placed:world_content->movers)if(placed.id==mover){pose.x+=center.x-placed.start.x;pose.y+=center.y-placed.start.y;pose.z+=center.z-placed.start.z;}
#ifdef DAE_CHARACTER_SCENE
            if(composer_render){pose={};pose.scale=1;}
#endif
if(shell.draft && editor.selected==object_id)pose=*shell.draft;

#ifdef DAE_ANIMATION
            auto skin=scene_skins.find(model_id);auto* selected_skin=skin==scene_skins.end()?nullptr:&skin->second;
#ifdef DAE_CHARACTER_SCENE
            if(character_render||graph_render)selected_skin=&*authoring_skin;
#endif
            auto* drawn_ptr=selected_skin?&selected_skin->gpu:nullptr;if(drawn_ptr&&!args.skin_reference){const std::vector<JointMatrix>* gameplay_pose=nullptr;
#ifdef DAE_CHARACTER_SCENE
                if(composer_render)gameplay_pose=&composer_matrices;else if(character&&selected_skin->model.rig.definition.id==character_resources->rig.definition.id&&selected_skin->model.rig.definition.signature==character_resources->rig.definition.signature)gameplay_pose=viewer?viewer->pose(object_id):character->pose(object_id);
#endif
                upload_palette(context,bone_buffer,*selected_skin,diagnostic_clip.get(),args.pose_tick,gameplay_pose);}
#else
            UploadedModel* drawn_ptr=nullptr;
#endif
            auto& drawn=drawn_ptr?*drawn_ptr:(model_id==id?gpu:scene_models.at(model_id));auto rotation=XMMatrixRotationRollPitchYaw(pose.pitch,pose.yaw,pose.roll);auto world=(args.scene.empty()?XMMatrixTranslation(-drawn.center[0],-drawn.center[1],-drawn.center[2]):XMMatrixIdentity())*XMMatrixScaling(pose.scale,pose.scale,pose.scale)*rotation*XMMatrixTranslation(pose.x,pose.y,pose.z);
            for(const auto& mesh:drawn.meshes){IBuffer* vb=mesh.vertices;Uint64 offset{};context->SetVertexBuffers(0,1,&vb,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);context->SetIndexBuffer(mesh.indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                for(const auto& part:mesh.parts){const auto& material=drawn.materials.at(part.material);Constants constants;XMStoreFloat4x4(&constants.mvp,world*view*projection);XMStoreFloat4x4(&constants.rotation,rotation);constants.color={material.value.color[0]*mesh.color[0],material.value.color[1]*mesh.color[1],material.value.color[2]*mesh.color[2],material.value.color[3]*mesh.color[3]};constants.surface={material.value.roughness,material.value.metallic,0,0};void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Uniform buffer mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);

#ifdef DAE_ANIMATION
                    context->SetPipelineState((drawn_ptr&&!args.skin_reference)?(material.value.double_sided?skin_pso.RawPtr():skin_cull.RawPtr()):(material.value.double_sided?pso.RawPtr():pso_cull.RawPtr()));
#else
                    context->SetPipelineState(material.value.double_sided?pso.RawPtr():pso_cull.RawPtr());
#endif
                    context->CommitShaderResources(material.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);DrawIndexedAttribs draw;draw.NumIndices=part.count;draw.IndexType=VT_UINT32;draw.FirstIndexLocation=part.first;draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(draw);}
            }
#ifdef DAE_CHARACTER_SCENE
            const darkangel::editor_app::WornMask* wearing=nullptr;if(character&&character_resources){if(object_id==character_player||(!args.mask_tune.empty()&&character_resources->combat&&object_id==first_actor()))wearing=darkangel::editor_app::RuntimeInventory::worn(*character_resources,character->ability());else if(auto observed=character->observer_if_ready(object_id,double(character->motor().tick)))wearing=darkangel::editor_app::RuntimeInventory::worn(*character_resources,observed->frame.ability);}
            if(wearing&&shell.workspace==darkangel::editor_app::Workspace::Game){
                auto worn=*wearing;const auto* worn_pose=viewer?viewer->pose(object_id):character->pose(object_id);if(!args.mask_tune.empty()){std::array<float,7> tune{};std::stringstream stream(args.mask_tune);std::string item;for(unsigned index=0;index<7&&std::getline(stream,item,',');++index)tune[index]=std::stof(item);worn.position={tune[0],tune[1],tune[2]};worn.rotation={tune[3],tune[4],tune[5]};worn.scale=tune[6];}
                if(!worn_models.contains(worn.model)&&!worn_failed.contains(worn.model)){try{worn_models.emplace(worn.model,upload(device,load_cooked_model(args.registry,args.cas,worn.model),pso,pso_cull));}catch(const std::exception& error){worn_failed.insert(worn.model);editor.log(std::string("Mask visual unavailable: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Error);}}
                auto uploaded=worn_models.find(worn.model);
                if(uploaded!=worn_models.end()&&worn_pose&&worn.joint<worn_pose->size()){
                    XMFLOAT4X4 joint;std::memcpy(&joint,(*worn_pose)[worn.joint].values.data(),sizeof(joint));const float radians=XM_PI/180.f;
                    auto mask_world=XMMatrixScaling(worn.scale,worn.scale,worn.scale)*XMMatrixRotationRollPitchYaw(worn.rotation[0]*radians,worn.rotation[1]*radians,worn.rotation[2]*radians)*XMMatrixTranslation(worn.position[0],worn.position[1],worn.position[2])*XMLoadFloat4x4(&joint)*world;
                    auto mask_rotation=mask_world;mask_rotation.r[3]=XMVectorSet(0,0,0,1);for(unsigned axis=0;axis<3;++axis)mask_rotation.r[axis]=XMVector3Normalize(mask_rotation.r[axis]);
                    for(const auto& mesh:uploaded->second.meshes){IBuffer* vb=mesh.vertices;Uint64 offset{};context->SetVertexBuffers(0,1,&vb,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);context->SetIndexBuffer(mesh.indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        for(const auto& part:mesh.parts){const auto& material=uploaded->second.materials.at(part.material);Constants constants;XMStoreFloat4x4(&constants.mvp,mask_world*view*projection);XMStoreFloat4x4(&constants.rotation,mask_rotation);constants.color={material.value.color[0]*mesh.color[0]*worn.tint[0],material.value.color[1]*mesh.color[1]*worn.tint[1],material.value.color[2]*mesh.color[2]*worn.tint[2],material.value.color[3]*mesh.color[3]*worn.tint[3]};constants.surface={material.value.roughness,material.value.metallic,0,0};void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Uniform buffer mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);
                            context->SetPipelineState(pso.RawPtr());context->CommitShaderResources(material.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);DrawIndexedAttribs draw;draw.NumIndices=part.count;draw.IndexType=VT_UINT32;draw.FirstIndexLocation=part.first;draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(draw);}}
                }
            }else if(!character&&!worn_models.empty()){worn_models.clear();worn_failed.clear();}
#endif
        }};
#ifdef DAE_CHARACTER_SCENE
        // While the party travels the editor's document and resources may already be the destination's; the loading panel covers the view.
        const bool travelling=travel&&travel->busy();
#else
        const bool travelling=false;
#endif
        if(!travelling)draw_objects(view,projection,nullptr);
#ifdef DAE_CHARACTER_SCENE
        // A second local player's view: the same objects from its own client's world and poses, through its own camera.
        shell.second_view=character&&character->players().size()>1&&character->client(character->players()[1])&&shell.workspace==darkangel::editor_app::Workspace::Game;
        if(shell.second_view&&scene_color_second){const auto* second=character->client(character->players()[1]);const auto& walker=second->predicted_motor();if(!director_second)director_second=std::make_unique<CameraDirector>(character_resources->cameras,walker.yaw);
            const auto& eye=director_second->update(args.exercise_heavy?1./60:std::min(elapsed_seconds,.1),CameraTargets{{walker.position.x,walker.position.y,walker.position.z},std::nullopt},std::span<const std::string>{},std::span<const TagId>{});
            auto* rtv_second=scene_color_second->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);auto* dsv_second=scene_depth_second->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);context->SetRenderTargets(1,&rtv_second,dsv_second,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearRenderTarget(rtv_second,background,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearDepthStencil(dsv_second,CLEAR_DEPTH_FLAG,1,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Viewport viewport_second;viewport_second.Width=static_cast<float>(scene_color_second->GetDesc().Width);viewport_second.Height=static_cast<float>(scene_color_second->GetDesc().Height);context->SetViewports(1,&viewport_second,scene_color_second->GetDesc().Width,scene_color_second->GetDesc().Height);
            const auto view_second=XMMatrixLookAtRH(XMVectorSet(float(eye.position[0]),float(eye.position[1]),float(eye.position[2]),1),XMVectorSet(float(eye.target[0]),float(eye.target[1]),float(eye.target[2]),1),XMVectorSet(0,1,0,0));const auto projection_second=XMMatrixPerspectiveFovRH(float(eye.fov_degrees)*XM_PI/180.f,viewport_second.Width/viewport_second.Height,gpu.radius*.01f,gpu.radius*40);
            XMStoreFloat4x4(&shell.game_view_second,view_second);XMStoreFloat4x4(&shell.game_projection_second,projection_second);if(!travelling)draw_objects(view_second,projection_second,second);
            context->SetRenderTargets(1,&rtv,dsv,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->SetViewports(1,&viewport,scene_color->GetDesc().Width,scene_color->GetDesc().Height);}
#endif
#ifdef DAE_CHARACTER_SCENE
        // Projectiles in flight, from the client's replicated list, pointing along their velocity.
        if(character&&shell.workspace==darkangel::editor_app::Workspace::Game){projectiles_seen=std::max(projectiles_seen,character->projectiles().size());
            for(const auto& shot:character->projectiles()){const double speed=std::sqrt(shot.velocity.x*shot.velocity.x+shot.velocity.y*shot.velocity.y+shot.velocity.z*shot.velocity.z);if(speed<1e-6)continue;
                auto rotation=XMMatrixRotationRollPitchYaw(float(-std::asin(std::clamp(shot.velocity.y/speed,-1.,1.))),float(std::atan2(shot.velocity.x,shot.velocity.z)),0);auto world=rotation*XMMatrixTranslation(float(shot.position.x),float(shot.position.y),float(shot.position.z));
                for(const auto& mesh:projectile_gpu.meshes){IBuffer* vb=mesh.vertices;Uint64 offset{};context->SetVertexBuffers(0,1,&vb,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);context->SetIndexBuffer(mesh.indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    for(const auto& part:mesh.parts){const auto& material=projectile_gpu.materials.at(part.material);Constants constants;XMStoreFloat4x4(&constants.mvp,world*view*projection);XMStoreFloat4x4(&constants.rotation,rotation);constants.color={material.value.color[0],material.value.color[1],material.value.color[2],material.value.color[3]};constants.surface={material.value.roughness,material.value.metallic,0,0};void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Uniform buffer mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);
                        context->SetPipelineState(pso.RawPtr());context->CommitShaderResources(material.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);DrawIndexedAttribs draw;draw.NumIndices=part.count;draw.IndexType=VT_UINT32;draw.FirstIndexLocation=part.first;draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(draw);}}}}
#ifdef DAE_AUDIO
        // Audio every frame: the mixer as the project (or its open draft) has it, encounter profiles layered while their encounter runs, and the engine's clock outside Play.
        if(audio){if(frame%6==0||audio_tab.sound!=AssetId{}||shell.workspace==darkangel::editor_app::Workspace::Audio){const auto mix=project_mix();if(!audio_applied||!(*audio_applied==mix)){if(audio_director)audio_director->mix(mix);else audio->mix(mix);audio_applied=mix;}}
            if(audio_director&&character)for(const auto id:character->encounters()){const auto wanted=encounter_profiles.find(id);if(wanted==encounter_profiles.end())continue;const auto profile=play_profiles.find(wanted->second);if(profile==play_profiles.end())continue;const auto status=character->encounter(id);const std::uint64_t key=std::hash<std::string>{}(id.text());if(status&&status->phase==darkangel::EncounterPhase::Running)audio_director->push(key,profile->second);else audio_director->pop(key);}
            if(!character)audio->update(args.hidden?1./60:std::clamp(seconds,0.,.25));}
#endif
        // The VFX tab's preview: the selected effect in a loop at the origin, cooked again when its source changes on disk.
        if(vfx&&!character&&shell.workspace==darkangel::editor_app::Workspace::Vfx&&vfx_preview.selected!=AssetId{}){auto& preview=vfx_preview;
            if(editor.assets&&frame%30==0){std::error_code ignored;const auto written=std::filesystem::last_write_time(editor.assets->source_root()/preview.path,ignored);if(!ignored&&written!=preview.seen)vfx_cook();}
            const double step=args.hidden?1./60:seconds;vfx->update(step);if(preview.cooked&&!vfx->alive(preview.handle)){preview.idle+=step;if(preview.handle==0||preview.idle>.5){preview.handle=vfx->play(preview.selected,{0,preview.height,0});preview.idle=0;}}
            darkangel::VfxView shown;XMFLOAT4X4 stored;XMStoreFloat4x4(&stored,view);std::memcpy(shown.view.data(),&stored,sizeof(stored));XMStoreFloat4x4(&stored,projection);std::memcpy(shown.projection.data(),&stored,sizeof(stored));XMStoreFloat4x4(&stored,XMMatrixInverse(nullptr,view));
            shown.eye={stored._41,stored._42,stored._43};shown.forward={-stored._31,-stored._32,-stored._33};shown.near_plane=gpu.radius*.01f;shown.far_plane=gpu.radius*40;
            try{vfx->draw(shown);}catch(const std::exception& error){vfx_preview.error=error.what();}}
        // Visual effects of the Play session: advanced on the session's tick clock, drawn after the opaque scene against its depth.
        if(vfx&&character&&shell.workspace==darkangel::editor_app::Workspace::Game){const auto tick=character->motor().tick;if(vfx_tick&&tick>vfx_tick){vfx->update(double(std::min<std::uint64_t>(tick-vfx_tick,120))/60.);
#ifdef DAE_AUDIO
                if(audio)audio->update(double(std::min<std::uint64_t>(tick-vfx_tick,120))/60.);
#endif
            }vfx_tick=tick;cue_effects.follow();
            {const auto& rig=character_resources->rig.definition;
                for(const auto& request:cue_primitives.trails.emitting()){const auto identity=character->identity(request.network);if(!identity)continue;const auto* pose=character->pose(*identity);const auto handle=character->presentation().find(*identity);if(!pose||!character->presentation().valid(handle))continue;
                    std::string key=request.definition->socket;if(const auto socket=rig.sockets.find(key);socket!=rig.sockets.end())key=socket->second;const auto joint=std::find_if(rig.joints.begin(),rig.joints.end(),[&](const auto& candidate){return candidate.key==key;});if(joint==rig.joints.end())continue;const auto index=std::size_t(joint-rig.joints.begin());if(index>=pose->size())continue;
                    const auto& at=character->presentation().read(handle).transform;XMFLOAT4X4 stored_joint;std::memcpy(&stored_joint,(*pose)[index].values.data(),sizeof(stored_joint));
                    const auto joint_world=XMLoadFloat4x4(&stored_joint)*XMMatrixScaling(float(at.scale),float(at.scale),float(at.scale))*XMMatrixRotationRollPitchYaw(float(at.pitch),float(at.yaw),float(at.roll))*XMMatrixTranslation(float(at.x),float(at.y),float(at.z));
                    const auto point=[&](const std::array<float,3>& offset){XMFLOAT3 out;XMStoreFloat3(&out,XMVector3TransformCoord(XMVectorSet(offset[0],offset[1],offset[2],1),joint_world));return std::array<float,3>{out.x,out.y,out.z};};
                    cue_primitives.trails.sample(request.instance,tick,point(request.definition->base),point(request.definition->tip));}
                cue_primitives.trails.advance(tick);cue_primitives.decals.advance(tick);std::vector<darkangel::PrimitiveVertex> built;std::vector<darkangel::VfxQuadVertex> quads;
                const auto hand=[&](bool additive){quads.clear();for(const auto& vertex:built)quads.push_back({vertex.position,vertex.color});built.clear();vfx->quads(quads,additive);};
                cue_primitives.decals.build(tick,built);hand(false);cue_primitives.trails.build(tick,built);hand(true);}
            darkangel::VfxView shown;XMFLOAT4X4 stored;XMStoreFloat4x4(&stored,view);std::memcpy(shown.view.data(),&stored,sizeof(stored));XMStoreFloat4x4(&stored,projection);std::memcpy(shown.projection.data(),&stored,sizeof(stored));XMStoreFloat4x4(&stored,XMMatrixInverse(nullptr,view));
            shown.eye={stored._41,stored._42,stored._43};shown.forward={-stored._31,-stored._32,-stored._33};shown.near_plane=gpu.radius*.01f;shown.far_plane=gpu.radius*40;
#ifdef DAE_AUDIO
            if(audio){audio->listener(shown.eye,shown.forward,{0,1,0});cue_sounds.follow();}
#endif
            try{vfx->draw(shown);}catch(const std::exception& error){editor.log(std::string("Visual effects stopped: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Error);cue_effects.bind(nullptr,nullptr,{});vfx.reset();}}
#endif
        // Scripted shutdown waits for a coherent final view. A baseline refresh can
        // start during the same frame as quit; two arbitrary render frames are not a
        // delivery fence and would publish an unready final receipt.
#ifdef DAE_CHARACTER_SCENE
        if(script_quit&&character&&character->bootstrapped())args.frames=frame+1;
#endif
        auto* back=swap->GetCurrentBackBufferRTV();context->SetRenderTargets(1,&back,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearRenderTarget(back,background,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);gui->Render(context);if(script_capture)script_captured=true;if(!args.capture.empty() && (script_capture||(args.frames && frame+1==args.frames&&!script_captured)))capture(device,context,back->GetTexture(),args.capture,args.capture_workspace?darkangel::editor_app::ViewArea{0,0,static_cast<float>(desc.Width),static_cast<float>(desc.Height)}:area);swap->Present(args.hidden?0:1);++frame;
    }
#ifdef DAE_CHARACTER_SCENE
    window_input=nullptr;
    if(args.composer_playback){const auto expected=std::min(args.composer_tick+args.frames,double(composer_pose->clip().ticks));require(std::abs(shell.composer_tick-expected)<1e-6,"Composer preview clock failed");if(expected==composer_pose->clip().ticks)require(!shell.composer_playing,"Preview did not stop at clip end");std::cout<<"Composer preview playback clock="<<shell.composer_tick<<" source=unchanged\n";}
    if(args.exercise_composer){require(composer_pose&&shell.composer_visible&&!editor.preview.active()&&!character&&!editor.authoring.dirty()&&editor.document->world().serialize()==import_baseline,"Composer scrub changed gameplay or authoring state");std::cout<<"Composer isolated pose tick="<<shell.composer_tick<<" joints="<<composer_matrices.size()<<" gameplay=stopped source=unchanged\n";}
    if(args.exercise_player_kit_play){require(character&&character->ability()&&character->pending_prediction()==0&&!editor.authoring.dirty(),"Player Kit fresh Play failed authority/source boundary");auto target=character->presentation().find(first_actor());require(character->presentation().valid(target),"Player Kit target absent");double health=character->presentation().read(target).health.current,stamina=-1;for(const auto& attribute:character->ability()->attributes)if(attribute.id==3)stamina=attribute.value;require(health==75&&stamina==90&&editor.document->world().serialize()==import_baseline,"Player Kit fresh Play damage/cost/scene isolation failed");std::cout<<"Player Kit creation -> paired assignment/Undo/Redo -> private closure -> coordinated Save -> complete fresh Player scene/resources -> WorldSession light health75 stamina90 pending0\n";editor.stop();}
    if(!args.mask_editor.empty()){require(editor.assets&&shell.mask_editor==AssetId::parse(args.mask_editor)&&editor.authoring.drafts.contains(shell.mask_editor)&&editor.authoring.drafts.at(shell.mask_editor).value.at("kind")=="mask"&&shell.author_diagnostic.empty()&&!editor.authoring.dirty()&&!editor.preview.active(),"Mask Editor did not stay open on the mask draft, or opening it changed a source");std::cout<<"Mask Editor open mask="<<editor.authoring.drafts.at(shell.mask_editor).value.at("name").get<std::string>()<<" workspace="<<(shell.workspace==darkangel::editor_app::Workspace::Npc?"npc":shell.workspace==darkangel::editor_app::Workspace::Player?"player":"other")<<" selection_kept="<<((shell.workspace==darkangel::editor_app::Workspace::Npc?shell.npc_selection:shell.player_selection)!=shell.mask_editor?1:0)<<" dirty=0\n";}
    if(args.exercise_heavy&&!character&&!editor.console.empty())throw std::runtime_error(editor.console.back().message);
    
    
    if(args.exercise_native_navigation){require(editor.authoring.dirty()&&!editor.preview.active()&&!character&&editor.document->world().serialize()==import_baseline,"Native navigation changed gameplay or scene");for(const auto& [asset,draft]:editor.authoring.drafts){if(draft.pending)require(!std::filesystem::exists(editor.assets->source_root()/draft.asset.path),"Navigation published pending source");else require(file_sha256(editor.assets->source_root()/draft.asset.path)==sha256(draft.saved),"Navigation published edited source");}std::cout<<"Native typed navigation: Player/Character/input/kit/ability/action/graph/attributes, pending history retained, invalid route preserved, source and gameplay unchanged\n";}
    if(args.exercise_creation_drafts){require(editor.authoring.dirty()&&!editor.preview.active()&&!character&&composer_pose&&authoring_skin&&shell.composer_visible&&editor.document->world().serialize()==import_baseline,"Pending creation preview changed gameplay/scene");for(auto id:{shell.character_selection,shell.player_selection}){const auto& draft=editor.authoring.open(*editor.assets,id);require(draft.pending&&!std::filesystem::exists(editor.assets->source_root()/draft.asset.path),"Pending creation published source");}auto catalog=editor.assets->assets();for(auto id:{shell.character_selection,shell.player_selection})require(std::none_of(catalog.begin(),catalog.end(),[&](const auto& asset){return asset.id==id;}),"Pending creation registered catalog identity");std::cout<<"Creation draft Undo/Redo/Character preview joints="<<composer_matrices.size()<<" source absent, catalog unchanged, gameplay stopped\n";}
    if(args.exercise_history_view){require(editor.authoring.dirty()&&!editor.preview.active()&&!character&&editor.document->world().serialize()==import_baseline,"History view changed gameplay or scene");auto& draft=editor.authoring.open(*editor.assets,shell.authored_selection);require(file_sha256(editor.assets->source_root()/draft.asset.path)==sha256(draft.saved),"History view published source");std::cout<<"History pending cost21; source unchanged, scene unchanged, gameplay stopped\n";}
    
    if(args.exercise_graph_result){require(graph_pose&&graph_pose->inputs().count>0&&graph_pose->inputs().count<=4,"Missing bounded native graph result");float sum=0;for(const auto& layer:graph_pose->inputs().span()){require(layer.clip&&std::isfinite(layer.weight)&&layer.weight>=0,"Invalid native graph result layer");sum+=layer.weight;}require(std::abs(sum-1)<.001f,"Native graph result weights not normalized");std::cout<<"Native graph blend result clips="<<graph_pose->inputs().count<<" normalized="<<sum<<" tick="<<graph_pose->state().tick<<" gameplay=stopped\n";}
    if(args.exercise_graph_preview){require(graph_pose&&authoring_skin&&shell.composer_visible&&!editor.preview.active()&&!character&&!editor.authoring.dirty()&&editor.document->world().serialize()==import_baseline,"Graph preview changed gameplay/source state");auto& draft=editor.authoring.open(*editor.assets,shell.authored_selection);require(file_sha256(editor.assets->source_root()/draft.asset.path)==sha256(draft.saved),"Graph preview published source");std::cout<<"Graph isolated preview tick="<<graph_pose->state().tick<<" joints="<<composer_matrices.size()<<" gameplay=stopped source=unchanged\n";}
    if(args.exercise_character_preview){require(composer_pose&&authoring_skin&&shell.composer_visible&&!editor.preview.active()&&!character&&!editor.authoring.dirty()&&editor.document->world().serialize()==import_baseline,"Character preview changed gameplay/source state");auto& draft=editor.authoring.open(*editor.assets,shell.character_selection);require(file_sha256(editor.assets->source_root()/draft.asset.path)==sha256(draft.saved),"Character preview published source");std::cout<<"Authored Character preview tick="<<shell.composer_tick<<" joints="<<composer_matrices.size()<<" gameplay=stopped source=unchanged\n";}
    if(args.exercise_actor_workflow){require(character&&character->motor().tick==args.frames-4&&character->pending_prediction()==0,"Authored Player clock/correction failed");auto target=character->presentation().find(first_actor());double health=character->presentation().read(target).health.current,stamina=-1;for(const auto& value:character->ability()->attributes)if(value.id==3)stamina=value.value;require(health==(args.exercise_creation_vertical?83:75)&&stamina==(args.exercise_creation_vertical?88:args.exercise_player_defaults?50:90)&&editor.document->world().serialize()==import_baseline,"Authored Player native combat/isolation failed");if(args.exercise_player_defaults){double player_health=-1;for(const auto& value:character->ability()->attributes)if(value.id==2)player_health=value.value;require(player_health==80,"Authored Player starting Health did not replicate");std::cout<<"Player defaults owner_health="<<player_health<<" target_health="<<health<<" stamina="<<stamina<<" scene=unchanged\n";}if(args.exercise_graph_authoring)std::cout<<"Graph point edit -> Undo -> Redo -> frozen Save -> fresh native Player Play passed\n";if(args.exercise_player_input)std::cout<<"Player input Keyboard.K routed through native semantic light action; history and frozen Player override passed\n";if(args.exercise_creation_vertical){auto effects=character->effects(first_actor());require(effects&&effects->current()&&effects->current()->effects.size()==1&&effects->current()->effects[0].definition==shell.binding_effect&&effects->current()->effects[0].credit.source_network==4&&effects->current()->effects[0].credit.activation==1,"Created ordinary effect failed authoritative replication/attribution");auto ability=std::find_if(character_resources->combat->abilities.begin(),character_resources->combat->abilities.end(),[&](const auto& definition){return definition->id==shell.binding_ability;});require(ability!=character_resources->combat->abilities.end()&&(*ability)->action->id==shell.composer_asset&&(*ability)->deferred_commit_block,"Fresh Play retained stale Composer/commit references");std::cout<<"Created native vertical: six sources, private validation, coordinated Save, fresh scene/Player/Kit/Composer resources, deferred cost12 damage17 status source4/activation1, health83 stamina88 pending0\n";}std::cout<<"Authored Player fresh Play health="<<health<<" stamina="<<stamina<<" pending="<<character->pending_prediction()<<'\n';editor.stop();}
    if(args.exercise_upper_body){auto definition=std::find_if(character_resources->combat->abilities.begin(),character_resources->combat->abilities.end(),[](const auto& ability){return ability->action->upper_body;});require(definition!=character_resources->combat->abilities.end()&&(*definition)->action->mask&&!(*definition)->action->motion->motor_root&&character_resources->graph->active_layer_bound()<=3,"Masked editor did not prepare upper-body action");std::cout<<"Masked action frozen_generation="<<(*definition)->action->mask->generation<<" locomotion_bound="<<character_resources->graph->active_layer_bound()<<" total_bound="<<character_resources->graph->active_layer_bound()+1<<'\n';}
    if(args.exercise_death){require(bool(character)&&bool(character_resources->combat->death),"Death exercise needs a kit death timeline");auto target=character->presentation().find(first_actor());auto health=character->presentation().read(target).health.current;auto shown=character->death(first_actor());require(health<=0&&shown&&*shown>0&&!character->death(character_resources->player)&&character->pending_prediction()==0&&editor.document->world().serialize()==import_baseline,"Editor Game did not present the target death timeline");std::cout<<"Editor death tick="<<character->motor().tick<<" target_health="<<health<<" death_clip_tick="<<*shown<<" of "<<character_resources->combat->death->duration/darkangel::action_tick_units<<" player_death=none pending=0\n";editor.stop();}if(args.exercise_mask_switch){require(bool(character)&&character->ability()&&character->loadout()==2&&character->predicted_loadout()==2&&character->pending_prediction()==0&&character->pending_abilities()==0&&editor.document->world().serialize()==import_baseline,"Editor Game did not switch the active mask through authority and prediction");const auto& name=character_resources->mask_names[2];require(!name.empty()&&name!=character_resources->mask_names[0],"Mask switch exercise needs two named masks in slots Up and Down");
#ifdef DAE_HUD
        require(bool(hud)&&hud->model().mask==name&&hud->text("mask")==name,"Runtime HUD does not show the switched mask");
#endif
        std::cout<<"Editor mask switch tick="<<character->motor().tick<<" loadout="<<character->loadout()<<" mask="<<name<<" worn="<<(character_resources->masks[2]?1:0)<<" pending=0\n";editor.stop();}
#ifdef DAE_NAVIGATION
    if(args.exercise_navigation_overlay){
#ifdef DAE_NAVIGATION
        unsigned border=0;for(const auto& edge:navigation.outline)border+=edge.border?1:0;require(navigation.error.empty()&&bool(navigation.mesh)&&navigation.shown&&border>=3&&navigation.outline.size()>=navigation.mesh->polygons()&&navigation_drawn>0,navigation.error.empty()?"Editor Scene: the baked navigation mesh was not drawn over the viewport":navigation.error.c_str());
        std::cout<<"Editor navigation_overlay polygons="<<navigation.mesh->polygons()<<" edges="<<navigation.outline.size()<<" border="<<border<<" drawn="<<navigation_drawn<<"\n";
#else
        require(false,"This build has no navigation library");
#endif
    }
    if(args.exercise_navigation_bake){require(navigation.error.empty()&&bool(navigation.mesh)&&navigation.mesh->polygons()>0&&navigation.triangles>0&&character_resources&&character_resources->collision&&navigation.key==navigation_key(*character_resources->collision)&&shell.workspace==darkangel::editor_app::Workspace::Tools&&shell.tool==darkangel::editor_app::ToolView::Navigation,navigation.error.empty()?"Navigation tool did not bake the scene collision":navigation.error.c_str());
        // Play must reuse this bake for a session over the same collision, and fall back to an on-demand mesh otherwise.
        const auto before=navigation.reused;auto reused=navigation_for(*character_resources->collision);darkangel::CollisionDefinition other;other.id=darkangel::AssetId::random();other.schema=2;other.boxes={{1,{0,-.5,0},{8,.5,8}}};auto fresh=navigation_for(other);
        require(reused==navigation.mesh&&navigation.reused==before+1&&fresh!=navigation.mesh&&fresh->polygons()>0,"Play did not reuse the bake for unchanged collision or did not rebuild for different collision");
        std::cout<<"Navigation bake polygons="<<navigation.mesh->polygons()<<" source_triangles="<<navigation.triangles<<" play_reuses_bake=1 on_demand_for_other_collision=1\n";}
#endif
    if(args.exercise_ranged){require(bool(character)&&character->ability()&&bool(director),"Ranged exercise needs a combat session");const auto& kit=*character_resources->combat->kit;const auto ranged=kit.slots[unsigned(darkangel::CombatSlot::RangedLight)].ability;auto ability=std::find_if(character_resources->combat->abilities.begin(),character_resources->combat->abilities.end(),[&](const auto& definition){return definition->id==ranged;});
        auto target=character->presentation().find(first_actor());const double health=character->presentation().read(target).health.current;const auto seen=character->observer(first_actor(),double(character->motor().tick)).frame.motor.position;const double distance=std::hypot(seen.x-character->motor().position.x,seen.z-character->motor().position.z);
        require(ranged!=darkangel::AssetId{}&&ability!=character_resources->combat->abilities.end()&&(*ability)->aim&&director->rig().cameras[director->live()].facing==CameraFacing::Camera&&(health<100||!character->projectiles().empty())&&character->pending_prediction()==0&&editor.document->world().serialize()==import_baseline,"Editor Game did not hit the target with the aimed ranged ability through authority");
        std::cout<<"Editor ranged tick="<<character->motor().tick<<" camera="<<director->rig().cameras[director->live()].name<<" range="<<(*ability)->aim->range<<" distance="<<distance<<" target_health="<<health<<" aim_pitch="<<character->motor().pitch<<" projectile="<<((*ability)->aim->projectile?1:0)<<" in_flight="<<character->projectiles().size()<<" seen_in_flight="<<projectiles_seen<<" pending=0";
#ifdef DAE_HUD
        require(bool(hud)&&hud->model().crosshair&&hud->box("crosshair"),"Runtime HUD does not show the crosshair while aiming");std::cout<<" hud_crosshair=1";
#endif
        std::cout<<"\n";editor.stop();}
    if(args.exercise_soft_lock){require(bool(character)&&character->ability(),"Soft lock exercise needs a combat session");const auto host=character->motor(),predicted=character->predicted_motor();const auto seen=character->observer(first_actor(),double(host.tick)).frame.motor.position;const double toward=std::atan2(seen.x-host.position.x,seen.z-host.position.z),error=std::abs(std::remainder(host.yaw-toward,6.283185307179586)),distance=std::hypot(seen.x-host.position.x,seen.z-host.position.z);
        auto ability=std::find_if(character_resources->combat->abilities.begin(),character_resources->combat->abilities.end(),[&](const auto& definition){return definition->id==character->ability()->ability;});
        require(character->ability()->active&&ability!=character_resources->combat->abilities.end()&&(*ability)->soft_lock&&character->soft_lock()==first_actor()&&error<.05&&distance<2.5&&std::hypot(predicted.position.x-host.position.x,predicted.position.z-host.position.z)<1e-6&&std::abs(std::remainder(predicted.yaw-host.yaw,6.283185307179586))<1e-9&&character->pending_prediction()==0&&editor.document->world().serialize()==import_baseline,"Editor Game did not soft-lock the melee attack onto the target through authority and prediction");
        std::cout<<"Editor soft lock tick="<<host.tick<<" action_clock="<<character->ability()->action->clock/darkangel::action_tick_units<<" yaw_error="<<error<<" distance="<<distance<<" predicted_error=0 pending=0";
#ifdef DAE_HUD
        require(bool(hud)&&hud->model().target_locked&&hud->box("lock"),"Runtime HUD does not show the soft-lock indicator");std::cout<<" hud_lock=1";
#endif
        std::cout<<'\n';editor.stop();}
    if(args.exercise_spline){const auto built=build_splines();std::size_t points=0,expected=0;for(const auto& spline:built){points+=spline.points.size();if(spline.curve&&spline.definition.scatter)expected+=darkangel::scatter_along(*spline.curve,*spline.definition.scatter,spline.definition.closed).size();}
        require(!built.empty()&&splines_drawn==built.size()&&points>=2&&expected>=2&&scatter_drawn==expected,"Editor Scene: the scene's spline was not drawn, or its scattered models were not");std::cout<<"Editor spline splines="<<built.size()<<" points="<<points<<" length="<<(built.front().curve?built.front().curve->length():0.)<<" scatter="<<scatter_drawn<<"\n";}
    if(args.exercise_transition){require(bool(character)&&bool(travel)&&travel->scene(),"The transition exercise ended without a Play");const auto status=travel->status();unsigned used=0;for(const auto& [object,json]:editor.document->plan().interactables)if(const auto count=character->interactable_used(object))used+=*count;
        std::string phase=darkangel::transition_phase_text(status.phase);std::replace(phase.begin(),phase.end(),' ','_');auto failure=travel_failure;std::replace(failure.begin(),failure.end(),' ','_');
        std::cout<<"Editor transition tick="<<character->motor().tick<<" players="<<character->players().size()<<" arrivals="<<travel_arrivals<<" failures="<<travel_failures<<" phase="<<phase<<" scene="<<travel->scene()->scene.text()<<" document="<<editor.document->root().text()<<" authoring="<<authoring_scene.text()<<" lever_used="<<used<<" first_prompt="<<(character->interaction_prompt()?1:0)<<" overlay_frames="<<travel_overlay_frames<<" loading="<<(hud&&hud->model().loading?1:0)<<" ledger_records="<<play_ledger.records().size()<<" scripts="<<player_script_at<<"/"<<player_script.size()<<","<<script_at<<"/"<<script.size()<<" pending="<<character->pending_prediction()<<(failure.empty()?"":" failure="+failure.substr(0,160))<<"\n";
        const auto authoring=authoring_scene;editor.stop();require(editor.document->root()==authoring,"Stopping Play after travel did not bring the authoring scene back");}
    else if(args.exercise_two_players){require(bool(character)&&character->players().size()==2,"The two-player exercise needs a Play with two local players");const auto second_id=character->players()[1];const auto* first=character->client(character->players()[0]);const auto* second=character->client(second_id);const auto* outcome=second->last_interaction();unsigned used=0;for(const auto& [object,json]:editor.document->plan().interactables)if(const auto count=character->interactable_used(object))used+=*count;
        const auto& walked=*character->authority().motor(second_id);
        require(script_at==script.size()&&script_interactions==1&&outcome&&outcome->result==darkangel::InteractionResult::Accepted&&used==1&&!first->last_interaction()&&first->pose(second_id)&&second->pose(character_resources->player)&&first->pending_prediction()==0&&second->pending_prediction()==0&&shell.second_view&&scene_color_second&&hud_second_drawn,"Editor Game: the second local player did not walk to the lever and use it through authority, or is not shown from both clients");
        std::string heard;for(const auto& event:character->world_events().log())heard+=(heard.empty()?"":",")+event.name;
        std::cout<<"Editor two_players tick="<<character->motor().tick<<" players="<<character->players().size()<<" viewports=2 second_x="<<walked.position.x<<" second_z="<<walked.position.z<<" second_result="<<darkangel::interaction_result_text(outcome->result)<<" used="<<used<<" first_asked=0 events="<<heard<<"\n";}
    if(args.exercise_interaction){require(bool(character)&&!character->encounters().empty(),"Interaction exercise needs a combat session with an interactable and an encounter");const auto status=*character->encounter(character->encounters().front());const auto* outcome=character->last_interaction();unsigned used=0;for(const auto& [object,json]:editor.document->plan().interactables)if(const auto count=character->interactable_used(object))used+=*count;
        require(interaction_offered&&interactions_asked==1&&outcome&&outcome->result==darkangel::InteractionResult::Accepted&&used==1&&status.phase!=darkangel::EncounterPhase::Waiting&&!character->interaction_prompt(),"Editor Game: the interact input did not use the offered interactable through the server and start the encounter");
        std::string heard;for(const auto& event:character->world_events().log())heard+=(heard.empty()?"":",")+event.name;std::cout<<"Editor interaction asked="<<interactions_asked<<" result="<<darkangel::interaction_result_text(outcome->result)<<" used="<<used<<" spawned="<<status.spawned<<" events="<<heard<<"\n";}
    if(args.select_encounter){require(volumes_drawn>0,"Editor Scene: the encounter's volumes were not drawn");std::cout<<"Editor encounter_scene volumes_drawn="<<volumes_drawn<<"\n";}
    if(args.exercise_encounter){require(bool(character)&&!character->encounters().empty(),"Encounter exercise needs a combat session with an encounter");const auto status=*character->encounter(character->encounters().front());unsigned fired=0;for(const auto& [object,json]:editor.document->plan().triggers)if(const auto count=character->trigger_fired(object))fired+=*count;std::string heard;for(const auto& event:character->world_events().log())heard+=(heard.empty()?"":",")+event.name;
        require(fired>=1&&status.phase!=darkangel::EncounterPhase::Waiting&&status.spawned>=1&&character->actors().size()>=status.spawned&&volumes_drawn>0,"Editor Game: walking into the trigger volume did not start the encounter");
        // Encounter enemies have the same readouts as spawn-point NPCs (decision, senses, perception) and one of them is the HUD target.
        unsigned readouts=0;for(const auto id:character->actors())if(character->actor_encounter(id)){const auto* config=character->actor(id);require(config&&!config->name.empty()&&character->perception(id),"Editor Game: an encounter enemy has no configuration or perception to list");if(!character->controller_status(id).empty())++readouts;}
        const auto shown=choose_hud_target();require(readouts>=1&&shown&&character->actor_encounter(*shown)&&character->actor(*shown),"Editor Game: no encounter enemy has a decision readout, or none became the HUD target");
#ifdef DAE_HUD
        require(bool(hud)&&hud->model().target_name==character->actor(*shown)->name,"Runtime HUD does not name the encounter enemy as its target");
#endif
        std::cout<<"Editor encounter fired="<<fired<<" spawned="<<status.spawned<<" alive="<<status.alive<<" wave="<<status.wave<<" actors="<<character->actors().size()<<" volumes_drawn="<<volumes_drawn<<" player_z="<<character->motor().position.z<<" events="<<heard<<" hud_target="<<character->actor(*shown)->name<<" readouts="<<readouts<<"\n";}
#ifdef DAE_AUDIO
    if(args.exercise_audio){require(bool(audio),"The audio exercise ended without an audio service");const auto heard=audio->stats();const auto held=audio_director?audio_director->stats():darkangel::AudioDirectorStats{};const auto text=[](AssetId id){return id==AssetId{}?std::string("none"):id.text();};
        std::size_t waves{},definitions{},profiles{},mixers{};for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();waves+=extension==".wav";definitions+=extension==".dasound";profiles+=extension==".daaudioprofile";mixers+=extension==".damix";}
        std::cout<<"Editor audio workspace="<<(shell.workspace==darkangel::editor_app::Workspace::Audio?1:0)<<" waves="<<waves<<" definitions="<<definitions<<" profiles="<<profiles<<" mixers="<<mixers<<" driver="<<heard.driver<<" loaded="<<heard.sounds<<" playing="<<heard.playing<<" started="<<heard.started<<" audition="<<(audio->alive(audio_tab.audition)?1:0)
            <<" bus_music="<<heard.bus_gain[1]<<" bus_ambient="<<heard.bus_gain[2]<<" bus_npc="<<heard.bus_gain[4]<<" limit="<<(heard.limit?1:0)<<" director="<<(audio_director?1:0)<<" profile="<<text(held.profile)<<" music="<<text(held.music)<<" ambient="<<held.ambient<<" layers="<<held.layers<<" missing="<<held.missing<<"\n";}
#endif
    if(args.exercise_cue_form){require(bool(editor.assets)&&vfx_preview.cue!=AssetId{},"The cue form exercise did not make a cue");const auto registry=editor.assets->cas_path().parent_path()/("cue-form-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};const std::array roots{vfx_preview.cue};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);const auto cooked=darkangel::load_cooked_cues(registry,editor.assets->cas_path());require(cooked.size()==1,"The saved cue did not cook");
        std::size_t cues{};std::string path;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".dacue"){++cues;if(asset.id==vfx_preview.cue)path=asset.path;}
        std::cout<<"Editor cue_form cues="<<cues<<" path="<<path<<" saved="<<(editor.authoring.drafts.at(vfx_preview.cue).dirty()?0:1)<<" tail="<<cooked[0].tail_ticks<<" visual_resolved="<<(cooked[0].visual_asset!=AssetId{}?1:0)<<" audio_resolved="<<(cooked[0].audio_asset!=AssetId{}?1:0)<<"\n";}
    if(args.vfx_workspace){const auto drawn=vfx?vfx->stats():darkangel::VfxStats{};require(shell.workspace==darkangel::editor_app::Workspace::Vfx&&!character,"The VFX tab was left during its exercise");
        std::cout<<"Editor vfx_tab effects="<<vfx_effects().size()<<" selected="<<(vfx_preview.path.empty()?std::string("none"):vfx_preview.path)<<" cooked="<<(vfx_preview.cooked?1:0)<<" cooks="<<vfx_preview.cooks<<" textures="<<vfx_preview.textures<<" playing="<<(vfx&&vfx->alive(vfx_preview.handle)?1:0)<<" particles="<<drawn.instances<<" quads="<<drawn.quads<<" unsupported="<<drawn.unsupported<<" refused="<<(vfx_preview.error.empty()?0:1)<<"\n";if(!vfx_preview.error.empty())std::cout<<"Editor vfx_tab error: "<<vfx_preview.error<<"\n";}
    if(args.exercise_cues){require(bool(character)&&bool(cue_player)&&bool(play_cues),"Cue exercise needs a combat session with a cue player");const auto list=[](const std::vector<std::string>& keys){std::string all;for(const auto& key:keys)all+=(all.empty()?"":",")+key;return all.empty()?std::string("none"):all;};
        // What the sinks hold is exactly what the player holds for them, and nothing was dropped.
        std::size_t visual_live=0,audio_live=0;for(const auto& [instance,held]:cue_visual.held()){(void)instance;require(character->identity(held.network).has_value(),"A live visual cue names an actor that is not in the session");++visual_live;}for(const auto& [instance,held]:cue_audio.held()){(void)instance;require(character->identity(held.network).has_value(),"A live audio cue names an actor that is not in the session");++audio_live;}
        require(cue_player->dropped()==0&&cue_player->live()>=std::max(visual_live,audio_live)&&cue_visual.starts()==cue_visual.releases()+visual_live&&cue_audio.starts()==cue_audio.releases()+audio_live,"Cue sinks and the cue player disagree about what is live");
        std::cout<<"Editor cues tick="<<character->motor().tick<<" assets="<<play_cues->size()<<" closure="<<play_cue_closure<<" live="<<cue_player->live()<<" visual_live="<<visual_live<<" audio_live="<<audio_live<<" refused="<<(args.exercise_reject?1:0)<<" visual_starts="<<cue_visual.starts()<<" audio_starts="<<cue_audio.starts()<<" audio_stops="<<cue_audio.stops()<<" unresolved="<<cue_player->unresolved()<<" visual="<<list(cue_visual.started())<<" audio="<<list(cue_audio.started());{const auto drawn=vfx?vfx->stats():darkangel::VfxStats{};std::cout<<" vfx_loaded="<<drawn.effects<<" vfx_playing="<<drawn.playing<<" vfx_particles="<<drawn.instances<<" vfx_quads="<<drawn.quads<<" vfx_draws="<<drawn.draws<<" vfx_unsupported="<<drawn.unsupported<<" vfx_missing="<<cue_effects.missing()<<" trails="<<cue_primitives.trails.live()<<" trail_points="<<cue_primitives.trails.points()<<" decals="<<cue_primitives.decals.live()<<" primitive_quads="<<drawn.primitives<<" primitive_missing="<<cue_primitives.missing();{std::cout<<" effect_generations=";bool first=true;for(const auto& [name,generation]:play_effect_generations){std::cout<<(first?"":";")<<name<<":"<<generation;first=false;}if(first)std::cout<<"none";}}
#ifdef DAE_AUDIO
        {std::cout<<" sound_generations=";bool first=true;for(const auto& [name,id]:play_sounds){std::cout<<(first?"":";")<<name<<":"<<(audio_generations.contains(id)?audio_generations.at(id).substr(0,12):std::string("none"));first=false;}if(first)std::cout<<"none";}{const auto heard=audio?audio->stats():darkangel::AudioStats{};std::cout<<" audio_service="<<(audio?1:0)<<" audio_device="<<(heard.device?1:0)<<" audio_loaded="<<heard.sounds<<" audio_played="<<heard.started<<" audio_missing="<<cue_sounds.missing();}
#endif
        std::cout<<"\n";}
    if(args.select_spawn){require(senses_drawn>0,"Editor Scene: the selected spawn point's senses were not drawn");std::cout<<"Editor senses_scene drawn="<<senses_drawn<<"\n";}
    if(args.exercise_senses){require(bool(character),"Senses exercise needs a combat session");const auto* perception=character->perception(first_actor());require(perception&&!senses_early&&senses_noticed&&senses_drawn>0,"Editor Game: the guard noticed the player before it walked into the cone, never noticed it, or its senses were not drawn");std::cout<<"Editor senses noticed_tick="<<senses_noticed<<" noticed_z="<<senses_noticed_z<<" sensors="<<perception->senses().sensors.size()<<" perceived="<<perception->actors().size()<<" senses_drawn="<<senses_drawn<<"\n";}
    if(args.exercise_attacked&&!args.exercise_encounter&&!args.exercise_interaction){require(bool(character)&&character->ability()&&!behavior_running.empty(),"Attacked exercise needs a combat session with a spawned NPC that runs a behaviour");double health=-1;for(const auto& attribute:character->ability()->attributes)if(attribute.id==character->ability()->health_attribute)health=attribute.value;auto receipt=character->controller_receipt(first_actor());auto reaction=character->reaction(character_resources->player);auto death=character->death(character_resources->player);require(receipt&&health>=0&&health<100&&character->pending_prediction()==0&&editor.document->world().serialize()==import_baseline,"Editor Game: no spawned NPC damaged the player through authority");std::cout<<"Editor attacked tick="<<character->motor().tick<<" player_health="<<health<<" reaction_tick="<<(reaction&&reaction->suppressed==darkangel::ReactionSuppression::None?reaction->tick:-1.)<<" death_tick="<<(death?*death:-1.)<<" pending=0 actors="<<character->actors().size()<<" nav_polygons="<<navigation_polygons;if(!behavior_running.empty()){auto shown=behavior_running;std::replace(shown.begin(),shown.end(),' ','_');std::cout<<" behavior="<<shown;}{auto decision=character->controller_status(first_actor());if(!decision.empty()){std::replace(decision.begin(),decision.end(),' ','_');std::cout<<" decision="<<decision;}}
#ifdef DAE_HUD
        {require(bool(hud)&&hud_renderer.drawn()>0&&hud->model().health==health,"Editor Game did not draw the runtime HUD from confirmed Health");auto shown=hud->text("health-text");std::erase(shown,' ');std::cout<<" hud_health="<<shown;auto target_shown=hud->text("target-text");std::erase(target_shown,' ');auto named=hud->text("target-name");std::replace(named.begin(),named.end(),' ','_');std::cout<<" hud_target="<<target_shown<<" hud_target_name="<<named;}
#endif
        std::cout<<'\n';editor.stop();}if(args.exercise_dodge){require(bool(character)&&character->ability(),"Dodge exercise needs a combat session");const auto& kit=*character_resources->combat->kit;auto dodge=kit.slots[unsigned(darkangel::CombatSlot::Dodge)].ability;double stamina=-1;for(const auto& attribute:character->ability()->attributes)if(attribute.id==3)stamina=attribute.value;auto host=character->motor().position,predicted=character->predicted_motor().position;require(dodge!=darkangel::AssetId{}&&character->ability()->active&&character->ability()->ability==dodge&&stamina==85&&character->pending_prediction()==0&&std::hypot(predicted.x-host.x,predicted.z-host.z)<1e-6&&editor.document->world().serialize()==import_baseline,"Editor Game did not run the authored dodge through authority and prediction");std::cout<<"Editor dodge tick="<<character->motor().tick<<" action_clock="<<character->ability()->action->clock/darkangel::action_tick_units<<" stamina="<<stamina<<" yaw="<<character->motor().yaw<<" position="<<host.x<<","<<host.z<<" predicted_error=0 pending=0\n";editor.stop();}if(args.exercise_aim){require(bool(character)&&bool(director),"Aim exercise needs a Play session");auto target=character->presentation().find(first_actor());const auto& live=director->rig().cameras[director->live()];const auto& pose=director->pose();const auto position=character->motor().position;const double reach=std::hypot(std::hypot(pose.position[0]-position.x,pose.position[1]-position.y),pose.position[2]-position.z);
            require(live.name=="Aim"&&live.facing==CameraFacing::Camera&&director->blend()==1&&std::abs(pose.fov_degrees-live.fov_degrees)<1e-9&&reach<4&&std::abs(std::remainder(character->motor().yaw-director->yaw(),6.283185307179586))<.35&&std::abs(character->motor().pitch)<1.4&&character->presentation().read(target).health.current==100&&character->pending_prediction()==0&&editor.document->world().serialize()==import_baseline,"Editor Game did not blend to the aim camera while the ranged modifier was held");
            std::cout<<"Editor camera live="<<live.name<<" blend="<<director->blend()<<" fov="<<pose.fov_degrees<<" camera_distance="<<reach<<" character_faces_camera=1 aim_yaw_offset="<<std::remainder(character->motor().yaw-director->yaw(),6.283185307179586)<<" aim_pitch="<<character->motor().pitch<<" pending=0\n";editor.stop();}if(args.exercise_camera_look){require(bool(character)&&bool(director),"Look exercise needs Play");require(director->rig().cameras[director->live()].name=="FreeLook"&&std::abs(director->yaw()+.21)<1e-6&&std::abs(director->pitch()-(12*3.141592653589793/180-.105))<1e-6&&character->pending_prediction()==0,"Scripted right/up mouse delta did not orbit right/up in FreeLook");std::cout<<"Camera scripted mouse right/up yaw="<<director->yaw()<<" pitch="<<director->pitch()<<" pending=0\n";editor.stop();}if(args.exercise_heavy&&!args.exercise_soft_lock&&!args.exercise_ranged&&!args.exercise_death&&!args.exercise_attacked&&!args.exercise_mask_switch&&!args.exercise_dodge&&!args.exercise_aim&&!args.exercise_camera_look&&!args.exercise_two_players){require(character&&character->motor().tick==args.frames&&character->pending_prediction()==0&&character->ability()&&!character->ability()->reservation,"Native heavy editor clocks/claims failed");auto target=character->presentation().find(first_actor());double health=character->presentation().read(target).health.current,stamina=-1;for(const auto& attribute:character->ability()->attributes)if(attribute.id==3)stamina=attribute.value;auto status=character->effects(first_actor());require(health==((args.exercise_authoring||args.exercise_upper_body)?(args.frames==30?60:39):(args.frames==30?65:55))&&stamina==((args.exercise_authoring||args.exercise_upper_body)?80:70)&&status&&status->cues().size()==(args.frames==30?1:0)&&editor.document->world().serialize()==import_baseline,"Native heavy editor damage/cost/status/isolation failed");std::cout<<"Native heavy editor tick="<<character->motor().tick<<" target_health="<<health<<" stamina="<<stamina<<" pending="<<character->pending_prediction()<<'\n';if(args.exercise_reaction){auto reaction=character->reaction(first_actor());require(reaction&&reaction->suppressed==darkangel::ReactionSuppression::None&&reaction->tick==double(character->motor().tick-reaction->start)&&!character->reaction(character_resources->player)&&status->current()&&status->current()->effects.size()==2,"Editor Game did not present the authored target reaction beside Burn");std::cout<<"Editor reaction effect_handle="<<reaction->key.handle<<" onset="<<reaction->start<<" clip_tick="<<reaction->tick<<" of "<<reaction->action->duration/darkangel::action_tick_units<<" player_reaction=none effects=2\n";}editor.stop();}
    if(args.exercise_combat){require(character&&character->motor().tick==args.frames&&character->pending_prediction()==0&&character->ability(),"Native combat editor clocks/correction failed");auto target=character->presentation().find(first_actor());require(character->presentation().valid(target)&&character->presentation().read(target).health.current==(args.exercise_reject?100:args.frames>=61?50:75),args.exercise_reject?"A refused press damaged the target":"Native combat editor target damage failed");double stamina=-1;for(const auto& attribute:character->ability()->attributes)if(attribute.id==3)stamina=attribute.value;require(stamina==(args.exercise_reject?100:args.frames>=61?80:90)&&editor.document->world().serialize()==import_baseline,args.exercise_reject?"A refused press left its cost spent":"Native combat cost/isolation failed");std::cout<<"Native combat editor tick="<<character->motor().tick<<" target_health="<<character->presentation().read(target).health.current<<" stamina="<<stamina<<" pending="<<character->pending_prediction()<<'\n';editor.stop();}
    if(args.exercise_character){require(character&&character->motor().tick==args.frames&&character->motor().position.z>4,"Native character editor exercise did not move at fixed ticks");if(args.exercise_workspaces)std::cout<<"Workspace transitions preserved authoring and resumed after neutral input\n";std::cout<<"Native character editor tick="<<character->motor().tick<<" foot="<<character->motor().position.x<<','<<character->motor().position.y<<','<<character->motor().position.z<<" graph_phase="<<character->graph().phase<<" pending="<<character->pending_prediction()<<'\n';editor.stop();}
#endif
#ifdef DAE_EDITOR_GNS
    party_receipt(frame);
#endif
    #ifdef DAE_CHARACTER_SCENE
    authoring_source_exercise->finish();
#endif
    context->Flush();context->WaitForIdle();window_gui=nullptr;gui.reset();if(IsWindow(window))DestroyWindow(window);std::cout<<"DarkAngel editor rendered "<<frame<<" frames with "<<args.backend<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"DarkAngelEditor: "<<e.what()<<'\n';return 1;}}
