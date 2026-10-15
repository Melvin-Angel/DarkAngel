#include "scene_authority_impl.hpp"
#include <darkangel/scene_authority.hpp>
#include <darkangel/world_session.hpp>
#include <darkangel/inventory_session.hpp>
#include <darkangel/assembly.hpp>
#include "scene_shared.hpp"
#include "scene_world_objects.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <iterator>
#include <set>

namespace darkangel {
using scene_detail::require;

std::size_t scene_actor_demand(const CharacterSceneCombat& combat){std::size_t planned=combat.actors.size();for(const auto& encounter:combat.encounters)planned+=encounter.definition.concurrent();return planned;}
SceneAuthority::SceneAuthority(SessionHandshake hello,std::span<const ObjectData> objects,std::span<const SceneSeat> party,const CollisionDefinition& collision,std::shared_ptr<const CharacterSceneCombat> combat,const SceneStart& start,SceneTiming timing)
    :impl_(std::make_unique<Impl>(std::move(hello),objects,party,collision,std::move(combat),start,timing)){}
SceneAuthority::~SceneAuthority()=default;
void SceneAuthority::step(std::uint64_t tick){impl_->step(tick);}
void SceneAuthority::pump(){impl_->pump();}
std::uint64_t SceneAuthority::tick()const{return impl_->authoritative.tick();}
std::optional<SceneTransitionRequest> SceneAuthority::take_transition(){auto asked=impl_->travel;impl_->travel.reset();return asked;}
void SceneAuthority::capture(){impl_->capture();}
void SceneAuthority::remove_collision(std::uint64_t id){impl_->authoritative.remove(id);}
bool SceneAuthority::remove_member(StableId id){return impl_->remove_member(id);}
void SceneAuthority::add_member(const SceneSeat& seat){impl_->add_member(seat);}
std::vector<std::pair<StableId,std::string>> SceneAuthority::take_refused(){auto result=std::move(impl_->refused);impl_->refused.clear();return result;}
std::span<const StableId> SceneAuthority::party()const{return impl_->member_identities;}
std::vector<std::uint64_t> SceneAuthority::characters()const{std::vector<std::uint64_t> result;for(const auto& member:impl_->members)result.push_back(member.id);for(const auto& actor:impl_->actors)result.push_back(actor.id);return result;}
std::optional<std::uint64_t> SceneAuthority::network(StableId member)const{const auto* found=impl_->member(member);return found?std::optional<std::uint64_t>{found->id}:std::nullopt;}
const MotorState* SceneAuthority::motor(StableId member)const{const auto* found=impl_->member(member);return found?&found->motor->state():nullptr;}
std::span<const AbilityCommitUpdate> SceneAuthority::commitments()const{return impl_->commitment_updates;}
std::span<const StableId> SceneAuthority::actors()const{return impl_->actor_identities;}
const CharacterSceneActor* SceneAuthority::actor(StableId id)const{const auto* found=impl_->find(id);return found?found->config:nullptr;}
const AbilityReceipt* SceneAuthority::controller_receipt(StableId actor)const{const auto* found=impl_->find(actor);return found&&found->receipt?&*found->receipt:nullptr;}
std::string SceneAuthority::controller_status(StableId actor)const{const auto* found=impl_->find(actor);return found&&found->config->controller?found->config->controller->status():std::string{};}
std::optional<StableId> SceneAuthority::controller_target(StableId actor)const{const auto* found=impl_->find(actor);return found?found->chosen:std::nullopt;}
const Perception* SceneAuthority::perception(StableId actor)const{const auto* found=impl_->find(actor);return found?found->perception.get():nullptr;}
void SceneAuthority::emit_world_event(std::string name){impl_->world.emit(std::move(name));}
void SceneAuthority::refuse_next_activation(StableId member){const auto* found=impl_->member(member);require(bool(impl_->combat)&&found,"A refusal needs a combat session and a party member");impl_->server.refuse_next_ability(found->id);}
const WorldEvents& SceneAuthority::world_events()const{return impl_->world;}
std::span<const StableId> SceneAuthority::encounters()const{return impl_->encounter_identities;}
std::optional<EncounterStatus> SceneAuthority::encounter(StableId id)const{const auto& p=*impl_;for(std::size_t index=0;index<p.directors.size();++index)if(p.encounter_identities[index]==id)return p.directors[index].status();return std::nullopt;}
std::optional<StableId> SceneAuthority::actor_encounter(StableId actor)const{const auto* found=impl_->find(actor);if(found&&found->encounter)return impl_->encounter_identities[*found->encounter];return std::nullopt;}
std::optional<unsigned> SceneAuthority::trigger_fired(StableId id)const{const auto& p=*impl_;for(std::size_t index=0;index<p.triggers.size();++index)if(p.combat->triggers[index].id==id)return p.triggers[index].fired();return std::nullopt;}
const WorldStateView& SceneAuthority::world_state()const{return impl_->view_now;}
bool SceneAuthority::down_member(StableId id){auto& p=*impl_;if(!p.combat)return false;for(const auto& m:p.members)if(m.identity==id){p.server.damage(m.owner,1e9);return true;}return false;}
std::vector<std::pair<StableId,MotorVec>> SceneAuthority::movers()const{return impl_->world_objects?impl_->world_objects->movers():std::vector<std::pair<StableId,MotorVec>>{};}
std::optional<InventoryReceipt> SceneAuthority::grant_item(StableId id,AssetId item,unsigned quantity){auto& p=*impl_;if(!p.inventory_session)return std::nullopt;for(const auto& known:p.members)if(known.identity==id)return p.inventory_store->grant(known.profile,item,quantity);return std::nullopt;}
std::optional<unsigned> SceneAuthority::interactable_used(StableId id)const{const auto& p=*impl_;for(std::size_t index=0;index<p.interactables.size();++index)if(p.combat->interactables[index].id==id)return p.interactables[index].used();return std::nullopt;}
std::uint64_t SceneAuthority::server_packets(StableId member)const{const auto* found=impl_->member(member);require(found,"Unknown party member");return impl_->server.packets_sent(found->peer);}
std::vector<std::uint64_t> SceneAuthority::server_packet_kinds(StableId member)const{const auto* found=impl_->member(member);require(found,"Unknown party member");return impl_->server.packets_by_kind(found->peer);}
const CharacterSceneProfile& SceneAuthority::profile()const{return impl_->profile;}
}
