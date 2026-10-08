#pragma once
#include <darkangel/animation.hpp>
namespace darkangel {
struct JointMaskSource { AssetId id,skeleton; std::string canonical_source,generation; std::map<std::string,float> weights; };
struct JointMaskDefinition { AssetId id,skeleton; std::string signature,generation; std::vector<float> weights; };
JointMaskSource decode_joint_mask_source(std::string_view);
JointMaskDefinition compile_joint_mask(const JointMaskSource&,const RigDefinition&);
}
