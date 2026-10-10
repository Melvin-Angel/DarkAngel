#include <darkangel/character_scene.hpp>
#include <darkangel/ability_prediction.hpp>
#include <darkangel/character_presentation.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iterator>

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
    std::unique_ptr<OwnerAbilityPrediction> ability_prediction;std::deque<MotorInput> semantic_commands;
    std::map<std::uint64_t,std::unique_ptr<ObserverAbility>> observers;
    std::map<std::uint64_t,std::unique_ptr<EffectPresentation>> effect_views;
    std::vector<AbilityCommitUpdate> commitment_updates;
    std::vector<EffectCueUpdate> effect_cue_updates;
    CollisionHistory history;
    std::vector<CollisionMesh> prepared_geometry;
    std::shared_ptr<const AnimationGraphPlan> presentation_plan;
    AnimationGraphInstance graph;
    GraphPoseInputs graph_inputs;
    RigPose rig;
    std::vector<JointMatrix> matrices;
    std::unique_ptr<ObservedCharacterPose> target_pose;std::unique_ptr<ActionPoseMixer> action_mixer;
    StableId player_identity;
    SimulationClock clock;
    std::uint64_t player{};
    bool pending_jump{},failed{};unsigned resynchronizations{};
    Impl(SessionHandshake hello,std::span<const ObjectData> objects,StableId identity,const CollisionDefinition& collision,
         std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition definition,std::string_view archive,std::optional<CharacterSceneCombat> configured)
        :transport(create_loopback(hello)),server(SessionRole::Server,hello),client(SessionRole::Client,hello),presentation_plan(plan),graph(std::move(plan)),rig(std::move(definition),archive){
        player_identity=identity;combat=std::move(configured);require((hello.protocol==2||hello.protocol==3)&&(!combat||hello.protocol==3)&&rig.definition().human,"Character scene requires wire2 and the initial human profile");require(!objects.empty()&&objects.size()<=128&&identity,"Character scene object/player bounds");
        if(auto tags=graph.tag_dictionary())require(combat&&combat->tags&&combat->tags->registry()==tags->registry()&&combat->tags->generation()==tags->generation(),"Character graph/actor tag registry mismatch");
        host_peer=transport.host->open(hello);client_peer=transport.client->open(hello);
        server.attach(*transport.host,host_peer);client.attach(*transport.client,client_peer);
        for(const auto& box:collision.boxes)require(!box.moving&&!box.dynamic&&!box.sensor,"Character scene initial profile requires static collision");
        MotorVec foot,target_foot;double yaw{},target_yaw{};
        for(const auto& object:objects){auto id=server.create(object);if(object.id==identity){require(!player,"Duplicate character scene player");require(object.transform.scale==1&&object.transform.pitch==0&&object.transform.roll==0&&object.health.current>0,"Character scene requires an upright, unscaled live player");player=id;foot={object.transform.x,object.transform.y,object.transform.z};yaw=object.transform.yaw;}if(combat&&object.id==combat->target){require(object.id!=identity&&!target&&object.transform.scale==1&&object.health.current>0,"Combat target profile");target=id;target_foot={object.transform.x,object.transform.y,object.transform.z};target_yaw=object.transform.yaw;}}
        require(player,"Character scene player missing");authoritative.load_scene(collision);prediction->load_scene(collision);
        host_motor=std::make_unique<CharacterMotor>(authoritative,foot,player);owner_motor=std::make_unique<CharacterMotor>(*prediction,foot,player);
        if(combat){require(combat->kit&&target&&combat->evaluator&&(bool(combat->damage)||bool(combat->combat_damage)),"Combat scene requires frozen kit/target/game evaluator");validate_combat_kit(*combat->kit,combat->input);for(const auto& ability:combat->abilities){require(ability&&ability->action->motion,"Scene action requires a frozen clip");auto clip=combat->clips.at(ability->action->motion->clip.id);require(clip&&clip->definition().skeleton==rig.definition().id&&clip->definition().signature==rig.definition().signature&&clip->archive_generation()==ability->action->motion->archive_generation,"Scene action clip generation/rig mismatch");}
            target_motor=std::make_unique<CharacterMotor>(authoritative,target_foot,target);auto state=target_motor->state();state.yaw=target_yaw;target_motor->restore(state);server.bind_melee_query(std::make_shared<PhysicsMeleeQuery>(authoritative));if(combat->combat_damage)server.register_combat_evaluator(combat->evaluator,combat->combat_damage);else server.register_damage_evaluator(combat->evaluator,combat->damage);for(const auto& [id,evaluator]:combat->effect_evaluators)server.register_effect_evaluator(id,evaluator);ability_owner=server.configure_abilities(player,combat->attributes,combat->health,combat->maximum_health);auto target_owner=server.configure_abilities(target,combat->target_attributes.empty()?combat->attributes:combat->target_attributes,combat->health,combat->maximum_health);if(combat->tags){server.configure_ability_tags(ability_owner,combat->tags);server.configure_ability_tags(target_owner,combat->tags);}server.equip_combat_kit(ability_owner,combat->kit,combat->input,combat->abilities);server.publish_motor(target,target_motor->state());server.drain_ability_actions();}
        auto host_state=host_motor->state();host_state.topology=authoritative.topology();host_state.yaw=yaw;host_motor->restore(host_state);auto owner_state=owner_motor->state();owner_state.yaw=yaw;owner_motor->restore(owner_state);
        owner=std::make_unique<OwnerPrediction>(*prediction,*owner_motor);prepared_geometry=prediction->capture().meshes;
        server.own_motor(player,host_peer);server.publish_motor(player,host_motor->state());
        auto initial=authoritative.capture();history.retain(initial);server.publish_collision(collision_stream(initial));
        for(unsigned work=0;work<32&&(!client.motors().contains(player)||!client.collision_control_ready()||(combat&&(!client.ability_corrections().contains(player)||!client.public_abilities().contains(player)||!client.public_abilities().contains(target))));++work)pump();
        require(client.readiness(client_peer)==SessionReadiness::Ready&&client.collision_control_ready()&&client.motors().contains(player),"Character scene bootstrap/control work limit");
        if(combat){require(client.ability_corrections().contains(player),"Combat prediction bootstrap work limit");ability_prediction=std::make_unique<OwnerAbilityPrediction>(client.ability_corrections().at(player).ability,combat->attributes,combat->kit,combat->input,combat->abilities,client.ability_corrections().at(player).motor.epoch,combat->tags);}
        if(combat){std::vector<std::shared_ptr<const ActionDefinition>> actions;for(const auto& ability:combat->abilities)actions.push_back(ability->action);for(auto id:{player,target}){require(client.public_abilities().contains(id),"Combat public bootstrap work limit");auto prepared=std::make_unique<ObserverAbility>(id==player||combat->target_attributes.empty()?combat->attributes:combat->target_attributes,combat->health,combat->maximum_health,actions,combat->tags);prepared->push(client.public_abilities().at(id));observers.emplace(id,std::move(prepared));if(!combat->effects.empty()){require(bool(combat->tags),"Scene effect preparation needs a tag registry");effect_views.emplace(id,std::make_unique<EffectPresentation>(AttributeVisibility::Public,AttributeSet(id==player||combat->target_attributes.empty()?combat->attributes:combat->target_attributes),combat->tags,combat->effects));}}
            action_mixer=std::make_unique<ActionPoseMixer>(*presentation_plan,rig.definition(),actions);target_pose=std::make_unique<ObservedCharacterPose>(presentation_plan,rig.definition(),archive,actions,combat->clips,combat->tags);target_pose->sample(observers.at(target)->sample(0).frame);
        }
        GraphParameters initial_parameters;if(graph.tag_dictionary())initial_parameters.tags=&client.ability_corrections().at(player).ability.tags;
        graph_inputs=graph.evaluate(initial_parameters);matrices=rig.blend(graph_inputs.span());
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
    MotionRequest predicted_motion(const AbilityPredictionMotion& predicted,MotorInput& command)const{
        const auto& v=predicted.local.root.translation;const auto yaw=command.yaw;command.yaw=std::remainder(yaw+predicted.local.root.yaw,6.283185307179586);MotionRequest request;request.root={std::cos(yaw)*v[0]+std::sin(yaw)*v[2],v[1],-std::sin(yaw)*v[0]+std::cos(yaw)*v[2]};request.action=predicted.activation;request.lock=predicted.local.movement_lock;return request;
    }
    void enqueue(std::span<const InputEvent> events){if(!combat){require(events.empty(),"Combat input requires a configured kit");return;}auto staged=pending_events;for(const auto& event:events)for(const auto& slot:combat->kit->slots)if(slot.input_action==event.action){require(staged.size()<32,"Combat input queue full; resynchronize");staged.push_back(event);}pending_events=std::move(staged);}
    void step(CharacterSceneInput input){
        require(!failed,"Character scene requires a fresh synchronized session");require(!input.crouch,"Character scene posture animation is not in the initial standing profile");require(std::isfinite(input.x)&&std::isfinite(input.z)&&std::isfinite(input.yaw),"Character scene input finite");
        const auto tick=authoritative.tick()+1;MotorInput command{tick,tick,owner_motor->state().epoch,input.x,input.z,input.yaw,input.jump,input.crouch};validate_motor_input(command);
        require(client.collision_control_ready()&&client.submit_motor(player,command),"Character scene control unavailable; resynchronize");
        std::vector<AbilityPredictionInput> prediction_input;
        if(combat){require(client.ability_corrections().contains(player)&&!client.ability_correction_needs_resync(),"Combat correction unavailable; resynchronize");const auto& current=client.ability_corrections().at(player).ability;for(const auto& event:pending_events)for(const auto& slot:combat->kit->slots)if(slot.input_action==event.action){require(next_operation!=UINT64_MAX,"Combat operation identity exhausted");AbilityIntent intent{player,next_operation++,tick,owner_motor->state().epoch,current.grant_generation,slot.slot,event.edge,event.cancelled,false};require(client.submit_ability_intent(intent),"Combat input delivery unavailable; resynchronize");prediction_input.push_back({intent});}pending_events.clear();}
        MotionRequest authoritative_motion,predicted_request;auto predicted_command=command;
        if(combat)predicted_request=predicted_motion(ability_prediction->advance(tick,prediction_input),predicted_command);
        semantic_commands.push_back(command);require(semantic_commands.size()<=30,"Character scene semantic history overflow; resynchronize");
        server.tick();require(server.motor_input_ready(player,tick),"Character scene owner command unavailable");
        auto accepted=server.consume_motor(player,tick,command.epoch);
        if(combat){auto before=server.ability_snapshot(ability_owner);server.advance_abilities(tick);auto after=server.ability_snapshot(ability_owner);auto updates=server.drain_ability_actions();if(after.active){auto from=before.active&&before.active==after.active?before.action->clock:0;authoritative_motion=motion(after,from,after.action->clock,accepted);}else if(before.active)for(const auto& update:updates)if(update.handle==*before.active&&update.phase==ActionPhase::Completed){auto total=std::uint64_t(definition(before).action->duration)*definition(before).action->loops;authoritative_motion=motion(before,before.action->clock,total,accepted);}}
        owner->predict(predicted_command,predicted_request);prediction->step();owner_motor->post_physics();prediction->finish_tick();
        host_motor->step(accepted,authoritative_motion);if(target_motor)target_motor->step({tick,tick,target_motor->state().epoch,0,0,target_motor->state().yaw});authoritative.step();host_motor->post_physics();if(target_motor)target_motor->post_physics();authoritative.finish_tick();
        auto frame=authoritative.capture();history.retain(frame);server.publish_motor(player,host_motor->state());if(target_motor){server.publish_motor(target,target_motor->state());server.resolve_ability_hits(tick);commitment_updates=server.drain_ability_commitments();server.drain_effect_outcomes();}server.publish_collision(collision_stream(frame));pump();
        auto public_ready=[&]{return client.public_abilities().contains(player)&&client.public_abilities().contains(target)&&client.public_abilities().at(player).ability.tick==tick&&client.public_abilities().at(target).ability.tick==tick;};
        auto snapshots_ready=[&]{return client.motors().contains(player)&&client.motors().at(player).tick==tick&&!client.collisions().empty()&&client.collisions().back().tick==tick&&client.collision_control_ready()&&(!combat||(client.ability_corrections().contains(player)&&client.ability_corrections().at(player).ability.tick==tick&&public_ready()));};
        for(unsigned work=0;work<32&&!snapshots_ready();++work)pump();
        require(client.motors().contains(player)&&client.motors().at(player).tick==tick&&!client.collisions().empty()&&client.collisions().back().tick==tick,"Character scene snapshot delivery bound");
        if(combat){const auto& correction=client.ability_corrections().at(player);require(correction.ability.tick==tick&&correction.motor.tick==tick,"Atomic combat correction clock");if(correction.ability.active)definition(correction.ability);}
        if(combat){auto prepared=*ability_prediction;for(const auto& receipt:client.drain_ability_receipts())prepared.receipt(receipt);const auto& correction=client.ability_corrections().at(player);prepared.reconcile(correction.ability);std::vector<MotorCommand> regenerated;
            for(const auto& replay:prepared.replay_motion()){auto found=std::find_if(semantic_commands.begin(),semantic_commands.end(),[&](const auto& value){return value.tick==replay.tick;});require(found!=semantic_commands.end(),"Combat correction missing motor input history");auto replay_input=*found;auto request=predicted_motion(replay,replay_input);regenerated.push_back({replay_input,request});}
            require(owner->reconcile(correction.motor,history,regenerated)==ReplayResult::Applied&&!owner->needs_resync(),"Character scene atomic combat/motor correction failed; resynchronize");*ability_prediction=std::move(prepared);
        }else require(owner->reconcile(client.motors().at(player),history)==ReplayResult::Applied&&!owner->needs_resync(),"Character scene isolated correction failed; resynchronize");
        std::erase_if(semantic_commands,[&](const auto& input){return input.tick<=client.motors().at(player).tick;});
        if(combat){require(public_ready(),"Combat public snapshot delivery bound; resynchronize");for(auto& [id,observer]:observers)observer->push(client.public_abilities().at(id));effect_cue_updates.clear();for(auto& [id,view]:effect_views){require(!client.effect_frame_needs_resync(id,AttributeVisibility::Public),"Scene effect state requires resynchronization");auto found=client.effect_frames(AttributeVisibility::Public).find(id);if(found!=client.effect_frames(AttributeVisibility::Public).end())view->push(found->second);auto cues=view->drain_cues();effect_cue_updates.insert(effect_cue_updates.end(),std::make_move_iterator(cues.begin()),std::make_move_iterator(cues.end()));require(effect_cue_updates.size()<=1024,"Scene persistent cue work bound");}}
        if(target_pose)target_pose->sample(observers.at(target)->sample(double(tick)).frame);
        const auto& state=host_motor->state();double x=state.achieved.x*60-state.support_velocity.x,z=state.achieved.z*60-state.support_velocity.z;
        GraphParameters parameters;parameters.speed=float(std::hypot(x,z));parameters.forward=float(std::sin(state.yaw)*x+std::cos(state.yaw)*z);parameters.lateral=float(std::cos(state.yaw)*x-std::sin(state.yaw)*z);
        if(graph.tag_dictionary())parameters.tags=&client.ability_corrections().at(player).ability.tags;
        auto selected=graph.evaluate(parameters);double stride{};
        for(const auto& layer:selected.span()){const auto& clip=layer.clip->definition();const auto& end=clip.root.back();stride+=layer.weight*std::hypot(end[0],end[2])*60/clip.ticks;}
        if(parameters.speed>.01&&stride>.01)parameters.playback_rate=float(std::clamp(parameters.speed/stride,0.,4.));
        graph_inputs=graph.advance(tick,parameters);
        if(combat){require(client.ability_corrections().contains(player)&&client.ability_corrections().at(player).ability.tick==tick,"Combat correction delivery bound");const auto& correction=client.ability_corrections().at(player);require(correction.motor.tick==tick,"Combat motor/action atomic clock");if(correction.ability.active){const auto& ability=definition(correction.ability);matrices=action_mixer->sample(rig,graph_inputs,*ability.action,*combat->clips.at(ability.action->motion->clip.id),double(correction.ability.action->clock%ability.action->duration)/action_tick_units);}else matrices=rig.blend(graph_inputs.span());client.acknowledge_ability_correction(player);client.drain_ability_receipts();client.tick();server.tick();}else matrices=rig.blend(graph_inputs.span());

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
const GraphPoseInputs& CharacterSceneSession::graph_inputs()const{return impl_->graph_inputs;}
const std::vector<JointMatrix>& CharacterSceneSession::pose()const{return impl_->matrices;}
const std::vector<JointMatrix>* CharacterSceneSession::pose(StableId actor)const{if(actor==impl_->player_identity)return &impl_->matrices;if(impl_->combat&&actor==impl_->combat->target&&impl_->target_pose)return &impl_->target_pose->matrices();return nullptr;}
const GraphPoseInputs* CharacterSceneSession::graph_inputs(StableId actor)const{if(actor==impl_->player_identity)return &impl_->graph_inputs;if(impl_->combat&&actor==impl_->combat->target&&impl_->target_pose)return &impl_->target_pose->inputs();return nullptr;}
std::size_t CharacterSceneSession::pending_prediction()const{return impl_->owner->pending();}
unsigned CharacterSceneSession::resynchronizations()const{return impl_->resynchronizations;}
const AbilityOwnerSnapshot* CharacterSceneSession::ability()const{auto found=impl_->client.ability_corrections().find(impl_->player);return found==impl_->client.ability_corrections().end()?nullptr:&found->second.ability;}
const AbilityOwnerSnapshot* CharacterSceneSession::predicted_ability()const{return impl_->ability_prediction?&impl_->ability_prediction->view():nullptr;}
std::size_t CharacterSceneSession::pending_abilities()const{return impl_->ability_prediction?impl_->ability_prediction->pending():0;}
MotorVec CharacterSceneSession::prediction_visual_offset()const{return impl_->owner->visual_offset();}
ObserverAbilitySample CharacterSceneSession::observer(StableId identity,double render_tick)const{auto& p=*impl_;auto handle=p.client.world().find(identity);require(p.client.world().valid(handle),"Observer character lifecycle missing");auto id=p.client.world().read(handle).network.value;require(p.observers.contains(id)&&!p.client.public_ability_needs_resync(id),"Observer character state requires resynchronization");return p.observers.at(id)->sample(render_tick);}
double CharacterSceneSession::debt()const{return impl_->clock.debt();}
}

namespace darkangel {
std::span<const AbilityCommitUpdate> CharacterSceneSession::commitments()const{return impl_->commitment_updates;}
std::span<const EffectCueUpdate> CharacterSceneSession::effect_cues()const{return impl_->effect_cue_updates;}
const EffectPresentation* CharacterSceneSession::effects(StableId actor)const{for(const auto& [id,view]:impl_->effect_views)if(impl_->client.objects().at(id).id==actor)return view.get();return nullptr;}
}
