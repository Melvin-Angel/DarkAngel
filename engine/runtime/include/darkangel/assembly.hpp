#pragma once
#include <array>
#include <darkangel/asset_id.hpp>
#include <darkangel/script_runtime.hpp>
#include <functional>
#include <map>
namespace darkangel {
struct ObjectOrigin {StableId object;AssetId definition;std::string local_path,name,layer;};
struct PlannedBinding {StableId object;ScriptBinding binding;};
struct SpawnPlan {
    std::string scene_json,fingerprint;
    std::vector<ObjectOrigin> origins;
    std::vector<PlannedBinding> bindings;
    std::map<StableId,AssetId> models;
    // Spawn point objects and the NPC definition each one spawns.
    std::map<StableId,AssetId> spawns;
    // Gameplay volumes. encounters and triggers hold the object's validated component JSON. A volume names the
    // encounter whose bounds it is part of, a spawn point may name the encounter that uses it instead of spawning
    // at Play start. An owner that is no longer in the scene leaves an orphan, which Play ignores.
    struct Volume {StableId encounter;std::array<double,3> half{};};
    // interactables hold the object's validated "interactable" component JSON; the object may also be a placed model.
    std::map<StableId,std::string> interactables;
    // Party travel: entries and transitions hold the object's validated "entry" and "transition" component JSON. A
    // transition names its destination entry by that entry's identity in the destination scene's own plan.
    std::map<StableId,std::string> entries,transitions;
    // Splines hold the object's validated "spline" component JSON; a spline point names its spline and its place in the order.
    struct SplinePoint {StableId spline;unsigned index{};};
    std::map<StableId,std::string> splines;std::map<StableId,SplinePoint> spline_points;
    // The scene's audio profile: the one object with an "audio" component, and the profile asset it names. Presentation only.
    std::map<StableId,AssetId> audio_profiles;
    std::map<StableId,std::string> encounters,triggers;std::map<StableId,Volume> volumes;std::map<StableId,StableId> spawn_owners;
    std::string serialize() const;
    static SpawnPlan deserialize(std::string_view);
};
using AssemblySources=std::map<AssetId,std::string>;
// Stable-keyed source graph -> native scene and explicit script/resource prerequisites.
SpawnPlan resolve_assembly(AssetId root,StableId placement,const AssemblySources&);
// Identity of the runtime actor a spawn point creates. Derived from the spawn point alone, so
// the same placement yields the same actor on every Play and on client and server alike.
StableId spawned_actor(StableId spawn_point);
// Identity of the index-th enemy an encounter spawns. Derived from the encounter alone, so every Play of a scene has the same enemies.
StableId encounter_actor(StableId encounter,unsigned index);
// Identity of a party member beyond the first (seat 1 to 3) while players have no profile of their own. The first
// member is the scene's player object.
StableId party_member(unsigned seat);
// The stable identity of one scattered instance of a spline. Derived: there is no scene object behind it.
StableId scatter_instance(StableId spline,unsigned index);
struct ScriptDefinition {ScriptDescriptor descriptor;std::string cooked;};
struct CookedScene {
    SpawnPlan plan;std::map<StableId,ScriptDefinition> scripts;
    std::string serialize() const;
    static CookedScene deserialize(std::string_view);
};
std::map<StableId,ScriptDefinition> cook_script_definitions(const AssemblySources&);
class SceneSession {
public:
    struct Prepared {
        SpawnPlan plan;
        std::unique_ptr<World> world;
        std::unique_ptr<ScriptRuntime> scripts; // destroyed before its world
    };
    explicit SceneSession(WorldDomain domain):domain_(domain){}
    std::unique_ptr<Prepared> prepare(const SpawnPlan&,const std::map<StableId,ScriptDefinition>&,
        const std::function<void(AssetId)>& validate_resource={}) const;
    void activate(std::unique_ptr<Prepared>);
    void retire();
    Prepared* active() const{return active_.get();}
    void step(double seconds);
private:
    WorldDomain domain_;std::unique_ptr<Prepared> active_;
};
}
