#include "../apps/editor/character_preview.hpp"
#include <darkangel/combat_kit_assets.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <cmath>
using namespace darkangel;
using namespace darkangel::editor_app;
void check(bool b,const char* error){if(!b)throw std::runtime_error(error);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejected Composer candidate");}
int main(int argc,char** argv){try{
 check(argc==2||argc==3,"Pass an existing isolated authoring fixture");
 nlohmann::json fixture;std::ifstream(argv[1])>>fixture;
 auto registry=std::filesystem::path(fixture.at("registry").get<std::string>()),cas=std::filesystem::path(fixture.at("cache").get<std::string>())/"cas";
 auto kit=load_cooked_combat_kit(registry,cas,AssetId::parse(fixture.at("kit").get<std::string>()));
 const auto& action=*kit.abilities.at(1)->action;check(action.motion.has_value(),"Expected frozen action clip");
 auto cooked=load_cooked_clip(registry,cas,action.motion->clip.id);
 CharacterPreviewResources resources;resources.rig=kit.stance.rig;
 ComposerPosePreview preview(resources,cooked,action.duration);
 RigPose independent(resources.rig.definition,resources.rig.archive);AnimationClip reference(cooked.definition,cooked.archive);
 auto begin=preview.sample(0);auto middle=preview.sample(20);
 check(begin.size()==81&&middle.size()==81,"Canonical preview joint count");
 bool changed=false;for(unsigned i=0;i<begin.size();++i)for(unsigned j=0;j<16;++j)changed|=std::abs(begin[i].values[j]-middle[i].values[j])>1e-5f;
 check(changed,"Scrub changed no joint poses");
 auto expected=independent.sample(reference,20);
 for(unsigned i=0;i<middle.size();++i)check(middle[i].values==expected[i].values,"Isolated preview differs from native clip sampling");
 auto before=preview.sample(20);
 rejects([&]{preview.sample(-1);});rejects([&]{preview.sample(double(cooked.definition.ticks)+1);});rejects([&]{preview.sample(std::numeric_limits<double>::quiet_NaN());});
 rejects([&]{ComposerPosePreview wrong(resources,cooked,action.duration+1024);});
 auto mismatch=resources;mismatch.rig.definition.signature=std::string(64,'0');rejects([&]{ComposerPosePreview wrong(mismatch,cooked,action.duration);});
 for(unsigned i=0;i<before.size();++i)check(before[i].values==preview.sample(20)[i].values,"Failed candidate affected prior pose");
 preview.sample(cooked.definition.ticks);auto repeat=preview.sample(0);
 for(unsigned i=0;i<begin.size();++i)check(begin[i].values==repeat[i].values,"Repeated scrubbing leaked state");
 if(argc==3){
  auto source=std::filesystem::path(fixture.at("source").get<std::string>());auto character_path=source/"characters/gui_character.dacharacter";std::ifstream file(character_path);nlohmann::json character;file>>character;auto id=AssetId::parse(character.at("asset").get<std::string>());auto original_hash=file_sha256(character_path);AssetService assets(source,std::filesystem::path(argv[1]).parent_path()/("cp-"+AssetId::random().text().substr(0,8)));assets.scan();NativeAuthoring author;auto prepared=prepare_authored_character_pose(assets,author,id,cooked.definition.id);auto posed=prepared.pose->sample(20);check(prepared.character.definition.skin!=AssetId{}&&posed.size()==81,"Authored Character frozen pose failed");for(unsigned i=0;i<posed.size();++i)check(posed[i].values==expected[i].values,"Authored Character differs from existing native sampler");auto& value=author.open(assets,id).value;auto original=value;value["skin"]=AssetId::random().text();rejects([&]{prepare_authored_character_pose(assets,author,id,cooked.definition.id);});value=original;check(file_sha256(character_path)==original_hash,"Character pose preview modified source");for(unsigned i=0;i<posed.size();++i)check(prepared.pose->sample(20)[i].values==posed[i].values,"Failed Character candidate changed prior pose");std::cout<<"Authored Character preview frozen rig/skin/clip, exact poses and source/failed-candidate retention passed\n";
 }
 std::cout<<"Composer: frozen rig/clip compatibility, exact native poses, independent buffers, bounded scrub, failed-candidate retention and endpoints passed\n";
 return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
