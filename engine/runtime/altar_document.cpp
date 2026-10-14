#include "altar_document.hpp"
#include <fstream>
#include <stdexcept>

namespace darkangel {
namespace {void require(bool b,const char* e){if(!b)throw std::runtime_error(e);}}
AltarDocument::AltarDocument(Rml::Context& context,const std::filesystem::path& path){
    auto model=context.CreateDataModel("altar");require(bool(model),"Altar data model missing");
    auto row=model.RegisterStruct<AltarUiDestination>();row.RegisterMember("key",&AltarUiDestination::key);row.RegisterMember("element",&AltarUiDestination::element);row.RegisterMember("name",&AltarUiDestination::name);row.RegisterMember("here",&AltarUiDestination::here);
    require(model.RegisterArray<std::vector<AltarUiDestination>>(),"Altar array registration");
    require(model.Bind("open",&model_.open)&&model.Bind("can_rest",&model_.can_rest)&&model.Bind("can_travel",&model_.can_travel)&&model.Bind("busy",&model_.busy)&&model.Bind("name",&model_.name)&&model.Bind("status",&model_.status)&&model.Bind("destinations",&model_.destinations),"Altar binding");
    auto event=[&](const char* name,Rml::DataEventFunc function){require(model.BindEventCallback(name,std::move(function)),"Altar event binding");};
    event("rest",[this](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList&){if(model_.can_rest&&!model_.busy)queue({AltarUiActionKind::Rest});});
    event("close",[this](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList&){queue({AltarUiActionKind::Close});});
    event("travel",[this](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList& args){if(!model_.can_travel||model_.busy||args.size()!=1)return;const auto key=args[0].Get<Rml::String>();for(std::size_t i=0;i<model_.destinations.size();++i)if(model_.destinations[i].key==key&&!model_.destinations[i].here)queue({AltarUiActionKind::Travel,i});});
    handle_=model.GetModelHandle();std::ifstream file(path,std::ios::binary);require(bool(file),"Altar document missing");std::string source((std::istreambuf_iterator<char>(file)),{});require(!source.empty()&&source.size()<=1024*1024,"Altar document bound");
    document_=context.LoadDocumentFromMemory(source,path.filename().generic_string());require(document_,"Altar document load");document_->Show();
}
void AltarDocument::queue(AltarUiAction value){if(actions_.size()<8)actions_.push_back(value);}
void AltarDocument::update(const AltarUiModel& value){
    require(value.destinations.size()<=128&&value.name.size()<=64&&value.status.size()<=256,"Altar UI model bounds");for(const auto& d:value.destinations)require(d.name.size()<=64&&d.key.size()<=128&&d.element.size()<=160,"Altar UI destination bounds");
    if(value!=model_){if(!value.open)actions_.clear();model_=value;handle_.DirtyAllVariables();}
}
Rml::Element* AltarDocument::find(const std::string& id)const{return document_->GetElementById(id);}
std::vector<AltarUiAction> AltarDocument::drain(){auto result=std::move(actions_);actions_.clear();return result;}
}
