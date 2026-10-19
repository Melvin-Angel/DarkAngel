#include <darkangel/scene_authority.hpp>
#include <darkangel/actor_controller.hpp>
#include <darkangel/scene_client.hpp>
#include <darkangel/collision_asset.hpp>
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
    std::vector<CharacterSceneInput> held;mutable std::vector<StableId> guest_actors;std::vector<StableId> candidates;
    SimulationClock clock;SceneTiming timing{SceneTiming::Lockstep};bool started{},pending_jump{},failed{};std::optional<StableId> pending_interact;CharacterSceneProfile profile;double pumping{};
    Impl(SessionHandshake hello,std::span<const ObjectData> objects,StableId identity,const CollisionDefinition& collision,
         std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition definition,std::string_view archive,std::optional<CharacterSceneCombat> configured,std::span<const ScenePlayer> companions,const SceneStart& start,std::span<const SceneSeat> remote,SceneTiming mode):timing(mode){
        if(configured){configured->combat_rig=definition;configured->combat_rig_archive=std::string(archive);combat=std::make_shared<const CharacterSceneCombat>(std::move(*configured));}
        require(companions.size()+remote.size()<scene_party_limit&&((companions.empty()&&remote.empty())||combat)&& (remote.empty()||mode==SceneTiming::Independent),"Further players need a combat scene and fit the party limit");
        std::vector<SceneSeat> seats;std::vector<ConnectionHandle> peers;
        auto seat=[&](StableId id,MotorVec position,double yaw){links.push_back(create_loopback(hello));const auto host=links.back().host->open(hello);peers.push_back(links.back().client->open(hello));seats.push_back({id,links.back().host.get(),host,position,yaw});party.push_back(id);};
        seat(identity,{},0);for(const auto& companion:companions)seat(companion.id,companion.position,companion.yaw);
        for(const auto& other:remote){seats.push_back(other);party.push_back(other.id);}
        authority=std::make_unique<SceneAuthority>(hello,objects,seats,collision,combat,start,mode);
        for(std::size_t index=0;index<peers.size();++index)clients.push_back(std::make_unique<SceneClient>(hello,*links[index].client,peers[index],party[index],party,collision,plan,definition,archive,combat,mode,start.profiles.contains(party[index])?start.profiles.at(party[index]):party[index]));
        auto bootstrapped=[&]{if(mode==SceneTiming::Lockstep&&!synchronized())return false;for(const auto& client:clients)if(!client->bootstrapped())return false;return true;};
        for(unsigned work=0;work<32&&!bootstrapped();++work)pump();
        if(mode==SceneTiming::Lockstep||bootstrapped()){for(auto& client:clients)client->start();started=true;}
        // A companion that has been given no input yet keeps facing the way it was placed.
        held.resize(clients.size());for(std::size_t index=1;index<clients.size();++index)held[index].yaw=authority->motor(party[index])->yaw;
    }
    Impl(SessionHandshake hello,Transport& transport,ConnectionHandle peer,StableId player,std::span<const StableId> members,const CollisionDefinition& collision,
         std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition rig,std::string_view archive,std::optional<CharacterSceneCombat> configured,StableId profile):party(members.begin(),members.end()),candidates(members.begin(),members.end()),timing(SceneTiming::Independent){
        if(configured){configured->combat_rig=rig;configured->combat_rig_archive=std::string(archive);combat=std::make_shared<const CharacterSceneCombat>(std::move(*configured));}
        clients.push_back(std::make_unique<SceneClient>(hello,transport,peer,player,party,collision,std::move(plan),std::move(rig),archive,combat,SceneTiming::Independent,profile));held.resize(1);
    }
    bool bootstrapped()const{for(const auto& client:clients)if(!client->bootstrapped())return false;return !clients.empty();}
    void start_if_ready(){if(!started&&bootstrapped()){for(auto& client:clients)client->start();started=true;}}
    unsigned independent(double seconds,CharacterSceneInput input){
        pump();start_if_ready();if(!started)return 0;
        const auto predicted=clients.front()->advance(seconds,std::move(input));for(std::size_t index=1;index<clients.size();++index){clients[index]->advance(seconds,held[index]);held[index].jump=false;held[index].interact.reset();}
        unsigned steps{};if(authority){steps=clock.advance(seconds);for(unsigned n=0;n<steps;++n){authority->step(authority->tick()+1);pump();}profile=authority->profile();}else{steps=predicted;profile={};}
        for(const auto& client:clients){const auto& spent=client->profile();profile.prediction+=spent.prediction;profile.presentation+=spent.presentation;profile.replication+=spent.replication;}return steps;
    }
    // Lockstep only: every client holds exactly the characters authority simulates.
    bool synchronized()const{const auto characters=authority->characters();for(const auto& client:clients){if(client->characters()!=characters.size())return false;for(const auto id:characters)if(!client->knows(id))return false;}return true;}
    void pump(){if(authority)authority->pump();for(auto& client:clients)client->receive();if(authority){authority->pump();const auto members=authority->party();party.assign(members.begin(),members.end());}
        // A guest sees the party as the members whose avatars the host replicates.
        else{std::vector<StableId> present;const auto& world=clients.front()->presentation();for(const auto id:candidates)if(world.valid(world.find(id)))present.push_back(id);if(!present.empty())party=std::move(present);}}
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
CharacterSceneSession::CharacterSceneSession(SessionHandshake hello,std::span<const ObjectData> objects,StableId player,const CollisionDefinition& collision,std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition rig,std::string_view archive,std::optional<CharacterSceneCombat> combat,std::span<const ScenePlayer> companions,const SceneStart& start,std::span<const SceneSeat> remote,SceneTiming timing)
    :impl_(std::make_unique<Impl>(std::move(hello),objects,player,collision,std::move(plan),std::move(rig),archive,std::move(combat),companions,start,remote,timing)){}
CharacterSceneSession::CharacterSceneSession(SessionHandshake hello,Transport& transport,ConnectionHandle peer,StableId player,std::span<const StableId> party,const CollisionDefinition& collision,std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition rig,std::string_view archive,std::optional<CharacterSceneCombat> combat,StableId profile)
    :impl_(std::make_unique<Impl>(std::move(hello),transport,peer,player,party,collision,std::move(plan),std::move(rig),archive,std::move(combat),profile)){}
CharacterSceneSession::~CharacterSceneSession()=default;
bool CharacterSceneSession::hosting()const{return bool(impl_->authority);}
bool CharacterSceneSession::bootstrapped()const{return impl_->started&&impl_->bootstrapped();}
void CharacterSceneSession::pump(){impl_->pump();impl_->start_if_ready();}
unsigned CharacterSceneSession::advance(double seconds,CharacterSceneInput input){auto& p=*impl_;if(p.timing==SceneTiming::Independent){require(!p.failed,"Character scene requires a fresh synchronized session");require(std::isfinite(seconds)&&seconds>=0&&seconds<=10,"Character scene clock delta bounds");validate_motor_input({1,1,1,input.x,input.z,input.yaw,input.jump,input.crouch,input.pitch});try{return p.independent(seconds,std::move(input));}catch(...){p.failed=true;throw;}}require(!p.failed,"Character scene requires a fresh synchronized session");require(std::isfinite(seconds)&&seconds>=0&&seconds<=10,"Character scene clock delta bounds");validate_motor_input({1,1,1,input.x,input.z,input.yaw,input.jump,input.crouch,input.pitch});p.enqueue(input.combat_events);input.combat_events.clear();p.pending_jump|=input.jump;if(input.interact)p.pending_interact=input.interact;auto steps=p.clock.advance(seconds);for(unsigned index=0;index<steps;++index){input.jump=p.pending_jump;input.interact=p.pending_interact;try{p.step(input);}catch(...){p.failed=true;throw;}p.pending_jump=false;p.pending_interact.reset();}return steps;}
void CharacterSceneSession::step(CharacterSceneInput input){if(impl_->timing==SceneTiming::Independent){advance(1./60,std::move(input));return;}try{impl_->enqueue(input.combat_events);input.combat_events.clear();impl_->step(input);}catch(...){impl_->failed=true;throw;}}
void CharacterSceneSession::input(StableId companion,CharacterSceneInput input){auto& p=*impl_;std::size_t local=1;while(local<p.clients.size()&&p.clients[local]->player()!=companion)++local;require(local<p.clients.size(),"Input names a local companion");validate_motor_input({1,1,1,input.x,input.z,input.yaw,input.jump,input.crouch,input.pitch});p.clients[local]->enqueue(input.combat_events);auto& held=p.held[local];const bool jump=held.jump||input.jump;const auto interact=input.interact?input.interact:held.interact;input.combat_events.clear();held=std::move(input);held.jump=jump;held.interact=interact;}
bool CharacterSceneSession::remove_member(StableId member){auto& p=*impl_;require(hosting(),"Guest cannot remove a party member");if(client(member)||!p.authority->remove_member(member))return false;const auto members=p.authority->party();p.party.assign(members.begin(),members.end());return true;}
void CharacterSceneSession::add_member(const SceneSeat& seat){auto& p=*impl_;require(hosting()&&p.timing==SceneTiming::Independent,"Live admission needs an independent host");p.authority->add_member(seat);for(auto& client:p.clients)client->admit(seat.id);}
std::vector<std::pair<StableId,std::string>> CharacterSceneSession::take_refused(){require(hosting(),"Guest holds no admissions");return impl_->authority->take_refused();}
std::span<const StableId> CharacterSceneSession::players()const{return impl_->party;}
const SceneAuthority& CharacterSceneSession::authority()const{require(bool(impl_->authority),"Guest has no scene authority");return *impl_->authority;}
std::optional<SceneTransitionRequest> CharacterSceneSession::take_transition(){return impl_->authority?impl_->authority->take_transition():std::nullopt;}
void CharacterSceneSession::capture(){require(hosting(),"Guest cannot capture shared world state");impl_->authority->capture();}
const SceneClient* CharacterSceneSession::client(StableId player)const{for(const auto& client:impl_->clients)if(client->player()==player)return client.get();return nullptr;}
SceneClient* CharacterSceneSession::client(StableId player){for(const auto& client:impl_->clients)if(client->player()==player)return client.get();return nullptr;}
void CharacterSceneSession::remove_collision(std::uint64_t id){require(hosting(),"Guest cannot remove shared collision");impl_->authority->remove_collision(id);}
const World& CharacterSceneSession::presentation()const{return impl_->clients.front()->presentation();}
const MotorState& CharacterSceneSession::motor()const{return impl_->authority?*impl_->authority->motor(impl_->clients.front()->player()):impl_->clients.front()->confirmed_motor();}
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
std::optional<ObserverAbilitySample> CharacterSceneSession::observer_if_ready(StableId identity,double render_tick)const{return impl_->clients.front()->observer_if_ready(identity,render_tick);}
double CharacterSceneSession::debt()const{return impl_->timing==SceneTiming::Independent?impl_->clients.front()->debt():impl_->clock.debt();}
std::span<const AbilityCommitUpdate> CharacterSceneSession::commitments()const{return impl_->authority?impl_->authority->commitments():std::span<const AbilityCommitUpdate>{};}
std::span<const EffectCueUpdate> CharacterSceneSession::effect_cues()const{return impl_->clients.front()->effect_cues();}
std::span<const ActionCueUpdate> CharacterSceneSession::action_cues()const{return impl_->clients.front()->action_cues();}
std::span<const ActionCueCut> CharacterSceneSession::action_cue_cuts()const{return impl_->clients.front()->action_cue_cuts();}
std::span<const FootstepUpdate> CharacterSceneSession::footsteps()const{return impl_->clients.front()->footsteps();}
void CharacterSceneSession::present_cues(CuePlayer* player){impl_->clients.front()->present_cues(player);}
std::optional<StableId> CharacterSceneSession::identity(std::uint64_t network)const{return impl_->clients.front()->identity(network);}
unsigned CharacterSceneSession::loadout()const{return impl_->clients.front()->loadout();}
unsigned CharacterSceneSession::predicted_loadout()const{return impl_->clients.front()->predicted_loadout();}
std::optional<StableId> CharacterSceneSession::soft_lock()const{return impl_->clients.front()->soft_lock();}
std::span<const StableId> CharacterSceneSession::actors()const{if(impl_->authority)return impl_->authority->actors();impl_->guest_actors=impl_->clients.front()->actors();return impl_->guest_actors;}
const std::vector<ProjectileView>& CharacterSceneSession::projectiles()const{return impl_->clients.front()->projectiles();}
const CharacterSceneProfile& CharacterSceneSession::profile()const{return impl_->profile;}
std::uint64_t CharacterSceneSession::server_packets()const{return impl_->authority?impl_->authority->server_packets(impl_->party.front()):0;}
std::vector<std::uint64_t> CharacterSceneSession::server_packet_kinds()const{return impl_->authority?impl_->authority->server_packet_kinds(impl_->party.front()):std::vector<std::uint64_t>{};}
AimSolution CharacterSceneSession::aim(MotorVec camera,MotorVec direction,double range,MotorVec muzzle)const{return impl_->clients.front()->aim(camera,direction,range,muzzle);}
std::string CharacterSceneSession::controller_status(StableId actor)const{return impl_->authority?impl_->authority->controller_status(actor):std::string{};}
const AbilityReceipt* CharacterSceneSession::controller_receipt(StableId actor)const{return impl_->authority?impl_->authority->controller_receipt(actor):nullptr;}
std::optional<StableId> CharacterSceneSession::controller_target(StableId actor)const{return impl_->authority?impl_->authority->controller_target(actor):std::nullopt;}
void CharacterSceneSession::emit_world_event(std::string name){require(hosting(),"Guest cannot emit shared world events");impl_->authority->emit_world_event(std::move(name));}
void CharacterSceneSession::refuse_next_activation(){require(hosting(),"Guest cannot refuse authority operations");impl_->authority->refuse_next_activation(impl_->party.front());}
const WorldEvents& CharacterSceneSession::world_events()const{require(hosting(),"World event debug state is host-only");return impl_->authority->world_events();}
std::span<const StableId> CharacterSceneSession::encounters()const{return impl_->authority?impl_->authority->encounters():std::span<const StableId>{};}
std::optional<EncounterStatus> CharacterSceneSession::encounter(StableId id)const{return impl_->authority?impl_->authority->encounter(id):std::nullopt;}
std::optional<StableId> CharacterSceneSession::actor_encounter(StableId actor)const{return impl_->authority?impl_->authority->actor_encounter(actor):impl_->clients.front()->actor_encounter(actor);}
std::optional<InteractionPrompt> CharacterSceneSession::interaction_prompt()const{return impl_->clients.front()->interaction_prompt();}
const InteractionOutcome* CharacterSceneSession::last_interaction()const{return impl_->clients.front()->last_interaction();}
std::optional<unsigned> CharacterSceneSession::interactable_used(StableId id)const{return impl_->authority?impl_->authority->interactable_used(id):std::nullopt;}
bool CharacterSceneSession::down_member(StableId member){return impl_->authority&&impl_->authority->down_member(member);}
std::optional<InventoryReceipt> CharacterSceneSession::grant_item(StableId member,AssetId item,unsigned quantity){return impl_->authority?impl_->authority->grant_item(member,item,quantity):std::nullopt;}
std::optional<unsigned> CharacterSceneSession::trigger_fired(StableId id)const{return impl_->authority?impl_->authority->trigger_fired(id):std::nullopt;}
const CharacterSceneActor* CharacterSceneSession::actor(StableId id)const{return impl_->authority?impl_->authority->actor(id):impl_->clients.front()->actor(id);}
const Perception* CharacterSceneSession::perception(StableId actor)const{return impl_->authority?impl_->authority->perception(actor):nullptr;}
std::optional<double> CharacterSceneSession::death(StableId actor)const{return impl_->clients.front()->death(actor);}
const ReactionSample* CharacterSceneSession::reaction(StableId actor)const{return impl_->clients.front()->reaction(actor);}
const EffectPresentation* CharacterSceneSession::effects(StableId actor)const{return impl_->clients.front()->effects(actor);}
}
