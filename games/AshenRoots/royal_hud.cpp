#include <ashen_roots/royal_hud.hpp>
#include <ashen_roots/royal_tags.hpp>
#include <algorithm>
namespace ashen_roots {
using namespace darkangel;
HudViewModel royal_hud(const AbilityOwnerSnapshot& owner,std::span<const AttributeDefinition> schema,std::optional<Health> target,std::string_view mask){
 auto value=[&](AttributeId id){for(const auto& attribute:owner.attributes)if(attribute.id==id)return attribute.value;return 0.;};
 HudViewModel model;model.health=value(owner.health_attribute);model.maximum_health=value(owner.maximum_health_attribute);
 // Stamina is the Royal schema's attribute 3; its ceiling is the authored bound or its maximum attribute.
 for(const auto& attribute:schema)if(attribute.id==3){model.stamina=value(3);model.maximum_stamina=attribute.maximum_attribute?value(attribute.maximum_attribute):attribute.maximum;}
 model.staggered=std::find(owner.tags.values.begin(),owner.tags.values.end(),royal_tags::Tag_State_Staggered_6)!=owner.tags.values.end();
 if(target){model.has_target=true;model.target_health=target->current;model.target_maximum_health=target->maximum;}
 model.mask=std::string(mask);
 return model;
}
}
