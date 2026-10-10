#include "character_preview.hpp"
#include <darkangel/hash.hpp>
#include <darkangel/graph_assets.hpp>
#include <darkangel/combat_kit_assets.hpp>
#include <darkangel/actor_assets.hpp>
#include <darkangel/model_collision.hpp>
#include <ashen_roots/royal_combat.hpp>
#include <DirectXMath.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
namespace darkangel::editor_app {
namespace {void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}}
ComposerPosePreview::ComposerPosePreview(const CharacterPreviewResources& resources,CookedClip cooked,unsigned duration){
    require(cooked.definition.skeleton==resources.rig.definition.id&&cooked.definition.signature==resources.rig.definition.signature&&cooked.definition.joints==resources.rig.definition.joints.size(),"Composer clip/character rig mismatch");
    require(cooked.definition.ticks&&cooked.definition.ticks*action_tick_units==duration,"Composer clip/action duration mismatch");
    clip_=std::make_unique<AnimationClip>(cooked.definition,cooked.archive);
    pose_=std::make_unique<RigPose>(resources.rig.definition,resources.rig.archive);
    sample(0);
}
const std::vector<JointMatrix>& ComposerPosePreview::sample(double tick){
    require(std::isfinite(tick)&&tick>=0&&tick<=clip_->definition().ticks,"Composer scrub tick outside clip");
    // Sampling the exact final tick of a looping clip intentionally uses the
    // existing clip sampler's wrap semantics, identical to native presentation.
    return pose_->sample(*clip_,tick);
}
AuthoredCharacterPose prepare_authored_character_pose(AssetService& assets,NativeAuthoring& author,AssetId character,AssetId clip){
 auto& draft=author.open(assets,character);require(draft.value.at("kind")=="character","Choose a Character definition");std::vector<NativeSourceEdit> edits;
 for(const auto& [id,value]:author.drafts)if(id==character||value.dirty())edits.push_back({value.asset.path,sha256(value.saved),value.value.dump(2)+"\n"});
 auto candidate=assets.prepare_native(edits,{&clip,1});auto cooked=load_cooked_character(candidate.registry(),assets.cas_path(),character);auto animation=load_cooked_clip(candidate.registry(),assets.cas_path(),clip);CharacterPreviewResources resources;resources.skin=cooked.definition.skin;resources.rig=cooked.skin.rig;
 auto pose=std::make_unique<ComposerPosePreview>(resources,animation,animation.definition.ticks*action_tick_units);return {std::move(cooked),std::move(animation),std::move(pose)};
}
CharacterPreviewResources load_character_preview(const std::filesystem::path& registry,const std::filesystem::path& cas,std::string_view references,const RuntimeSkinnedModel& skin,StableId player,StableId target,AssetId collision){
    if(references.starts_with("player:")){auto player_asset=load_cooked_player(registry,cas,AssetId::parse(references.substr(7)));require(player_asset.character.definition.skin==skin.id,"Player Character skin does not match the scene's placed skin; update scene composition before Play");auto prepared=load_character_preview(registry,cas,"kit:"+player_asset.definition.loadout.kit.text(),skin,player,target,collision);prepared.combat->target_attributes=prepared.combat->attributes;prepared.combat->attributes=std::move(player_asset.attributes);prepared.combat->input=std::move(player_asset.input);prepared.combat->actor_generation=player_asset.definition.generation;return prepared;}
    auto finish=[&](CharacterPreviewResources resources){if(collision!=AssetId{})resources.collision=std::make_shared<const CollisionDefinition>(load_cooked_collision_scene(registry,cas,collision));return resources;};
    if(references.starts_with("kit:")){require(player&&target&&player!=target,"Combat preview requires explicit player and target scene identities");auto cooked=load_cooked_combat_kit(registry,cas,AssetId::parse(references.substr(4)));require(cooked.stance.rig.definition.id==skin.rig.definition.id&&cooked.stance.rig.definition.signature==skin.rig.definition.signature,"Combat kit/skin canonical rig mismatch");CharacterPreviewResources result{skin.id,cooked.stance.rig,cooked.stance.plan};result.player=player;CharacterSceneCombat combat;combat.kit=cooked.definition;combat.input=std::move(cooked.input);combat.attributes=cooked.attributes.definitions();combat.health=2;combat.maximum_health=1;combat.abilities=std::move(cooked.abilities);if(cooked.tags)combat.tags=cooked.tags->dictionary();combat.target=target;combat.evaluator=1;combat.effects=std::move(cooked.effects);ashen_roots::configure_royal_combat(combat);for(const auto& ability:combat.abilities){require(ability->action->motion.has_value(),"Combat action needs a frozen clip");auto clip=load_cooked_clip(registry,cas,ability->action->motion->clip.id);combat.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}result.combat=std::move(combat);return finish(std::move(result));}
    if(references.starts_with("graph:")){auto cooked=load_cooked_graph(registry,cas,AssetId::parse(references.substr(6)));require(cooked.rig.definition.id==skin.rig.definition.id&&cooked.rig.definition.signature==skin.rig.definition.signature,"Character graph/skin canonical rig mismatch");AnimationGraphInstance instance(cooked.plan);RigPose probe(skin.rig.definition,skin.rig.archive);probe.blend(instance.evaluate({}).span());return finish({skin.id,std::move(cooked.rig),std::move(cooked.plan)});}
    require(references.size()<=512,"Character clip reference limit");std::vector<std::shared_ptr<const AnimationClip>> clips;
    while(!references.empty()){auto comma=references.find(',');auto text=references.substr(0,comma);auto cooked=load_cooked_clip(registry,cas,AssetId::parse(text));clips.push_back(std::make_shared<const AnimationClip>(cooked.definition,cooked.archive));if(comma==references.npos)break;references.remove_prefix(comma+1);}
    require(clips.size()==9,"Character preset requires idle/walk/left/back/right/run/run-left/run-back/run-right");
    std::vector<GraphNode> nodes;for(unsigned index=0;index<clips.size();++index){GraphNode node;node.id=index+1;node.kind=GraphNodeKind::Clip;node.clip=clips[index];nodes.push_back(std::move(node));}
    GraphNode root;root.id=20;root.kind=GraphNodeKind::Blend2D;root.points={{1,0,0},{3,2,0},{2,0,2},{5,-2,0},{4,0,-2},{7,5,0},{6,0,5},{9,-5,0},{8,0,-5}};
    for(unsigned index=0;index<4;++index){unsigned a=index+1,b=(index+1)%4+1,c=index+5,d=(index+1)%4+5;root.triangles.push_back({0,a,b});root.triangles.push_back({a,c,d});root.triangles.push_back({a,d,b});}nodes.push_back(root);
    auto graph=std::make_shared<const AnimationGraphPlan>(sha256("native editor cardinal locomotion preset v1"),20,std::move(nodes));AnimationGraphInstance instance(graph);RigPose probe(skin.rig.definition,skin.rig.archive);probe.blend(instance.evaluate({}).span());return finish({skin.id,skin.rig,std::move(graph)});
}
std::pair<StableId,std::unique_ptr<CharacterSceneSession>> prepare_character_preview(const SpawnPlan& plan,const World& world,const CharacterPreviewResources& resources,const std::function<RuntimeModel(AssetId)>& load){
    std::vector<ObjectData> objects;for(const auto& origin:plan.origins){auto object=world.read(world.find(origin.object));if(resources.combat&&!resources.combat->actor_generation.empty()&&object.id==resources.player){AttributeSet initial(resources.combat->attributes);object.health={initial.value(resources.combat->maximum_health),initial.value(resources.combat->health)};}objects.push_back(std::move(object));}
    StableId player;CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;std::uint64_t identity=1000;
    if(resources.collision){collision=*resources.collision;require(collision.boxes.empty()&&!collision.meshes.empty()&&std::all_of(collision.meshes.begin(),collision.meshes.end(),[](const auto& mesh){return bool(mesh.data);}),"Character scene requires the explicit static mesh collision profile");}
    std::set<std::uint64_t> prepared_meshes;
    for(const auto& [object,asset]:plan.models){if(asset==resources.skin){if(resources.player){require(object==resources.player||(resources.combat&&object==resources.combat->target),"Combat preview character needs an explicit role");if(object==resources.player)player=object;}else{require(!player,"Initial character preview supports one player");player=object;}continue;}require(collision.boxes.size()<64,"Preview collision proxy limit");auto model=load(asset);require(!model.meshes.empty(),"Preview model has no bounds");
        if(resources.collision){auto expected=bake_model_collision(model,world.read(world.find(object)).transform,collision.meshes.front().data->runtime);const CollisionMeshDefinition* matched{};for(const auto& mesh:collision.meshes){require(bool(mesh.data),"Character collision geometry missing");if(mesh.data->vertices==expected.data->vertices&&mesh.data->triangles==expected.data->triangles&&mesh.data->materials==expected.data->materials){require(!matched,"Ambiguous duplicate collision placement");matched=&mesh;}}require(matched&&prepared_meshes.insert(matched->id).second,"Character scene collision is stale for the model/placement; bake its native source again");continue;}
        auto minimum=model.meshes.front().minimum,maximum=model.meshes.front().maximum;
        for(const auto& mesh:model.meshes)for(unsigned axis=0;axis<3;++axis){minimum[axis]=std::min(minimum[axis],mesh.minimum[axis]);maximum[axis]=std::max(maximum[axis],mesh.maximum[axis]);}
        auto pose=world.read(world.find(object)).transform;using namespace DirectX;auto rotation=XMMatrixRotationRollPitchYaw(float(pose.pitch),float(pose.yaw),float(pose.roll));auto center=XMVector3TransformCoord(XMVectorSet((minimum[0]+maximum[0])*.5f,(minimum[1]+maximum[1])*.5f,(minimum[2]+maximum[2])*.5f,1),XMMatrixScaling(float(pose.scale),float(pose.scale),float(pose.scale))*rotation*XMMatrixTranslation(float(pose.x),float(pose.y),float(pose.z)));XMFLOAT3 point;XMFLOAT4 quaternion;XMStoreFloat3(&point,center);XMStoreFloat4(&quaternion,XMQuaternionRotationRollPitchYaw(float(pose.pitch),float(pose.yaw),float(pose.roll)));
        CollisionBox box;box.id=++identity;box.center={point.x,point.y,point.z};box.half={std::max(.001,(maximum[0]-minimum[0])*pose.scale*.5),std::max(.001,(maximum[1]-minimum[1])*pose.scale*.5),std::max(.001,(maximum[2]-minimum[2])*pose.scale*.5)};box.rotation={quaternion.x,quaternion.y,quaternion.z,quaternion.w};collision.boxes.push_back(box);
    }
    if(resources.collision)require(prepared_meshes.size()==collision.meshes.size(),"Character scene has unused collision placements");
    std::ostringstream geometry_identity;geometry_identity<<std::hexfloat;for(const auto& box:collision.boxes){geometry_identity<<box.id<<':'<<box.center.x<<':'<<box.center.y<<':'<<box.center.z<<':'<<box.half.x<<':'<<box.half.y<<':'<<box.half.z;for(auto value:box.rotation)geometry_identity<<':'<<value;}for(const auto& mesh:collision.meshes)geometry_identity<<mesh.id<<':'<<mesh.data->runtime.text()<<':'<<mesh.data->signature;
    require(bool(player),"Scene has no configured skinned player");SessionHandshake hello{resources.combat?3u:2u,sha256(resources.combat?"DarkAngel native character preview wire3":"DarkAngel native character preview wire2"),sha256(plan.fingerprint+resources.rig.definition.signature+(resources.combat?resources.combat->kit->generation+resources.combat->actor_generation:"")+geometry_identity.str()),1};
    return {player,std::make_unique<CharacterSceneSession>(std::move(hello),objects,player,collision,resources.graph,resources.rig.definition,resources.rig.archive,resources.combat)};
}
}
