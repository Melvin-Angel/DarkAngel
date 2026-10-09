#include "assets_internal.hpp"
#include <darkangel/combat_kit_assets.hpp>
#include <darkangel/hash.hpp>
#include <set>
namespace darkangel {
using namespace assets_detail;
namespace {
void fields(const Json& value,std::initializer_list<const char*> names){require(value.is_object()&&value.size()==names.size(),"CombatKit field count");for(auto name:names)require(value.contains(name),"CombatKit missing/unknown field");}
AssetId reference(const Json& value){return AssetId::parse(value.get<std::string>());}
std::shared_ptr<const CombatKitDefinition> definition(const Json& data,const InputProfile& input){
    fields(data,{"schema","kind","asset","input","attributes","locomotion_stance","slots"});require(data.at("schema")==1&&data.at("kind")=="combat_kit","CombatKit schema/type");require(reference(data.at("input"))==input.id,"CombatKit input identity");reference(data.at("attributes"));
    CombatKitDefinition result;result.id=reference(data.at("asset"));result.locomotion_stance=reference(data.at("locomotion_stance"));result.generation=sha256(data.dump());
    const auto& slots=data.at("slots");require(slots.is_array()&&slots.size()==combat_slot_count,"CombatKit canonical slot count");const char* names[]={"light","heavy","ranged_light","ranged_heavy","spell1","spell2","block","parry"};
    for(unsigned index=0;index<combat_slot_count;++index){const auto& slot=slots[index];fields(slot,{"slot","input_action","ability"});require(slot.at("slot")==names[index],"CombatKit slot order");auto& target=result.slots[index];target.slot=static_cast<CombatSlot>(index);require(slot.at("input_action").is_number_unsigned()&&slot.at("input_action").get<std::uint64_t>()<=UINT32_MAX,"CombatKit input action integer");target.input_action=slot.at("input_action").get<unsigned>();if(!slot.at("ability").is_null())target.ability=reference(slot.at("ability"));}
    validate_combat_kit(result,input);return std::make_shared<const CombatKitDefinition>(std::move(result));
}
struct Registry {
    std::map<AssetId,Json> records;std::filesystem::path cas;
    Registry(const std::filesystem::path& path,const std::filesystem::path& storage):cas(storage){auto manifest=json(read(path,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&!manifest.at("assets").empty()&&manifest.at("assets").size()<=512,"CombatKit registry schema/count");for(const auto& record:manifest.at("assets"))require(records.emplace(reference(record.at("id")),record).second,"Duplicate CombatKit product identity");require(records.contains(reference(manifest.at("root"))),"CombatKit registry root missing");}
    Json load(AssetId id)const{const auto& record=records.at(id);require(record.at("kind")=="combat_kit"&&record.at("extension")=="combat-kit.json","CombatKit product type");auto digest=record.at("sha256").get<std::string>();require(digest.size()==64&&digest.find_first_not_of("0123456789abcdef")==digest.npos,"CombatKit digest");auto bytes=read(cas/(digest+".combat-kit.json"),65536);require(sha256(bytes)==digest,"CombatKit product hash mismatch");return json(bytes,65536);}
};
}
CookedCombatKit load_cooked_combat_kit(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    Registry records(registry,cas);auto data=records.load(id);auto frozen=data.at("frozen");data.erase("frozen");require(frozen.is_object()&&!frozen.empty()&&frozen.size()<=128,"CombatKit frozen dependency count");
    for(const auto& [key,digest]:frozen.items())require(records.records.at(AssetId::parse(key)).at("sha256")==digest,"CombatKit frozen dependency generation mismatch");
    CookedCombatKit result;result.input=load_cooked_input(registry,cas,reference(data.at("input")));result.definition=definition(data,result.input);require(result.definition->id==id,"CombatKit product identity mismatch");
    auto pinned=[&](AssetId dependency){require(frozen.contains(dependency.text()),"CombatKit missing frozen dependency");};pinned(result.input.id);pinned(reference(data.at("attributes")));pinned(result.definition->locomotion_stance);
    result.attributes=load_cooked_attributes(registry,cas,reference(data.at("attributes")));result.stance=load_cooked_graph(registry,cas,result.definition->locomotion_stance);std::set<AssetId> abilities;
    for(const auto& slot:result.definition->slots)if(slot.ability!=AssetId{}&&abilities.insert(slot.ability).second){pinned(slot.ability);auto ability=load_cooked_ability(registry,cas,slot.ability);require(ability.attributes.id==result.attributes.id&&ability.attributes.generation==result.attributes.generation,"CombatKit ability attribute schema mismatch");if(ability.tags){pinned(ability.tags->id);require(!result.tags||(result.tags->id==ability.tags->id&&result.tags->generation==ability.tags->generation),"CombatKit needs one coherent actor tag registry");result.tags=std::move(ability.tags);}result.abilities.push_back(std::move(ability.definition));}
    auto full=data;full["frozen"]=std::move(frozen);auto compiled=*result.definition;compiled.generation=sha256(full.dump());result.definition=std::make_shared<const CombatKitDefinition>(std::move(compiled));return result;
}
namespace assets_detail {
Import import_combat_kit(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,65536);auto data=json(bytes,65536);auto locators=data.at("sources");data.erase("sources");fields(locators,{"input","attributes","locomotion_stance","abilities"});
    auto input_path=within(root,locators.at("input").get<std::string>()),attributes_path=within(root,locators.at("attributes").get<std::string>()),graph_path=within(root,locators.at("locomotion_stance").get<std::string>());
    require(input_path.extension()==".dainput"&&attributes_path.extension()==".daattributes"&&graph_path.extension()==".dagraph","CombatKit source dependency type");auto input=decode_input_source(read(input_path,65536));auto kit=definition(data,input);if(!inspect)require(ids.size()==1&&ids.at("$source")==kit->id,"CombatKit source identity");
    auto attributes_id=reference(data.at("attributes"));require(reference(json(read(attributes_path,65536)).at("asset"))==attributes_id&&reference(json(read(graph_path,65536)).at("asset"))==kit->locomotion_stance,"CombatKit source dependency identity");
    Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);std::map<AssetId,Product> products;std::set<AssetId> closure,abilities;
    auto merge=[&](Import imported){require(!imported.identities.empty(),"CombatKit dependency inspection identities");for(auto asset:imported.identities){require(asset!=kit->id,"CombatKit parent/dependency identity conflict");closure.insert(asset);}for(const auto& [path,hash]:imported.inputs){auto [found,inserted]=result.inputs.emplace(path,hash);require(inserted||found->second==hash,"CombatKit source changed during composite inspection");}for(auto& product:imported.products){auto [found,inserted]=products.emplace(product.id,product);require(inserted||(found->second.kind==product.kind&&found->second.extension==product.extension&&found->second.bytes==product.bytes&&found->second.required==product.required),"CombatKit conflicting shared frozen generations");}};
    merge(import_input(root,input_path,{{"$source",input.id}},inspect));merge(import_ability(root,attributes_path,{{"$source",attributes_id}},inspect));merge(import_graph(root,graph_path,{{"$source",kit->locomotion_stance}},inspect));
    for(const auto& slot:kit->slots)if(slot.ability!=AssetId{})abilities.insert(slot.ability);const auto& locations=locators.at("abilities");require(locations.is_object()&&locations.size()==abilities.size(),"CombatKit ability locator catalogue");
    std::optional<AssetId> tag_registry;for(auto ability:abilities){auto path=within(root,locations.at(ability.text()).get<std::string>());require(path.extension()==".daability","CombatKit ability source type");auto source_data=json(read(path,65536));require(reference(source_data.at("asset"))==ability&&reference(source_data.at("attributes"))==attributes_id,"CombatKit ability source/schema identity");if(source_data.contains("tags")){auto tags=reference(source_data.at("tags"));require(!tag_registry||*tag_registry==tags,"CombatKit needs one coherent actor tag registry");tag_registry=tags;}merge(import_ability(root,path,{{"$source",ability}},inspect));}
    require(closure.size()<=128,"CombatKit closure budget");result.product_count=closure.size()+1;result.identities.assign(closure.begin(),closure.end());result.identities.push_back(kit->id);if(inspect)return result;require(products.size()==closure.size(),"CombatKit closure product count");
    auto frozen=Json::object();std::vector<AssetId> dependencies;for(auto& [asset,product]:products){frozen[asset.text()]=sha256(product.bytes);dependencies.push_back(asset);result.products.push_back(std::move(product));}data["frozen"]=std::move(frozen);result.products.push_back({kit->id,"combat_kit","combat-kit.json",data.dump(),std::move(dependencies)});return result;
}
}
}
