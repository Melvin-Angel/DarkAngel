#include <darkangel/ui_runtime.hpp>
#include <RmlUi/Core.h>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace darkangel {
namespace {
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
// RmlUi reports load and binding problems through its system interface; they are collected until a loader takes them.
class System final:public Rml::SystemInterface {
public:
    double GetElapsedTime()override{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start_).count();}
    bool LogMessage(Rml::Log::Type type,const Rml::String& message)override{if(type==Rml::Log::LT_ERROR||type==Rml::Log::LT_ASSERT||type==Rml::Log::LT_WARNING){if(problems.size()<4096)problems+=message+"\n";}return true;}
    std::string problems;
private:std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
};
std::mutex runtime_mutex;std::weak_ptr<UiRuntime> current;
// Images are looked up by render interfaces that hold no runtime pointer; they live as long as the runtime.
std::map<std::string,UiRuntime::Image>& images(){static std::map<std::string,UiRuntime::Image> registered;return registered;}
}
struct UiRuntime::Impl {
    System system;std::vector<std::unique_ptr<std::string>> fonts;std::set<std::string> font_keys;std::set<Rml::Context*> contexts;unsigned next_context{};
};
UiRuntime::UiRuntime():impl_(std::make_unique<Impl>()){Rml::SetSystemInterface(&impl_->system);require(Rml::Initialise(),"RmlUi initialisation failed");}
UiRuntime::~UiRuntime(){for(auto* context:impl_->contexts)Rml::RemoveContext(context->GetName());impl_->contexts.clear();Rml::Shutdown();images().clear();}
std::shared_ptr<UiRuntime> UiRuntime::acquire(){
    std::lock_guard lock(runtime_mutex);if(auto existing=current.lock())return existing;
    std::shared_ptr<UiRuntime> created(new UiRuntime());current=created;return created;
}
bool UiRuntime::alive(){std::lock_guard lock(runtime_mutex);return !current.expired();}
Rml::Context* UiRuntime::create_context(std::string_view purpose,int width,int height,Rml::RenderInterface& renderer){
    require(width>0&&height>0&&width<=16384&&height<=16384,"UI context dimensions");require(impl_->contexts.size()<272,"UI context limit");
    const auto name="darkangel-"+std::string(purpose)+"-"+std::to_string(++impl_->next_context);
    auto* context=Rml::CreateContext(name,{width,height},&renderer);require(context!=nullptr,"UI context creation failed");impl_->contexts.insert(context);return context;
}
void UiRuntime::destroy_context(Rml::Context* context){if(!context||!impl_->contexts.erase(context))return;Rml::RemoveContext(context->GetName());}
void UiRuntime::register_font(std::string_view family,std::string bytes){
    require(!family.empty()&&family.size()<=64&&!bytes.empty()&&bytes.size()<=64*1024*1024,"UI font bounds");
    const auto key=std::string(family)+"|"+std::to_string(bytes.size())+"|"+std::to_string(std::hash<std::string>{}(bytes));if(impl_->font_keys.contains(key))return;
    impl_->fonts.push_back(std::make_unique<std::string>(std::move(bytes)));const auto& stored=*impl_->fonts.back();
    require(Rml::LoadFontFace({reinterpret_cast<const Rml::byte*>(stored.data()),stored.size()},Rml::String(family),Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,true),"UI font face rejected");
    impl_->font_keys.insert(key);
}
void UiRuntime::register_image(std::string url,Image value){
    require(url.starts_with("da://")&&url.size()<=128,"UI image URL is da://<asset-id>");require(value.width>0&&value.height>0&&value.width<=4096&&value.height<=4096&&value.rgba.size()==std::size_t(value.width)*std::size_t(value.height)*4,"UI image bounds");
    std::lock_guard lock(runtime_mutex);images()[std::move(url)]=std::move(value);
}
std::optional<UiRuntime::Image> UiRuntime::image(std::string_view url){std::lock_guard lock(runtime_mutex);auto found=images().find(std::string(url));if(found==images().end())return std::nullopt;return found->second;}
std::string UiRuntime::take_problems(){auto result=std::move(impl_->system.problems);impl_->system.problems.clear();return result;}
std::string ui_image_url(std::string_view asset_text){return "da://"+std::string(asset_text);}
}
