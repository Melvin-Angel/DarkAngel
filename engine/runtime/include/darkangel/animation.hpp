#pragma once
#include <darkangel/asset_id.hpp>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <span>
namespace darkangel {
struct RigJoint {std::string key;int parent{-1};std::array<float,3> translation{},scale{1,1,1};std::array<float,4> rotation{0,0,0,1};};
struct RigDefinition {AssetId id,runtime;bool human{};std::string signature;std::vector<RigJoint> joints;std::map<std::string,std::string> sockets;};
RigDefinition decode_rig_source(std::string_view);
struct JointMatrix {std::array<float,16> values;};
struct ClipDefinition {
    AssetId id,runtime,skeleton;std::string signature;
    unsigned ticks{},joints{};bool loop{};
    // Cumulative translation XYZ and unwrapped yaw, relative to clip time zero.
    std::vector<std::array<float,4>> root;
};
ClipDefinition decode_clip_manifest(std::string_view);
struct ClipMotion {std::array<double,3> translation{};double yaw{};};
// Forward tick intervals only, at most eight ticks of work. Loop transforms
// compose translation with yaw rather than adding a hidden reset displacement.
ClipMotion clip_root_delta(const ClipDefinition&,double from_tick,double to_tick);
class AnimationClip {
public:
    AnimationClip(ClipDefinition,std::string_view verified_archive);~AnimationClip();
    AnimationClip(const AnimationClip&)=delete;
    const ClipDefinition& definition()const;
    std::string_view archive_generation()const;
private:struct Impl;std::unique_ptr<Impl> impl_;friend class RigPose;
};
// Regular local-space pose layers. Masks are compiled canonical joint order,
// never retargeting maps. Additive clips and graph/slot arbitration are separate.
struct PoseLayer {const AnimationClip* clip{};double tick{};float weight{};std::span<const float> mask;};
// Pinned Ozz runtime is private. Each instance owns its buffers.
class RigPose {
public:
    RigPose(RigDefinition,std::string_view verified_archive);~RigPose();
    RigPose(const RigPose&)=delete;
    const std::vector<JointMatrix>& rest_pose();
    const std::vector<JointMatrix>& sample(const AnimationClip&,double tick);
    const std::vector<JointMatrix>& blend(std::span<const PoseLayer>);
    const RigDefinition& definition()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
