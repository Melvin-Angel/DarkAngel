#include <darkangel/character_scene.hpp>
#include <darkangel/animation_assets.hpp>
#include <darkangel/hash.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
using namespace darkangel;
namespace {void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}}
int main(){try{
    auto project=std::filesystem::path(DAE_SOURCE_DIR),sources=std::filesystem::path(DAE_BINARY_DIR)/("character-scene-"+AssetId::random().text());std::filesystem::create_directories(sources);
    std::filesystem::copy_file(project/"content/animation/canonical_human.daskeleton",sources/"human.daskeleton");AssetService assets(sources,sources/"cache");
    std::vector<std::shared_ptr<const AnimationClip>> clips;CookedRig rig;
    for(auto name:{"idle","omni-walk","omni-left","omni-back","omni-right","omni-run","omni-run-left","omni-run-back","omni-run-right"}){
        auto file=std::string(name)+".glb";std::filesystem::copy_file(project/"content/royal_district/clips"/file,sources/file);auto id=assets.adopt_clip(file,"human.daskeleton",true);assets.cook(file);auto registry=sources/(std::string(name)+".json");assets.package(id,registry);auto clip=load_cooked_clip(registry,assets.cas_path(),id);if(clips.empty())rig=load_cooked_rig(registry,assets.cas_path(),clip.definition.skeleton);clips.push_back(std::make_shared<const AnimationClip>(clip.definition,clip.archive));
    }
    std::vector<GraphNode> nodes;for(unsigned index=0;index<clips.size();++index){GraphNode node;node.id=index+1;node.kind=GraphNodeKind::Clip;node.clip=clips[index];nodes.push_back(node);}
    GraphNode root;root.id=20;root.kind=GraphNodeKind::Blend2D;root.points={{1,0,0},{3,2,0},{2,0,2},{5,-2,0},{4,0,-2},{7,5,0},{6,0,5},{9,-5,0},{8,0,-5}};
    for(unsigned index=0;index<4;++index){unsigned a=index+1,b=(index+1)%4+1,c=index+5,d=(index+1)%4+5;root.triangles.push_back({0,a,b});root.triangles.push_back({a,c,d});root.triangles.push_back({a,d,b});}nodes.push_back(root);auto plan=std::make_shared<const AnimationGraphPlan>(sha256("native character scene fixture v1"),20,nodes);
    CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;collision.boxes={{100,{0,-.5,0},{16,.5,16}},{101,{4,1.5,0},{.5,1.5,5}}};
    ObjectData player;player.id={7,1};ObjectData prop;prop.id={7,2};prop.transform.x=8;std::array<ObjectData,2> objects{player,prop};World authoring(WorldDomain::Authoring);for(auto object:objects)authoring.create(object);auto frozen=authoring.serialize();
    SessionHandshake hello{2,sha256("native character scene schema"),sha256("verified scene fixture content"),502};
    MotorState baseline;GraphState graph_baseline;std::vector<JointMatrix> pose_baseline;
    for(double fps:{0.,30.,60.,144.}){
        CharacterSceneSession session(hello,objects,player.id,collision,plan,rig.definition,rig.archive);
        while(session.motor().tick<180){if(fps)session.advance(1/fps,{1,0});else session.step({1,0});session.pose();}
        require(session.motor().tick==180&&session.graph().tick==180&&session.pending_prediction()==0,"Motor/graph clocks and owner receipts agree");
        require(session.motor().position.x>3.1&&session.motor().position.x<3.3&&std::abs(session.motor().position.y)<.001,"Movement uses authoritative wall-constrained physics");
        require(session.predicted_motor().position==session.motor().position,"Loopback owner correction matches authority");
        auto avatar=session.presentation().read(session.presentation().find(player.id));require(avatar.transform.x==session.motor().position.x&&avatar.transform.y==session.motor().position.y,"Presentation comes from shared session snapshot");
        require(authoring.serialize()==frozen,"Scene simulation changed authoring");
        if(!fps){baseline=session.motor();graph_baseline=session.graph();pose_baseline=session.pose();}
        else{require(session.motor().position==baseline.position&&session.graph().phase==graph_baseline.phase,"Render rate changed fixed movement or animation clock");for(unsigned joint=0;joint<pose_baseline.size();++joint)require(session.pose()[joint].values==pose_baseline[joint].values,"Render rate changed tick pose");}
    }
    CharacterSceneSession removed(hello,objects,player.id,collision,plan,rig.definition,rig.archive);removed.step({});removed.remove_collision(100);for(unsigned tick=0;tick<20;++tick)removed.step({});require(removed.motor().position.y<-.5&&removed.predicted_motor().position==removed.motor().position&&removed.resynchronizations()==1,"Support removal creates one fresh prepared prediction baseline");
    CharacterSceneSession timing(hello,objects,player.id,collision,plan,rig.definition,rig.archive);timing.step({});require(timing.advance(0,{0,0,0,true})==0,"Input edge may precede a fixed step");timing.advance(1./60,{});require(timing.motor().position.y>.05,"Pending jump edge consumed on the next fixed tick");auto tick=timing.motor().tick;require(timing.advance(.5,{})==8&&timing.motor().tick==tick+8&&timing.debt()>.3,"Bounded catch-up retains wall-time debt");
    bool denied=false;try{timing.advance(11,{});}catch(...){denied=true;}require(denied,"Excessive time delta rejects");
    std::cout<<"Native character scene: WorldSession bootstrap/prepared collision ACK, owner prediction/isolated correction, achieved-movement graph, support removal, jump edge/debt and 30/60/144/headless poses passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
