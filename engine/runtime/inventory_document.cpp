#include "inventory_document.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <set>
#include <stdexcept>

namespace darkangel {
namespace {void require(bool b,const char* e){if(!b)throw std::runtime_error(e);}std::uint64_t instance(std::string_view key){std::uint64_t value{};const auto parsed=std::from_chars(key.data(),key.data()+key.size(),value);require(parsed.ec==std::errc{}&&parsed.ptr==key.data()+key.size()&&value,"Inventory UI instance identity");return value;}}
InventoryDocument::InventoryDocument(Rml::Context& context,const std::filesystem::path& path){
    auto model=context.CreateDataModel("inventory");require(bool(model),"Inventory data model missing");
    auto row=model.RegisterStruct<InventoryUiItem>();row.RegisterMember("key",&InventoryUiItem::key);row.RegisterMember("element",&InventoryUiItem::element);row.RegisterMember("name",&InventoryUiItem::name);row.RegisterMember("kind",&InventoryUiItem::kind);row.RegisterMember("quantity",&InventoryUiItem::quantity);row.RegisterMember("equipped",&InventoryUiItem::equipped);row.RegisterMember("mask",&InventoryUiItem::mask);row.RegisterMember("usable",&InventoryUiItem::usable);
    auto slot=model.RegisterStruct<InventoryUiSlot>();slot.RegisterMember("index",&InventoryUiSlot::index);slot.RegisterMember("name",&InventoryUiSlot::name);slot.RegisterMember("label",&InventoryUiSlot::label);slot.RegisterMember("element",&InventoryUiSlot::element);slot.RegisterMember("equip_element",&InventoryUiSlot::equip_element);slot.RegisterMember("unequip_element",&InventoryUiSlot::unequip_element);slot.RegisterMember("empty",&InventoryUiSlot::empty);
    require(model.RegisterArray<std::vector<InventoryUiItem>>()&&model.RegisterArray<std::vector<InventoryUiSlot>>(),"Inventory array registration");
    require(model.Bind("empty",&empty_)&&model.Bind("open",&model_.open)&&model.Bind("ready",&model_.ready)&&model.Bind("busy",&model_.busy)&&model.Bind("items",&model_.items)&&model.Bind("slots",&model_.slots)&&model.Bind("status",&model_.status)&&model.Bind("selected",&selected_)&&model.Bind("selected_name",&name_)&&model.Bind("selected_description",&description_)&&model.Bind("selected_kind",&kind_)&&model.Bind("selected_mask",&selected_mask_)&&model.Bind("selected_usable",&selected_usable_),"Inventory binding");
    auto event=[&](const char* name,Rml::DataEventFunc function){require(model.BindEventCallback(name,std::move(function)),"Inventory event binding");};
    event("toggle",[this](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList&){queue({InventoryUiActionKind::Toggle});});
    event("close",[this](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList&){queue({InventoryUiActionKind::Close});});
    event("select",[this](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList& args){if(args.size()==1)select(args[0].Get<Rml::String>());});
    event("equip",[this](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList& args){if(model_.ready&&!model_.busy&&selected_mask_&&args.size()==1){const auto slot=args[0].Get<int>();if(slot>=0&&slot<4)queue({InventoryUiActionKind::Equip,instance(selected_),unsigned(slot)});}});
    event("use",[this](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList&){if(model_.ready&&!model_.busy&&selected_usable_&&!selected_.empty())queue({InventoryUiActionKind::Use,instance(selected_)});});
    event("unequip",[this](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList& args){if(model_.ready&&!model_.busy&&args.size()==1){const auto slot=args[0].Get<int>();if(slot>=0&&slot<4&&!model_.slots[std::size_t(slot)].empty)queue({InventoryUiActionKind::Unequip,0,unsigned(slot)});}});
    handle_=model.GetModelHandle();std::ifstream file(path,std::ios::binary);require(bool(file),"Inventory document missing");std::string source((std::istreambuf_iterator<char>(file)),{});require(!source.empty()&&source.size()<=1024*1024,"Inventory document bound");document_=context.LoadDocumentFromMemory(source,path.filename().generic_string());require(document_,"Inventory document load");document_->Show();
}
void InventoryDocument::queue(InventoryUiAction value){if(actions_.size()<16)actions_.push_back(value);}
void InventoryDocument::select(std::string key){auto found=std::find_if(model_.items.begin(),model_.items.end(),[&](const auto& row){return row.key==key;});selected_=found==model_.items.end()?"":found->key;name_=found==model_.items.end()?"Select an item":found->name;description_=found==model_.items.end()?"":found->description;kind_=found==model_.items.end()?"":found->kind;selected_mask_=found!=model_.items.end()&&found->mask;selected_usable_=found!=model_.items.end()&&found->usable;handle_.DirtyAllVariables();}
void InventoryDocument::update(const InventoryUiModel& value){
    require(value.items.size()<=128&&value.slots.size()==4&&value.status.size()<=256&&value.owner.size()<=64,"Inventory UI model bounds");std::set<std::string> keys;
    for(const auto& row:value.items){instance(row.key);require(keys.insert(row.key).second&&row.name.size()<=128&&row.description.size()<=4096&&row.kind.size()<=32&&row.quantity>0&&row.quantity<=1000000&&row.equipped>=-1&&row.equipped<4,"Inventory UI row");}
    for(unsigned i=0;i<4;++i)require(value.slots[i].index==int(i)&&value.slots[i].name.size()<=32&&value.slots[i].label.size()<=128,"Inventory UI slot");
    if(value!=model_){const bool changed_owner=value.owner!=model_.owner;model_=value;empty_=model_.items.empty();if(changed_owner){selected_.clear();actions_.clear();}select(selected_);handle_.DirtyAllVariables();}
}
Rml::Element* InventoryDocument::find(const std::string& id)const{return document_->GetElementById(id);}
std::vector<InventoryUiAction> InventoryDocument::drain(){auto result=std::move(actions_);actions_.clear();return result;}
}
