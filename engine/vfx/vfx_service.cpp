#include <darkangel/vfx.hpp>
#include <Effekseer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.StandardRenderer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.SpriteRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.RibbonRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.RingRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.TrackRendererBase.h>
#include <RenderDevice.h>
#include <DeviceContext.h>
#include <Texture.h>
#include <Buffer.h>
#include <PipelineState.h>
#include <ShaderResourceBinding.h>
#include <RefCntAutoPtr.hpp>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace darkangel {
namespace {
using namespace Diligent;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
// One quad is four vertices; Effekseer hands every node kind over as quads.
constexpr int square_limit=8192;
// What is drawn of a vertex. Every Effekseer vertex layout starts with these twenty-four bytes.
struct DrawnVertex {float position[3];std::uint8_t color[4];float uv[2];};
constexpr const char* shader_source=R"(
cbuffer Frame {row_major float4x4 ViewProjection;float4 Scaling;};
struct VertexInput {float3 Position:ATTRIB0;float4 Color:ATTRIB1;float2 UV:ATTRIB2;};
struct VertexOutput {float4 Position:SV_POSITION;float4 Color:COLOR0;float2 UV:TEXCOORD0;};
VertexOutput VSMain(VertexInput vertex){VertexOutput result;result.Position=mul(float4(vertex.Position,1),ViewProjection);result.Color=vertex.Color;result.UV=vertex.UV;return result;}
Texture2D Albedo;SamplerState Albedo_sampler;
float4 PSMain(VertexOutput vertex):SV_TARGET {float4 color=Albedo.Sample(Albedo_sampler,vertex.UV)*vertex.Color;color.rgb*=Scaling.x;return color;}
)";
struct FrameConstants {float view_projection[16];float scaling[4];};
// A texture as Effekseer holds it: the Diligent view behind the backend handle.
struct BackendTexture final:Effekseer::Backend::Texture {
    RefCntAutoPtr<ITexture> texture;ITextureView* view{};
    BackendTexture(RefCntAutoPtr<ITexture> made,int width,int height):texture(std::move(made)){view=texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);param_.Size={width,height,1};param_.Format=Effekseer::Backend::TextureFormatType::R8G8B8A8_UNORM;}
};
RefCntAutoPtr<ITexture> upload(IRenderDevice* device,const CookedTexture& cooked,const char* name){
    require(!cooked.mips.empty()&&cooked.mips.size()<=16,"Effect texture mip bounds");TextureDesc desc;desc.Name=name;desc.Type=RESOURCE_DIM_TEX_2D;desc.Width=cooked.mips.front().width;desc.Height=cooked.mips.front().height;desc.MipLevels=static_cast<Uint32>(cooked.mips.size());
    // Effect colours are authored as they should appear; the texture is sampled as plain values.
    desc.Format=TEX_FORMAT_RGBA8_UNORM;desc.BindFlags=BIND_SHADER_RESOURCE;desc.Usage=USAGE_IMMUTABLE;std::vector<TextureSubResData> levels;
    for(const auto& mip:cooked.mips){require(mip.rgba.size()==std::size_t(mip.width)*mip.height*4,"Effect texture mip size");TextureSubResData data;data.pData=mip.rgba.data();data.Stride=mip.width*4;levels.push_back(data);}
    TextureData data;data.pSubResources=levels.data();data.NumSubresources=static_cast<Uint32>(levels.size());RefCntAutoPtr<ITexture> texture;device->CreateTexture(desc,&data,&texture);require(texture,"Effect texture upload failed");return texture;
}
class Renderer;
// Collects the quads Effekseer's node renderers write during one draw, grouped by render state. It stands where
// Effekseer's own standard renderer would; the node renderers only need these three functions from it.
class Batcher {
public:
    struct Batch {EffekseerRenderer::StandardRendererState state;std::size_t offset{},size{};int stride{};};
    Batcher():arena_(std::size_t(square_limit)*4*96){}
    void BeginRenderingAndRenderingIfRequired(const EffekseerRenderer::StandardRendererState& state,std::int32_t count,int& stride,void*& data){
        stride=EffekseerRenderer::StandardRenderer<Renderer,int>::CalculateCurrentStride(state);const std::size_t need=std::size_t(count>0?count:0)*std::size_t(stride);
        if(!need||need>arena_.size()-used_){data=nullptr;if(need)++overflow;return;}
        data=arena_.data()+used_;if(!batches.empty()&&batches.back().stride==stride&&batches.back().state==state)batches.back().size+=need;else batches.push_back({state,used_,need,stride});used_+=need;
    }
    void EndRenderingAndRenderingIfRequired(){}
    EffekseerRenderer::StandardRendererState& GetState(){return batches.empty()?idle_:batches.back().state;}
    const std::uint8_t* data()const{return arena_.data();}
    void reset(){batches.clear();used_=0;overflow=0;}
    std::vector<Batch> batches;unsigned overflow{};
private:
    std::vector<std::uint8_t> arena_;std::size_t used_{};EffekseerRenderer::StandardRendererState idle_;
};
// The part of an Effekseer renderer the shared node renderers use: camera, a quad budget and the batcher.
class Renderer final:public EffekseerRenderer::Renderer,public Effekseer::ReferenceObject {
public:
    int GetRef()override{return Effekseer::ReferenceObject::GetRef();}
    int AddRef()override{return Effekseer::ReferenceObject::AddRef();}
    int Release()override{return Effekseer::ReferenceObject::Release();}
    void OnLostDevice()override{}
    void OnResetDevice()override{}
    void SetRestorationOfStatesFlag(bool)override{}
    bool BeginRendering()override{batcher.reset();return true;}
    bool EndRendering()override{return true;}
    std::int32_t GetSquareMaxCount()const override{return square_limit;}
    Effekseer::SpriteRendererRef CreateSpriteRenderer()override{return Effekseer::SpriteRendererRef(new EffekseerRenderer::SpriteRendererBase<Renderer,false>(this));}
    Effekseer::RibbonRendererRef CreateRibbonRenderer()override{return Effekseer::RibbonRendererRef(new EffekseerRenderer::RibbonRendererBase<Renderer,false>(this));}
    Effekseer::RingRendererRef CreateRingRenderer()override{return Effekseer::RingRendererRef(new EffekseerRenderer::RingRendererBase<Renderer,false>(this));}
    Effekseer::TrackRendererRef CreateTrackRenderer()override{return Effekseer::TrackRendererRef(new EffekseerRenderer::TrackRendererBase<Renderer,false>(this));}
    // Models are refused at cook time; nothing draws them.
    Effekseer::ModelRendererRef CreateModelRenderer()override{return nullptr;}
    Effekseer::TextureLoaderRef CreateTextureLoader(Effekseer::FileInterfaceRef)override{return nullptr;}
    Effekseer::ModelLoaderRef CreateModelLoader(Effekseer::FileInterfaceRef)override{return nullptr;}
    Effekseer::MaterialLoaderRef CreateMaterialLoader(Effekseer::FileInterfaceRef)override{return nullptr;}
    void ResetRenderState()override{}
    EffekseerRenderer::DistortingCallback* GetDistortingCallback()override{return nullptr;}
    void SetDistortingCallback(EffekseerRenderer::DistortingCallback*)override{}
    Batcher* GetStandardRenderer(){return &batcher;}
    Batcher batcher;
};
// Gives an effect the textures of its own cooked bundle, by the path the effect asks for.
class BundleTextures final:public Effekseer::TextureLoader {
public:
    Effekseer::TextureRef Load(const char16_t* path,Effekseer::TextureType)override{
        std::string key;for(const char16_t* at=path;at&&*at;++at)key.push_back(*at<0x80?char(*at):'?');const auto found=current.find(key);if(found==current.end()){++missing;return nullptr;}
        auto texture=Effekseer::MakeRefPtr<Effekseer::Texture>();texture->SetBackend(found->second);return texture;
    }
    void Unload(Effekseer::TextureRef)override{}
    std::map<std::string,Effekseer::Backend::TextureRef> current;unsigned missing{};
};
}
struct VfxService::Impl {
    RefCntAutoPtr<IRenderDevice> device;RefCntAutoPtr<IDeviceContext> context;TEXTURE_FORMAT color{},depth{};
    Effekseer::ManagerRef manager;Effekseer::RefPtr<Renderer> renderer;Effekseer::RefPtr<BundleTextures> textures;
    struct Loaded {Effekseer::EffectRef effect;std::string generation;};std::map<AssetId,Loaded> effects;
    std::map<std::uint64_t,Effekseer::Handle> playing;std::uint64_t next{1};
    RefCntAutoPtr<IShader> vertex_shader,pixel_shader;RefCntAutoPtr<IBuffer> constants,vertices,indices;RefCntAutoPtr<ITexture> white;
    // One pipeline per blend and depth state; one resource binding per pipeline and texture.
    std::map<std::tuple<int,bool,bool>,RefCntAutoPtr<IPipelineState>> pipelines;std::map<std::pair<IPipelineState*,ITextureView*>,RefCntAutoPtr<IShaderResourceBinding>> bindings;
    std::vector<DrawnVertex> packed,queued_blend,queued_add;VfxStats latest;
    IPipelineState* pipeline(Effekseer::AlphaBlendType blend,bool test,bool write){
        const auto key=std::make_tuple(int(blend),test,write);if(auto found=pipelines.find(key);found!=pipelines.end())return found->second;
        GraphicsPipelineStateCreateInfo ci;ci.PSODesc.Name="DarkAngel visual effect";ci.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;ci.pVS=vertex_shader;ci.pPS=pixel_shader;auto& graphics=ci.GraphicsPipeline;
        graphics.NumRenderTargets=1;graphics.RTVFormats[0]=color;graphics.DSVFormat=depth;graphics.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;graphics.RasterizerDesc.CullMode=CULL_MODE_NONE;
        graphics.DepthStencilDesc.DepthEnable=test;graphics.DepthStencilDesc.DepthWriteEnable=test&&write;graphics.DepthStencilDesc.DepthFunc=COMPARISON_FUNC_LESS_EQUAL;
        auto& target=graphics.BlendDesc.RenderTargets[0];target.BlendEnable=blend!=Effekseer::AlphaBlendType::Opacity;target.BlendOp=BLEND_OPERATION_ADD;target.SrcBlendAlpha=BLEND_FACTOR_ZERO;target.DestBlendAlpha=BLEND_FACTOR_ONE;target.BlendOpAlpha=BLEND_OPERATION_ADD;
        switch(blend){
        case Effekseer::AlphaBlendType::Add:target.SrcBlend=BLEND_FACTOR_SRC_ALPHA;target.DestBlend=BLEND_FACTOR_ONE;break;
        case Effekseer::AlphaBlendType::Sub:target.SrcBlend=BLEND_FACTOR_SRC_ALPHA;target.DestBlend=BLEND_FACTOR_ONE;target.BlendOp=BLEND_OPERATION_REV_SUBTRACT;break;
        case Effekseer::AlphaBlendType::Mul:target.SrcBlend=BLEND_FACTOR_ZERO;target.DestBlend=BLEND_FACTOR_SRC_COLOR;break;
        default:target.SrcBlend=BLEND_FACTOR_SRC_ALPHA;target.DestBlend=BLEND_FACTOR_INV_SRC_ALPHA;break;}
        LayoutElement layout[]={{0,0,3,VT_FLOAT32,false},{1,0,4,VT_UINT8,true},{2,0,2,VT_FLOAT32,false}};graphics.InputLayout.LayoutElements=layout;graphics.InputLayout.NumElements=3;
        ShaderResourceVariableDesc variables[]={{SHADER_TYPE_PIXEL,"Albedo",SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};ci.PSODesc.ResourceLayout.Variables=variables;ci.PSODesc.ResourceLayout.NumVariables=1;
        SamplerDesc sampler;sampler.MinFilter=FILTER_TYPE_LINEAR;sampler.MagFilter=FILTER_TYPE_LINEAR;sampler.MipFilter=FILTER_TYPE_LINEAR;sampler.AddressU=TEXTURE_ADDRESS_CLAMP;sampler.AddressV=TEXTURE_ADDRESS_CLAMP;
        ImmutableSamplerDesc samplers[]={{SHADER_TYPE_PIXEL,"Albedo",sampler}};ci.PSODesc.ResourceLayout.ImmutableSamplers=samplers;ci.PSODesc.ResourceLayout.NumImmutableSamplers=1;
        RefCntAutoPtr<IPipelineState> made;device->CreateGraphicsPipelineState(ci,&made);require(made,"Visual effect pipeline creation failed");
        auto* vertex_frame=made->GetStaticVariableByName(SHADER_TYPE_VERTEX,"Frame");auto* pixel_frame=made->GetStaticVariableByName(SHADER_TYPE_PIXEL,"Frame");require(vertex_frame&&pixel_frame,"Visual effect shader changed its Frame contract");vertex_frame->Set(constants);pixel_frame->Set(constants);
        return pipelines.emplace(key,made).first->second;
    }
    IShaderResourceBinding* binding(IPipelineState* state,ITextureView* view){
        const auto key=std::make_pair(state,view);if(auto found=bindings.find(key);found!=bindings.end())return found->second;
        RefCntAutoPtr<IShaderResourceBinding> made;state->CreateShaderResourceBinding(&made,true);require(made,"Visual effect resource binding failed");auto* albedo=made->GetVariableByName(SHADER_TYPE_PIXEL,"Albedo");require(albedo!=nullptr,"Visual effect shader changed its texture contract");albedo->Set(view);
        return bindings.emplace(key,made).first->second;
    }
};
VfxService::VfxService(IRenderDevice* device,IDeviceContext* context,int color_format,int depth_format):impl_(std::make_unique<Impl>()){
    auto& p=*impl_;require(device&&context,"A visual effect service needs a device and a context");p.device=device;p.context=context;p.color=static_cast<TEXTURE_FORMAT>(color_format);p.depth=static_cast<TEXTURE_FORMAT>(depth_format);
    ShaderCreateInfo shader;shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_HLSL;shader.ShaderCompiler=SHADER_COMPILER_DXC;shader.HLSLVersion={6,0};shader.Desc.UseCombinedTextureSamplers=true;shader.Source=shader_source;
    shader.Desc.Name="DarkAngel visual effect VS";shader.Desc.ShaderType=SHADER_TYPE_VERTEX;shader.EntryPoint="VSMain";device->CreateShader(shader,&p.vertex_shader);
    shader.Desc.Name="DarkAngel visual effect PS";shader.Desc.ShaderType=SHADER_TYPE_PIXEL;shader.EntryPoint="PSMain";device->CreateShader(shader,&p.pixel_shader);require(p.vertex_shader&&p.pixel_shader,"Visual effect shader compilation failed");
    BufferDesc uniform;uniform.Name="DarkAngel visual effect constants";uniform.Size=sizeof(FrameConstants);uniform.Usage=USAGE_DYNAMIC;uniform.BindFlags=BIND_UNIFORM_BUFFER;uniform.CPUAccessFlags=CPU_ACCESS_WRITE;device->CreateBuffer(uniform,nullptr,&p.constants);
    BufferDesc vertex;vertex.Name="DarkAngel visual effect vertices";vertex.Size=std::uint64_t(square_limit)*4*sizeof(DrawnVertex);vertex.Usage=USAGE_DYNAMIC;vertex.BindFlags=BIND_VERTEX_BUFFER;vertex.CPUAccessFlags=CPU_ACCESS_WRITE;device->CreateBuffer(vertex,nullptr,&p.vertices);
    // Effekseer's quad winding: 3,1,0 and 3,0,2.
    std::vector<std::uint32_t> quads(std::size_t(square_limit)*6);for(std::uint32_t quad=0;quad<square_limit;++quad){const std::uint32_t corners[]={3,1,0,3,0,2};for(unsigned index=0;index<6;++index)quads[quad*6+index]=quad*4+corners[index];}
    BufferDesc index;index.Name="DarkAngel visual effect quads";index.Size=quads.size()*sizeof(std::uint32_t);index.Usage=USAGE_IMMUTABLE;index.BindFlags=BIND_INDEX_BUFFER;BufferData contents{quads.data(),index.Size};device->CreateBuffer(index,&contents,&p.indices);
    require(p.constants&&p.vertices&&p.indices,"Visual effect buffer creation failed");
    CookedTexture plain;plain.mips.push_back({1,1,{255,255,255,255}});p.white=upload(device,plain,"DarkAngel visual effect white");
    p.manager=Effekseer::Manager::Create(square_limit);require(p.manager!=nullptr,"Effekseer manager creation failed");p.renderer=Effekseer::RefPtr<Renderer>(new Renderer());p.textures=Effekseer::MakeRefPtr<BundleTextures>();
    p.manager->SetCoordinateSystem(Effekseer::CoordinateSystem::RH);p.manager->SetSpriteRenderer(p.renderer->CreateSpriteRenderer());p.manager->SetRibbonRenderer(p.renderer->CreateRibbonRenderer());p.manager->SetRingRenderer(p.renderer->CreateRingRenderer());p.manager->SetTrackRenderer(p.renderer->CreateTrackRenderer());p.manager->SetTextureLoader(p.textures);
}
VfxService::~VfxService(){if(impl_&&impl_->manager!=nullptr){impl_->manager->StopAllEffects();impl_->effects.clear();impl_->manager.Reset();}}
void VfxService::load(const CookedVfx& cooked){
    auto& p=*impl_;require(cooked.id!=AssetId{}&&!cooked.effect.empty()&&cooked.effect.size()<=16*1024*1024&&cooked.textures.size()<=16,"Cooked visual effect bounds");
    if(auto held=p.effects.find(cooked.id);held!=p.effects.end()&&held->second.generation==cooked.generation)return;
    p.textures->current.clear();p.textures->missing=0;for(const auto& [path,texture]:cooked.textures){auto made=upload(p.device,texture,"DarkAngel visual effect texture");p.textures->current[path]=Effekseer::Backend::TextureRef(new BackendTexture(std::move(made),int(texture.mips.front().width),int(texture.mips.front().height)));}
    auto effect=Effekseer::Effect::Create(p.manager,cooked.effect.data(),static_cast<std::int32_t>(cooked.effect.size()));const auto missing=p.textures->missing;p.textures->current.clear();
    require(effect!=nullptr,"Effekseer could not load the cooked effect");require(missing==0,"A cooked effect asks for a texture its bundle does not hold");p.effects[cooked.id]={std::move(effect),cooked.generation};
}
bool VfxService::loaded(AssetId id)const{return impl_->effects.contains(id);}
std::uint64_t VfxService::play(AssetId id,std::array<float,3> position){
    auto& p=*impl_;const auto found=p.effects.find(id);if(found==p.effects.end())return 0;const auto handle=p.manager->Play(found->second.effect,position[0],position[1],position[2]);if(handle<0)return 0;
    const auto own=p.next++;p.playing.emplace(own,handle);return own;
}
void VfxService::move(std::uint64_t handle,std::array<float,3> position){auto& p=*impl_;if(auto found=p.playing.find(handle);found!=p.playing.end())p.manager->SetLocation(found->second,position[0],position[1],position[2]);}
void VfxService::stop(std::uint64_t handle){auto& p=*impl_;if(auto found=p.playing.find(handle);found!=p.playing.end())p.manager->StopRoot(found->second);}
void VfxService::kill(std::uint64_t handle){auto& p=*impl_;if(auto found=p.playing.find(handle);found!=p.playing.end()){p.manager->StopEffect(found->second);p.playing.erase(found);}}
bool VfxService::alive(std::uint64_t handle)const{const auto& p=*impl_;const auto found=p.playing.find(handle);return found!=p.playing.end()&&p.manager->Exists(found->second);}
void VfxService::update(double seconds){
    auto& p=*impl_;require(seconds>=0&&seconds<=10,"Visual effect clock delta bounds");p.manager->Update(static_cast<float>(seconds*60));
    for(auto at=p.playing.begin();at!=p.playing.end();)if(!p.manager->Exists(at->second))at=p.playing.erase(at);else ++at;
}
void VfxService::quads(std::span<const VfxQuadVertex> vertices,bool additive){
    auto& p=*impl_;require(vertices.size()%4==0,"Primitive quads come four vertices at a time");auto& queue=additive?p.queued_add:p.queued_blend;if(queue.size()/4+vertices.size()/4>square_limit/4)return;
    for(const auto& vertex:vertices){DrawnVertex drawn{};for(int axis=0;axis<3;++axis)drawn.position[axis]=vertex.position[std::size_t(axis)];for(int channel=0;channel<4;++channel)drawn.color[channel]=static_cast<std::uint8_t>(std::clamp(vertex.color[std::size_t(channel)],0.f,1.f)*255.f+.5f);drawn.uv[0]=drawn.uv[1]=.5f;queue.push_back(drawn);}
}
void VfxService::draw(const VfxView& view){
    auto& p=*impl_;Effekseer::Matrix44 camera,projection;for(int row=0;row<4;++row)for(int column=0;column<4;++column){camera.Values[row][column]=view.view[std::size_t(row*4+column)];projection.Values[row][column]=view.projection[std::size_t(row*4+column)];}
    p.renderer->SetCameraMatrix(camera);p.renderer->SetProjectionMatrix(projection);p.renderer->SetCameraParameter({view.forward[0],view.forward[1],view.forward[2]},{view.eye[0],view.eye[1],view.eye[2]});
    Effekseer::Matrix44 combined;Effekseer::Matrix44::Mul(combined,camera,projection);Effekseer::Manager::DrawParameter parameter;parameter.ViewProjectionMatrix=combined;parameter.ZNear=view.near_plane;parameter.ZFar=view.far_plane;parameter.CameraPosition={view.eye[0],view.eye[1],view.eye[2]};
    p.renderer->BeginRendering();p.manager->Draw(parameter);p.renderer->EndRendering();
    auto& batcher=p.renderer->batcher;p.latest.draws=p.latest.quads=0;p.latest.unsupported=batcher.overflow;p.packed.clear();
    struct Draw {IPipelineState* pipeline;ITextureView* texture;float scaling;std::uint32_t first,quads;};std::vector<Draw> draws;
    for(const auto& batch:batcher.batches){const auto type=batch.state.Collector.ShaderType;
        // Lit, distortion and material batches need shading this renderer does not have; cooking refuses the effects that are known to need it.
        if(type!=EffekseerRenderer::RendererShaderType::Unlit&&type!=EffekseerRenderer::RendererShaderType::AdvancedUnlit){++p.latest.unsupported;continue;}
        const std::size_t count=batch.size/std::size_t(batch.stride),quads=count/4;if(!quads||p.packed.size()/4+quads>square_limit)continue;const auto first=static_cast<std::uint32_t>(p.packed.size());
        for(std::size_t vertex=0;vertex<quads*4;++vertex){DrawnVertex drawn;std::memcpy(&drawn,batcher.data()+batch.offset+vertex*std::size_t(batch.stride),sizeof(drawn));p.packed.push_back(drawn);}
        ITextureView* texture=p.white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);if(batch.state.Collector.TextureCount>0&&batch.state.Collector.Textures[0]!=nullptr)if(auto* held=dynamic_cast<BackendTexture*>(batch.state.Collector.Textures[0].Get()))texture=held->view;
        draws.push_back({p.pipeline(batch.state.AlphaBlend,batch.state.DepthTest,batch.state.DepthWrite),texture,batch.state.EmissiveScaling,first,static_cast<std::uint32_t>(quads)});}
    // Native primitives after the effects: alpha blended first (decals lie on surfaces), then additive (trails).
    p.latest.primitives=0;for(auto* queue:{&p.queued_blend,&p.queued_add}){const std::size_t quads=queue->size()/4;if(quads&&p.packed.size()/4+quads<=square_limit){const auto first=static_cast<std::uint32_t>(p.packed.size());p.packed.insert(p.packed.end(),queue->begin(),queue->end());
            draws.push_back({p.pipeline(queue==&p.queued_add?Effekseer::AlphaBlendType::Add:Effekseer::AlphaBlendType::Blend,true,false),p.white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),1.f,first,static_cast<std::uint32_t>(quads)});p.latest.primitives+=static_cast<unsigned>(quads);}queue->clear();}
    if(draws.empty())return;
    {void* mapped{};p.context->MapBuffer(p.vertices,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped!=nullptr,"Visual effect vertex mapping failed");std::memcpy(mapped,p.packed.data(),p.packed.size()*sizeof(DrawnVertex));p.context->UnmapBuffer(p.vertices,MAP_WRITE);}
    IBuffer* buffers[]={p.vertices};p.context->SetVertexBuffers(0,1,buffers,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);p.context->SetIndexBuffer(p.indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    for(const auto& draw:draws){FrameConstants constants{};for(int row=0;row<4;++row)for(int column=0;column<4;++column)constants.view_projection[row*4+column]=combined.Values[row][column];constants.scaling[0]=draw.scaling;
        void* mapped{};p.context->MapBuffer(p.constants,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped!=nullptr,"Visual effect constant mapping failed");std::memcpy(mapped,&constants,sizeof(constants));p.context->UnmapBuffer(p.constants,MAP_WRITE);
        p.context->SetPipelineState(draw.pipeline);p.context->CommitShaderResources(p.binding(draw.pipeline,draw.texture),RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawIndexedAttribs attributes;attributes.NumIndices=draw.quads*6;attributes.IndexType=VT_UINT32;attributes.BaseVertex=draw.first;attributes.Flags=DRAW_FLAG_VERIFY_ALL;p.context->DrawIndexed(attributes);++p.latest.draws;p.latest.quads+=draw.quads;}
}
VfxStats VfxService::stats()const{const auto& p=*impl_;auto result=p.latest;result.effects=p.effects.size();result.playing=p.playing.size();result.instances=static_cast<std::size_t>(p.manager->GetTotalInstanceCount());return result;}
}
