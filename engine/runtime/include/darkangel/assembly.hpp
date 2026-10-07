#pragma once
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
    std::string serialize() const;
    static SpawnPlan deserialize(std::string_view);
};
using AssemblySources=std::map<AssetId,std::string>;
// Stable-keyed source graph -> native scene and explicit script/resource prerequisites.
SpawnPlan resolve_assembly(AssetId root,StableId placement,const AssemblySources&);
struct ScriptDefinition {ScriptDescriptor descriptor;std::string cooked;};
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
