#include "../apps/editor/native_authoring.hpp"
#include "../apps/editor/composer_timing.hpp"
#include <darkangel/ability_assets.hpp>
#include <darkangel/hash.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
using namespace darkangel;
using namespace darkangel::editor_app;
std::string read(const std::filesystem::path& path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
void check(bool b,const char* error){if(!b)throw std::runtime_error(error);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejected structural candidate");}
int main(int argc,char** argv){try{
 check(composer_drag_units(50,100,60)==30*action_tick_units&&composer_drag_units(-25,100,60)==-15ll*action_tick_units&&composer_drag_units(1,100,60)==614,"Timeline pixel conversion lost fixed action units");
 check(argc==2||argc==3,"Pass existing isolated authoring fixture");auto fixture=nlohmann::json::parse(read(argv[1]));
 auto root=std::filesystem::path(argv[1]).parent_path()/("cs-"+AssetId::random().text());auto source=root/"s";std::filesystem::create_directories(root);
 std::filesystem::copy(fixture.at("source").get<std::string>(),source,std::filesystem::copy_options::recursive);
 AssetService assets(source,root/"cache");assets.scan();NativeAuthoring author;
 auto path="royal_district/combat/heavy.daaction";auto bytes=read(source/path);auto original=nlohmann::json::parse(bytes);auto id=AssetId::parse(original.at("asset").get<std::string>());
 assets.cook(path);assets.package(id,root/"before.json");auto& value=author.open(assets,id).value;
 if(argc==3){
  check(std::string_view(argv[2])=="--gestures","Unknown structural test option");
  const auto block=original["blocks"][1];auto begin=block["begin"].get<unsigned>(),end=block["end"].get<unsigned>(),block_id=block["id"].get<unsigned>();
  author.set_action_block_times(assets,id,block_id,begin+256,end+256,true);author.set_action_block_times(assets,id,block_id,begin+1024,end+1024,true);author.record_changes(assets,"Finish gesture");
  author.undo(assets);check(value==original&&!author.can_undo(),"Gesture was not grouped into one Undo");author.redo(assets);check(value["blocks"][1]["begin"]==begin+1024,"Grouped gesture Redo failed");author.undo(assets);
  author.set_action_block_times(assets,id,block_id,begin+512,end+512,true);auto revision=author.revision();author.cancel_edit(assets,revision,"Drag action block "+std::to_string(block_id));check(value==original&&!author.can_undo(),"Cancelled gesture retained mutation/history");
  auto prior=value;revision=author.revision();rejects([&]{author.set_action_block_times(assets,id,block_id,end,end,true);});check(value==prior&&author.revision()==revision,"Invalid interval applied a gesture");
  author.set_action_block_times(assets,id,block_id,begin+1024,end+1024,true);auto moved=value;auto old_revision=author.revision();author.set_action_block_times(assets,id,4,6144,6144);rejects([&]{author.cancel_edit(assets,old_revision,"Drag action block "+std::to_string(block_id));});check(value["blocks"][1]==moved["blocks"][1],"Stale cancellation changed unrelated edits");author.undo(assets);author.undo(assets);check(value==original,"Independent marker gesture Undo failed");
  author.set_action_block_times(assets,id,block_id,begin,end-1024,true);author.record_changes(assets,"Finish resize");author.save_all(assets);assets.package(id,root/"resize.json");auto cooked=load_cooked_action(root/"resize.json",assets.cas_path(),id);auto hit=std::find_if(cooked.blocks.begin(),cooked.blocks.end(),[&](const auto& b){return b.id==block_id;});check(hit!=cooked.blocks.end()&&hit->begin==begin&&hit->end==end-1024,"Resize missing from native cooked generation");
  std::cout<<"Composer gestures: grouped fractional move/resize, Undo/Redo, cancellation, stale/invalid rejection, independent marker edits and native Save/cook passed\n";return 0;
 }
 if(original.contains("motion")){
  auto clip=AssetId::parse(original["motion"]["clip"].get<std::string>());auto created=author.create_action_from_animation(assets,clip,"actions/FromAnimation.daaction");
  const auto draft=author.drafts.at(created).value;check(draft["motion"]["clip"]==clip.text()&&draft["duration"]==original["duration"]&&draft["blocks"].empty(),"Animation-to-composer binding or duration incorrect");
  rejects([&]{author.create_action_from_animation(assets,clip,"actions/FromAnimation.daaction");});check(author.drafts.at(created).value==draft,"Duplicate creation mutated existing composer");
  author.undo(assets);check(!author.drafts.contains(created),"Composer creation Undo retained draft");author.redo(assets);check(author.drafts.at(created).value==draft,"Composer creation Redo changed identity/binding");author.undo(assets);
  auto ability_source=nlohmann::json::parse(read(source/"royal_district/combat/heavy.daability"));auto ability_id=AssetId::parse(ability_source.at("asset").get<std::string>());auto& ability_draft=author.open(assets,ability_id).value;
  auto continuation=author.create_ability_composer(assets,ability_id,1,clip,"actions/ContinuationFromAnimation.daaction");check(ability_draft["combo_steps"][0]["action"]==continuation.text(),"Created composer was not assigned to next combo step");
  author.undo(assets);check(!author.drafts.contains(continuation)&&ability_draft==ability_source,"Creation and assignment did not Undo atomically");author.redo(assets);check(author.drafts.contains(continuation)&&ability_draft["combo_steps"][0]["action"]==continuation.text(),"Atomic composer assignment Redo lost identity");check(author.create_ability_composer(assets,ability_id,1,clip,"actions/UnusedDuplicate.daaction")==continuation,"Valid assigned composer was duplicated");author.undo(assets);
 }
 unsigned added=author.add_action_block(assets,id,{0,7,1024,1024,ActionBlockKind::Cue,"Test.New"});
 for(const auto& block:original["blocks"])check(std::find(value["blocks"].begin(),value["blocks"].end(),block)!=value["blocks"].end(),"Existing block identity/data changed");
 author.undo(assets);check(value==original,"Create block Undo failed");author.redo(assets);check(authoring_action(value).blocks.size()==original["blocks"].size()+1,"Create block Redo failed");
 {const auto spear_source=nlohmann::json::parse(read(source/"royal_district/combat/earth/earth_spear.daaction"));const auto spear_id=AssetId::parse(spear_source.at("asset").get<std::string>());auto parsed=authoring_action(spear_source);auto shot=std::find_if(parsed.blocks.begin(),parsed.blocks.end(),[](const auto& block){return block.projectile.has_value();});check(shot!=parsed.blocks.end(),"Migrated projectile fixture missing");auto copy=*shot;copy.id=0;copy.track=7;const auto duplicate=author.add_action_block(assets,spear_id,copy);const auto duplicated=authoring_action(author.drafts.at(spear_id).value);auto block=std::find_if(duplicated.blocks.begin(),duplicated.blocks.end(),[&](const auto& block){return block.id==duplicate;});check(block!=duplicated.blocks.end()&&block->effects.size()==shot->effects.size()&&block->effects[0].asset==shot->effects[0].asset&&block->projectile->visual_model==shot->projectile->visual_model&&block->projectile->preparation->release_block==duplicate,"Timeline duplication lost typed assets or retained old release identity");author.undo(assets);check(author.drafts.at(spear_id).value==spear_source,"Typed block duplication Undo changed original");}
 auto edited=value;auto revision=author.revision();rejects([&]{author.add_action_block(assets,id,{0,32,0,0,ActionBlockKind::Cue,"Bad.Track"});});check(value==edited&&author.revision()==revision,"Invalid block mutated history/draft");
 author.remove_action_block(assets,id,2);rejects([&]{author.save_all(assets);});check(read(source/path)==bytes,"Missing hit reference published source");assets.package(id,root/"failed.json");check(read(root/"before.json")==read(root/"failed.json"),"Failed consumer validation changed playable head");author.undo(assets);check(value==edited,"Failed candidate block removal Undo failed");
 author.save_all(assets);assets.package(id,root/"after.json");auto cooked=load_cooked_action(root/"after.json",assets.cas_path(),id);check(std::any_of(cooked.blocks.begin(),cooked.blocks.end(),[&](const auto& b){return b.id==added&&b.key=="Test.New";}),"Structural edit missing from cooked action");
 author.remove_action_block(assets,id,added);auto newer=author.add_action_block(assets,id,{0,7,2048,2048,ActionBlockKind::Cue,"Test.Newer"});check(newer>added,"Session reused removed/undone block identity");author.undo(assets);author.undo(assets);check(value==edited,"Structural history after Save failed");
 auto hit=author.add_action_block(assets,id,{0,8,16384,27648,ActionBlockKind::HitWindow,"Test.Hit"});auto marker=author.add_action_block(assets,id,{0,9,6144,6144,ActionBlockKind::Commit,"Test.Commit"});
 auto ability_path="royal_district/combat/heavy.daability";auto ability_source=nlohmann::json::parse(read(source/ability_path));auto ability=AssetId::parse(ability_source.at("asset").get<std::string>());auto& ability_value=author.open(assets,ability).value;
 auto changed=ability_value;auto profile=changed["melee"][0];profile["block"]=hit;changed["melee"].push_back(profile);changed["deferred_commit_block"]=marker;
 author.apply(assets,ability,author.revision(),changed,"Bind typed hit and commit blocks");author.save_all(assets);
 check(nlohmann::json::parse(read(source/ability_path))["deferred_commit_block"]==marker,"Typed commit reference was not saved");edited=value;
 auto saved_bytes=read(source/path);{std::ofstream output(source/path);output<<saved_bytes<<" ";}auto external=read(source/path);rejects([&]{author.add_action_block(assets,id,{0,7,0,0,ActionBlockKind::Cue,"Stale.New"});});check(value==edited&&read(source/path)==external,"Stale structural command affected draft/source");
 std::cout<<"Composer structural: stable IDs, typed blocks, Undo/Redo, invalid/stale rejection, dependent hit validation, prior source/head retention and native cooked publication passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
