#include <darkangel/item_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/hash.hpp>
#include "../apps/editor/native_authoring.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace darkangel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){bool failed{};try{action();}catch(const std::exception&){failed=true;}check(failed,"Expected item validation failure");}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
}
int main(){try{
    auto root=std::filesystem::path(DAE_BINARY_DIR)/("item-assets-"+AssetId::random().text());auto source=root/"s";std::filesystem::create_directories(source);std::filesystem::copy(std::filesystem::path(DAE_SOURCE_DIR)/"content",source,std::filesystem::copy_options::recursive);
    AssetService assets(source,root/"c");assets.scan();editor_app::NativeAuthoring author;
    auto regular=author.create_item(assets,"Temple key","regular"),consumable=author.create_item(assets,"Potion","consumable");auto created=author.inventory(assets);check(created.size()>=2,"Item draft catalog missing");
    auto icon=assets.adopt("vfx/placeholder/Texture/glow.png");auto& image_item=author.open(assets,consumable).value;image_item["icon"]=icon.text();image_item["sources"]["icon"]="vfx/placeholder/Texture/glow.png";
    auto model=assets.cook("royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf").root;image_item["model"]=model.text();image_item["sources"]["model"]="royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf";
    author.open(assets,regular).value["description"]="Opens the temple gate.";author.record_changes(assets,"Item description");author.undo(assets);check(author.open(assets,regular).value.at("description")=="","Item Undo failed");author.redo(assets);author.save_all(assets);
    assets.package(consumable,root/"image.json");check(load_cooked_item(root/"image.json",assets.cas_path(),consumable).definition.icon==icon&&load_cooked_item(root/"image.json",assets.cas_path(),consumable).definition.model==model,"Inventory image/model closure missing");
    assets.package(regular,root/"regular.json");auto old=load_cooked_item(root/"regular.json",assets.cas_path(),regular);check(old.definition.description=="Opens the temple gate."&&old.definition.classification=="regular","Frozen regular item differs");
    auto path=source/author.open(assets,regular).asset.path;auto saved=read(path);auto bad=author.open(assets,regular).value;bad["stack_limit"]=0;author.apply(assets,regular,author.revision(),bad,"Invalid item quantity");rejects([&]{author.save_all(assets);});check(read(path)==saved&&load_cooked_item(root/"regular.json",assets.cas_path(),regular).generation==old.generation,"Failed item Save changed source/product");author.undo(assets);
    auto catalog=assets.assets();auto mask_source=std::find_if(catalog.begin(),catalog.end(),[](const auto& entry){return std::filesystem::path(entry.path).extension()==".dafacemask";});check(mask_source!=catalog.end(),"Fixture mask missing");auto mask=author.create_item(assets,"Owned mask","mask",mask_source->id);author.save_all(assets);assets.package(mask,root/"mask.json");check(load_cooked_item(root/"mask.json",assets.cas_path(),mask).definition.mask==mask_source->id,"Mask item dependency missing");
    auto manifest=nlohmann::json::parse(read(root/"mask.json"));for(auto& record:manifest["assets"])if(record["id"]==mask_source->id.text())record["sha256"]=std::string(64,'0');std::ofstream(root/"mixed.json")<<manifest.dump();rejects([&]{load_cooked_item(root/"mixed.json",assets.cas_path(),mask);});
    auto pending=author.create_item(assets,"Discard me","regular");author.delete_item(assets,pending);check(!author.drafts.contains(pending),"Pending item Delete failed");
    // Uncooked external sources count as references too; rejected deletion preserves both catalog and source.
    std::ofstream(source/"referenced.json")<<nlohmann::json{{"item",regular.text()}}.dump();rejects([&]{author.delete_item(assets,regular);});check(std::filesystem::exists(path),"Referenced item was deleted");{const auto identity=regular.text();constexpr char hex[]="0123456789abcdef";const auto first=static_cast<unsigned char>(identity[0]);std::ofstream(source/"referenced.json")<<"{\"item\":\"\\u00"<<hex[first>>4]<<hex[first&15]<<identity.substr(1)<<"\"}";rejects([&]{author.delete_item(assets,regular);});check(std::filesystem::exists(path),"Escaped JSON reference was bypassed");}std::filesystem::remove(source/"referenced.json");
    rejects([&]{assets.delete_item(author.open(assets,regular).asset.path,std::string(64,'0'));});author.delete_item(assets,regular);check(!std::filesystem::exists(path),"Item Delete left source");assets.scan();catalog=assets.assets();check(std::none_of(catalog.begin(),catalog.end(),[&](const auto& entry){return entry.id==regular;}),"Deleted item remained in catalog");check(load_cooked_item(root/"regular.json",assets.cas_path(),regular).definition.id==regular,"Deletion invalidated an existing frozen package");
    check(!std::filesystem::is_empty(root/"c/deleted-items"),"Deleted item source was not preserved");
    auto terrain=assets.cook("royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf").root;const std::array<AssetId,2> editor_roots{consumable,mask};assets.package(terrain,root/"editor.json",editor_roots);std::ofstream(std::filesystem::path(DAE_BINARY_DIR)/"item-fixture.json")<<nlohmann::json{{"source",source.generic_string()},{"cache",(root/"c").generic_string()},{"registry",(root/"editor.json").generic_string()},{"model",terrain.text()},{"consumable",consumable.text()},{"mask",mask.text()}}.dump(2);
    std::cout<<"Item assets: three types, native create/history/Save rollback, frozen mask closure/mixed generation, referenced/stale Delete, archive and prior package preservation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
