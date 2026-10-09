#include <darkangel/character_scene.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}}
struct CharacterSceneSession::Impl {
    LoopbackPair transport;
    WorldSession server,client;
    ConnectionHandle host_peer,client_peer;
    PhysicsWorld authoritative;std::unique_ptr<PhysicsWorld> prediction=std::make_unique<PhysicsWorld>(PhysicsWorld::Mode::Prediction);
    std::unique_ptr<CharacterMotor> host_motor,owner_motor,target_motor;
    std::optional<CharacterSceneCombat> combat;AbilityOwnerHandle ability_owner;std::uint64_t target{},next_operation{1};std::vector<InputEvent> pending_events;
    std::unique_ptr<OwnerPrediction> owner;
    CollisionHistory history;
    std::vector<CollisionMesh> prepared_geometry;
    AnimationGraphInstance graph;
    RigPose rig;
    std::vector<JointMatrix> matrices;
    SimulationClock clock;
    std::uint64_t player{};
    bool pending_jump{},failed{};unsigned resynchronizations{};
    Impl(SessionHandshake hello,std::span<const ObjectData> objects,StableId identity,const CollisionDefinition& collision,
         std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition definition,std::string_view archive,std::optional<CharacterSceneCombat> configured)
        :transport(create_loopback(hello)),server(SessionRole::Server,hello),client(SessionRole::Client,hello),graph(std::move(plan)),rig(std::move(definition),archive){
        combat=std::move(configured);require((hello.protocol==2||hello.protocol==3)&&(!combat||hello.protocol==3)&&rig.definition().human,"Character scene requires wire2 and the initial human profile");require(!objects.empty()&&objects.size()<=128&&identity,"Character scene object/player bounds");
        host_peer=transport.host->open(hello);client_peer=transport.client->open(hello);
        server.attach(*transport.host,host_peer);client.attach(*transport.client,client_peer);
        for(const auto& box:collision.boxes)require(!box.moving&&!box.dynamic&&!box.sensor,"Character scene initial profile requires static collision");
        MotorVec foot,target_foot;double yaw{},target_yaw{};
        for(const auto& object:objects){auto id=server.create(object);if(object.id==identity){require(!player,"Duplicate character scene player");require(object.transform.scale==1&&object.transform.pitch==0&&object.transform.roll==0&&object.health.current>0,"Character scene requires an upright, unscaled live player");player=id;foot={object.transform.x,object.transform.y,object.transform.z};yaw=object.transform.yaw;}if(combat&&object.id==combat->target){require(object.id!=identity&&!target&&object.transform.scale==1&&object.health.current>0,"Combat target profile");target=id;target_foot={object.transform.x,object.transform.y,object.transform.z};target_yaw=object.transform.yaw;}}
        require(player,"Character scene player missing");authoritative.load_scene(collision);prediction->load_scene(collision);
        host_motor=std::make_unique<CharacterMotor>(authoritative,foot,player);owner_motor=std::make_unique<CharacterMotor>(*prediction,foot,player);
        if(combat){require(combat->kit&&target&&combat->evaluator&&bool(combat->damage),"Combat scene requires frozen kit/target/game evaluator");validate_combat_kit(*combat->kit,combat->input);for(const auto& ability:combat->abilities){require(ability&&ability->action->motion&&!ability->action->upper_body,"Initial scene action requires a full-body frozen clip");auto clip=combat->clips.at(ability->action->motion->clip.id);require(clip&&clip->definition().skeleton==rig.definition().id&&clip->definition().signature==rig.definition().signature&&clip->archive_generation()==ability->action->motion->archive_generation,"Scene action clip generation/rig mismatch");}
            target_motor=std::make_unique<CharacterMotor>(authoritative,target_foot,target);auto state=target_motor->state();state.yaw=target_yaw;target_motor->restore(state);server.bind_melee_query(std::make_shared<PhysicsMeleeQuery>(authoritative));server.register_damage_evaluator(combat->evaluator,combat->damage);ability_owner=server.configure_abilities(player,combat->attributes,combat->health,combat->maximum_health);server.configure_abilities(target,combat->attributes,combat->health,combat->maximum_health);server.equip_combat_kit(ability_owner,combat->kit,combat->input,combat->abilities);server.publish_motor(target,target_motor->state());server.drain_ability_actions();}
        auto host_state=host_motor->state();host_state.topology=authoritative.topology();host_state.yaw=yaw;host_motor->restore(host_state);auto owner_state=owner_motor->state();owner_state.yaw=yaw;owner_motor->restore(owner_state);
        owner=std::make_unique<OwnerPrediction>(*prediction,*owner_motor);prepared_geometry=prediction->capture().meshes;
        server.own_motor(player,host_peer);server.publish_motor(player,host_motor->state());
        auto initial=authoritative.capture();history.retain(initial);server.publish_collision(collision_stream(initial));
        for(unsigned work=0;work<32&&(!client.motors().contains(player)||!client.collision_control_ready()||(combat&&!client.ability_corrections().contains(player)));++work)pump();
        require(client.readiness(client_peer)==SessionReadiness::Ready&&client.collision_control_ready()&&client.motors().contains(player),"Character scene bootstrap/control work limit");
        matrices=rig.blend(graph.evaluate({}).span());
    }
    void pump(){server.tick();client.tick();
        if(client.readiness(client_peer)==SessionReadiness::Ready&&!client.collisions().empty()){
            // Native geometry is prepared before acknowledging any frame.
            auto frame=prepare_collision_frame(client.collisions().back(),prepared_geometry);
            auto snapshot=client.motors().find(player);if(snapshot==client.motors().end()||snapshot->second.tick!=frame.tick)return;
            if(frame.tick<prediction->tick())return;
            require(frame.tick==prediction->tick(),"Character scene collision tick mismatch; resynchronize");
            if(frame.topology!=prediction->topology()||snapshot->second.epoch!=owner_motor->state().epoch){
                // A new baseline retires the old prediction world. No live world
                // is moved backward and no unacknowledged input is silently kept.
                require(snapshot->second.tick==owner_motor->state().tick&&snapshot->second.sequence==owner_motor->state().sequence,"Character scene topology baseline has pending input; resynchronize");
                auto replacement=std::make_unique<PhysicsWorld>(PhysicsWorld::Mode::Prediction);replacement->load(frame,player);auto motor=std::make_unique<CharacterMotor>(*replacement,snapshot->second.position,player,snapshot->second.crouched);replacement->load(frame,player);motor->restore(snapshot->second);
                owner.reset();owner_motor.reset();prediction=std::move(replacement);owner_motor=std::move(motor);owner=std::make_unique<OwnerPrediction>(*prediction,*owner_motor);++resynchronizations;
            }else require(prediction->update_prediction(frame,*owner_motor),"Character scene prediction collision preparation failed");
            client.acknowledge_collision(frame.tick);
        }
        client.tick();server.tick();
    }
    const AbilityDefinition& definition(const AbilityOwnerSnapshot& snapshot)const{
        require(combat&&snapshot.active&&snapshot.action,"Active combat correction required");for(const auto& ability:combat->abilities)if(ability->id==snapshot.ability){require(ability->generation==snapshot.ability_generation&&ability->action->id==snapshot.action_definition&&ability->action->generation==snapshot.action->generation,"Combat correction frozen generation mismatch");return *ability;}throw std::runtime_error("Combat correction missing local ability");
    }
    MotionRequest motion(const AbilityOwnerSnapshot& snapshot,std::uint64_t from,std::uint64_t to,MotorInput& command)const{
        const auto& action=*definition(snapshot).action;auto local=action_motion_between(action,from,to);const auto& v=local.root.translation;auto yaw=command.yaw;command.yaw=std::remainder(command.yaw+local.root.yaw,6.283185307179586);MotionRequest request;request.root={std::cos(yaw)*v[0]+std::sin(yaw)*v[2],v[1],-std::sin(yaw)*v[0]+std::cos(yaw)*v[2]};request.action=snapshot.active->activation;request.lock=local.movement_lock;return request;
    }
    void enqueue(std::span<const InputEvent> events){if(!combat){require(events.empty(),"Combat input requires a configured kit");return;}auto staged=pending_events;for(const auto& event:events)for(const auto& slot:combat->kit->slots)if(slot.input_action==event.action){require(staged.size()<32,"Combat input queue full; resynchronize");staged.push_back(event);}pending_events=std::move(staged);}
    void step(CharacterSceneInput input){
        require(!failed,"Character scene requires a fresh synchronized session");require(!input.crouch,"Character scene posture animation is not in the initial standing profile");require(std::isfinite(input.x)&&std::isfinite(input.z)&&std::isfinite(input.yaw),"Character scene input finite");
        const auto tick=authoritative.tick()+1;MotorInput command{tick,tick,owner_motor->state().epoch,input.x,input.z,input.yaw,input.jump,input.crouch};validate_motor_input(command);
        require(client.collision_control_ready()&&client.submit_motor(player,command),"Character scene control unavailable; resynchronize");
        if(combat){require(client.ability_corrections().contains(player)&&!client.ability_correction_needs_resync(),"Combat correction unavailable; resynchronize");const auto& current=client.ability_corrections().at(player).ability;for(const auto& event:pending_events)for(const auto& slot:combat->kit->slots)if(slot.input_action==event.action){require(next_operation!=UINT64_MAX&&client.submit_ability_intent({player,next_operation++,tick,owner_motor->state().epoch,current.grant_generation,slot.slot,event.edge,event.cancelled,false}),"Combat input delivery unavailable; resynchronize");}pending_events.clear();}
        server.tick();require(server.motor_input_ready(player,tick),"Character scene owner command unavailable");
        auto accepted=server.consume_motor(player,tick,command.epoch);
        MotionRequest authoritative_motion,predicted_motion;auto predicted_command=command;
        if(combat){auto before=server.ability_snapshot(ability_owner);const auto& confirmed=client.ability_corrections().at(player).ability;if(confirmed.active){auto total=std::uint64_t(definition(confirmed).action->duration)*definition(confirmed).action->loops;predicted_motion=motion(confirmed,confirmed.action->clock,std::min(total,confirmed.action->clock+action_tick_units),predicted_command);}server.advance_abilities(tick);auto after=server.ability_snapshot(ability_owner);auto updates=server.drain_ability_actions();if(after.active){auto from=before.active&&before.active==after.active?before.action->clock:0;authoritative_motion=motion(after,from,after.action->clock,accepted);}else if(before.active)for(const auto& update:updates)if(update.handle==*before.active&&update.phase==ActionPhase::Completed){auto total=std::uint64_t(definition(before).action->duration)*definition(before).action->loops;authoritative_motion=motion(before,before.action->clock,total,accepted);}}
        owner->predict(predicted_command,predicted_motion);prediction->step();owner_motor->post_physics();prediction->finish_tick();
        host_motor->step(accepted,authoritative_motion);if(target_motor)target_motor->step({tick,tick,target_motor->state().epoch,0,0,target_motor->state().yaw});authoritative.step();host_motor->post_physics();if(target_motor)target_motor->post_physics();authoritative.finish_tick();
        auto frame=authoritative.capture();history.retain(frame);server.publish_motor(player,host_motor->state());if(target_motor){server.publish_motor(target,target_motor->state());server.resolve_ability_hits(tick);}server.publish_collision(collision_stream(frame));pump();
        for(unsigned work=0;combat&&work<32&&(!client.ability_corrections().contains(player)||client.ability_corrections().at(player).ability.tick!=tick||!client.collision_control_ready());++work)pump();
        require(client.motors().contains(player)&&client.motors().at(player).tick==tick&&!client.collisions().empty()&&client.collisions().back().tick==tick,"Character scene snapshot delivery bound");
        if(combat){const auto& correction=client.ability_corrections().at(player);require(correction.ability.tick==tick&&correction.motor.tick==tick,"Atomic combat correction clock");if(correction.ability.active)definition(correction.ability);}
        require(owner->reconcile(combat?client.ability_corrections().at(player).motor:client.motors().at(player),history)==ReplayResult::Applied&&!owner->needs_resync(),"Character scene isolated correction failed; resynchronize");
        const auto& state=host_motor->state();double x=state.achieved.x*60-state.support_velocity.x,z=state.achieved.z*60-state.support_velocity.z;
        GraphParameters parameters;parameters.speed=float(std::hypot(x,z));parameters.forward=float(std::sin(state.yaw)*x+std::cos(state.yaw)*z);parameters.lateral=float(std::cos(state.yaw)*x-std::sin(state.yaw)*z);
        auto selected=graph.evaluate(parameters);double stride{};
        for(const auto& layer:selected.span()){const auto& clip=layer.clip->definition();const auto& end=clip.root.back();stride+=layer.weight*std::hypot(end[0],end[2])*60/clip.ticks;}
        if(parameters.speed>.01&&stride>.01)parameters.playback_rate=float(std::clamp(parameters.speed/stride,0.,4.));
        matrices=rig.blend(graph.advance(tick,parameters).span());
        if(combat){require(client.ability_corrections().contains(player)&&client.ability_corrections().at(player).ability.tick==tick,"Combat correction delivery bound");const auto& correction=client.ability_corrections().at(player);require(correction.motor.tick==tick,"Combat motor/action atomic clock");if(correction.ability.active){const auto& ability=definition(correction.ability);matrices=rig.sample(*combat->clips.at(ability.action->motion->clip.id),double(correction.ability.action->clock)/action_tick_units);}client.acknowledge_ability_correction(player);client.drain_ability_receipts();client.tick();server.tick();}

    }
};
CharacterSceneSession::CharacterSceneSession(SessionHandshake hello,std::span<const ObjectData> objects,StableId player,const CollisionDefinition& collision,std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition rig,std::string_view archive,std::optional<CharacterSceneCombat> combat)
    :impl_(std::make_unique<Impl>(std::move(hello),objects,player,collision,std::move(plan),std::move(rig),archive,std::move(combat))){}
CharacterSceneSession::~CharacterSceneSession()=default;
unsigned CharacterSceneSession::advance(double seconds,CharacterSceneInput input){auto& p=*impl_;require(!p.failed,"Character scene requires a fresh synchronized session");require(std::isfinite(seconds)&&seconds>=0&&seconds<=10,"Character scene clock delta bounds");validate_motor_input({1,1,1,input.x,input.z,input.yaw,input.jump,input.crouch});p.enqueue(input.combat_events);input.combat_events.clear();p.pending_jump|=input.jump;auto steps=p.clock.advance(seconds);for(unsigned index=0;index<steps;++index){input.jump=p.pending_jump;try{p.step(input);}catch(...){p.failed=true;throw;}p.pending_jump=false;}return steps;}
void CharacterSceneSession::step(CharacterSceneInput input){try{impl_->enqueue(input.combat_events);input.combat_events.clear();impl_->step(input);}catch(...){impl_->failed=true;throw;}}
void CharacterSceneSession::remove_collision(std::uint64_t id){impl_->authoritative.remove(id);}
const World& CharacterSceneSession::presentation()const{return impl_->client.world();}
const MotorState& CharacterSceneSession::motor()const{return impl_->host_motor->state();}
const MotorState& CharacterSceneSession::predicted_motor()const{return impl_->owner_motor->state();}
const GraphState& CharacterSceneSession::graph()const{return impl_->graph.state();}
const std::vector<JointMatrix>& CharacterSceneSession::pose()const{return impl_->matrices;}
std::size_t CharacterSceneSession::pending_prediction()const{return impl_->owner->pending();}
unsigned CharacterSceneSession::resynchronizations()const{return impl_->resynchronizations;}
const AbilityOwnerSnapshot* CharacterSceneSession::ability()const{auto found=impl_->client.ability_corrections().find(impl_->player);return found==impl_->client.ability_corrections().end()?nullptr:&found->second.ability;}
double CharacterSceneSession::debt()const{return impl_->clock.debt();}
}
