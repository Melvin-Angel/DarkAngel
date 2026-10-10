#pragma once
#include <darkangel/character_scene.hpp>
#include <darkangel/character_presentation.hpp>
#include <darkangel/animation_assets.hpp>
#include <darkangel/actor_assets.hpp>
#include <darkangel/graph_assets.hpp>
#include "native_authoring.hpp"
#include <darkangel/assembly.hpp>
#include <functional>
namespace darkangel::editor_app {
// Presentation of the active equipped mask: a static model on a rig socket joint.
struct WornMask {AssetId mask,model;std::string name;std::array<float,4> tint{1,1,1,1};unsigned joint{};std::array<float,3> position{},rotation{};float scale{1};};
struct CharacterPreviewResources {CameraRigDefinition cameras{default_camera_rig()};std::array<std::optional<WornMask>,mask_slot_count> masks;std::optional<WornMask> mask;std::array<std::string,mask_slot_count> mask_names;unsigned active_mask{};AssetId skin;CookedRig rig;std::shared_ptr<const AnimationGraphPlan> graph;StableId player;std::optional<CharacterSceneCombat> combat;std::shared_ptr<const CollisionDefinition> collision;};
// Authoring-only pose sampling. Owns independent Ozz buffers and never creates
// a WorldSession, action activation, motor request or gameplay event.
class ComposerPosePreview {
public:
    ComposerPosePreview(const CharacterPreviewResources&,CookedClip,unsigned action_duration,std::shared_ptr<const ActionDefinition> = {});
    const std::vector<JointMatrix>& sample(double tick);
    const ClipDefinition& clip()const{return clip_->definition();}
    std::string_view generation()const{return clip_->archive_generation();}
    const ActionDefinition* action()const{return action_.get();}
private:
    std::unique_ptr<AnimationClip> clip_;std::unique_ptr<RigPose> pose_;
    std::shared_ptr<const ActionDefinition> action_;std::unique_ptr<AnimationGraphInstance> graph_;std::unique_ptr<ActionPoseMixer> mixer_;ActorTagSnapshot tags_;
};
std::unique_ptr<ComposerPosePreview> prepare_authored_action_pose(AssetService&,NativeAuthoring&,const CharacterPreviewResources&,AssetId action,AssetId graph);
struct AuthoredCharacterPose {CookedCharacter character;CookedClip clip;std::unique_ptr<ComposerPosePreview> pose;};
AuthoredCharacterPose prepare_authored_character_pose(AssetService&,NativeAuthoring&,AssetId character,AssetId clip);
class GraphPosePreview {
public:
    explicit GraphPosePreview(const CookedGraph&);
    const std::vector<JointMatrix>& sample(unsigned tick,GraphParameters);
    const GraphState& state()const{return graph_.state();}
    const GraphPoseInputs& inputs()const{return inputs_;}
    GraphParameters parameters()const{return parameters_;}
    const TagDictionary* tag_dictionary()const{return graph_.tag_dictionary();}
private:AnimationGraphInstance graph_;RigPose pose_;GraphPoseInputs inputs_;GraphParameters parameters_;
    ActorTagSnapshot tags_;
};
struct AuthoredGraphPose {CookedCharacter character;CookedGraph graph;std::unique_ptr<GraphPosePreview> pose;};
AuthoredGraphPose prepare_authored_graph_pose(AssetService&,NativeAuthoring&,AssetId character,AssetId graph);
CharacterPreviewResources load_character_preview(const std::filesystem::path& registry,const std::filesystem::path& cas,std::string_view clips,const RuntimeSkinnedModel&,StableId player={},StableId target={},AssetId collision={});
std::pair<StableId,std::unique_ptr<CharacterSceneSession>> prepare_character_preview(const SpawnPlan&,const World&,const CharacterPreviewResources&,const std::function<RuntimeModel(AssetId)>&);
}
