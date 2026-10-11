#include <darkangel/hud.hpp>
#include <RmlUi/Core.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace darkangel {
namespace {
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
bool hud_alive=false;
std::string read(const std::filesystem::path& path,std::size_t limit){
    std::ifstream file(path,std::ios::binary);require(bool(file),"HUD file missing");std::string bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());require(!bytes.empty()&&bytes.size()<=limit,"HUD file size bound");return bytes;
}
double percent(double value,double maximum){return maximum>0&&std::isfinite(value)?std::clamp(value/maximum,0.,1.)*100:0;}
Rml::String amount(double value,double maximum){return std::to_string(std::lround(std::ceil(std::max(value,0.))))+" / "+std::to_string(std::lround(std::max(maximum,0.)));}
// Errors while loading or binding are failures of the game's document, not log noise.
class System final:public Rml::SystemInterface {
public:
    double GetElapsedTime()override{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start_).count();}
    bool LogMessage(Rml::Log::Type type,const Rml::String& message)override{if(type==Rml::Log::LT_ERROR||type==Rml::Log::LT_ASSERT||type==Rml::Log::LT_WARNING){if(problems.size()<4096)problems+=message+"\n";}return true;}
    std::string problems;
private:std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
};
}
struct Hud::Impl {
    System system;std::string font;Rml::Context* context{};Rml::ElementDocument* document{};Rml::DataModelHandle handle;HudViewModel model;
    // Bound variables. Text is formatted natively so the document carries no rules.
    float health_percent{},stamina_percent{},target_percent{};Rml::String health_text,stamina_text,target_text,mask;bool dead{},staggered{},has_target{},has_mask{};
    void assign(const HudViewModel& value){
        model=value;health_percent=float(percent(value.health,value.maximum_health));stamina_percent=float(percent(value.stamina,value.maximum_stamina));target_percent=float(percent(value.target_health,value.target_maximum_health));
        health_text=amount(value.health,value.maximum_health);stamina_text=amount(value.stamina,value.maximum_stamina);target_text=amount(value.target_health,value.target_maximum_health);dead=value.health<=0;staggered=value.staggered&&!dead;has_target=value.has_target;mask=value.mask;has_mask=!value.mask.empty();
    }
    Rml::Element* find(const std::string& id)const{return document->GetElementById(id);}
};
Hud::Hud(Rml::RenderInterface& renderer,const std::filesystem::path& document,const std::filesystem::path& font,int width,int height):impl_(std::make_unique<Impl>()){
    require(!hud_alive,"Only one HUD can exist at a time");require(width>0&&height>0&&width<=16384&&height<=16384,"HUD dimensions");
    // The document is self-contained (inline style, no images), so it is read here and given a plain name:
    // RmlUi's URL parser does not accept Windows drive paths.
    auto source=read(document,1024*1024);impl_->font=read(font,64*1024*1024);
    Rml::SetSystemInterface(&impl_->system);require(Rml::Initialise(),"RmlUi initialisation failed");hud_alive=true;
    try{
        impl_->context=Rml::CreateContext("darkangel-hud",{width,height},&renderer);require(impl_->context!=nullptr,"HUD context creation failed");
        require(Rml::LoadFontFace({reinterpret_cast<const Rml::byte*>(impl_->font.data()),impl_->font.size()},"HUD",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal,true),"HUD font face rejected");
        auto model=impl_->context->CreateDataModel("hud");require(bool(model),"HUD data model creation failed");
        bool bound=model.Bind("health_percent",&impl_->health_percent)&&model.Bind("stamina_percent",&impl_->stamina_percent)&&model.Bind("target_percent",&impl_->target_percent)&&model.Bind("health_text",&impl_->health_text)&&model.Bind("stamina_text",&impl_->stamina_text)&&model.Bind("target_text",&impl_->target_text)&&model.Bind("dead",&impl_->dead)&&model.Bind("staggered",&impl_->staggered)&&model.Bind("has_target",&impl_->has_target)&&model.Bind("mask",&impl_->mask)&&model.Bind("has_mask",&impl_->has_mask);
        require(bound,"HUD view model binding failed");impl_->handle=model.GetModelHandle();impl_->assign({});
        impl_->document=impl_->context->LoadDocumentFromMemory(source,document.filename().generic_string());require(impl_->document!=nullptr,"HUD document failed to load");impl_->document->Show();impl_->context->Update();
        if(!impl_->system.problems.empty())throw std::runtime_error("HUD document problems: "+impl_->system.problems);
    }catch(...){Rml::Shutdown();hud_alive=false;throw;}
}
Hud::~Hud(){Rml::Shutdown();hud_alive=false;}
void Hud::resize(int width,int height){require(width>0&&height>0&&width<=16384&&height<=16384,"HUD dimensions");if(impl_->context->GetDimensions()!=Rml::Vector2i{width,height})impl_->context->SetDimensions({width,height});}
void Hud::update(const HudViewModel& value){
    for(double number:{value.health,value.maximum_health,value.stamina,value.maximum_stamina,value.target_health,value.target_maximum_health})require(std::isfinite(number)&&number>=0&&number<=1e9,"HUD view model bounds");require(value.mask.size()<=64,"HUD mask name bound");
    if(!(value==impl_->model)){impl_->assign(value);impl_->handle.DirtyAllVariables();}
    impl_->context->Update();
}
void Hud::render(){impl_->context->Render();}
const HudViewModel& Hud::model()const{return impl_->model;}
std::optional<Hud::Box> Hud::box(const std::string& id)const{
    auto* element=impl_->find(id);if(!element||!element->IsVisible(true))return std::nullopt;
    for(auto* parent=element;parent;parent=parent->GetParentNode())if(parent->GetDisplay()==Rml::Style::Display::None)return std::nullopt;
    auto offset=element->GetAbsoluteOffset(Rml::BoxArea::Border);auto size=element->GetBox().GetSize(Rml::BoxArea::Border);return Box{offset.x,offset.y,size.x,size.y};
}
std::string Hud::text(const std::string& id)const{auto* element=impl_->find(id);return element?std::string(element->GetInnerRML()):std::string{};}
}
