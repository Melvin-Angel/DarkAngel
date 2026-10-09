#pragma once
#include <darkangel/animation_assets.hpp>
#include <darkangel/animation_graph.hpp>
namespace darkangel {
// The initial cooked locomotion profile uses the existing compiled selectors.
// The plan owns all frozen clips; runtime loading needs no source mount.
struct CookedGraph {
    AssetId id;
    CookedRig rig;
    std::shared_ptr<const AnimationGraphPlan> plan;
};
CookedGraph load_cooked_graph(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
}
