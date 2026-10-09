#include <ashen_roots/royal_combat.hpp>
#include <algorithm>
#include <stdexcept>
namespace ashen_roots {
using namespace darkangel;
void configure_royal_combat(CharacterSceneCombat& combat){
 combat.damage=[](const DamageContext& context){return context.power;};
 if(combat.effects.empty())return;
 if(!combat.tags)throw std::invalid_argument("Royal effect rules require a prepared tag dictionary");
 auto tag=[&](std::string_view name){for(const auto& definition:combat.tags->definitions())if(definition.name==name)return definition.id;throw std::invalid_argument("Missing Royal gameplay tag");};
 auto invulnerable=tag("State.Invulnerable");std::shared_ptr<const EffectDefinition> burn;
 for(const auto& effect:combat.effects)for(const auto& cue:effect->cues)if(cue.key=="Status.Burn"){if(burn)throw std::invalid_argument("Ambiguous Royal Burn definition");burn=effect;}
 if(!burn||burn->evaluator!=2)throw std::invalid_argument("Royal Burn evaluator binding");
 combat.effect_evaluators.emplace(2,[](const EffectContext& context){return std::vector<ResourceDelta>{{2,-context.credit.power}};});
 combat.combat_damage=[burn,invulnerable](const DamageContext& context){
  if(context.target_tags&&context.target_tags->has(invulnerable))return CombatEvaluation{};
  CombatEvaluation result{context.power,{}};double health{};for(const auto& field:context.target_attributes)if(field.id==2)health=field.value;
  // Initial authored heavy/fire type. Light attacks retain their existing rule.
  if(context.damage_type==2&&result.damage>0&&health>result.damage)result.effects.push_back({burn,5});return result;
 };
}
}
