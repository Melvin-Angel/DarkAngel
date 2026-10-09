#include <ashen_roots/royal_combat.hpp>
#include <ashen_roots/royal_tags.hpp>
#include <algorithm>
#include <stdexcept>
namespace ashen_roots {
using namespace darkangel;
void configure_royal_combat(CharacterSceneCombat& combat){
 combat.damage=[](const DamageContext& context){return context.power;};
 if(combat.effects.empty())return;
 if(!combat.tags)throw std::invalid_argument("Royal effect rules require a prepared tag dictionary");
 if(!royal_tags::matches(*combat.tags))throw std::invalid_argument("Royal generated tag registry generation mismatch");
 auto invulnerable=royal_tags::Tag_State_Invulnerable_2;
 std::map<std::pair<AssetId,unsigned>,std::vector<DamageEffectRequest>> bindings;
 for(const auto& effect:combat.effects)if(effect->evaluator!=0&&effect->evaluator!=2)throw std::invalid_argument("Royal effect needs a registered game execution evaluator");
 for(const auto& binding:combat.kit->effect_bindings){auto effect=std::find_if(combat.effects.begin(),combat.effects.end(),[&](const auto& value){return value->id==binding.effect;});if(effect==combat.effects.end())throw std::invalid_argument("Royal missing prepared bound effect");bindings[{binding.ability,binding.hit_block}].push_back({*effect,binding.power});}
 combat.effect_evaluators.emplace(2,[](const EffectContext& context){return std::vector<ResourceDelta>{{2,-context.credit.power}};});
 combat.combat_damage=[bindings=std::move(bindings),invulnerable](const DamageContext& context){
  if(context.target_tags&&context.target_tags->has(invulnerable))return CombatEvaluation{};
  CombatEvaluation result{context.power,{}};if(result.damage>0){auto found=bindings.find({context.ability,context.block});if(found!=bindings.end())result.effects=found->second;}return result;
 };
}
}
