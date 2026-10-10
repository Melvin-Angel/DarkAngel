#pragma once
#include "native_authoring.hpp"
#include <darkangel/effect_assets.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>

namespace darkangel::editor_app {
// Authoring-only state. No WorldSession, live actor handles or source publication.
class EffectPreview {
public:
 EffectPreview(AssetService& assets,NativeAuthoring& author,AssetId id){
  author.open(assets,id);
  std::vector<NativeSourceEdit> edits;
  for(const auto& [asset,draft]:author.drafts)if(asset==id||draft.dirty())
   edits.push_back({draft.asset.path,draft.pending?std::string{}:sha256(draft.saved),draft.value.dump(2)+"\n",draft.pending});
  candidate_=assets.prepare_native(edits,{&id,1});
  cooked=load_cooked_effect(candidate_.registry(),assets.cas_path(),id);
  if(cooked.definition->evaluator!=0)throw std::runtime_error("Preview supports ordinary modifiers/statuses only; custom evaluators must be tested in Game");
  attributes=std::make_unique<AttributeSet>(cooked.attributes.definitions());
  effects=std::make_unique<OwnedEffects>(cooked.tags.dictionary());
  for(const auto& definition:attributes->definitions())base.push_back(attributes->value(definition.id));
  authoring_revision=author.revision();
 }
 void apply(){handle=effects->apply(*cooked.definition,{1,1,++activation_,0,{}},tick,*attributes,{}).first;}
 void advance(unsigned ticks){
  if(ticks>600||tick+ticks>3600)throw std::runtime_error("Preview advance is bounded to 600 ticks per step and 3600 total; prepare again to reset");
  for(unsigned step=0;step<ticks;++step){effects->advance(tick+1,*attributes,[](std::uint32_t){return EffectEvaluator{};});++tick;}
 }
 void remove(){
  auto active=effects->snapshot();
  if(std::any_of(active.begin(),active.end(),[&](const auto& entry){return entry.handle==handle;}))effects->remove(handle,*attributes);
 }
 std::uint64_t authoring_revision{};
 CookedEffect cooked;std::unique_ptr<AttributeSet> attributes;std::unique_ptr<OwnedEffects> effects;
 std::vector<double> base;std::uint64_t tick{};EffectHandle handle;
private:
 PreparedNativeEdit candidate_;std::uint64_t activation_{};
};
}
