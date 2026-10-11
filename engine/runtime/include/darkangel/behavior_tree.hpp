#pragma once
#include <darkangel/actor_controller.hpp>
#include <darkangel/behavior.hpp>
#include <darkangel/navigation.hpp>

namespace darkangel {
// BT.CPP executor adapter: runs one authored behaviour tree as a controller behind the
// seam. The tree is ticked once per authoritative tick with a native blackboard (the
// controller view in, the controller command out). The approach leaf follows the
// navigation mesh when one is supplied and walks straight otherwise.
std::shared_ptr<ActorController> make_behavior_controller(const BehaviorDefinition&,std::shared_ptr<const NavigationMesh> = {});
}
