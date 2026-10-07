#include <EngineFactoryD3D12.h>
#include <EngineFactoryVk.h>
#include <RenderDevice.h>
#include <DeviceContext.h>
#include <Texture.h>
#include <RefCntAutoPtr.hpp>
#include <iostream>
#include <string_view>
using namespace Diligent;
int main(int argc,char** argv){
    if(argc!=2)return 2;RefCntAutoPtr<IRenderDevice> device;RefCntAutoPtr<IDeviceContext> context;
    if(std::string_view(argv[1])=="d3d12"){auto* factory=GetEngineFactoryD3D12();EngineD3D12CreateInfo ci;factory->CreateDeviceAndContextsD3D12(ci,&device,&context);}
    else if(std::string_view(argv[1])=="vulkan"){auto* factory=GetEngineFactoryVk();EngineVkCreateInfo ci;factory->CreateDeviceAndContextsVk(ci,&device,&context);}else return 2;
    if(!device || !context){std::cerr<<"Diligent device unavailable\n";return 1;}
    TextureDesc desc;desc.Name="DarkAngel M2 offscreen probe";desc.Type=RESOURCE_DIM_TEX_2D;desc.Width=64;desc.Height=64;desc.Format=TEX_FORMAT_RGBA8_UNORM;desc.BindFlags=BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> target;device->CreateTexture(desc,nullptr,&target);if(!target)return 1;
    auto* rtv=target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);context->SetRenderTargets(1,&rtv,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float color[]={.12f,.2f,.3f,1};context->ClearRenderTarget(rtv,color,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->Flush();context->WaitForIdle();
    std::cout<<"Diligent "<<argv[1]<<" created device and cleared offscreen target\n";return 0;
}
