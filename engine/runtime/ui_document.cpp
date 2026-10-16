#include <darkangel/ui_document.hpp>
#include "owner_ui_binding.hpp"
#include "inventory_document.hpp"
#include "altar_document.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace darkangel {namespace {
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
double percent(double value,double maximum){return maximum>0&&std::isfinite(value)?std::clamp(value/maximum,0.,1.)*100:0;}
Rml::String amount(double value,double maximum){return std::to_string(std::lround(std::ceil(std::max(value,0.))))+" / "+std::to_string(std::lround(std::max(maximum,0.)));}
}
std::string compose_ui_document(std::string_view markup,std::string_view style){
    std::string document(markup);const auto head=document.find("</head>");if(head==std::string::npos)throw std::runtime_error("A UI document needs a <head> for its style");
    document.insert(head,"<style>"+std::string(style)+"</style>");return document;
}
struct UiDocumentPreview::Impl {
    std::shared_ptr<UiRuntime> runtime;Rml::Context* context{};Rml::ElementDocument* document{};Rml::DataModelHandle handle;std::string good_markup,good_style,problems;bool stale{};std::string focus;
    std::unique_ptr<OwnerUiBinding> owner;std::unique_ptr<InventoryDocument> inventory;std::unique_ptr<AltarDocument> altar;bool owner_driven{};
    UiPreviewSample sample;float health_percent{},stamina_percent{},cooldown_percent{},scale{1};Rml::String health_text,stamina_text,name,prompt;bool flag{};std::vector<std::string> commands;
    void assign(const UiPreviewSample& value){sample=value;scale=float(value.scale);health_percent=float(percent(value.health,value.maximum_health));stamina_percent=float(percent(value.stamina,value.maximum_stamina));
        cooldown_percent=float(std::clamp(value.cooldown,0.,1.)*100);health_text=amount(value.health,value.maximum_health);stamina_text=amount(value.stamina,value.maximum_stamina);name=value.name;prompt=value.prompt;flag=value.flag;}
    // Loads one document; problems leave nothing loaded and are returned.
    std::string try_load(std::string_view markup,std::string_view style,std::string_view binding){
        if(binding=="owner-v1"&&!owner)owner=std::make_unique<OwnerUiBinding>(*context);
        else if(binding=="inventory-v1"&&!inventory){inventory=std::make_unique<InventoryDocument>(*context);InventoryUiModel seed;seed.open=seed.ready=true;for(int i=0;i<4;++i)seed.slots.push_back({i,"Mask "+std::to_string(i+1),"Empty"});inventory->update(seed);}
        else if(binding=="altar-v1"&&!altar)altar=std::make_unique<AltarDocument>(*context);
        else if(binding!="ui-v1"&&binding!="owner-v1"&&binding!="inventory-v1"&&binding!="altar-v1")return "Unknown UI binding contract";
        runtime->take_problems();std::string text;try{text=compose_ui_document(markup,style);}catch(const std::exception& error){return error.what();}
        auto* loaded=context->LoadDocumentFromMemory(text,"authored-ui.rml");if(!loaded){auto problems=runtime->take_problems();return problems.empty()?"The document did not load":problems;}
        if(auto problems=runtime->take_problems();!problems.empty()){loaded->Close();context->Update();return problems;}
        if(document){document->Close();}document=loaded;focus.clear();document->Show();context->Update();return {};}
};
UiDocumentPreview::UiDocumentPreview(Rml::RenderInterface& renderer,UiFont fallback_font,int width,int height):impl_(std::make_unique<Impl>()){
    require(width>0&&height>0&&width<=16384&&height<=16384,"UI preview dimensions");
    impl_->runtime=UiRuntime::acquire();
    try{
        impl_->context=impl_->runtime->create_context("ui-preview",width,height,renderer);
        impl_->runtime->register_font(fallback_font.family.empty()?"HUD":fallback_font.family,std::move(fallback_font.bytes));
        auto model=impl_->context->CreateDataModel("ui");require(bool(model),"UI preview data model creation failed");auto& p=*impl_;
        bool bound=model.Bind("health_percent",&p.health_percent)&&model.Bind("stamina_percent",&p.stamina_percent)&&model.Bind("cooldown_percent",&p.cooldown_percent)&&model.Bind("health_text",&p.health_text)&&model.Bind("stamina_text",&p.stamina_text)&&model.Bind("name",&p.name)&&model.Bind("prompt",&p.prompt)&&model.Bind("flag",&p.flag)&&model.Bind("scale",&p.scale);
        bound=bound&&model.BindEventCallback("command",[&p](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList& arguments){if(p.commands.size()<64)p.commands.push_back(arguments.empty()?std::string("(no id)"):arguments[0].Get<Rml::String>());});
        require(bound,"UI preview data model binding failed");impl_->handle=model.GetModelHandle();impl_->assign({});
    }catch(...){impl_->runtime->destroy_context(impl_->context);throw;}
}
UiDocumentPreview::~UiDocumentPreview(){impl_->runtime->destroy_context(impl_->context);}
void UiDocumentPreview::register_font(UiFont font){impl_->runtime->register_font(font.family,std::move(font.bytes));}
void UiDocumentPreview::register_image(std::string url,UiRuntime::Image image){impl_->runtime->register_image(std::move(url),std::move(image));}
bool UiDocumentPreview::load(std::string_view markup,std::string_view style,std::string_view binding){
    auto& p=*impl_;require(markup.size()<=65536&&style.size()<=65536,"UI document text bound");auto problems=p.try_load(markup,style,binding);
    if(problems.empty()){p.good_markup=markup;p.good_style=style;p.problems.clear();p.stale=false;p.handle.DirtyAllVariables();return true;}
    p.problems=problems.substr(0,4096);p.stale=p.document!=nullptr;return false;
}
bool UiDocumentPreview::stale()const{return impl_->stale;}
const std::string& UiDocumentPreview::problems()const{return impl_->problems;}
void UiDocumentPreview::resize(int width,int height){require(width>0&&height>0&&width<=16384&&height<=16384,"UI preview dimensions");if(impl_->context->GetDimensions()!=Rml::Vector2i{width,height})impl_->context->SetDimensions({width,height});}
void UiDocumentPreview::update(const UiPreviewSample& value){
    for(double number:{value.health,value.maximum_health,value.stamina,value.maximum_stamina,value.cooldown,value.scale})require(std::isfinite(number)&&number>=0&&number<=1e9,"UI preview sample bounds");
    require(value.name.size()<=64&&value.prompt.size()<=64,"UI preview sample text bounds");
    if(impl_->owner&&!impl_->owner_driven){OwnerUiModel sample;sample.health=value.health;sample.maximum_health=value.maximum_health;sample.stamina=value.stamina;sample.maximum_stamina=value.maximum_stamina;sample.mask=value.name;sample.prompt=value.prompt;sample.has_target=value.flag;sample.target_health=value.health;sample.target_maximum_health=value.maximum_health;sample.target_name=value.name;impl_->owner->update(sample);}
    if(!(value==impl_->sample)){impl_->assign(value);impl_->handle.DirtyAllVariables();}impl_->context->Update();impl_->runtime->take_problems();
}
void UiDocumentPreview::update_owner(const OwnerUiModel& model){if(impl_->owner){impl_->owner_driven=true;impl_->owner->update(model);impl_->context->Update();}}
void UiDocumentPreview::update_inventory(const InventoryUiModel& model){require(bool(impl_->inventory),"Document needs inventory-v1 binding");impl_->inventory->update(model);impl_->context->Update();}
std::vector<InventoryUiAction> UiDocumentPreview::drain_inventory_actions(){return impl_->inventory?impl_->inventory->drain():std::vector<InventoryUiAction>{};}
void UiDocumentPreview::update_altar(const AltarUiModel& model){require(bool(impl_->altar),"Document needs altar-v1 binding");impl_->altar->update(model);impl_->context->Update();}
std::vector<AltarUiAction> UiDocumentPreview::drain_altar_actions(){return impl_->altar?impl_->altar->drain():std::vector<AltarUiAction>{};}
void UiDocumentPreview::render(){impl_->context->Render();}
void UiDocumentPreview::pointer(int x,int y,bool pressed,bool released,float wheel){impl_->context->ProcessMouseMove(x,y,0);if(pressed)impl_->context->ProcessMouseButtonDown(0,0);if(released)impl_->context->ProcessMouseButtonUp(0,0);if(wheel)impl_->context->ProcessMouseWheel(-wheel,0);impl_->context->Update();}
bool UiDocumentPreview::key(UiKey key){
    auto& p=*impl_;if(!p.document||key==UiKey::Cancel)return false;
    Rml::ElementList elements;p.document->QuerySelectorAll(elements,"button, [data-event-click]");
    elements.erase(std::remove_if(elements.begin(),elements.end(),[](auto* e){if(!e->IsVisible(true)||e->HasAttribute("disabled"))return true;for(auto* p=e;p;p=p->GetParentNode())if(p->GetDisplay()==Rml::Style::Display::None)return true;return false;}),elements.end());
    if(elements.empty())return false;
    auto found=std::find_if(elements.begin(),elements.end(),[&](auto* e){return e->GetId()==p.focus;});
    std::size_t index=found==elements.end()?0:std::size_t(found-elements.begin());
    if(key==UiKey::Confirm){elements[index]->Click();}
    else if(found!=elements.end()){const bool back=key==UiKey::Up||key==UiKey::Left;index=(index+elements.size()+(back?-1:1))%elements.size();}
    p.focus=elements[index]->GetId();elements[index]->Focus(true);p.context->Update();return true;
}
std::string UiDocumentPreview::focused()const{return impl_->focus;}
std::vector<std::string> UiDocumentPreview::drain_commands(){auto result=std::move(impl_->commands);impl_->commands.clear();return result;}
std::optional<UiBox> UiDocumentPreview::box(const std::string& id)const{
    if(!impl_->document)return std::nullopt;auto* element=impl_->document->GetElementById(id);if(!element||!element->IsVisible(true))return std::nullopt;
    for(auto* e=element;e;e=e->GetParentNode())if(e->GetDisplay()==Rml::Style::Display::None)return std::nullopt;
    auto offset=element->GetAbsoluteOffset(Rml::BoxArea::Border);auto size=element->GetBox().GetSize(Rml::BoxArea::Border);return UiBox{offset.x,offset.y,size.x,size.y};
}
std::string UiDocumentPreview::text(const std::string& id)const{if(!impl_->document)return {};auto* element=impl_->document->GetElementById(id);return element?std::string(element->GetInnerRML()):std::string{};}
}
