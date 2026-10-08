#pragma once
#include <darkangel/asset_id.hpp>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <map>
namespace darkangel {
struct RigJoint {std::string key;int parent{-1};std::array<float,3> translation{},scale{1,1,1};std::array<float,4> rotation{0,0,0,1};};
struct RigDefinition {AssetId id,runtime;bool human{};std::string signature;std::vector<RigJoint> joints;std::map<std::string,std::string> sockets;};
RigDefinition decode_rig_source(std::string_view);
struct JointMatrix {std::array<float,16> values;};
// Pinned Ozz runtime is private. Each instance owns its buffers.
class RigPose {
public:
    RigPose(RigDefinition,std::string_view verified_archive);~RigPose();
    RigPose(const RigPose&)=delete;
    const std::vector<JointMatrix>& rest_pose();
    const RigDefinition& definition()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
