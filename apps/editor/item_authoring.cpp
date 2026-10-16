#include "native_authoring.hpp"
#include <darkangel/inventory.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <stdexcept>
namespace darkangel::editor_app {
AssetId NativeAuthoring::create_item(AssetService& assets,std::string_view name,std::string_view classification,AssetId mask){
    if(classification!="regular"&&classification!="consumable"&&classification!="mask")throw std::invalid_argument("Choose Regular, Consumable or Mask");
    ItemDefinition item;item.id=AssetId::random();item.name=std::string(name);item.classification=classification;item.mask=mask;item.equippable=classification=="mask";item.usable=classification=="consumable";item.stack_limit=classification=="mask"?1:99;ItemCatalog catalog;catalog.insert(item);
    auto value=nlohmann::json::parse(encode_item_definition(item));value["sources"]=nlohmann::json::object();
    if(mask!=AssetId{}){auto entries=inventory(assets);auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.id==mask&&std::filesystem::path(entry.path).extension()==".dafacemask";});if(found==entries.end())throw std::invalid_argument("Select an existing Mask definition");value["sources"]["mask"]=found->path;}
    return stage_creation(assets,"items/"+item.id.text()+".daitem",std::move(value));
}
void NativeAuthoring::rename_prefab(AssetService& assets,AssetId id,std::string_view name){
    if(name.empty()||name.size()>64||!std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}))throw std::invalid_argument("Use a name with letters, numbers, underscores or hyphens");
    auto& prefab=open(assets,id);if(prefab.value.at("kind")!="prefab")throw std::invalid_argument("Select a prefab");if(prefab.dirty())throw std::invalid_argument("Save or undo the prefab's changes before renaming its file");
    assets.rename_native(prefab.asset.path,sha256(prefab.saved),"prefabs/"+std::string(name)+".daprefab");
    // The draft is opened again from the new path; its values and history are unchanged.
    drafts.erase(id);observed_.erase(id);open(assets,id);++revision_;continuous_=false;
}
void NativeAuthoring::delete_prefab(AssetService& assets,AssetId id){
    auto& prefab=open(assets,id);if(prefab.value.at("kind")!="prefab")throw std::invalid_argument("Select a prefab");const auto identity=id.text();
    for(const auto& [other,draft]:drafts)if(other!=id&&draft.value.dump().find(identity)!=std::string::npos)throw std::invalid_argument("Another draft references this prefab; remove that reference first");
    if(!prefab.pending)assets.delete_item(prefab.asset.path,sha256(prefab.saved));
    auto touches=[&](const Command& command){return command.before.contains(id)||command.after.contains(id)||command.creations.contains(id);};std::erase_if(undo_,touches);std::erase_if(redo_,touches);drafts.erase(id);observed_.erase(id);++revision_;continuous_=false;
}
void NativeAuthoring::delete_item(AssetService& assets,AssetId id){
    auto& item=open(assets,id);if(item.value.at("kind")!="item")throw std::invalid_argument("Select an Item");const auto identity=id.text();
    for(const auto& [other,draft]:drafts)if(other!=id&&draft.value.dump().find(identity)!=std::string::npos)throw std::invalid_argument("Another draft references this item; remove that reference first");
    if(!item.pending)assets.delete_item(item.asset.path,sha256(item.saved));
    auto touches=[&](const Command& command){return command.before.contains(id)||command.after.contains(id)||command.creations.contains(id);};std::erase_if(undo_,touches);std::erase_if(redo_,touches);drafts.erase(id);observed_.erase(id);++revision_;continuous_=false;
}
}
