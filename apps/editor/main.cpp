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
struct Options {std::filesystem::path registry,cas,capture;std::string model,backend{"d3d12"};unsigned frames{};bool hidden{};};
Options options(int argc,char** argv){Options out;for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--hidden"){out.hidden=true;continue;}require(i+1<argc,"Missing editor option value");std::string value=argv[++i];if(arg=="--registry")out.registry=value;else if(arg=="--cas")out.cas=value;else if(arg=="--model")out.model=value;else if(arg=="--capture")out.capture=value;else if(arg=="--frames")out.frames=static_cast<unsigned>(std::stoul(value));else if(arg=="--backend")out.backend=value;else throw std::runtime_error("Unknown editor option");}require(!out.registry.empty() && !out.cas.empty() && !out.model.empty(),"DarkAngelEditor requires --registry file --cas directory --model UUID");require(out.backend=="d3d12" || out.backend=="vulkan","Unsupported backend");require(out.frames<=10000,"Frame budget limit");return out;}
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
RefCntAutoPtr<IPipelineState> pipeline(IRenderDevice* device,IBuffer* constants,TEXTURE_FORMAT color,TEXTURE_FORMAT depth,bool cull){
    ShaderCreateInfo shader;shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_HLSL;shader.ShaderCompiler=SHADER_COMPILER_DXC;shader.HLSLVersion={6,0};shader.Desc.UseCombinedTextureSamplers=true;shader.Source=shader_source;
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
    result->GetStaticVariableByName(SHADER_TYPE_VERTEX,"Frame")->Set(constants);result->GetStaticVariableByName(SHADER_TYPE_PIXEL,"Frame")->Set(constants);return result;
}
RefCntAutoPtr<ITexture> texture(IRenderDevice* device,const CookedTexture& cooked){require(!cooked.mips.empty(),"Missing texture mips");TextureDesc desc;desc.Name="Cooked DarkAngel sRGB color";desc.Type=RESOURCE_DIM_TEX_2D;desc.Width=cooked.mips[0].width;desc.Height=cooked.mips[0].height;desc.MipLevels=static_cast<Uint32>(cooked.mips.size());desc.Format=TEX_FORMAT_RGBA8_UNORM_SRGB;desc.Usage=USAGE_IMMUTABLE;desc.BindFlags=BIND_SHADER_RESOURCE;
    std::vector<TextureSubResData> subresources;for(const auto& mip:cooked.mips){TextureSubResData data;data.pData=mip.rgba.data();data.Stride=mip.width*4;subresources.push_back(data);}TextureData data;data.pSubResources=subresources.data();data.NumSubresources=static_cast<Uint32>(subresources.size());RefCntAutoPtr<ITexture> result;device->CreateTexture(desc,&data,&result);require(result,"Cooked texture upload failed");return result;
}
void capture(IRenderDevice* device,IDeviceContext* context,ITexture* source,const std::filesystem::path& path){
    TextureDesc desc=source->GetDesc();desc.Name="DarkAngel capture readback";desc.BindFlags=BIND_NONE;desc.Usage=USAGE_STAGING;desc.CPUAccessFlags=CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;device->CreateTexture(desc,nullptr,&staging);require(staging,"Readback texture creation failed");context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);CopyTextureAttribs copy{source,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,staging,RESOURCE_STATE_TRANSITION_MODE_TRANSITION};context->CopyTexture(copy);context->Flush();context->WaitForIdle();
    MappedTextureSubresource mapped;context->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);require(mapped.pData,"GPU readback failed");
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(desc.Width)*desc.Height*4);for(unsigned y=0;y<desc.Height;++y)std::memcpy(pixels.data()+static_cast<std::size_t>(y)*desc.Width*4,static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.Stride,desc.Width*4);context->UnmapTextureSubresource(staging,0,0);
    // The viewport area excludes the inspector; UI pixels alone cannot satisfy this gate.
    std::size_t changed{};auto ref=(static_cast<std::size_t>(desc.Height)-2)*desc.Width*4+(desc.Width-2)*4;
    for(unsigned y=80;y+80<desc.Height;++y)for(unsigned x=360;x+40<desc.Width;++x){auto at=(static_cast<std::size_t>(y)*desc.Width+x)*4;unsigned delta{};for(unsigned c=0;c<3;++c)delta+=static_cast<unsigned>(std::abs(static_cast<int>(pixels[at+c])-pixels[ref+c]));if(delta>40)++changed;}
    require(changed>100,"Viewport did not draw foreground geometry");std::filesystem::create_directories(path.parent_path());DirectX::Image image{desc.Width,desc.Height,DXGI_FORMAT_R8G8B8A8_UNORM,static_cast<std::size_t>(desc.Width)*4,pixels.size(),pixels.data()};require(SUCCEEDED(DirectX::SaveToWICFile(image,DirectX::WIC_FLAGS_NONE,GUID_ContainerFormatPng,path.c_str())),"PNG capture failed");std::cout<<"Viewport foreground pixels="<<changed<<" capture="<<path.string()<<'\n';
}
double property(const ObjectData& data,TypeId type,const PropertyMetadata& field){const void* component=type==1?static_cast<const void*>(&data.transform):static_cast<const void*>(&data.health);double value;std::memcpy(&value,static_cast<const char*>(component)+field.offset,sizeof(value));return value;}
}
int main(int argc,char** argv){try{
    auto args=options(argc,argv);auto id=AssetId::parse(args.model);auto model=load_cooked_model(args.registry,args.cas,id);require(!model.meshes.empty(),"Cooked model has no meshes");
    ComApartment apartment;
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DarkAngelM2Editor";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);require(RegisterClassW(&wc),"Window class registration failed");
    RECT rect{0,0,1280,800};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);HWND window=CreateWindowW(wc.lpszClassName,L"DarkAngel — M2 asset and editor slice",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,wc.hInstance,nullptr);require(window,"Editor window creation failed");
    RefCntAutoPtr<IRenderDevice> device;RefCntAutoPtr<IDeviceContext> context;RefCntAutoPtr<ISwapChain> swap;
    SwapChainDesc swap_desc;swap_desc.Width=1280;swap_desc.Height=800;swap_desc.ColorBufferFormat=TEX_FORMAT_RGBA8_UNORM_SRGB;swap_desc.DepthBufferFormat=TEX_FORMAT_D32_FLOAT;NativeWindow native(window);
    if(args.backend=="d3d12"){auto* factory=GetEngineFactoryD3D12();EngineD3D12CreateInfo ci;ci.pDxCompilerPath=DAE_DXC_DLL;ci.EnableValidation=true;factory->CreateDeviceAndContextsD3D12(ci,&device,&context);require(device && context,"D3D12 device initialization failed");factory->CreateSwapChainD3D12(device,context,swap_desc,FullScreenModeDesc{},native,&swap);}
    else{auto* factory=GetEngineFactoryVk();EngineVkCreateInfo ci;ci.pDxCompilerPath=DAE_DXC_DLL;ci.EnableValidation=true;factory->CreateDeviceAndContextsVk(ci,&device,&context);require(device && context,"Vulkan device initialization failed");factory->CreateSwapChainVk(device,context,swap_desc,native,&swap);}require(swap,"Swap chain creation failed");
    auto gui=ImGuiImplWin32::Create(ImGuiDiligentCreateInfo{device,swap->GetDesc()},window);window_gui=gui.get();ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_DockingEnable;ImGui::GetIO().IniFilename=nullptr;
    ImGui::StyleColorsDark();if(!args.hidden)ShowWindow(window,SW_SHOW);
    World authoring(WorldDomain::Authoring);ObjectData object;object.id={0xdae,1};auto entity=authoring.create(object);EditorService editor(authoring);std::string diagnostic="Loaded from cooked registry and CAS";
    BufferDesc cb;cb.Name="DarkAngel frame constants";cb.Size=sizeof(Constants);cb.Usage=USAGE_DYNAMIC;cb.BindFlags=BIND_UNIFORM_BUFFER;cb.CPUAccessFlags=CPU_ACCESS_WRITE;RefCntAutoPtr<IBuffer> constant_buffer;device->CreateBuffer(cb,nullptr,&constant_buffer);require(constant_buffer,"Constant buffer creation failed");
    auto pso=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,false);auto pso_cull=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,true);
    std::vector<GpuMesh> meshes;std::array<float,3> minimum=model.meshes[0].minimum,maximum=model.meshes[0].maximum;
    for(const auto& mesh:model.meshes){GpuMesh gpu;gpu.parts=mesh.parts;BufferDesc desc;desc.Name="Cooked static prop vertices";desc.Usage=USAGE_IMMUTABLE;desc.BindFlags=BIND_VERTEX_BUFFER;desc.Size=mesh.vertices.size()*sizeof(Vertex);BufferData data{mesh.vertices.data(),desc.Size};device->CreateBuffer(desc,&data,&gpu.vertices);desc.Name="Cooked static prop indices";desc.BindFlags=BIND_INDEX_BUFFER;desc.Size=mesh.indices.size()*sizeof(std::uint32_t);data={mesh.indices.data(),desc.Size};device->CreateBuffer(desc,&data,&gpu.indices);require(gpu.vertices && gpu.indices,"Mesh upload failed");meshes.push_back(std::move(gpu));for(unsigned c=0;c<3;++c){minimum[c]=std::min(minimum[c],mesh.minimum[c]);maximum[c]=std::max(maximum[c],mesh.maximum[c]);}}
    CookedTexture white;white.mips.push_back({1,1,{255,255,255,255}});auto fallback=texture(device,white);std::map<AssetId,RefCntAutoPtr<ITexture>> textures;for(const auto& [texture_id,cooked]:model.textures)textures.emplace(texture_id,texture(device,cooked));
    struct Material {CookedMaterial value;RefCntAutoPtr<IShaderResourceBinding> binding;};std::map<AssetId,Material> materials;
    for(const auto& material:model.materials){Material gpu;gpu.value=material;auto* chosen=material.double_sided?pso.RawPtr():pso_cull.RawPtr();chosen->CreateShaderResourceBinding(&gpu.binding,true);auto* view=(material.has_texture?textures.at(material.texture):fallback)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);gpu.binding->GetVariableByName(SHADER_TYPE_PIXEL,"Albedo")->Set(view);materials.emplace(material.id,std::move(gpu));}
    float center[3],radius{};for(unsigned c=0;c<3;++c){center[c]=(minimum[c]+maximum[c])*.5f;radius=std::max(radius,(maximum[c]-minimum[c])*.5f);}require(radius>1e-6f,"Model has zero bounds");
    bool running=true;unsigned frame{};while(running && (!args.frames || frame<args.frames)){
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)running=false;TranslateMessage(&message);DispatchMessageW(&message);}if(!running)break;
        if(resize_width && resize_height){context->WaitForIdle();swap->Resize(resize_width,resize_height);resize_width=resize_height=0;}
        const auto& desc=swap->GetDesc();gui->NewFrame(desc.Width,desc.Height,desc.PreTransform);ImGui::DockSpaceOverViewport(0,nullptr,ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::SetNextWindowPos({12,40},ImGuiCond_Once);ImGui::SetNextWindowSize({310,690},ImGuiCond_Once);ImGui::Begin("Scene and properties");
        ImGui::TextUnformatted("DarkAngel M2");ImGui::Text("Backend: %s",args.backend.c_str());ImGui::Text("Revision: %llu",static_cast<unsigned long long>(editor.revision()));ImGui::TextWrapped("Model: %s",id.text().c_str());ImGui::Separator();
        for(const auto& type:metadata())if(type.id<=2 && ImGui::CollapsingHeader(std::string(type.name).c_str(),ImGuiTreeNodeFlags_DefaultOpen))for(const auto& field:type.properties)if(field.flags&Editable){auto value=property(authoring.read(entity),type.id,field);auto label=std::string(field.name)+"##"+std::to_string(type.id)+":"+std::to_string(field.id);if(ImGui::InputDouble(label.c_str(),&value,.1,1,"%.3f")){try{PropertyChange change{object.id,type.id,field.id,value};auto prepared=editor.prepare(editor.revision(),{&change,1});editor.commit(prepared);diagnostic="Committed typed property transaction";}catch(const std::exception& e){diagnostic=e.what();}}}
        if(ImGui::Button("Undo")){try{editor.undo(editor.revision());diagnostic="Undid property transaction";}catch(const std::exception& e){diagnostic=e.what();}}ImGui::SameLine();if(ImGui::Button("Redo")){try{editor.redo(editor.revision());diagnostic="Redid property transaction";}catch(const std::exception& e){diagnostic=e.what();}}
        ImGui::Separator();ImGui::Text("Meshes: %u",static_cast<unsigned>(meshes.size()));ImGui::Text("Materials: %u",static_cast<unsigned>(materials.size()));ImGui::Text("Textures: %u",static_cast<unsigned>(textures.size()));ImGui::TextWrapped("%s",diagnostic.c_str());ImGui::End();
        auto* rtv=swap->GetCurrentBackBufferRTV();auto* dsv=swap->GetDepthBufferDSV();context->SetRenderTargets(1,&rtv,dsv,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);const float background[]={.015f,.025f,.045f,1};context->ClearRenderTarget(rtv,background,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearDepthStencil(dsv,CLEAR_DEPTH_FLAG,1,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport;viewport.TopLeftX=330;viewport.Width=static_cast<float>(desc.Width)-330;viewport.Height=static_cast<float>(desc.Height);context->SetViewports(1,&viewport,desc.Width,desc.Height);
        using namespace DirectX;auto yaw=static_cast<float>(authoring.read(entity).transform.yaw);auto rotation=XMMatrixRotationY(yaw);auto world=XMMatrixTranslation(-center[0],-center[1],-center[2])*rotation;auto view=XMMatrixLookAtRH(XMVectorSet(0,0,radius*3.8f,1),XMVectorZero(),XMVectorSet(0,1,0,0));auto projection=XMMatrixPerspectiveFovRH(XM_PIDIV4,viewport.Width/viewport.Height,radius*.01f,radius*30);
        for(const auto& mesh:meshes){IBuffer* vb=mesh.vertices;Uint64 offset{};context->SetVertexBuffers(0,1,&vb,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);context->SetIndexBuffer(mesh.indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            for(const auto& part:mesh.parts){const auto& material=materials.at(part.material);Constants constants;XMStoreFloat4x4(&constants.mvp,world*view*projection);XMStoreFloat4x4(&constants.rotation,rotation);constants.color={material.value.color[0],material.value.color[1],material.value.color[2],material.value.color[3]};constants.surface={material.value.roughness,material.value.metallic,0,0};void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Uniform buffer mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);
                context->SetPipelineState(material.value.double_sided?pso.RawPtr():pso_cull.RawPtr());context->CommitShaderResources(material.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);DrawIndexedAttribs draw;draw.NumIndices=part.count;draw.IndexType=VT_UINT32;draw.FirstIndexLocation=part.first;draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(draw);}
        }
        gui->Render(context);if(!args.capture.empty() && args.frames && frame+1==args.frames)capture(device,context,rtv->GetTexture(),args.capture);swap->Present(args.hidden?0:1);++frame;
    }
    context->Flush();context->WaitForIdle();window_gui=nullptr;gui.reset();if(IsWindow(window))DestroyWindow(window);std::cout<<"DarkAngel editor rendered "<<frame<<" frames with "<<args.backend<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"DarkAngelEditor: "<<e.what()<<'\n';return 1;}}
