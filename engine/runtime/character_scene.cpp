#include <darkangel/character_scene.hpp>
#include "scene_shared.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace darkangel {
using scene_detail::require;
struct CharacterSceneSession::Impl {
    // One loopback pair per local player. Declared first so both halves' sessions are destroyed before their transports.
    std::vector<LoopbackPair> links;
    std::shared_ptr<const CharacterSceneCombat> combat;
    std::unique_ptr<SceneAuthority> authority;std::vector<std::unique_ptr<SceneClient>> clients;std::vector<StableId> party;
    // A companion's input as last given, by seat; the first player's comes with advance and step.
    std::vector<CharacterSceneInput> held;
    SimulationClock clock;bool pending_jump{},failed{};CharacterSceneProfile profile;double pumping{};
    Impl(SessionHandshake hello,std::span<const ObjectData> objects,StableId identity,const CollisionDefinition& collision,
         std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition definition,std::string_view archive,std::optional<CharacterSceneCombat> configured,std::span<const ScenePlayer> companions){
        if(configured)combat=std::make_shared<const CharacterSceneCombat>(std::move(*configured));
        require(companions.size()<scene_party_limit&&(companions.empty()||combat),"Further players need a combat scene and fit the party limit");
        std::vector<SceneSeat> seats;std::vector<ConnectionHandle> peers;
        auto seat=[&](StableId id,MotorVec position,double yaw){links.push_back(create_loopback(hello));const auto host=links.back().host->open(hello);peers.push_back(links.back().client->open(hello));seats.push_back({id,links.back().host.get(),host,position,yaw});party.push_back(id);};
        seat(identity,{},0);for(const auto& companion:companions)seat(companion.id,companion.position,companion.yaw);
        authority=std::make_unique<SceneAuthority>(hello,objects,seats,collision,combat);
        for(std::size_t index=0;index<party.size();++index)clients.push_back(std::make_unique<SceneClient>(hello,*links[index].client,peers[index],party[index],party,collision,plan,definition,archive,combat));
        auto bootstrapped=[&]{if(!synchronized())return false;for(const auto& client:clients)if(!client->bootstrapped())return false;return true;};
        for(unsigned work=0;work<32&&!bootstrapped();++work)pump();
        for(auto& client:clients)client->start();
        held.resize(party.size());
    }
    // Lockstep only: every client holds exactly the characters authority simulates.
    bool synchronized()const{const auto characters=authority->characters();for(const auto& client:clients){if(client->characters()!=characters.size())return false;for(const auto id:characters)if(!client->knows(id))return false;}return true;}
    void pump(){authority->pump();for(auto& client:clients)client->receive();authority->pump();}
    void enqueue(std::span<const InputEvent> events){clients.front()->enqueue(events);}
    void step(const CharacterSceneInput& input){
        require(!failed,"Character scene requires a fresh synchronized session");
        const auto tick=authority->tick()+1;
        clients.front()->begin(tick,input);for(std::size_t index=1;index<clients.size();++index){clients[index]->begin(tick,held[index]);held[index].jump=false;held[index].interact.reset();}
        authority->step(tick);
        const auto mark=std::chrono::steady_clock::now();pump();
        auto ready=[&]{if(!synchronized())return false;for(auto& client:clients)if(!client->ready(tick))return false;return true;};
        for(unsigned work=0;work<32&&!ready();++work)pump();
        pumping+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-mark).count();
        for(auto& client:clients)client->finish(tick);
        if(combat){const auto after=std::chrono::steady_clock::now();for(auto& client:clients)client->flush();authority->pump();for(auto& client:clients)client->settle(tick);pumping+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-after).count();}
        profile=authority->profile();profile.replication+=pumping;for(const auto& client:clients){const auto& spent=client->profile();profile.prediction+=spent.prediction;profile.presentation+=spent.presentation;profile.replication+=spent.replication;}
    }
    std::optional<std::size_t> seat(StableId player)const{for(std::size_t index=0;index<party.size();++index)if(party[index]==player)return index;return std::nullopt;}
};
namespace {
class ScriptedAttacker final:public ActorController {
public:
    explicit ScriptedAttacker(ScriptedAttack script):script_(script){}
    ControllerCommand decide(const ControllerView& view)override{
        if(view.tick<script_.first_tick||(view.tick-script_.first_tick)%script_.interval)return {};
        const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return {};
        const double dx=foe->motor.position.x-view.self.position.x,dz=foe->motor.position.z-view.self.position.z,distance=std::hypot(dx,dz);
        if((script_.range>0&&distance>script_.range)||view.busy)return {};
        ControllerCommand command;command.press=script_.slot;command.target=foe->id;if(script_.face_opponent&&distance>1e-6)command.face=std::atan2(dx,dz);pressed_=view.tick;return command;
    }
    std::string status()const override{return pressed_?"Scripted attack (last press tick "+std::to_string(pressed_)+")":"Scripted attack (waiting)";}
private:ScriptedAttack script_;std::uint64_t pressed_{};
};
}
std::shared_ptr<ActorController> make_scripted_attacker(ScriptedAttack script){require(static_cast<unsigned>(script.slot)<combat_slot_count&&script.first_tick&&script.interval&&std::isfinite(script.range)&&script.range>=0,"Scripted attacker schedule bounds");return std::make_shared<ScriptedAttacker>(script);}
const PerceivedActor* nearest_hostile(const ControllerView& view){
    const PerceivedActor* best{};double nearest{};
    for(const auto& other:view.perceived){if(!other.detected||!other.alive||other.faction==view.faction)continue;const double distance=std::hypot(other.motor.position.x-view.self.position.x,other.motor.position.z-view.self.position.z);if(!best||distance<nearest){best=&other;nearest=distance;}}
    return best;
}
const PerceivedActor* last_lost_hostile(const ControllerView& view){
    const PerceivedActor* best{};for(const auto& other:view.perceived)if(!other.detected&&other.seen&&other.alive&&other.faction!=view.faction&&(!best||other.seen>best->seen))best=&other;return best;
}
CharacterSceneSession::CharacterSceneSession(SessionHandshake hello,std::span<const ObjectData> objects,StableId player,const CollisionDefinition& collision,std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition rig,std::string_view archive,std::optional<CharacterSceneCombat> combat,std::span<const ScenePlayer> companions)
    :impl_(std::make_unique<Impl>(std::move(hello),objects,player,collision,std::move(plan),std::move(rig),archive,std::move(combat),companions)){}
CharacterSceneSession::~CharacterSceneSession()=default;
unsigned CharacterSceneSession::advance(double seconds,CharacterSceneInput input){auto& p=*impl_;require(!p.failed,"Character scene requires a fresh synchronized session");require(std::isfinite(seconds)&&seconds>=0&&seconds<=10,"Character scene clock delta bounds");validate_motor_input({1,1,1,input.x,input.z,input.yaw,input.jump,input.crouch,input.pitch});p.enqueue(input.combat_events);input.combat_events.clear();p.pending_jump|=input.jump;auto steps=p.clock.advance(seconds);for(unsigned index=0;index<steps;++index){input.jump=p.pending_jump;try{p.step(input);}catch(...){p.failed=true;throw;}p.pending_jump=false;input.interact.reset();}return steps;}
void CharacterSceneSession::step(CharacterSceneInput input){try{impl_->enqueue(input.combat_events);input.combat_events.clear();impl_->step(input);}catch(...){impl_->failed=true;throw;}}
void CharacterSceneSession::input(StableId companion,CharacterSceneInput input){auto& p=*impl_;const auto seat=p.seat(companion);require(seat&&*seat>0,"Input names a companion of this session");validate_motor_input({1,1,1,input.x,input.z,input.yaw,input.jump,input.crouch,input.pitch});p.clients[*seat]->enqueue(input.combat_events);auto& held=p.held[*seat];const bool jump=held.jump||input.jump;const auto interact=input.interact?input.interact:held.interact;input.combat_events.clear();held=std::move(input);held.jump=jump;held.interact=interact;}
std::span<const StableId> CharacterSceneSession::players()const{return impl_->party;}
const SceneAuthority& CharacterSceneSession::authority()const{return *impl_->authority;}
const SceneClient* CharacterSceneSession::client(StableId player)const{const auto seat=impl_->seat(player);return seat?impl_->clients[*seat].get():nullptr;}
SceneClient* CharacterSceneSession::client(StableId player){const auto seat=impl_->seat(player);return seat?impl_->clients[*seat].get():nullptr;}
void CharacterSceneSession::remove_collision(std::uint64_t id){impl_->authority->remove_collision(id);}
const World& CharacterSceneSession::presentation()const{return impl_->clients.front()->presentation();}
const MotorState& CharacterSceneSession::motor()const{return *impl_->authority->motor(impl_->party.front());}
const MotorState& CharacterSceneSession::predicted_motor()const{return impl_->clients.front()->predicted_motor();}
const GraphState& CharacterSceneSession::graph()const{return impl_->clients.front()->graph();}
const GraphPoseInputs& CharacterSceneSession::graph_inputs()const{return impl_->clients.front()->graph_inputs();}
const std::vector<JointMatrix>& CharacterSceneSession::pose()const{return impl_->clients.front()->pose();}
const std::vector<JointMatrix>* CharacterSceneSession::pose(StableId actor)const{return impl_->clients.front()->pose(actor);}
const GraphPoseInputs* CharacterSceneSession::graph_inputs(StableId actor)const{return impl_->clients.front()->graph_inputs(actor);}
std::size_t CharacterSceneSession::pending_prediction()const{return impl_->clients.front()->pending_prediction();}
unsigned CharacterSceneSession::resynchronizations()const{return impl_->clients.front()->resynchronizations();}
const AbilityOwnerSnapshot* CharacterSceneSession::ability()const{return impl_->clients.front()->ability();}
const AbilityOwnerSnapshot* CharacterSceneSession::predicted_ability()const{return impl_->clients.front()->predicted_ability();}
std::size_t CharacterSceneSession::pending_abilities()const{return impl_->clients.front()->pending_abilities();}
MotorVec CharacterSceneSession::prediction_visual_offset()const{return impl_->clients.front()->prediction_visual_offset();}
ObserverAbilitySample CharacterSceneSession::observer(StableId identity,double render_tick)const{return impl_->clients.front()->observer(identity,render_tick);}
double CharacterSceneSession::debt()const{return impl_->clock.debt();}
std::span<const AbilityCommitUpdate> CharacterSceneSession::commitments()const{return impl_->authority->commitments();}
std::span<const EffectCueUpdate> CharacterSceneSession::effect_cues()const{return impl_->clients.front()->effect_cues();}
std::span<const ActionCueUpdate> CharacterSceneSession::action_cues()const{return impl_->clients.front()->action_cues();}
std::span<const ActionCueCut> CharacterSceneSession::action_cue_cuts()const{return impl_->clients.front()->action_cue_cuts();}
std::span<const FootstepUpdate> CharacterSceneSession::footsteps()const{return impl_->clients.front()->footsteps();}
void CharacterSceneSession::present_cues(CuePlayer* player){impl_->clients.front()->present_cues(player);}
std::optional<StableId> CharacterSceneSession::identity(std::uint64_t network)const{return impl_->clients.front()->identity(network);}
unsigned CharacterSceneSession::loadout()const{return impl_->clients.front()->loadout();}
unsigned CharacterSceneSession::predicted_loadout()const{return impl_->clients.front()->predicted_loadout();}
std::optional<StableId> CharacterSceneSession::soft_lock()const{return impl_->clients.front()->soft_lock();}
std::span<const StableId> CharacterSceneSession::actors()const{return impl_->authority->actors();}
const std::vector<ProjectileView>& CharacterSceneSession::projectiles()const{return impl_->clients.front()->projectiles();}
const CharacterSceneProfile& CharacterSceneSession::profile()const{return impl_->profile;}
std::uint64_t CharacterSceneSession::server_packets()const{return impl_->authority->server_packets(impl_->party.front());}
std::vector<std::uint64_t> CharacterSceneSession::server_packet_kinds()const{return impl_->authority->server_packet_kinds(impl_->party.front());}
AimSolution CharacterSceneSession::aim(MotorVec camera,MotorVec direction,double range,MotorVec muzzle)const{return impl_->clients.front()->aim(camera,direction,range,muzzle);}
std::string CharacterSceneSession::controller_status(StableId actor)const{return impl_->authority->controller_status(actor);}
const AbilityReceipt* CharacterSceneSession::controller_receipt(StableId actor)const{return impl_->authority->controller_receipt(actor);}
std::optional<StableId> CharacterSceneSession::controller_target(StableId actor)const{return impl_->authority->controller_target(actor);}
void CharacterSceneSession::emit_world_event(std::string name){impl_->authority->emit_world_event(std::move(name));}
void CharacterSceneSession::refuse_next_activation(){impl_->authority->refuse_next_activation(impl_->party.front());}
const WorldEvents& CharacterSceneSession::world_events()const{return impl_->authority->world_events();}
std::span<const StableId> CharacterSceneSession::encounters()const{return impl_->authority->encounters();}
std::optional<EncounterStatus> CharacterSceneSession::encounter(StableId id)const{return impl_->authority->encounter(id);}
std::optional<StableId> CharacterSceneSession::actor_encounter(StableId actor)const{return impl_->authority->actor_encounter(actor);}
std::optional<InteractionPrompt> CharacterSceneSession::interaction_prompt()const{return impl_->clients.front()->interaction_prompt();}
const InteractionOutcome* CharacterSceneSession::last_interaction()const{return impl_->clients.front()->last_interaction();}
std::optional<unsigned> CharacterSceneSession::interactable_used(StableId id)const{return impl_->authority->interactable_used(id);}
std::optional<unsigned> CharacterSceneSession::trigger_fired(StableId id)const{return impl_->authority->trigger_fired(id);}
const CharacterSceneActor* CharacterSceneSession::actor(StableId id)const{return impl_->authority->actor(id);}
const Perception* CharacterSceneSession::perception(StableId actor)const{return impl_->authority->perception(actor);}
std::optional<double> CharacterSceneSession::death(StableId actor)const{return impl_->clients.front()->death(actor);}
const ReactionSample* CharacterSceneSession::reaction(StableId actor)const{return impl_->clients.front()->reaction(actor);}
const EffectPresentation* CharacterSceneSession::effects(StableId actor)const{return impl_->clients.front()->effects(actor);}
}
