#include "character_preview.hpp"
#include <darkangel/hash.hpp>
#include <darkangel/graph_assets.hpp>
#include <DirectXMath.h>
#include <stdexcept>
#include <algorithm>
namespace darkangel::editor_app {
namespace {void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}}
CharacterPreviewResources load_character_preview(const std::filesystem::path& registry,const std::filesystem::path& cas,std::string_view references,const RuntimeSkinnedModel& skin){
    if(references.starts_with("graph:")){auto cooked=load_cooked_graph(registry,cas,AssetId::parse(references.substr(6)));require(cooked.rig.definition.id==skin.rig.definition.id&&cooked.rig.definition.signature==skin.rig.definition.signature,"Character graph/skin canonical rig mismatch");AnimationGraphInstance instance(cooked.plan);RigPose probe(skin.rig.definition,skin.rig.archive);probe.blend(instance.evaluate({}).span());return {skin.id,std::move(cooked.rig),std::move(cooked.plan)};}
    require(references.size()<=512,"Character clip reference limit");std::vector<std::shared_ptr<const AnimationClip>> clips;
    while(!references.empty()){auto comma=references.find(',');auto text=references.substr(0,comma);auto cooked=load_cooked_clip(registry,cas,AssetId::parse(text));clips.push_back(std::make_shared<const AnimationClip>(cooked.definition,cooked.archive));if(comma==references.npos)break;references.remove_prefix(comma+1);}
    require(clips.size()==9,"Character preset requires idle/walk/left/back/right/run/run-left/run-back/run-right");
    std::vector<GraphNode> nodes;for(unsigned index=0;index<clips.size();++index){GraphNode node;node.id=index+1;node.kind=GraphNodeKind::Clip;node.clip=clips[index];nodes.push_back(std::move(node));}
    GraphNode root;root.id=20;root.kind=GraphNodeKind::Blend2D;root.points={{1,0,0},{3,2,0},{2,0,2},{5,-2,0},{4,0,-2},{7,5,0},{6,0,5},{9,-5,0},{8,0,-5}};
    for(unsigned index=0;index<4;++index){unsigned a=index+1,b=(index+1)%4+1,c=index+5,d=(index+1)%4+5;root.triangles.push_back({0,a,b});root.triangles.push_back({a,c,d});root.triangles.push_back({a,d,b});}nodes.push_back(root);
    auto graph=std::make_shared<const AnimationGraphPlan>(sha256("native editor cardinal locomotion preset v1"),20,std::move(nodes));AnimationGraphInstance instance(graph);RigPose probe(skin.rig.definition,skin.rig.archive);probe.blend(instance.evaluate({}).span());return {skin.id,skin.rig,std::move(graph)};
}
std::pair<StableId,std::unique_ptr<CharacterSceneSession>> prepare_character_preview(const SpawnPlan& plan,const World& world,const CharacterPreviewResources& resources,const std::function<RuntimeModel(AssetId)>& load){
    std::vector<ObjectData> objects;for(const auto& origin:plan.origins)objects.push_back(world.read(world.find(origin.object)));
    StableId player;CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;std::uint64_t identity=1000;
    for(const auto& [object,asset]:plan.models){if(asset==resources.skin){require(!player,"Initial character preview supports one player");player=object;continue;}require(collision.boxes.size()<64,"Preview collision proxy limit");auto model=load(asset);require(!model.meshes.empty(),"Preview model has no bounds");auto minimum=model.meshes.front().minimum,maximum=model.meshes.front().maximum;
        for(const auto& mesh:model.meshes)for(unsigned axis=0;axis<3;++axis){minimum[axis]=std::min(minimum[axis],mesh.minimum[axis]);maximum[axis]=std::max(maximum[axis],mesh.maximum[axis]);}
        auto pose=world.read(world.find(object)).transform;using namespace DirectX;auto rotation=XMMatrixRotationRollPitchYaw(float(pose.pitch),float(pose.yaw),float(pose.roll));auto center=XMVector3TransformCoord(XMVectorSet((minimum[0]+maximum[0])*.5f,(minimum[1]+maximum[1])*.5f,(minimum[2]+maximum[2])*.5f,1),XMMatrixScaling(float(pose.scale),float(pose.scale),float(pose.scale))*rotation*XMMatrixTranslation(float(pose.x),float(pose.y),float(pose.z)));XMFLOAT3 point;XMFLOAT4 quaternion;XMStoreFloat3(&point,center);XMStoreFloat4(&quaternion,XMQuaternionRotationRollPitchYaw(float(pose.pitch),float(pose.yaw),float(pose.roll)));
        CollisionBox box;box.id=++identity;box.center={point.x,point.y,point.z};box.half={std::max(.001,(maximum[0]-minimum[0])*pose.scale*.5),std::max(.001,(maximum[1]-minimum[1])*pose.scale*.5),std::max(.001,(maximum[2]-minimum[2])*pose.scale*.5)};box.rotation={quaternion.x,quaternion.y,quaternion.z,quaternion.w};collision.boxes.push_back(box);
    }
    require(bool(player),"Scene has no configured skinned player");SessionHandshake hello{2,sha256("DarkAngel native character preview wire2"),sha256(plan.fingerprint+resources.rig.definition.signature),1};
    return {player,std::make_unique<CharacterSceneSession>(std::move(hello),objects,player,collision,resources.graph,resources.rig.definition,resources.rig.archive)};
}
}
