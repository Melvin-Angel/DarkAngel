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
#include <darkangel/editor_service.hpp>
#include "editor_ui.hpp"
#include "editor_theme.hpp"
#ifdef DAE_AGENT_ENDPOINTS
#include <darkangel/editor_rpc.hpp>
#endif
#include <ImGuizmo.h>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

using namespace Diligent;
using namespace darkangel;
namespace {
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
struct ComApartment {ComApartment(){require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Editor COM initialization failed");}~ComApartment(){DirectX::SetWICFactory(nullptr);CoUninitialize();}};
struct Options {std::filesystem::path registry,cas,capture,agent_descriptor;std::string agent_project;bool agent_authoring{};std::string model,backend{"d3d12"};unsigned frames{},width{1280},height{800};bool hidden{},exercise{};};
Options options(int argc,char** argv){Options out;for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--agent-authoring"){out.agent_authoring=true;continue;}if(arg=="--hidden"){out.hidden=true;continue;}if(arg=="--exercise"){out.exercise=true;continue;}require(i+1<argc,"Missing editor option value");std::string value=argv[++i];if(arg=="--width")out.width=static_cast<unsigned>(std::stoul(value));else if(arg=="--height")out.height=static_cast<unsigned>(std::stoul(value));else if(arg=="--agent-descriptor")out.agent_descriptor=value;else if(arg=="--agent-project")out.agent_project=StableId::parse(value).text();else if(arg=="--registry")out.registry=value;else if(arg=="--cas")out.cas=value;else if(arg=="--model")out.model=value;else if(arg=="--capture")out.capture=value;else if(arg=="--frames")out.frames=static_cast<unsigned>(std::stoul(value));else if(arg=="--backend")out.backend=value;else throw std::runtime_error("Unknown editor option");}require(!out.registry.empty() && !out.cas.empty() && !out.model.empty(),"DarkAngelEditor requires --registry file --cas directory --model UUID");require(out.backend=="d3d12" || out.backend=="vulkan","Unsupported backend");require(out.width>=640 && out.width<=3840 && out.height>=480 && out.height<=2160,"Editor window size bounds");require(out.frames<=10000,"Frame budget limit");require(out.agent_descriptor.empty()==out.agent_project.empty() && (!out.agent_authoring || !out.agent_descriptor.empty()),"Agent requires explicit descriptor and project UUID");
#ifndef DAE_AGENT_ENDPOINTS
require(out.agent_descriptor.empty(),"This build excludes agent endpoints");
#endif
return out;}
ImGuiImplWin32* window_gui{};unsigned resize_width{},resize_height{};
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    if(window_gui && window_gui->Win32_ProcHandler(window,message,wparam,lparam))return 1;
    if(message==WM_SIZE){resize_width=LOWORD(lparam);resize_height=HIWORD(lparam);return 0;}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,wparam,lparam);
}
struct Constants {DirectX::XMFLOAT4X4 mvp,rotation;DirectX::XMFLOAT4 color,surface;};
struct GpuMesh {RefCntAutoPtr<IBuffer> vertices,indices;std::vector<MeshPart> parts;};
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
RefCntAutoPtr<IPipelineState> pipeline(IRenderDevice* device,IBuffer* constants,TEXTURE_FORMAT color,TEXTURE_FORMAT depth,bool cull,const char* source=shader_source){
    ShaderCreateInfo shader;shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_HLSL;shader.ShaderCompiler=SHADER_COMPILER_DXC;shader.HLSLVersion={6,0};shader.Desc.UseCombinedTextureSamplers=true;shader.Source=source;
    RefCntAutoPtr<IShader> vs,ps;shader.Desc.Name="DarkAngel stylized prop VS";shader.Desc.ShaderType=SHADER_TYPE_VERTEX;shader.EntryPoint="VSMain";device->CreateShader(shader,&vs);
    shader.Desc.Name="DarkAngel stylized prop PS";shader.Desc.ShaderType=SHADER_TYPE_PIXEL;shader.EntryPoint="PSMain";device->CreateShader(shader,&ps);require(vs && ps,"Pinned DXC shader compilation failed");
    GraphicsPipelineStateCreateInfo ci;ci.PSODesc.Name="DarkAngel stylized static prop";ci.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;ci.pVS=vs;ci.pPS=ps;
    auto& graphics=ci.GraphicsPipeline;graphics.NumRenderTargets=1;graphics.RTVFormats[0]=color;graphics.DSVFormat=depth;graphics.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode=cull?CULL_MODE_BACK:CULL_MODE_NONE;graphics.RasterizerDesc.FrontCounterClockwise=true;graphics.DepthStencilDesc.DepthEnable=true;graphics.DepthStencilDesc.DepthFunc=COMPARISON_FUNC_LESS;
    LayoutElement layout[]={{0,0,3,VT_FLOAT32,false},{1,0,3,VT_FLOAT32,false},{2,0,2,VT_FLOAT32,false}};graphics.InputLayout.LayoutElements=layout;graphics.InputLayout.NumElements=3;
    ShaderResourceVariableDesc variables[]={{SHADER_TYPE_PIXEL,"Albedo",SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};ci.PSODesc.ResourceLayout.Variables=variables;ci.PSODesc.ResourceLayout.NumVariables=1;
    SamplerDesc sampler;sampler.MinFilter=FILTER_TYPE_LINEAR;sampler.MagFilter=FILTER_TYPE_LINEAR;sampler.MipFilter=FILTER_TYPE_LINEAR;sampler.AddressU=TEXTURE_ADDRESS_WRAP;sampler.AddressV=TEXTURE_ADDRESS_WRAP;
    ImmutableSamplerDesc samplers[]={{SHADER_TYPE_PIXEL,"Albedo",sampler}};ci.PSODesc.ResourceLayout.ImmutableSamplers=samplers;ci.PSODesc.ResourceLayout.NumImmutableSamplers=1;
    RefCntAutoPtr<IPipelineState> result;device->CreateGraphicsPipelineState(ci,&result);require(result,"Pipeline creation failed");
    auto* vertex_frame=result->GetStaticVariableByName(SHADER_TYPE_VERTEX,"Frame");auto* pixel_frame=result->GetStaticVariableByName(SHADER_TYPE_PIXEL,"Frame");require(vertex_frame && pixel_frame,"Shader changed the declared Frame resource contract");vertex_frame->Set(constants);pixel_frame->Set(constants);return result;
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
    require(changed>2000,"Viewport did not draw foreground geometry");std::filesystem::create_directories(path.parent_path());DirectX::Image image{desc.Width,desc.Height,DXGI_FORMAT_R8G8B8A8_UNORM,static_cast<std::size_t>(desc.Width)*4,pixels.size(),pixels.data()};require(SUCCEEDED(DirectX::SaveToWICFile(image,DirectX::WIC_FLAGS_NONE,GUID_ContainerFormatPng,path.c_str())),"PNG capture failed");std::cout<<"Viewport foreground pixels="<<changed<<" capture="<<path.string()<<'\n';
}
struct Material {CookedMaterial value;RefCntAutoPtr<IShaderResourceBinding> binding;};
struct UploadedModel {
    std::vector<GpuMesh> meshes;std::map<AssetId,RefCntAutoPtr<ITexture>> textures;
    std::map<AssetId,Material> materials;RefCntAutoPtr<ITexture> fallback;
    std::array<float,3> center{};float radius{};
};
UploadedModel upload(IRenderDevice* device,const RuntimeModel& model,IPipelineState* pso,IPipelineState* pso_cull){
    require(!model.meshes.empty(),"Empty cooked model");UploadedModel out;auto minimum=model.meshes[0].minimum,maximum=model.meshes[0].maximum;
    for(const auto& mesh:model.meshes){GpuMesh gpu;gpu.parts=mesh.parts;BufferDesc desc;desc.Name="Cooked static vertices";desc.Usage=USAGE_IMMUTABLE;desc.BindFlags=BIND_VERTEX_BUFFER;desc.Size=mesh.vertices.size()*sizeof(Vertex);BufferData data{mesh.vertices.data(),desc.Size};device->CreateBuffer(desc,&data,&gpu.vertices);desc.Name="Cooked static indices";desc.BindFlags=BIND_INDEX_BUFFER;desc.Size=mesh.indices.size()*sizeof(std::uint32_t);data={mesh.indices.data(),desc.Size};device->CreateBuffer(desc,&data,&gpu.indices);require(gpu.vertices && gpu.indices,"Mesh upload failed");out.meshes.push_back(std::move(gpu));for(unsigned c=0;c<3;++c){minimum[c]=std::min(minimum[c],mesh.minimum[c]);maximum[c]=std::max(maximum[c],mesh.maximum[c]);}}
    CookedTexture white;white.mips.push_back({1,1,{255,255,255,255}});out.fallback=texture(device,white);for(const auto& [id,cooked]:model.textures)out.textures.emplace(id,texture(device,cooked));
    for(const auto& material:model.materials){Material gpu;gpu.value=material;auto* chosen=material.double_sided?pso:pso_cull;chosen->CreateShaderResourceBinding(&gpu.binding,true);require(gpu.binding,"Material binding creation failed");auto* variable=gpu.binding->GetVariableByName(SHADER_TYPE_PIXEL,"Albedo");require(variable,"Shader changed the declared Albedo resource contract");auto* view=(material.has_texture?out.textures.at(material.texture):out.fallback)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);variable->Set(view);out.materials.emplace(material.id,std::move(gpu));}
    for(unsigned c=0;c<3;++c){out.center[c]=(minimum[c]+maximum[c])*.5f;out.radius=std::max(out.radius,(maximum[c]-minimum[c])*.5f);}require(out.radius>1e-6f,"Model has zero bounds");return out;
}
std::string shader_text(){std::ifstream in("games/AshenRoots/shaders/stylized_prop.hlsl",std::ios::binary|std::ios::ate);if(!in)return shader_source;require(in.tellg()>=0 && in.tellg()<=256*1024,"Shader source byte limit");in.seekg(0);return {std::istreambuf_iterator<char>(in),{}};}

}
int main(int argc,char** argv){try{
    auto args=options(argc,argv);auto id=AssetId::parse(args.model);ModelStore store;auto lease=store.prepare(args.registry,args.cas,id);store.publish(lease);
    ComApartment apartment;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DarkAngelM2Editor";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);require(RegisterClassW(&wc),"Window class registration failed");
    RECT rect{0,0,static_cast<LONG>(args.width),static_cast<LONG>(args.height)};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);HWND window=CreateWindowW(wc.lpszClassName,L"DarkAngel Editor",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,wc.hInstance,nullptr);require(window,"Editor window creation failed");
    RefCntAutoPtr<IRenderDevice> device;RefCntAutoPtr<IDeviceContext> context;RefCntAutoPtr<ISwapChain> swap;
    SwapChainDesc swap_desc;swap_desc.Width=args.width;swap_desc.Height=args.height;swap_desc.ColorBufferFormat=TEX_FORMAT_RGBA8_UNORM_SRGB;swap_desc.DepthBufferFormat=TEX_FORMAT_D32_FLOAT;NativeWindow native(window);
    if(args.backend=="d3d12"){auto* factory=GetEngineFactoryD3D12();EngineD3D12CreateInfo ci;ci.pDxCompilerPath=DAE_DXC_DLL;ci.EnableValidation=true;factory->CreateDeviceAndContextsD3D12(ci,&device,&context);require(device && context,"D3D12 device initialization failed");factory->CreateSwapChainD3D12(device,context,swap_desc,FullScreenModeDesc{},native,&swap);}
    else{auto* factory=GetEngineFactoryVk();EngineVkCreateInfo ci;ci.pDxCompilerPath=DAE_DXC_DLL;ci.EnableValidation=true;factory->CreateDeviceAndContextsVk(ci,&device,&context);require(device && context,"Vulkan device initialization failed");factory->CreateSwapChainVk(device,context,swap_desc,native,&swap);}require(swap,"Swap chain creation failed");
    auto gui=ImGuiImplWin32::Create(ImGuiDiligentCreateInfo{device,swap->GetDesc()},window);window_gui=gui.get();ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_DockingEnable;ImGui::GetIO().IniFilename=nullptr;
    darkangel::editor_app::initialize_theme(GetDpiForWindow(window)/96.f);if(!args.hidden)ShowWindow(window,SW_SHOW);
    BufferDesc cb;cb.Name="DarkAngel frame constants";cb.Size=sizeof(Constants);cb.Usage=USAGE_DYNAMIC;cb.BindFlags=BIND_UNIFORM_BUFFER;cb.CPUAccessFlags=CPU_ACCESS_WRITE;RefCntAutoPtr<IBuffer> constant_buffer;device->CreateBuffer(cb,nullptr,&constant_buffer);require(constant_buffer,"Constant buffer creation failed");
    auto source=shader_text();auto pso=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,false,source.c_str());auto pso_cull=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,true,source.c_str());
    auto gpu=upload(device,*lease.model,pso,pso_cull);darkangel::editor_app::Controller editor(id,gpu.radius);darkangel::editor_app::Shell shell;
#ifdef DAE_AGENT_ENDPOINTS
std::unique_ptr<AgentEndpoint> agent;EditorDocument* agent_document{};auto poll_agent=[&]{if(args.agent_descriptor.empty())return;if(agent_document!=editor.document.get()){agent.reset();agent=std::make_unique<AgentEndpoint>(*editor.document,args.agent_project,args.agent_authoring,args.agent_descriptor);agent_document=editor.document.get();editor.log("Issued scoped agent descriptor for this authoring document.");}agent->poll();};
#endif
    auto reload_model=[&]{auto candidate=store.prepare(args.registry,args.cas,id);auto resources=upload(device,*candidate.model,pso,pso_cull);context->Flush();context->WaitForIdle();store.publish(candidate);gpu=std::move(resources);lease=store.acquire();store.collect();editor.log("Published resource generation after GPU upload; retired unpinned data at GPU idle.");};
    auto reload_shader=[&](const std::string& text){auto next=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,false,text.c_str());auto next_cull=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,true,text.c_str());auto resources=upload(device,*lease.model,next,next_cull);context->Flush();context->WaitForIdle();gpu=std::move(resources);pso=std::move(next);pso_cull=std::move(next_cull);editor.log("Shader candidate and material bindings committed at GPU idle.");};
    RefCntAutoPtr<ITexture> scene_color,scene_depth;unsigned scene_width=700,scene_height=500;auto last=std::chrono::steady_clock::now();
    bool running=true;unsigned frame{};while(running && (!args.frames || frame<args.frames)){
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)running=false;TranslateMessage(&message);DispatchMessageW(&message);}if(!running)break;
        if(resize_width && resize_height){context->WaitForIdle();swap->Resize(resize_width,resize_height);resize_width=resize_height=0;}
        auto now=std::chrono::steady_clock::now();auto seconds=std::chrono::duration<double>(now-last).count();last=now;seconds=std::clamp(seconds,0.,.1);
        if(args.exercise && frame==1){auto baseline=editor.document->world().serialize();editor.play();editor.step();require(editor.document->world().serialize()==baseline,"Play mutated authoring data");editor.stop();auto selected=editor.selected;auto handle=editor.document->world().find(selected);PropertyChange change{selected,1,1,.2};auto command=editor.document->prepare(editor.document->revision(),{&change,1});editor.document->commit(command);editor.document->undo(editor.document->revision());editor.document->redo(editor.document->revision());editor.save(".cache/editor/AcceptanceScene.dascene");editor.open(".cache/editor/AcceptanceScene.dascene");require(!editor.document->world().valid(handle),"Open/restart reused old world handle");
            auto generation=store.acquire().number;bool failed=false;try{store.prepare(".cache/editor/missing.registry",args.cas,id);}catch(...){failed=true;}require(failed && store.acquire().number==generation,"Failed model candidate replaced active generation");reload_model();failed=false;try{reload_shader("deliberate shader syntax failure");}catch(...){failed=true;}require(failed,"Invalid shader candidate accepted");editor.log("Acceptance: Play isolation, Step, undo/redo, save/open, model generation and failed shader preservation passed.");}
        editor.update(seconds);const auto& desc=swap->GetDesc();
        if(!scene_color || scene_color->GetDesc().Width!=scene_width || scene_color->GetDesc().Height!=scene_height){context->Flush();context->WaitForIdle();TextureDesc color;color.Name="Editor scene viewport";color.Type=RESOURCE_DIM_TEX_2D;color.Width=scene_width;color.Height=scene_height;color.Format=swap_desc.ColorBufferFormat;color.BindFlags=BIND_RENDER_TARGET|BIND_SHADER_RESOURCE;device->CreateTexture(color,nullptr,&scene_color);color.Name="Editor scene depth";color.Format=swap_desc.DepthBufferFormat;color.BindFlags=BIND_DEPTH_STENCIL;device->CreateTexture(color,nullptr,&scene_depth);require(scene_color && scene_depth,"Viewport resource creation failed");}
        darkangel::editor_app::set_theme_dpi(GetDpiForWindow(window)/96.f);gui->NewFrame(desc.Width,desc.Height,desc.PreTransform);ImGuizmo::BeginFrame();auto image=reinterpret_cast<ImTextureID>(scene_color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));auto area=shell.draw(editor,desc.Width,desc.Height,args.backend.c_str(),lease.number,seconds,image);
#ifdef DAE_AGENT_ENDPOINTS
poll_agent();
#endif
scene_width=std::max(1u,static_cast<unsigned>(area.width));scene_height=std::max(1u,static_cast<unsigned>(area.height));
        if(shell.reload_model){try{reload_model();}catch(const std::exception& error){context->Flush();context->WaitForIdle();editor.log(error.what(),darkangel::editor_app::ConsoleSeverity::Error);}shell.reload_model=false;}
        if(shell.reload_shader){try{reload_shader(shader_text());}catch(const std::exception& error){context->Flush();context->WaitForIdle();editor.log(error.what(),darkangel::editor_app::ConsoleSeverity::Error);}shell.reload_shader=false;}
        auto* rtv=scene_color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);auto* dsv=scene_depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);context->SetRenderTargets(1,&rtv,dsv,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);const float background[]={.015f,.025f,.045f,1};context->ClearRenderTarget(rtv,background,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearDepthStencil(dsv,CLEAR_DEPTH_FLAG,1,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport;viewport.Width=static_cast<float>(scene_color->GetDesc().Width);viewport.Height=static_cast<float>(scene_color->GetDesc().Height);context->SetViewports(1,&viewport,scene_color->GetDesc().Width,scene_color->GetDesc().Height);
        using namespace DirectX;auto view=XMMatrixLookAtRH(XMVectorSet(0,gpu.radius*.2f,gpu.radius*std::max(3.8f,2.4f/(.4142f*viewport.Width/viewport.Height)),1),XMVectorZero(),XMVectorSet(0,1,0,0));auto projection=XMMatrixPerspectiveFovRH(XM_PIDIV4,viewport.Width/viewport.Height,gpu.radius*.01f,gpu.radius*40);XMFLOAT4X4 view_matrix,projection_matrix;XMStoreFloat4x4(&view_matrix,view);XMStoreFloat4x4(&projection_matrix,projection);if(!args.hidden)shell.gizmo(editor,view_matrix,projection_matrix);
        for(const auto& [object_id,model_id]:editor.document->plan().models){auto entity=editor.displayed().find(object_id);if(!editor.displayed().valid(entity))continue;auto pose=editor.displayed().read(entity).transform;if(shell.draft && editor.selected==object_id)pose=*shell.draft;
            auto rotation=XMMatrixRotationRollPitchYaw(pose.pitch,pose.yaw,pose.roll);auto world=XMMatrixTranslation(-gpu.center[0],-gpu.center[1],-gpu.center[2])*XMMatrixScaling(pose.scale,pose.scale,pose.scale)*rotation*XMMatrixTranslation(pose.x,pose.y,pose.z);
            for(const auto& mesh:gpu.meshes){IBuffer* vb=mesh.vertices;Uint64 offset{};context->SetVertexBuffers(0,1,&vb,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);context->SetIndexBuffer(mesh.indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                for(const auto& part:mesh.parts){const auto& material=gpu.materials.at(part.material);Constants constants;XMStoreFloat4x4(&constants.mvp,world*view*projection);XMStoreFloat4x4(&constants.rotation,rotation);constants.color={material.value.color[0],material.value.color[1],material.value.color[2],material.value.color[3]};constants.surface={material.value.roughness,material.value.metallic,0,0};void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Uniform buffer mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);
                    context->SetPipelineState(material.value.double_sided?pso.RawPtr():pso_cull.RawPtr());context->CommitShaderResources(material.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);DrawIndexedAttribs draw;draw.NumIndices=part.count;draw.IndexType=VT_UINT32;draw.FirstIndexLocation=part.first;draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(draw);}
            }
        }
        auto* back=swap->GetCurrentBackBufferRTV();context->SetRenderTargets(1,&back,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearRenderTarget(back,background,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);gui->Render(context);if(!args.capture.empty() && args.frames && frame+1==args.frames)capture(device,context,back->GetTexture(),args.capture,area);swap->Present(args.hidden?0:1);++frame;
    }
    context->Flush();context->WaitForIdle();window_gui=nullptr;gui.reset();if(IsWindow(window))DestroyWindow(window);std::cout<<"DarkAngel editor rendered "<<frame<<" frames with "<<args.backend<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"DarkAngelEditor: "<<e.what()<<'\n';return 1;}}
