#pragma once
#include <darkangel/character_scene.hpp>
#include <darkangel/animation_assets.hpp>
#include <darkangel/actor_assets.hpp>
#include <darkangel/graph_assets.hpp>
#include "native_authoring.hpp"
#include <darkangel/assembly.hpp>
#include <functional>
namespace darkangel::editor_app {
struct CharacterPreviewResources {AssetId skin;CookedRig rig;std::shared_ptr<const AnimationGraphPlan> graph;StableId player;std::optional<CharacterSceneCombat> combat;std::shared_ptr<const CollisionDefinition> collision;};
// Authoring-only pose sampling. Owns independent Ozz buffers and never creates
// a WorldSession, action activation, motor request or gameplay event.
class ComposerPosePreview {
public:
    ComposerPosePreview(const CharacterPreviewResources&,CookedClip,unsigned action_duration);
    const std::vector<JointMatrix>& sample(double tick);
    const ClipDefinition& clip()const{return clip_->definition();}
    std::string_view generation()const{return clip_->archive_generation();}
private:
    std::unique_ptr<AnimationClip> clip_;std::unique_ptr<RigPose> pose_;
};
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
