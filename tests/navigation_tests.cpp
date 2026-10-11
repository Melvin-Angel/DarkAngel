#include <darkangel/navigation.hpp>
#include <darkangel/character_scene.hpp>
#include <darkangel/combat_kit_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
using namespace darkangel;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(){try{
    // A 32 m floor with a wall across the middle: x in [-4,4] at z=3. The straight line from the NPC to the player is blocked.
    CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;collision.boxes={{100,{0,-.5,0},{16,.5,16}},{101,{0,1,3},{4,1,.25}}};
    auto mesh=std::make_shared<const NavigationMesh>(collision);check(mesh->polygons()>0,"Navigation mesh has no walkable polygons");
    auto length=[](const std::vector<MotorVec>& points){double total{};for(std::size_t i=1;i<points.size();++i)total+=std::hypot(points[i].x-points[i-1].x,points[i].z-points[i-1].z);return total;};
    auto around=mesh->path({0,0,6},{0,0,0});check(around&&around->size()>=3,"No path around the wall");
    double widest{};for(const auto& point:*around){widest=std::max(widest,std::abs(point.x));check(!(std::abs(point.x)<3.9&&std::abs(point.z-3)<.3),"Path corner lies inside the wall");}
    check(widest>4&&length(*around)>9&&std::hypot(around->front().x,around->front().z-6)<.5&&std::hypot(around->back().x,around->back().z)<.5,"Path does not go around the wall end between its endpoints");
    auto open=mesh->path({-8,0,-8},{8,0,-8});check(open&&std::abs(length(*open)-16)<.6,"An unobstructed path is not the straight line");
    check(!mesh->path({0,0,6},{100,0,100})&&!mesh->path({100,0,100},{0,0,0}),"A path was found to or from a point off the mesh");
    {CollisionDefinition empty;empty.id=AssetId::random();empty.schema=2;bool rejected=false;try{NavigationMesh none(empty);}catch(const std::exception&){rejected=true;}check(rejected,"Navigation accepted a scene without static collision");}
    {CollisionDefinition moving=collision;for(auto& box:moving.boxes)box.moving=true;bool rejected=false;try{NavigationMesh none(moving);}catch(const std::exception&){rejected=true;}check(rejected,"Moving collision was baked into the navigation mesh");}
    {bool rejected=false;try{make_path_follower({},{});}catch(const std::exception&){rejected=true;}check(rejected,"A path follower without a mesh was created");}
    std::cout<<"Navigation mesh polygons="<<mesh->polygons()<<" path_corners="<<around->size()<<" path_length="<<length(*around)<<"\n";

    // In a session: a path-following controller walks the NPC around the wall and attacks; a straight-line controller cannot.
    auto content=std::filesystem::path(DAE_SOURCE_DIR)/"content",cache=std::filesystem::path(DAE_BINARY_DIR)/("navigation-"+AssetId::random().text());std::ifstream source(content/"royal_district/combat/player.dakit");auto data=nlohmann::json::parse(source);auto id=AssetId::parse(data.at("asset").get<std::string>());AssetService assets(content,cache);assets.cook("royal_district/combat/player.dakit");assets.package(id,cache/"registry.json");auto cooked=load_cooked_combat_kit(cache/"registry.json",assets.cas_path(),id);
    CharacterSceneCombat combat;combat.kit=cooked.definition;combat.input=cooked.input;combat.attributes=cooked.attributes.definitions();combat.health=2;combat.maximum_health=1;combat.abilities=cooked.abilities;if(cooked.tags)combat.tags=cooked.tags->dictionary();combat.evaluator=1;combat.damage=[](const DamageContext& context){return context.power;};for(const auto& ability:cooked.abilities){auto clip=load_cooked_clip(cache/"registry.json",assets.cas_path(),ability->action->motion->clip.id);combat.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}
    ObjectData player;player.id={9,1};ObjectData target;target.id={9,2};target.transform.z=6;std::array<ObjectData,2> objects{player,target};{CharacterSceneActor walker;walker.id=target.id;walker.kit=combat.kit;walker.input=combat.input;walker.abilities=combat.abilities;walker.attributes=combat.attributes;walker.senses.everything=true;combat.actors={walker};}SessionHandshake hello{3,sha256("navigation scene wire3"),sha256(cooked.definition->generation),704};
    auto player_health=[&](const CharacterSceneSession& scene){return scene.presentation().read(scene.presentation().find(player.id)).health.current;};
    auto where=[&](const CharacterSceneSession& scene){return scene.observer(target.id,double(scene.motor().tick)).frame.motor.position;};
    {
        struct Straight final:ActorController {ControllerCommand decide(const ControllerView& view)override{ControllerCommand command;const auto* foe=nearest_hostile(view);if(!foe)return command;const double dx=foe->motor.position.x-view.self.position.x,dz=foe->motor.position.z-view.self.position.z,distance=std::hypot(dx,dz);if(distance>1.4){command.move_x=dx/distance;command.move_z=dz/distance;}return command;}};
        auto blocked=combat;blocked.actors.front().controller=std::make_shared<Straight>();CharacterSceneSession scene(hello,objects,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,blocked);
        while(scene.motor().tick<300)scene.step({});check(where(scene).z>3.4&&player_health(scene)==100,"Control: a straight-line controller got through the wall");
    }
    {
        PathFollow follow;follow.attack.slot=CombatSlot::Light;follow.attack.first_tick=1;follow.attack.interval=120;auto guided=combat;guided.actors.front().controller=make_path_follower(mesh,follow);
        CharacterSceneSession scene(hello,objects,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,guided);const auto baseline=scene.resynchronizations();
        double widest_walk{};std::uint64_t arrived{},hit{};while(scene.motor().tick<900&&!hit){scene.step({});const auto position=where(scene);widest_walk=std::max(widest_walk,std::abs(position.x));if(!arrived&&std::hypot(position.x-scene.motor().position.x,position.z-scene.motor().position.z)<1.5)arrived=scene.motor().tick;if(player_health(scene)<100)hit=scene.motor().tick;}
        check(arrived&&widest_walk>4&&hit>arrived&&player_health(scene)==75,"Path follower did not walk around the wall, reach the player and hit once through authority");
        check(scene.controller_receipt(target.id)&&scene.controller_receipt(target.id)->committed&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"Path-following NPC disturbed owner prediction or its attack was not an authoritative commitment");
        std::cout<<"Path follower: around the wall (widest x "<<widest_walk<<" m), arrived tick "<<arrived<<", hit tick "<<hit<<"\n";
    }
    std::cout<<"Navigation: Recast/Detour mesh from static collision, path around an obstacle, off-mesh rejection, and a path-following controller reaching and attacking the player passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
