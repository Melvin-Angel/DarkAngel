#include "native_authoring.hpp"
#include <darkangel/hash.hpp>
#include <darkangel/tag_assets.hpp>
#include <fstream>
#include <set>
#include <algorithm>
#include <cctype>
#include <cmath>
namespace darkangel::editor_app {
namespace {void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);require(bool(f),"Cannot read native source");std::string s{std::istreambuf_iterator<char>(f),{}};require(s.size()<=65536,"Native draft size limit");return s;}}
ActionDefinition authoring_action(const nlohmann::json& source){
 auto core=source;if(core.at("schema")==2){core.erase("motion");core.erase("mask");core["schema"]=1;}
 return decode_action_source(core.dump());
}

namespace {
nlohmann::json& graph_node(nlohmann::json& value,unsigned id){require(value.at("kind")=="graph","Choose a graph asset");auto& nodes=value.at("nodes");auto found=std::find_if(nodes.begin(),nodes.end(),[&](const auto& node){return node.at("id")==id;});require(found!=nodes.end(),"Graph node no longer exists");return *found;}
}

unsigned NativeAuthoring::add_graph_blend(AssetService& assets,AssetId asset,bool two_dimensional,unsigned first,unsigned second,unsigned third){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;graph_node(value,first);graph_node(value,second);if(two_dimensional)graph_node(value,third);require(value["nodes"].size()<32,"Graph node limit is32");auto next=graph_ids_[asset];for(const auto& node:value["nodes"])next=std::max(next,node["id"].get<unsigned>());require(next<UINT32_MAX,"Graph node identity exhausted");++next;
 nlohmann::json points=nlohmann::json::array({{{"input",first},{"x",0.0},{"y",0.0}},{{"input",second},{"x",1.0},{"y",0.0}}}),triangles=nlohmann::json::array();if(two_dimensional){points.push_back({{"input",third},{"x",0.0},{"y",1.0}});triangles.push_back({0u,1u,2u});}value["nodes"].push_back({{"id",next},{"kind",two_dimensional?"blend2d":"blend1d"},{"parameter","speed"},{"points",std::move(points)},{"triangles",std::move(triangles)}});apply(assets,asset,revision_,std::move(value),"Create graph blend node");graph_ids_[asset]=next;return next;
}
unsigned NativeAuthoring::add_graph_tag_select(AssetService& assets,AssetId asset,AssetId registry,TagId tag,unsigned inactive,unsigned active){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;graph_node(value,inactive);graph_node(value,active);auto& dictionary=open(assets,registry);auto definitions=decode_tag_asset(dictionary.value.dump());definitions.dictionary()->validate({{tag},{},{}});
 for(const auto& field:definitions.fields)if(field.definition.id==tag)require(field.definition.visibility!=AttributeVisibility::Server,"Graph cannot select server-only tags");
 require(!value.contains("tags")||value.at("tags")==registry.text(),"Graph selectors require one coherent tag registry");require(value.at("nodes").size()<32,"Graph node limit is32");
 auto next=graph_ids_[asset];for(const auto& node:value.at("nodes"))next=std::max(next,node.at("id").get<unsigned>());require(next<UINT32_MAX,"Graph node identity exhausted");++next;
 value["tags"]=registry.text();value["tag_source"]=dictionary.asset.path;value["nodes"].push_back({{"id",next},{"kind","tag-select"},{"requirements",{{"all",{tag}},{"any",nlohmann::json::array()},{"none",nlohmann::json::array()}}},{"points",nlohmann::json::array({{{"input",inactive},{"x",0.0},{"y",0.0}},{{"input",active},{"x",0.0},{"y",0.0}}})}});
 check_sources(assets,{{registry,dictionary.value}});apply(assets,asset,revision_,std::move(value),"Create tag-conditioned graph selector");graph_ids_[asset]=next;return next;
}
unsigned NativeAuthoring::duplicate_graph_node(AssetService& assets,AssetId asset,unsigned node){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;auto copy=graph_node(value,node);require(value["nodes"].size()<32,"Graph node limit is 32");auto next=graph_ids_[asset];for(const auto& entry:value["nodes"])next=std::max(next,entry["id"].get<unsigned>());require(next<UINT32_MAX,"Graph node identity exhausted");copy["id"]=++next;value["nodes"].push_back(std::move(copy));apply(assets,asset,revision_,std::move(value),"Duplicate graph node");graph_ids_[asset]=next;return next;
}
void NativeAuthoring::connect_graph_input(AssetService& assets,AssetId asset,unsigned node,unsigned point,unsigned input){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;auto& target=graph_node(value,node);graph_node(value,input);
 require(target.contains("points")&&point<target.at("points").size(),"Choose an existing blend input port");
 std::set<unsigned> visited;std::vector<unsigned> pending{input};while(!pending.empty()){auto current=pending.back();pending.pop_back();require(current!=node,"Graph connection would create a cycle");if(!visited.insert(current).second)continue;const auto& source=graph_node(value,current);if(source.contains("points"))for(const auto& p:source.at("points"))pending.push_back(p.at("input").get<unsigned>());require(visited.size()<=32,"Graph node count");}
 target["points"][point]["input"]=input;apply(assets,asset,revision_,std::move(value),"Connect animation graph input");
}
void NativeAuthoring::remove_graph_node(AssetService& assets,AssetId asset,unsigned id){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;graph_node(value,id);require(value["root"]!=id,"Choose another root before removing this node");auto high=graph_ids_[asset];for(const auto& node:value["nodes"]){high=std::max(high,node["id"].get<unsigned>());if(node.contains("points"))for(const auto& point:node["points"])require(point["input"]!=id,"Disconnect node inputs before removing this node");}
 auto& nodes=value["nodes"];nodes.erase(std::find_if(nodes.begin(),nodes.end(),[&](const auto& node){return node["id"]==id;}));std::set<std::string> clips;for(const auto& node:nodes)if(node["kind"]=="clip")clips.insert(node["clip"].get<std::string>());auto& sources=value["sources"];for(auto it=sources.begin();it!=sources.end();)if(!clips.contains(it.key()))it=sources.erase(it);else ++it;if(std::none_of(nodes.begin(),nodes.end(),[](const auto& node){return node.at("kind")=="tag-select";})){value.erase("tags");value.erase("tag_source");}apply(assets,asset,revision_,std::move(value),"Remove graph node");graph_ids_[asset]=high;
}
void NativeAuthoring::add_graph_point(AssetService& assets,AssetId asset,unsigned id,unsigned input,double x,double y){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;graph_node(value,input);auto& node=graph_node(value,id);require((node["kind"]=="blend1d"||node["kind"]=="blend2d")&&node["points"].size()<32,"Select a blend node with room for a point");require(std::isfinite(x)&&std::isfinite(y)&&std::abs(x)<=100&&std::abs(y)<=100,"Graph coordinates must be finite within100");if(node["kind"]=="blend1d")require(y==0,"1D points require Y=0");node["points"].push_back({{"input",input},{"x",x},{"y",y}});apply(assets,asset,revision_,std::move(value),"Add graph blend point");
}
void NativeAuthoring::remove_graph_point(AssetService& assets,AssetId asset,unsigned id,unsigned point){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;auto& node=graph_node(value,id);require((node["kind"]=="blend1d"||node["kind"]=="blend2d")&&point<node["points"].size(),"Select an existing blend point");node["points"].erase(point);auto& triangles=node["triangles"];for(auto it=triangles.begin();it!=triangles.end();){bool removed=std::any_of(it->begin(),it->end(),[&](const auto& index){return index==point;});if(removed)it=triangles.erase(it);else{for(auto& index:*it)if(index.get<unsigned>()>point)index=index.get<unsigned>()-1;++it;}}apply(assets,asset,revision_,std::move(value),"Remove graph point and repair triangle indices");
}
void NativeAuthoring::add_graph_triangle(AssetService& assets,AssetId asset,unsigned id,std::array<unsigned,3> triangle){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;auto& node=graph_node(value,id);require(node["kind"]=="blend2d"&&node["triangles"].size()<64,"Select a 2D node with room for a triangle");require(triangle[0]!=triangle[1]&&triangle[0]!=triangle[2]&&triangle[1]!=triangle[2],"Triangle requires three distinct points");for(auto index:triangle)require(index<node["points"].size(),"Triangle point is outside the node");node["triangles"].push_back(triangle);apply(assets,asset,revision_,std::move(value),"Add graph triangle");
}
void NativeAuthoring::remove_graph_triangle(AssetService& assets,AssetId asset,unsigned id,unsigned triangle){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;auto& node=graph_node(value,id);require(node["kind"]=="blend2d"&&triangle<node["triangles"].size(),"Select an existing triangle");node["triangles"].erase(triangle);apply(assets,asset,revision_,std::move(value),"Remove graph triangle");
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
void NativeAuthoring::add_effect_modifier(AssetService& assets,AssetId asset,AttributeId attribute){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,asset).value;require(value.at("kind")=="effect","Choose an effect asset");auto schema=AssetId::parse(value.at("attributes").get<std::string>());auto fields=open(assets,schema).value;require(fields.at("kind")=="attributes","Effect requires an attribute schema");auto& definitions=fields.at("attributes");require(std::any_of(definitions.begin(),definitions.end(),[&](const auto& field){return field.at("id")==attribute&&field.at("kind")=="statistic";}),"Choose a statistic from the effect schema");require(value.at("modifiers").size()<16,"Effect modifier limit is16");check_sources(assets,{{asset,value},{schema,fields}});value["modifiers"].push_back({{"attribute",attribute},{"kind","flat"},{"magnitude",0},{"channel",""},{"priority",0}});apply(assets,asset,revision_,std::move(value),"Add effect modifier");
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
NativeDraft& NativeAuthoring::open(AssetService& assets,AssetId id){if(auto i=drafts.find(id);i!=drafts.end())return i->second;auto inventory=assets.assets();auto found=std::find_if(inventory.begin(),inventory.end(),[&](const auto& a){return a.id==id;});require(found!=inventory.end(),"Native source is missing; refresh assets");auto ext=std::filesystem::path(found->path).extension();require(ext==".dagraph"||ext==".dainput"||ext==".daability"||ext==".daeffect"||ext==".daaction"||ext==".dakit"||ext==".dacharacter"||ext==".daplayer"||ext==".daattributes"||ext==".datags","Choose a native gameplay asset");auto bytes=read(assets.source_root()/found->path);auto value=nlohmann::json::parse(bytes);require(AssetId::parse(value.at("asset").get<std::string>())==id,"Native source UUID differs from the selected catalog identity; existing file preserved");const char* kind=ext==".dagraph"?"graph":ext==".dainput"?"input":ext==".daability"?"ability":ext==".daeffect"?"effect":ext==".daaction"?"action":ext==".dakit"?"combat_kit":ext==".dacharacter"?"character":ext==".daplayer"?"player":ext==".datags"?"tags":"attributes";require(value.at("kind")==kind,"Native source kind differs from its source type; existing file preserved");require(drafts.size()<64,"Native draft limit; close a saved source to release a draft slot");auto inserted=drafts.emplace(id,NativeDraft{*found,bytes,std::move(value)}).first;observed_[id]=inserted->second.value;return inserted->second;}
NativeSourceComparison NativeAuthoring::compare_source(AssetService& assets,AssetId id){
 auto& draft=open(assets,id);NativeSourceComparison result{id,draft.asset.path,draft.pending?std::string{}:sha256(draft.saved),{},revision_,draft.pending};auto path=assets.source_root()/draft.asset.path;result.exists=std::filesystem::exists(path);
 if(result.exists){std::ifstream file(path,std::ios::binary);require(bool(file),"Cannot compare native source");std::string bytes(65537,'\0');file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));auto size=file.gcount();require(size<=65536,"Native source comparison size limit");require(!file.bad(),"Cannot compare native source bytes");bytes.resize(static_cast<std::size_t>(size));result.disk_sha=sha256(bytes);}
 result.matches=result.pending?!result.exists:result.exists&&result.disk_sha==result.baseline_sha;return result;
}
CookResult NativeAuthoring::save(AssetService& assets,AssetId id){auto& d=open(assets,id);if(std::any_of(drafts.begin(),drafts.end(),[](const auto& entry){return entry.second.pending;})){auto previous=d.asset.generation;save_all(assets);return {id,d.asset.generation,d.asset.generation!=previous};}record_changes(assets,"Edit gameplay fields");auto bytes=d.value.dump(2)+"\n";NativeSourceEdit edit{d.asset.path,sha256(d.saved),bytes};auto prepared=assets.prepare_native({&edit,1});auto results=assets.commit_native(prepared);d.saved=bytes;++revision_;for(const auto& r:results)if(r.root==id){d.asset.generation=r.generation;return r;}throw std::runtime_error("Saved source result missing");}

std::vector<AssetInfo> NativeAuthoring::inventory(AssetService& assets)const{auto result=assets.assets();for(const auto& [id,draft]:drafts)if(draft.pending)result.push_back(draft.asset);return result;}
AssetId NativeAuthoring::stage_creation(AssetService& assets,std::string path,nlohmann::json value){
 record_changes(assets,"Edit gameplay fields");require(drafts.size()<64,"Native draft limit");auto relative=std::filesystem::path(path);require(!relative.is_absolute()&&!relative.empty(),"Choose an owned relative source path");for(const auto& part:relative)require(part!=".."&&part!=".","Source path must stay in the owned project");require(!std::filesystem::exists(assets.source_root()/relative),"Source name already exists; existing file preserved");auto fold=[](std::string value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;};auto existing=inventory(assets);auto id=AssetId::parse(value.at("asset").get<std::string>());require(id!=AssetId{},"Creation requires a persistent UUID");for(const auto& asset:existing)require(asset.id!=id&&fold(asset.path)!=fold(path),"Source UUID/name already exists in catalog or drafts");
 NativeDraft draft{{id,std::move(path),0},value.dump(2)+"\n",std::move(value),true};Command command;command.label="Create "+draft.asset.path;command.after[id]=draft.value;command.creations[id]=draft;drafts.emplace(id,draft);observed_[id]=draft.value;undo_.push_back(std::move(command));redo_.clear();trim_history();continuous_=false;++revision_;return id;
}
AssetId NativeAuthoring::create_character(AssetService& assets,AssetId skin,std::string_view name){
 require(!name.empty()&&name.size()<=64&&std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}),"Use a name with letters, numbers, underscores or hyphens");auto inventory=this->inventory(assets);auto source=std::find_if(inventory.begin(),inventory.end(),[&](const auto& asset){return asset.id==skin;});require(source!=inventory.end(),"Select a catalog skin");auto id=AssetId::random();nlohmann::json value={{"schema",1},{"kind","character"},{"asset",id.text()},{"skin",skin.text()},{"sources",{{"skin",source->path}}}};return stage_creation(assets,"characters/"+std::string(name)+".dacharacter",std::move(value));
}
AssetId NativeAuthoring::create_player(AssetService& assets,AssetId character,AssetId kit,std::string_view name){
 require(!name.empty()&&name.size()<=64&&std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}),"Use a name with letters, numbers, underscores or hyphens");auto inventory=this->inventory(assets);auto path=[&](AssetId id,const char* extension){auto source=std::find_if(inventory.begin(),inventory.end(),[&](const auto& asset){return asset.id==id&&std::filesystem::path(asset.path).extension()==extension;});require(source!=inventory.end(),"Select a typed catalog actor reference");return source->path;};auto id=AssetId::random();nlohmann::json value={{"schema",1},{"kind","player"},{"asset",id.text()},{"character",character.text()},{"loadout",{{"kit",kit.text()}}},{"sources",{{"character",path(character,".dacharacter")},{"kit",path(kit,".dakit")}}}};return stage_creation(assets,"players/"+std::string(name)+".daplayer",std::move(value));
}
AssetId NativeAuthoring::duplicate(AssetService& assets,AssetId id,std::string_view name,bool blank){auto& d=open(assets,id);require(!name.empty()&&name.size()<=64&&std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}),"Use a name with letters, numbers, underscores or hyphens");auto value=d.value;auto fresh=AssetId::random();value["asset"]=fresh.text();if(value.contains("cues"))for(auto& cue:value["cues"])cue["id"]=AssetId::random().text();if(blank&&value["kind"]=="effect"){value["evaluator"]=0;value["period_ticks"]=0;value["execute_on_apply"]=false;if(value["lifetime"]=="instant"){value["lifetime"]="finite";value["duration_ticks"]=60;}value["granted_tags"]=nlohmann::json::array();value["modifiers"]=nlohmann::json::array();value["cues"]=nlohmann::json::array();value.erase("reaction");value["sources"].erase("reaction");}if(blank&&value["kind"]=="ability"){value["costs"]=nlohmann::json::array();value["cooldown_ticks"]=0;for(auto& hit:value["melee"])hit["power"]=0;}
 auto path=std::filesystem::path(d.asset.path).parent_path()/(std::string(name)+std::filesystem::path(d.asset.path).extension().string());return stage_creation(assets,path.generic_string(),std::move(value));}
AssetId NativeAuthoring::duplicate_ability_with_action(AssetService& assets,AssetId source,std::string_view name,bool blank){
 // Prepare the two related sources privately; a collision must not leave half a creation.
 NativeAuthoring candidate=*this;candidate.record_changes(assets,"Edit gameplay fields");
 auto original=candidate.open(assets,source).value;require(original.at("kind")=="ability","Select a native Ability template");
 auto action=AssetId::parse(original.at("action").get<std::string>());auto action_source=candidate.open(assets,action).value;
 require(action_source.at("kind")=="action","Ability requires a native Action Composer");candidate.check_sources(assets,{{source,original},{action,action_source}});
 auto copied_action=candidate.duplicate(assets,action,name);auto action_command=std::move(candidate.undo_.back());candidate.undo_.pop_back();
 auto copied_ability=candidate.duplicate(assets,source,name,blank);auto ability_command=std::move(candidate.undo_.back());candidate.undo_.pop_back();
 auto& draft=candidate.drafts.at(copied_ability);draft.value["action"]=copied_action.text();draft.value["sources"]["action"]=candidate.drafts.at(copied_action).asset.path;
 draft.saved=draft.value.dump(2)+"\n";candidate.observed_[copied_ability]=draft.value;
 ability_command.after[copied_ability]=draft.value;ability_command.creations[copied_ability]=draft;
 action_command.label="Create Ability and independent Composer";action_command.after.insert(ability_command.after.begin(),ability_command.after.end());action_command.creations.insert(ability_command.creations.begin(),ability_command.creations.end());candidate.undo_.push_back(std::move(action_command));candidate.trim_history();
 *this=std::move(candidate);return copied_ability;
}
void NativeAuthoring::bind_effect_reaction(AssetService& assets,AssetId effect,AssetId action){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,effect).value;require(value.at("kind")=="effect","Select a native Effect");
 if(action==AssetId{}){value.erase("reaction");value["sources"].erase("reaction");apply(assets,effect,revision_,std::move(value),"Clear effect reaction");return;}
 auto& timeline=open(assets,action);require(timeline.value.at("kind")=="action","Reaction needs a native Action Composer");check_sources(assets,{{action,timeline.value}});
 value["reaction"]=action.text();value["sources"]["reaction"]=timeline.asset.path;apply(assets,effect,revision_,std::move(value),"Bind effect reaction");
}
AssetId NativeAuthoring::create_presentation_action(AssetService& assets,AssetId template_action,std::string_view name){
 NativeAuthoring candidate=*this;candidate.record_changes(assets,"Edit gameplay fields");auto source=candidate.open(assets,template_action).value;
 require(source.at("kind")=="action"&&source.at("schema")==2&&source.contains("motion"),"Presentation timeline template needs a Composer action with a clip");
 auto copied=candidate.duplicate(assets,template_action,name);auto& draft=candidate.drafts.at(copied);auto& command=candidate.undo_.back();
 auto cues=nlohmann::json::array();for(const auto& block:draft.value.at("blocks"))if(block.at("kind")=="cue")cues.push_back(block);
 draft.value["blocks"]=std::move(cues);draft.value["slot"]="full-body";draft.value.erase("mask");draft.value["loops"]=1;draft.value["motion"]["policy"]="none";authoring_action(draft.value);
 draft.saved=draft.value.dump(2)+"\n";candidate.observed_[copied]=draft.value;command.after[copied]=draft.value;command.creations[copied]=draft;command.label="Create presentation timeline "+draft.asset.path;
 *this=std::move(candidate);return copied;
}
AssetId NativeAuthoring::create_reaction_action(AssetService& assets,AssetId effect,AssetId template_action,std::string_view name){
 // Prepare privately; a failed binding must not leave an orphan timeline.
 NativeAuthoring candidate=*this;require(candidate.open(assets,effect).value.at("kind")=="effect","Select a native Effect");
 auto copied=candidate.create_presentation_action(assets,template_action,name);candidate.bind_effect_reaction(assets,effect,copied);*this=std::move(candidate);return copied;
}
void NativeAuthoring::assign_dodge(AssetService& assets,AssetId kit,AssetId ability){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,kit).value;require(value.at("kind")=="combat_kit","Select a native Combat Kit");
 auto old=value.contains("dodge")?value["dodge"]["ability"]:nlohmann::json{};value.erase("dodge");
 if(ability!=AssetId{}){
  auto& a=open(assets,ability);require(a.value.at("kind")=="ability"&&value["attributes"]==a.value["attributes"],"Dodge needs an Ability using this kit's attribute schema");check_sources(assets,{{ability,a.value}});
  unsigned action=0;const auto& profile=open(assets,AssetId::parse(value.at("input").get<std::string>())).value;for(const auto& entry:profile.at("actions"))if(entry.at("name")=="dodge"&&entry.at("type")=="button")action=entry.at("id").get<unsigned>();
  require(action!=0,"The kit's input profile needs a button action named dodge");for(const auto& slot:value["slots"])require(slot["input_action"]!=action,"The dodge input action is already used by a combat slot");
  value["dodge"]={{"input_action",action},{"ability",ability.text()}};value["sources"]["abilities"][ability.text()]=a.asset.path;
 }
 bool retained=value.contains("dodge")&&value["dodge"]["ability"]==old;for(const auto& slot:value["slots"])retained|=slot["ability"]==old;
 if(old.is_string()&&!retained){value["sources"]["abilities"].erase(old.get<std::string>());if(value.contains("effect_bindings")){auto& bindings=value["effect_bindings"];bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[&](const auto& b){return b["ability"]==old;}),bindings.end());}}
 apply(assets,kit,revision_,std::move(value),ability==AssetId{}?"Clear kit dodge":"Assign kit dodge");
}
void NativeAuthoring::bind_kit_death(AssetService& assets,AssetId kit,AssetId action){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,kit).value;require(value.at("kind")=="combat_kit","Select a native Combat Kit");
 if(action==AssetId{}){value.erase("death");value["sources"].erase("death");apply(assets,kit,revision_,std::move(value),"Clear kit death timeline");return;}
 auto& timeline=open(assets,action);require(timeline.value.at("kind")=="action","Death presentation needs a native Action Composer");check_sources(assets,{{action,timeline.value}});
 value["death"]=action.text();value["sources"]["death"]=timeline.asset.path;apply(assets,kit,revision_,std::move(value),"Bind kit death timeline");
}
void NativeAuthoring::assign_player_kit(AssetService& assets,AssetId player,AssetId kit){
 record_changes(assets,"Edit gameplay fields");auto value=open(assets,player).value;auto& target=open(assets,kit);require(value.at("kind")=="player"&&target.value.at("kind")=="combat_kit","Select a Player and native Combat Kit");check_sources(assets,{{player,value},{kit,target.value}});value["loadout"]["kit"]=kit.text();value["sources"]["kit"]=target.asset.path;apply(assets,player,revision_,std::move(value),"Assign Player combat kit");
}
void NativeAuthoring::assign(AssetService& assets,AssetId kit,std::size_t slot,AssetId ability){record_changes(assets,"Edit gameplay fields");auto& k=open(assets,kit).value;auto& a=open(assets,ability);check_sources(assets,{{kit,k},{ability,a.value}});require(k["kind"]=="combat_kit"&&a.value["kind"]=="ability"&&slot<k["slots"].size(),"Select a kit slot and ability");require(k["attributes"]==a.value["attributes"],"Ability uses an incompatible attribute schema");auto old=k["slots"][slot]["ability"];k["slots"][slot]["ability"]=ability.text();k["sources"]["abilities"][ability.text()]=a.asset.path;
 bool retained=k.contains("dodge")&&k["dodge"]["ability"]==old;for(const auto& s:k["slots"])retained|=s["ability"]==old;if(!old.is_null()&&!retained){k["sources"]["abilities"].erase(old.get<std::string>());if(k.contains("effect_bindings")){auto& bindings=k["effect_bindings"];bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[&](const auto& b){return b["ability"]==old;}),bindings.end());}}
 record_changes(assets,"Assign kit slot");
}
void NativeAuthoring::bind(AssetService& assets,AssetId kit,AssetId ability,unsigned block,AssetId effect,double power){record_changes(assets,"Edit gameplay fields");auto& k=open(assets,kit).value;auto& a=open(assets,ability).value;auto& e=open(assets,effect);check_sources(assets,{{kit,k},{ability,a},{effect,e.value}});require(e.value["kind"]=="effect"&&k["attributes"]==e.value["attributes"],"Effect uses an incompatible attribute schema");bool granted=false,hit=false;for(const auto& s:k["slots"])granted|=s["ability"]==ability.text();for(const auto& h:a["melee"])hit|=h["block"]==block;require(granted&&hit,"Assign ability to the kit and select its hit window first");auto& effects=k["effects"];if(effects.is_null())effects=nlohmann::json::array();if(std::find(effects.begin(),effects.end(),effect.text())==effects.end())effects.push_back(effect.text());k["sources"]["effects"][effect.text()]=e.asset.path;auto& bindings=k["effect_bindings"];if(bindings.is_null())bindings=nlohmann::json::array();for(auto& b:bindings)if(b["ability"]==ability.text()&&b["hit_block"]==block&&b["effect"]==effect.text()){b["power"]=power;record_changes(assets,"Bind effect");return;}bindings.push_back({{"ability",ability.text()},{"hit_block",block},{"effect",effect.text()},{"power",power}});record_changes(assets,"Bind effect");}
bool NativeAuthoring::dirty()const{for(const auto& [id,d]:drafts)if(d.dirty())return true;return false;}
NativeValidationSummary NativeAuthoring::validate(AssetService& assets,AssetId selected,std::span<const AssetId> roots){
 record_changes(assets,"Edit gameplay fields");if(selected==AssetId{}){auto found=std::find_if(drafts.begin(),drafts.end(),[](const auto& entry){return entry.second.dirty();});if(found==drafts.end())found=drafts.begin();require(found!=drafts.end(),"Open a native source before validation");selected=found->first;}open(assets,selected);std::vector<NativeSourceEdit> edits;for(const auto& [id,draft]:drafts)if(id==selected||draft.dirty())edits.push_back({draft.asset.path,draft.pending?std::string{}:sha256(draft.saved),draft.value.dump(2)+"\n",draft.pending});auto candidate=assets.prepare_native(edits,roots);return {revision_,edits.size(),selected};
}
void NativeAuthoring::save_all(AssetService& assets,std::span<const AssetId> roots){
 record_changes(assets,"Edit gameplay fields");std::vector<NativeSourceEdit> edits;
 for(const auto& [id,d]:drafts)if(d.dirty())edits.push_back({d.asset.path,d.pending?std::string{}:sha256(d.saved),d.value.dump(2)+"\n",d.pending});
 if(edits.empty())return;auto candidate=assets.prepare_native(edits,roots);auto results=assets.commit_native(candidate);
 std::set<AssetId> published;for(auto& [id,d]:drafts)if(d.dirty()){if(d.pending)published.insert(id);d.pending=false;d.saved=d.value.dump(2)+"\n";for(const auto& r:results)if(r.root==id)d.asset.generation=r.generation;}
 auto prune=[&](auto& commands){commands.erase(std::remove_if(commands.begin(),commands.end(),[&](const auto& command){for(const auto& [id,draft]:command.creations)if(published.contains(id))return true;return false;}),commands.end());};prune(undo_);prune(redo_);
 ++revision_;continuous_=false;
}
void NativeAuthoring::check_sources(AssetService& assets,const Values& values)const{
 for(const auto& [id,value]:values){auto it=drafts.find(id);require(it!=drafts.end(),"Stale history command; source was reloaded");if(it->second.pending){require(!std::filesystem::exists(assets.source_root()/it->second.asset.path),"Pending source path was occupied externally; existing work preserved");continue;}if(read(assets.source_root()/it->second.asset.path)!=it->second.saved)throw std::runtime_error("Stale authoring command for "+it->second.asset.path+": external source changed. Reload source before editing or using history");}
}
void NativeAuthoring::record_changes(AssetService& assets,std::string_view label,bool continuous){
 Command command;command.label=label;
 for(const auto& [id,d]:drafts){auto it=observed_.find(id);require(it!=observed_.end(),"Reload sources through the authoring command");if(it->second!=d.value){command.before[id]=it->second;command.after[id]=d.value;}}
 if(command.after.empty()){if(!continuous)continuous_=false;return;}
 try{for(const auto& [id,before]:command.before){const auto& after=command.after.at(id);require(after.at("asset")==before.at("asset")&&after.at("kind")==before.at("kind")&&after.at("schema")==before.at("schema")&&AssetId::parse(after.at("asset").get<std::string>())==id,"Authoring preserves source UUID/type/schema");}check_sources(assets,command.before);}catch(...){for(const auto& [id,value]:command.before)drafts.at(id).value=value;throw;}
 if(continuous_&&!undo_.empty()&&undo_.back().label==command.label){auto& previous=undo_.back();for(const auto& [id,value]:command.before)previous.before.try_emplace(id,value);for(const auto& [id,value]:command.after)previous.after[id]=value;}
 else undo_.push_back(command);
 for(const auto& [id,value]:command.after)observed_[id]=value;redo_.clear();continuous_=continuous;++revision_;
 trim_history();
}
void NativeAuthoring::trim_history(){
 auto bytes=[](const Command& command){std::size_t total{};for(const auto& [id,value]:command.before)total+=value.dump().size();for(const auto& [id,value]:command.after)total+=value.dump().size();for(const auto& [id,draft]:command.creations)total+=draft.saved.size()+draft.value.dump().size()+draft.asset.path.size();return total;};std::size_t total{};for(const auto& command:undo_)total+=bytes(command);while(!undo_.empty()&&(undo_.size()>128||total>16*1024*1024)){total-=bytes(undo_.front());undo_.erase(undo_.begin());}
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

std::vector<NativeHistoryEntry> NativeAuthoring::history(bool forward)const{
 const auto& commands=forward?redo_:undo_;std::vector<NativeHistoryEntry> entries;entries.reserve(commands.size());for(auto it=commands.rbegin();it!=commands.rend();++it){NativeHistoryEntry entry{it->label,{},!it->creations.empty()};for(const auto& [id,value]:it->after)entry.assets.push_back(id);entries.push_back(std::move(entry));}return entries;
}
std::vector<NativeHistoryChange> NativeAuthoring::history_changes(bool forward,std::size_t newest)const{
 const auto& commands=forward?redo_:undo_;require(newest<commands.size(),"History entry no longer exists");const auto& command=commands[commands.size()-1-newest];std::vector<NativeHistoryChange> changes;
 for(const auto& [id,value]:command.after){auto patch=nlohmann::json::diff(command.before.contains(id)?command.before.at(id):nlohmann::json{},value);for(const auto& change:patch){if(changes.size()==128)return changes;auto preview=change.contains("value")?change["value"].dump():std::string("(removed)");if(preview.size()>512)preview=preview.substr(0,512)+"...";changes.push_back({id,change.at("op").get<std::string>(),change.at("path").get<std::string>(),std::move(preview)});}}return changes;
}
void NativeAuthoring::travel(AssetService& assets,bool forward){
 record_changes(assets,"Edit gameplay fields");auto& from=forward?redo_:undo_;auto& to=forward?undo_:redo_;require(!from.empty(),"Authoring history is empty");auto command=from.back();const auto& expected=forward?command.before:command.after;const auto& target=forward?command.after:command.before;
 if(!command.creations.empty()){
  if(forward){for(const auto& [id,draft]:command.creations){require(!drafts.contains(id)&&!std::filesystem::exists(assets.source_root()/draft.asset.path),"Creation Redo path/identity is no longer free");for(const auto& asset:inventory(assets))require(asset.id!=id&&asset.path!=draft.asset.path,"Creation Redo conflicts with another source");}for(const auto& [id,draft]:command.creations){drafts.emplace(id,draft);observed_[id]=draft.value;}}
  else{check_sources(assets,expected);for(const auto& [id,draft]:command.creations){require(drafts.at(id).pending&&drafts.at(id).value==command.after.at(id),"Published/changed creation cannot be removed by history");}for(const auto& [id,draft]:command.creations){drafts.erase(id);observed_.erase(id);}}
  to.push_back(std::move(command));from.pop_back();++revision_;continuous_=false;return;
 }
 check_sources(assets,expected);for(const auto& [id,value]:expected)require(drafts.at(id).value==value,"Stale history command; draft changed");
 for(const auto& [id,value]:target){drafts.at(id).value=value;observed_[id]=value;}to.push_back(std::move(command));from.pop_back();++revision_;continuous_=false;
}
void NativeAuthoring::undo(AssetService& assets){travel(assets,false);}void NativeAuthoring::redo(AssetService& assets){travel(assets,true);}
void NativeAuthoring::close(AssetService& assets,AssetId id){
 auto& draft=open(assets,id);require(!draft.pending&&!draft.dirty(),"Save or undo draft changes before closing this source");auto touches=[&](const Command& command){return command.before.contains(id)||command.after.contains(id)||command.creations.contains(id);};std::erase_if(undo_,touches);std::erase_if(redo_,touches);drafts.erase(id);observed_.erase(id);continuous_=false;++revision_;
}
void NativeAuthoring::reload(AssetService& assets,AssetId id){
 auto& d=open(assets,id);require(!d.pending,"Pending creation has no published source to reload");auto bytes=read(assets.source_root()/d.asset.path);auto value=nlohmann::json::parse(bytes);auto old=nlohmann::json::parse(d.saved);require(value.at("asset")==old.at("asset")&&value.at("kind")==old.at("kind")&&value.at("schema")==old.at("schema"),"Reload preserves UUID/type/schema; refresh inventory for replaced assets");
 d.saved=std::move(bytes);d.value=std::move(value);observed_[id]=d.value;auto touches=[&](const Command& command){return command.before.contains(id)||command.after.contains(id)||command.creations.contains(id);};std::erase_if(undo_,touches);std::erase_if(redo_,touches);continuous_=false;++revision_;
}
void NativeAuthoring::revert(AssetService& assets,AssetId id){auto& d=open(assets,id);apply(assets,id,revision_,nlohmann::json::parse(d.saved),"Revert draft");}
void package_authoring(AssetService& assets,std::span<const AssetId> roots,const std::filesystem::path& output){require(!roots.empty()&&roots.size()<=64,"Scene package root bounds");assets.scan();auto inventory=assets.assets();for(auto id:roots){auto found=std::find_if(inventory.begin(),inventory.end(),[&](const auto& a){return a.id==id;});require(found!=inventory.end(),"Scene package source is missing");assets.cook(found->path);}assets.package(roots.front(),output,roots.subspan(1));}
}
