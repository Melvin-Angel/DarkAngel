#pragma once
#include <darkangel/world.hpp>
#include <array>
#include <cstddef>
#include <limits>
namespace darkangel::schema {
inline constexpr double finite_limit=1.0e12;
inline constexpr std::array transform{
    PropertyMetadata{1,"yaw",ValueKind::Number,Editable|SaveGame|AgentWritable|ScriptVisible,-finite_limit,finite_limit,offsetof(Transform,yaw)},
    PropertyMetadata{2,"x",ValueKind::Number,Editable|SaveGame|AgentWritable|ScriptVisible,-finite_limit,finite_limit,offsetof(Transform,x)},
    PropertyMetadata{3,"y",ValueKind::Number,Editable|SaveGame|AgentWritable|ScriptVisible,-finite_limit,finite_limit,offsetof(Transform,y)},
    PropertyMetadata{4,"z",ValueKind::Number,Editable|SaveGame|AgentWritable|ScriptVisible,-finite_limit,finite_limit,offsetof(Transform,z)},
    PropertyMetadata{5,"pitch",ValueKind::Number,Editable|SaveGame|AgentWritable|ScriptVisible,-finite_limit,finite_limit,offsetof(Transform,pitch)},
    PropertyMetadata{6,"roll",ValueKind::Number,Editable|SaveGame|AgentWritable|ScriptVisible,-finite_limit,finite_limit,offsetof(Transform,roll)},
    PropertyMetadata{7,"scale",ValueKind::Number,Editable|SaveGame|AgentWritable|ScriptVisible,0.001,1000,offsetof(Transform,scale)}
};
inline constexpr std::array health{
    PropertyMetadata{1,"maximum",ValueKind::Number,Editable|SaveGame|ScriptVisible,0,finite_limit,offsetof(Health,maximum)},
    PropertyMetadata{2,"current",ValueKind::Number,Editable|Replicated|SaveGame|AgentWritable|ScriptVisible,0,finite_limit,offsetof(Health,current)}
};
inline constexpr std::array network{
    PropertyMetadata{1,"value",ValueKind::ExactUnsigned,Replicated|ReadOnly|ScriptVisible,0,0,offsetof(NetworkIdentity,value)}
};
inline constexpr std::array script_bindings{PropertyMetadata{1,"count",ValueKind::ExactUnsigned,ReadOnly,0,0,offsetof(ScriptBindings,count)}};
inline constexpr std::array types{
    TypeMetadata{1,2,"Transform",transform}, TypeMetadata{2,1,"Health",health}, TypeMetadata{3,1,"NetworkIdentity",network},TypeMetadata{4,1,"ScriptBindings",script_bindings}
};
}
