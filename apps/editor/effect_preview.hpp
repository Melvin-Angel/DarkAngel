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
  source_path=author.open(assets,id).asset.path;
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
 void reset(){
  auto fresh_attributes=std::make_unique<AttributeSet>(cooked.attributes.definitions());auto fresh_effects=std::make_unique<OwnedEffects>(cooked.tags.dictionary());attributes=std::move(fresh_attributes);effects=std::move(fresh_effects);tick=0;handle={};activation_=0;last_error.clear();last_operation="Reset frozen simulation; snapshot source/revision unchanged.";
 }
 void apply(std::uint64_t source=1){
  if(!source||activation_>=4096)throw std::runtime_error("Use a nonzero simulated source; reprepare after 4096 applications");
  auto before=effects->snapshot();auto next=activation_+1;auto result=effects->apply(*cooked.definition,{1,source,next,0,{}},tick,*attributes,{});activation_=next;handle=result.first;bool refreshed=std::any_of(before.begin(),before.end(),[&](const auto& entry){return entry.handle==handle;});last_operation=std::string(refreshed?"Refreshed":"Applied")+" effect "+std::to_string(handle.value)+" from simulated source "+std::to_string(source)+".";
 }
 void destroy_source(std::uint64_t source){if(!source)throw std::runtime_error("Use a nonzero simulated source");auto before=effects->snapshot();auto matching=std::count_if(before.begin(),before.end(),[&](const auto& entry){return entry.credit.session_epoch==1&&entry.credit.source_network==source;});effects->source_destroyed(1,source,*attributes);auto removed=before.size()-effects->snapshot().size();last_operation=!matching?"No active effect credited to this simulated source.":removed?"Removed "+std::to_string(removed)+" source-owned effect(s); other sources retained.":"Source effects retained: frozen Remove with source policy is disabled.";}
 void death_cleanup(){auto before=effects->snapshot().size();effects->death(*attributes);auto removed=before-effects->snapshot().size();last_operation=!before?"No active effects to clean up.":removed?"Death cleanup removed "+std::to_string(removed)+" effect(s).":"Effects retained: frozen Remove on death policy is disabled.";}
 std::vector<EffectSnapshot> contributors(TagId tag)const{
  auto result=effects->snapshot();const auto& dictionary=effects->tags().dictionary();
  std::erase_if(result,[&](const auto& entry){return entry.suppressed||entry.definition!=cooked.definition->id||!std::any_of(cooked.definition->tags.begin(),cooked.definition->tags.end(),[&](auto grant){return dictionary.descends(grant,tag);});});return result;
 }
 void advance(unsigned ticks){
  if(ticks>600||tick+ticks>3600)throw std::runtime_error("Preview advance is bounded to 600 ticks per step and 3600 total; prepare again to reset");
  auto before=effects->snapshot().size();for(unsigned step=0;step<ticks;++step){effects->advance(tick+1,*attributes,[](std::uint32_t){return EffectEvaluator{};});++tick;}last_operation="Advanced "+std::to_string(ticks)+" ticks; "+std::to_string(before-effects->snapshot().size())+" effect(s) ended.";
 }
 void remove(EffectHandle selected={}){
  if(!selected.value)selected=handle;auto active=effects->snapshot();
  if(std::any_of(active.begin(),active.end(),[&](const auto& entry){return entry.handle==selected;})){effects->remove(selected,*attributes);last_operation="Removed effect "+std::to_string(selected.value)+".";}else last_operation="No active effect matches the selected handle.";
 }
 void record_failure(std::string_view operation,std::string_view reason,std::uint64_t source=0){last_error=std::string(operation)+" failed for frozen Effect "+source_path+" ("+cooked.definition->id.text()+", revision "+std::to_string(authoring_revision)+(source?", source "+std::to_string(source):std::string{})+"): "+std::string(reason);}
 std::string source_path,last_error,last_operation;std::uint64_t authoring_revision{};
 CookedEffect cooked;std::unique_ptr<AttributeSet> attributes;std::unique_ptr<OwnedEffects> effects;
 std::vector<double> base;std::uint64_t tick{};EffectHandle handle;
private:
 PreparedNativeEdit candidate_;std::uint64_t activation_{};
};
}
