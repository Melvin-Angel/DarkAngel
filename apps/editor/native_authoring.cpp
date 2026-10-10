#include "native_authoring.hpp"
#include <darkangel/hash.hpp>
#include <fstream>
#include <set>
#include <algorithm>
#include <cctype>
namespace darkangel::editor_app {
namespace {void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);require(bool(f),"Cannot read native source");std::string s{std::istreambuf_iterator<char>(f),{}};require(s.size()<=65536,"Native draft size limit");return s;}}
ActionDefinition authoring_action(const nlohmann::json& source){
 auto core=source;if(core.at("schema")==2){core.erase("motion");core["schema"]=1;}
 return decode_action_source(core.dump());
}
unsigned NativeAuthoring::add_action_block(AssetService& assets,AssetId asset,const ActionBlock& block){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;auto action=authoring_action(value);
 require(action.blocks.size()<32,"Action has reached its 32-block limit");
 auto next=action_ids_[asset];for(const auto& existing:action.blocks)next=std::max(next,existing.id);
 require(next<UINT32_MAX,"Action block identity exhausted");++next;
 const char* kind=nullptr;
 switch(block.kind){case ActionBlockKind::Cue:kind="cue";break;case ActionBlockKind::HitWindow:kind="hit";break;case ActionBlockKind::Invulnerability:kind="invulnerability";break;case ActionBlockKind::MovementLock:kind="movement-lock";break;case ActionBlockKind::ComboWindow:kind="combo";break;case ActionBlockKind::Commit:kind="commit";break;}
 require(kind!=nullptr,"Unsupported action block kind");
 value["blocks"].push_back({{"id",next},{"track",block.track},{"begin",block.begin},{"end",block.end},{"kind",kind},{"key",block.key}});
 authoring_action(value);apply(assets,asset,revision_,std::move(value),"Create action block");action_ids_[asset]=next;return next;
}
void NativeAuthoring::remove_action_block(AssetService& assets,AssetId asset,unsigned block){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;require(value.at("kind")=="action","Select an action asset");
 auto& blocks=value.at("blocks");auto found=std::find_if(blocks.begin(),blocks.end(),[&](const auto& record){return record.at("id")==block;});
 require(found!=blocks.end(),"Action block no longer exists");unsigned high=action_ids_[asset];for(const auto& entry:blocks)high=std::max(high,entry.at("id").get<unsigned>());blocks.erase(found);authoring_action(value);
 apply(assets,asset,revision_,std::move(value),"Remove action block");action_ids_[asset]=high;
}
AttributeId NativeAuthoring::add_attribute(AssetService& assets,AssetId asset,std::string_view name,AttributeKind kind){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;auto schema=decode_attribute_asset(value.dump());
 require(schema.fields.size()<64&&static_cast<unsigned>(kind)<=1,"Attribute count/kind limit");
 auto next=attribute_ids_[asset];for(const auto& field:schema.fields)next=std::max(next,field.definition.id);require(next<UINT32_MAX,"Attribute identity exhausted");++next;
 value["attributes"].push_back({{"id",next},{"name",name},{"kind",kind==AttributeKind::Resource?"resource":"statistic"},{"base",0},{"minimum",0},{"maximum",kind==AttributeKind::Resource?100:1000},{"maximum_attribute",0},{"unit","points"},{"visibility","owner"}});
 decode_attribute_asset(value.dump());apply(assets,asset,revision_,std::move(value),"Create attribute definition");attribute_ids_[asset]=next;return next;
}
void NativeAuthoring::remove_attribute(AssetService& assets,AssetId asset,AttributeId attribute){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;require(value.at("kind")=="attributes","Select an attribute schema");auto& fields=value.at("attributes");
 auto found=std::find_if(fields.begin(),fields.end(),[&](const auto& field){return field.at("id")==attribute;});require(found!=fields.end(),"Attribute no longer exists");
 auto high=attribute_ids_[asset];for(const auto& field:fields)high=std::max(high,field.at("id").get<AttributeId>());fields.erase(found);decode_attribute_asset(value.dump());
 apply(assets,asset,revision_,std::move(value),"Remove attribute definition");attribute_ids_[asset]=high;
}
NativeDraft& NativeAuthoring::open(AssetService& assets,AssetId id){if(auto i=drafts.find(id);i!=drafts.end())return i->second;auto inventory=assets.assets();auto found=std::find_if(inventory.begin(),inventory.end(),[&](const auto& a){return a.id==id;});require(found!=inventory.end(),"Native source is missing; refresh assets");auto ext=std::filesystem::path(found->path).extension();require(ext==".daability"||ext==".daeffect"||ext==".daaction"||ext==".dakit"||ext==".dacharacter"||ext==".daplayer"||ext==".daattributes","Choose a native gameplay asset");auto bytes=read(assets.source_root()/found->path);require(drafts.size()<64,"Native draft limit");auto inserted=drafts.emplace(id,NativeDraft{*found,bytes,nlohmann::json::parse(bytes)}).first;observed_[id]=inserted->second.value;return inserted->second;}
CookResult NativeAuthoring::save(AssetService& assets,AssetId id){auto& d=open(assets,id);record_changes(assets,"Edit gameplay fields");auto bytes=d.value.dump(2)+"\n";NativeSourceEdit edit{d.asset.path,sha256(d.saved),bytes};auto prepared=assets.prepare_native({&edit,1});auto results=assets.commit_native(prepared);d.saved=bytes;++revision_;for(const auto& r:results)if(r.root==id){d.asset.generation=r.generation;return r;}throw std::runtime_error("Saved source result missing");}
AssetId NativeAuthoring::create_character(AssetService& assets,AssetId skin,std::string_view name){
 require(!name.empty()&&name.size()<=64&&std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}),"Use a name with letters, numbers, underscores or hyphens");auto inventory=assets.assets();auto source=std::find_if(inventory.begin(),inventory.end(),[&](const auto& asset){return asset.id==skin;});require(source!=inventory.end(),"Select a catalog skin");auto id=AssetId::random();nlohmann::json value={{"schema",1},{"kind","character"},{"asset",id.text()},{"skin",skin.text()},{"sources",{{"skin",source->path}}}};assets.create_native("characters/"+std::string(name)+".dacharacter",value.dump(2)+"\n");open(assets,id);return id;
}
AssetId NativeAuthoring::create_player(AssetService& assets,AssetId character,AssetId kit,std::string_view name){
 require(!name.empty()&&name.size()<=64&&std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}),"Use a name with letters, numbers, underscores or hyphens");auto inventory=assets.assets();auto path=[&](AssetId id,const char* extension){auto source=std::find_if(inventory.begin(),inventory.end(),[&](const auto& asset){return asset.id==id&&std::filesystem::path(asset.path).extension()==extension;});require(source!=inventory.end(),"Select a typed catalog actor reference");return source->path;};auto id=AssetId::random();nlohmann::json value={{"schema",1},{"kind","player"},{"asset",id.text()},{"character",character.text()},{"loadout",{{"kit",kit.text()}}},{"sources",{{"character",path(character,".dacharacter")},{"kit",path(kit,".dakit")}}}};assets.create_native("players/"+std::string(name)+".daplayer",value.dump(2)+"\n");open(assets,id);return id;
}
AssetId NativeAuthoring::duplicate(AssetService& assets,AssetId id,std::string_view name,bool blank){auto& d=open(assets,id);require(!name.empty()&&name.size()<=64&&std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}),"Use a name with letters, numbers, underscores or hyphens");auto value=d.value;auto fresh=AssetId::random();value["asset"]=fresh.text();if(value.contains("cues"))for(auto& cue:value["cues"])cue["id"]=AssetId::random().text();if(blank&&value["kind"]=="effect"){value["evaluator"]=0;value["period_ticks"]=0;value["granted_tags"]=nlohmann::json::array();value["modifiers"]=nlohmann::json::array();value["cues"]=nlohmann::json::array();}if(blank&&value["kind"]=="ability"){value["costs"]=nlohmann::json::array();value["cooldown_ticks"]=0;for(auto& hit:value["melee"])hit["power"]=0;}
 auto path=std::filesystem::path(d.asset.path).parent_path()/(std::string(name)+std::filesystem::path(d.asset.path).extension().string());assets.create_native(path.generic_string(),value.dump(2)+"\n");open(assets,fresh);return fresh;}
void NativeAuthoring::assign(AssetService& assets,AssetId kit,std::size_t slot,AssetId ability){record_changes(assets,"Edit gameplay fields");auto& k=open(assets,kit).value;auto& a=open(assets,ability);check_sources(assets,{{kit,k},{ability,a.value}});require(k["kind"]=="combat_kit"&&a.value["kind"]=="ability"&&slot<k["slots"].size(),"Select a kit slot and ability");require(k["attributes"]==a.value["attributes"],"Ability uses an incompatible attribute schema");auto old=k["slots"][slot]["ability"];k["slots"][slot]["ability"]=ability.text();k["sources"]["abilities"][ability.text()]=a.asset.path;
 bool retained=false;for(const auto& s:k["slots"])retained|=s["ability"]==old;if(!old.is_null()&&!retained){k["sources"]["abilities"].erase(old.get<std::string>());if(k.contains("effect_bindings")){auto& bindings=k["effect_bindings"];bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[&](const auto& b){return b["ability"]==old;}),bindings.end());}}
 record_changes(assets,"Assign kit slot");
}
void NativeAuthoring::bind(AssetService& assets,AssetId kit,AssetId ability,unsigned block,AssetId effect,double power){record_changes(assets,"Edit gameplay fields");auto& k=open(assets,kit).value;auto& a=open(assets,ability).value;auto& e=open(assets,effect);check_sources(assets,{{kit,k},{ability,a},{effect,e.value}});require(e.value["kind"]=="effect"&&k["attributes"]==e.value["attributes"],"Effect uses an incompatible attribute schema");bool granted=false,hit=false;for(const auto& s:k["slots"])granted|=s["ability"]==ability.text();for(const auto& h:a["melee"])hit|=h["block"]==block;require(granted&&hit,"Assign ability to the kit and select its hit window first");auto& effects=k["effects"];if(effects.is_null())effects=nlohmann::json::array();if(std::find(effects.begin(),effects.end(),effect.text())==effects.end())effects.push_back(effect.text());k["sources"]["effects"][effect.text()]=e.asset.path;auto& bindings=k["effect_bindings"];if(bindings.is_null())bindings=nlohmann::json::array();for(auto& b:bindings)if(b["ability"]==ability.text()&&b["hit_block"]==block&&b["effect"]==effect.text()){b["power"]=power;record_changes(assets,"Bind effect");return;}bindings.push_back({{"ability",ability.text()},{"hit_block",block},{"effect",effect.text()},{"power",power}});record_changes(assets,"Bind effect");}
bool NativeAuthoring::dirty()const{for(const auto& [id,d]:drafts)if(d.dirty())return true;return false;}
void NativeAuthoring::save_all(AssetService& assets,std::span<const AssetId> roots){
 record_changes(assets,"Edit gameplay fields");std::vector<NativeSourceEdit> edits;
 for(const auto& [id,d]:drafts)if(d.dirty())edits.push_back({d.asset.path,sha256(d.saved),d.value.dump(2)+"\n"});
 if(edits.empty())return;auto candidate=assets.prepare_native(edits,roots);auto results=assets.commit_native(candidate);
 for(auto& [id,d]:drafts)if(d.dirty()){d.saved=d.value.dump(2)+"\n";for(const auto& r:results)if(r.root==id)d.asset.generation=r.generation;}
 ++revision_;continuous_=false;
}
void NativeAuthoring::check_sources(AssetService& assets,const Values& values)const{
 for(const auto& [id,value]:values){auto it=drafts.find(id);require(it!=drafts.end(),"Stale history command; source was reloaded");if(read(assets.source_root()/it->second.asset.path)!=it->second.saved)throw std::runtime_error("Stale authoring command for "+it->second.asset.path+": external source changed. Reload source before editing or using history");}
}
void NativeAuthoring::record_changes(AssetService& assets,std::string_view label,bool continuous){
 Command command;command.label=label;
 for(const auto& [id,d]:drafts){auto it=observed_.find(id);require(it!=observed_.end(),"Reload sources through the authoring command");if(it->second!=d.value){command.before[id]=it->second;command.after[id]=d.value;}}
 if(command.after.empty()){if(!continuous)continuous_=false;return;}
 try{check_sources(assets,command.before);}catch(...){for(const auto& [id,value]:command.before)drafts.at(id).value=value;throw;}
 if(continuous_&&!undo_.empty()&&undo_.back().label==command.label){auto& previous=undo_.back();for(const auto& [id,value]:command.before)previous.before.try_emplace(id,value);for(const auto& [id,value]:command.after)previous.after[id]=value;}
 else undo_.push_back(command);
 for(const auto& [id,value]:command.after)observed_[id]=value;redo_.clear();continuous_=continuous;++revision_;
 std::size_t bytes{};for(const auto& entry:undo_){for(const auto& [id,v]:entry.before)bytes+=v.dump().size();for(const auto& [id,v]:entry.after)bytes+=v.dump().size();}
 while(undo_.size()>128||bytes>16*1024*1024){for(const auto& [id,v]:undo_.front().before)bytes-=v.dump().size();for(const auto& [id,v]:undo_.front().after)bytes-=v.dump().size();undo_.erase(undo_.begin());}
}
void NativeAuthoring::set_action_block_times(AssetService& assets,AssetId asset,unsigned id,unsigned begin,unsigned end,bool continuous){
 auto value=open(assets,asset).value;auto& blocks=value.at("blocks");
 auto found=std::find_if(blocks.begin(),blocks.end(),[&](const auto& b){return b.at("id")==id;});require(found!=blocks.end(),"Action block no longer exists");
 (*found)["begin"]=begin;(*found)["end"]=end;authoring_action(value);
 apply(assets,asset,revision_,std::move(value),"Drag action block "+std::to_string(id),continuous);
}
void NativeAuthoring::cancel_edit(AssetService& assets,std::uint64_t expected,std::string_view label){
 require(expected==revision_&&!undo_.empty()&&undo_.back().label==label,"Gesture changed elsewhere; cannot cancel a different command");
 check_sources(assets,undo_.back().before);
 for(const auto& [id,value]:undo_.back().before){drafts.at(id).value=value;observed_[id]=value;}
 undo_.pop_back();continuous_=false;++revision_;
}
void NativeAuthoring::apply(AssetService& assets,AssetId id,std::uint64_t expected,nlohmann::json value,std::string_view label,bool continuous){
 require(expected==revision_,"Stale authoring command revision");auto& d=open(assets,id);check_sources(assets,{{id,d.value}});auto old=d.value;d.value=std::move(value);try{record_changes(assets,label,continuous);}catch(...){d.value=old;throw;}
}
void NativeAuthoring::travel(AssetService& assets,bool forward){
 record_changes(assets,"Edit gameplay fields");auto& from=forward?redo_:undo_;auto& to=forward?undo_:redo_;require(!from.empty(),"Authoring history is empty");auto command=from.back();const auto& expected=forward?command.before:command.after;const auto& target=forward?command.after:command.before;
 check_sources(assets,expected);for(const auto& [id,value]:expected)require(drafts.at(id).value==value,"Stale history command; draft changed");
 for(const auto& [id,value]:target){drafts.at(id).value=value;observed_[id]=value;}to.push_back(std::move(command));from.pop_back();++revision_;continuous_=false;
}
void NativeAuthoring::undo(AssetService& assets){travel(assets,false);}void NativeAuthoring::redo(AssetService& assets){travel(assets,true);}
void NativeAuthoring::reload(AssetService& assets,AssetId id){
 auto& d=open(assets,id);auto bytes=read(assets.source_root()/d.asset.path);auto value=nlohmann::json::parse(bytes);auto old=nlohmann::json::parse(d.saved);require(value.at("asset")==old.at("asset")&&value.at("kind")==old.at("kind")&&value.at("schema")==old.at("schema"),"Reload preserves UUID/type/schema; refresh inventory for replaced assets");
 d.saved=std::move(bytes);d.value=std::move(value);observed_[id]=d.value;undo_.clear();redo_.clear();continuous_=false;++revision_;
}
void NativeAuthoring::revert(AssetService& assets,AssetId id){auto& d=open(assets,id);apply(assets,id,revision_,nlohmann::json::parse(d.saved),"Revert draft");}
void package_authoring(AssetService& assets,std::span<const AssetId> roots,const std::filesystem::path& output){require(!roots.empty()&&roots.size()<=64,"Scene package root bounds");assets.scan();auto inventory=assets.assets();for(auto id:roots){auto found=std::find_if(inventory.begin(),inventory.end(),[&](const auto& a){return a.id==id;});require(found!=inventory.end(),"Scene package source is missing");assets.cook(found->path);}assets.package(roots.front(),output,roots.subspan(1));}
}
