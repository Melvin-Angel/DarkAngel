#include <darkangel/scene_client.hpp>
#include <darkangel/ability_prediction.hpp>
#include <darkangel/character_presentation.hpp>
#include <darkangel/assembly.hpp>
#include "scene_shared.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <set>

namespace darkangel {
using scene_detail::require;
struct SceneClient::Impl {
    Transport& transport;
    WorldSession client;
    ConnectionHandle peer;
    std::unique_ptr<PhysicsWorld> prediction=std::make_unique<PhysicsWorld>(PhysicsWorld::Mode::Prediction);
    std::unique_ptr<CharacterMotor> owner_motor;
    std::shared_ptr<const CharacterSceneCombat> combat;
    // Another character this client presents: an actor or another player. What it is comes from the prepared scene
    // content by its scene identity; the pose is built once its first public frame has arrived. Kept by network identity.
    struct Remote {StableId identity;const std::vector<AttributeDefinition>* attributes{};bool hostile{};std::unique_ptr<ObservedCharacterPose> pose;};
    std::map<std::uint64_t,Remote> remotes;std::set<std::uint64_t> scenery;
    // Scene identity of every character the content can put in the session beside the party: the scene's actors and
    // each enemy an encounter can spawn.
    std::map<StableId,const CharacterSceneActor*> prepared;std::vector<StableId> party;
    std::vector<WorldInteractable> reach;std::set<StableId> interactables_seen_used;std::optional<InteractionOutcome> interaction_outcome;
    std::uint64_t next_operation{1};std::vector<InputEvent> pending_events;
    std::vector<std::shared_ptr<const ActionDefinition>> prepared_actions;std::string rig_archive;
    std::unique_ptr<OwnerPrediction> owner;
    std::unique_ptr<OwnerAbilityPrediction> ability_prediction;std::deque<MotorInput> semantic_commands;
    std::map<std::uint64_t,std::unique_ptr<ObserverAbility>> observers;
    std::map<std::uint64_t,std::unique_ptr<EffectPresentation>> effect_views;
    std::vector<EffectCueUpdate> effect_cue_updates;
    // Action cue blocks derived from the client's own view of every actor's action clock, and the host's cue player, if any.
    // The owner's predicted action as it stood this tick before authority's answer for the tick was applied: what a client on a real
    // connection presents. Loopback answers within the tick, so without this a refused press would never be seen at all.
    AssetId predicted_cue_definition;std::optional<ActionState> predicted_cue_action;
    std::unique_ptr<ActionCueTracker> cue_tracker;std::vector<ActionCueUpdate> action_cue_updates;std::vector<ActionCueCut> action_cue_cuts;CuePlayer* cue_player{};FootstepTracker footstep_tracker;std::vector<FootstepUpdate> footstep_updates;
    // Collision frames as this client received and prepared them: what its reconciliation replays against.
    CollisionHistory history;
    std::vector<CollisionMesh> prepared_geometry;
    std::shared_ptr<const AnimationGraphPlan> presentation_plan;
    AnimationGraphInstance graph;
    GraphPoseInputs graph_inputs;
    RigPose rig;
    std::vector<JointMatrix> matrices;
    std::unique_ptr<ActionPoseMixer> action_mixer;
    // Shared read-only reaction selection for the owner's confirmed pose.
    std::unique_ptr<ReactionPresentation> reactions;std::optional<ReactionSample> player_reaction;
    std::unique_ptr<DeathPresentation> player_death;std::optional<double> player_death_tick;
    const EffectFrame* effect_frame(std::uint64_t id)const{auto found=effect_views.find(id);return found!=effect_views.end()&&found->second->current()?&*found->second->current():nullptr;}
    StableId player_identity;
    std::uint64_t player{},latest{};
    bool started{};unsigned resynchronizations{};CharacterSceneProfile profile;
    // Soft lock: the candidate the live prediction chose per unconfirmed tick (reused by replay) and the last tick the owner warped.
    std::map<std::uint64_t,std::uint64_t> predicted_locks;std::uint64_t soft_lock_tick{},soft_lock_actor{};
    MotorInput predicted_command;MotionRequest predicted_request;
    // Which NPC definition the index-th enemy of an encounter is: waves in order, or the timed encounter's one.
    static AssetId enemy_definition(const EncounterDefinition& definition,unsigned index){
        if(definition.mode==EncounterMode::Timed)return definition.timed_npc;for(const auto& wave:definition.waves){if(index<wave.count)return wave.npc;index-=wave.count;}return {};
    }
    Impl(SessionHandshake hello,Transport& link,ConnectionHandle handle,StableId identity,std::span<const StableId> members,const CollisionDefinition& collision,
         std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition definition,std::string_view archive,std::shared_ptr<const CharacterSceneCombat> configured)
        :transport(link),client(SessionRole::Client,hello),peer(handle),combat(std::move(configured)),party(members.begin(),members.end()),rig_archive(archive),presentation_plan(plan),graph(std::move(plan)),rig(std::move(definition),archive),player_identity(identity){
        require((hello.protocol==2||hello.protocol==3)&&(!combat||hello.protocol==3)&&rig.definition().human,"Character scene requires wire2 and the initial human profile");require(bool(identity)&&std::find(party.begin(),party.end(),identity)!=party.end(),"Character scene object/player bounds");
        if(auto tags=graph.tag_dictionary())require(combat&&combat->tags&&combat->tags->registry()==tags->registry()&&combat->tags->generation()==tags->generation(),"Character graph/actor tag registry mismatch");
        client.attach(transport,peer);
        prediction->load_scene(collision);
        if(combat){for(const auto& ability:combat->abilities){require(ability&&ability->action->motion,"Scene action requires a frozen clip");auto clip=combat->clips.at(ability->action->motion->clip.id);require(clip&&clip->definition().skeleton==rig.definition().id&&clip->definition().signature==rig.definition().signature&&clip->archive_generation()==ability->action->motion->archive_generation,"Scene action clip generation/rig mismatch");}
            for(const auto& actor:combat->actors)prepared.emplace(actor.id,&actor);
            for(const auto& encounter:combat->encounters)for(unsigned index=0;index<encounter.definition.enemies();++index){auto found=encounter.npcs.find(enemy_definition(encounter.definition,index));if(found!=encounter.npcs.end())prepared.emplace(encounter_actor(encounter.id,index),&found->second);}
            for(const auto& item:combat->interactables)reach.emplace_back(item.definition,item.position);}
    }
    // The replicated world names the player's character: its predicted motor starts where the object stands.
    void arm(){
        const auto handle=client.world().find(player_identity);if(!client.world().valid(handle))return;const auto& object=client.world().read(handle);player=object.network.value;
        owner_motor=std::make_unique<CharacterMotor>(*prediction,MotorVec{object.transform.x,object.transform.y,object.transform.z},player);auto state=owner_motor->state();state.yaw=object.transform.yaw;owner_motor->restore(state);
        owner=std::make_unique<OwnerPrediction>(*prediction,*owner_motor);prepared_geometry=prediction->capture().meshes;
    }
    // The characters of the replicated world: one that entered is described from the prepared content, one that left
    // loses everything this client prepared for it.
    void sync(){
        if(!combat)return;
        for(const auto& [id,object]:client.objects()){if(id==player||remotes.contains(id)||scenery.contains(id))continue;
            Remote remote;remote.identity=object.id;
            if(std::find(party.begin(),party.end(),object.id)!=party.end())remote.attributes=&combat->attributes;
            else if(auto found=prepared.find(object.id);found!=prepared.end()){remote.attributes=&found->second->attributes;remote.hostile=found->second->faction!=combat->faction;}
            else{scenery.insert(id);continue;}
            remotes.emplace(id,std::move(remote));}
        for(auto entry=remotes.begin();entry!=remotes.end();){if(client.objects().contains(entry->first)){++entry;continue;}const auto id=entry->first;observers.erase(id);effect_views.erase(id);if(cue_tracker)cue_tracker->retire(id);footstep_tracker.retire(id);if(cue_player)cue_player->retire(id);entry=remotes.erase(entry);}
    }
    void receive(){client.tick();prepare();client.tick();}
    void prepare(){
        if(client.readiness(peer)!=SessionReadiness::Ready)return;
        if(!player)arm();if(!player)return;
        sync();
        if(client.collisions().empty())return;
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
        const auto tick=frame.tick;if(!history.find(tick))history.retain(std::move(frame));
        client.acknowledge_collision(tick);
    }
    bool remotes_public()const{for(const auto& [id,remote]:remotes)if(!client.public_abilities().contains(id))return false;return true;}
    bool bootstrapped()const{
        if(!player||client.readiness(peer)!=SessionReadiness::Ready||!client.collision_control_ready()||!client.motors().contains(player))return false;
        return !combat||(client.ability_corrections().contains(player)&&client.public_abilities().contains(player)&&remotes_public());
    }
    void start(){
        require(!started&&player&&client.readiness(peer)==SessionReadiness::Ready&&client.collision_control_ready()&&client.motors().contains(player),"Character scene bootstrap/control work limit");
        if(combat){require(client.ability_corrections().contains(player),"Combat prediction bootstrap work limit");ability_prediction=std::make_unique<OwnerAbilityPrediction>(client.ability_corrections().at(player).ability,combat->attributes,combat->kit,combat->input,combat->abilities,client.ability_corrections().at(player).motor.epoch,combat->tags,combat->loadouts);
            for(const auto& ability:combat->abilities)prepared_actions.push_back(ability->action);cue_tracker=std::make_unique<ActionCueTracker>(prepared_actions);
            require(client.public_abilities().contains(player),"Combat public bootstrap work limit");{auto observed=std::make_unique<ObserverAbility>(combat->attributes,combat->health,combat->maximum_health,prepared_actions,combat->tags);observed->push(shown(player));observers.emplace(player,std::move(observed));}
            if(!combat->effects.empty()){require(bool(combat->tags),"Scene effect preparation needs a tag registry");effect_views.emplace(player,std::make_unique<EffectPresentation>(AttributeVisibility::Public,AttributeSet(combat->attributes),combat->tags,combat->effects));}
            action_mixer=std::make_unique<ActionPoseMixer>(*presentation_plan,rig.definition(),prepared_actions);reactions=std::make_unique<ReactionPresentation>(rig.definition(),combat->effects,combat->clips);if(combat->death)player_death=std::make_unique<DeathPresentation>(rig.definition(),combat->death,combat->clips);
            for(auto& [id,remote]:remotes)present(id,remote);
        }
        GraphParameters initial_parameters;if(graph.tag_dictionary())initial_parameters.tags=&client.ability_corrections().at(player).ability.tags;
        graph_inputs=graph.evaluate(initial_parameters);matrices=rig.blend(graph_inputs.span());latest=prediction->tick();started=true;
    }
    const AbilityDefinition& definition(const AbilityOwnerSnapshot& snapshot)const{return scene_detail::definition(combat.get(),snapshot);}
    // The owner predicts a movement restriction for a tick from what it has confirmed: its
    // corrected Health and the instance clocks of its latest checked public effect frame.
    bool predicted_restricted(std::uint64_t tick)const{
        const auto& confirmed=client.ability_corrections().at(player).ability;for(const auto& attribute:confirmed.attributes)if(attribute.id==confirmed.health_attribute&&attribute.value<=0)return true;
        if(const auto* frame=effect_frame(player))for(const auto& active:frame->effects)if(!active.suppressed&&active.start<tick&&(!active.end||tick<active.end))for(const auto& effect:combat->effects)if(effect->id==active.definition&&effect->generation==active.generation&&effect->lock_movement)return true;
        return false;
    }
    // A character's public frame as observers take it. The server sends a public ability frame only when its content
    // changed, and never its motor state; while the index says the held content is current, the frame is brought to the
    // tick of the character's state in the motor stream (a running action's clock advances by its rate) and paired with it.
    // A far character's motor state arrives every third tick, so its newest state may be up to two ticks old.
    bool public_current(std::uint64_t id,std::uint64_t tick)const{auto listed=client.public_index().find(id);auto held=client.public_abilities().find(id);auto motor=client.motors().find(id);return listed!=client.public_index().end()&&held!=client.public_abilities().end()&&motor!=client.motors().end()&&listed->second.tick==tick&&motor->second.tick+3>tick&&(held->second.ability.tick==tick||listed->second.content==public_ability_content(held->second.ability));}
    AbilityPublicFrame shown(std::uint64_t id)const{auto frame=client.public_abilities().at(id);auto motor=client.motors().find(id);if(motor!=client.motors().end()&&motor->second.tick>=frame.ability.tick&&motor->second.epoch==frame.avatar_epoch){frame.motor=motor->second;frame.ability=public_ability_at(frame.ability,motor->second.tick);}else throw std::runtime_error("A public frame arrived without the motor state of its tick; resynchronize");return frame;}
    // Observer, effect view and pose of another character, once its first public frame has arrived.
    void present(std::uint64_t id,Remote& remote){
        require(client.public_abilities().contains(id),"Combat public bootstrap work limit");auto observed=std::make_unique<ObserverAbility>(*remote.attributes,combat->health,combat->maximum_health,prepared_actions,combat->tags);const auto first=shown(id);observed->push(first);observers.emplace(id,std::move(observed));
        if(!combat->effects.empty()){require(bool(combat->tags),"Scene effect preparation needs a tag registry");effect_views.emplace(id,std::make_unique<EffectPresentation>(AttributeVisibility::Public,AttributeSet(*remote.attributes),combat->tags,combat->effects));}
        remote.pose=std::make_unique<ObservedCharacterPose>(presentation_plan,rig.definition(),rig_archive,prepared_actions,combat->clips,combat->tags,combat->effects,combat->death);remote.pose->sample(observers.at(id)->sample(double(first.ability.tick)).frame,effect_frame(id));
    }
    const Remote* find(StableId identity)const{for(const auto& [id,remote]:remotes)if(remote.identity==identity)return &remote;return nullptr;}
    // Soft-lock candidates for the owner: every live hostile character, from its confirmed public view (motor snapshot
    // and public Health).
    std::vector<TargetCandidate> client_candidates()const{
        std::vector<TargetCandidate> result;for(const auto& [id,remote]:remotes){if(!remote.hostile)continue;auto motor=client.motors().find(id);auto held=client.public_abilities().find(id);if(motor==client.motors().end()||held==client.public_abilities().end())continue;
            const auto& ability=held->second.ability;for(const auto& attribute:ability.attributes)if(attribute.id==ability.health_attribute&&attribute.value>0)result.push_back({id,motor->second.position});}
        return result;
    }
    MotionRequest predicted_motion(const AbilityPredictionMotion& predicted,MotorInput& command)const{
        const auto& v=predicted.local.root.translation;const auto yaw=command.yaw;command.yaw=std::remainder(yaw+predicted.local.root.yaw,6.283185307179586);MotionRequest request;request.root={std::cos(yaw)*v[0]+std::sin(yaw)*v[2],v[1],-std::sin(yaw)*v[0]+std::cos(yaw)*v[2]};request.action=predicted.activation;request.lock=predicted.local.movement_lock;request.warp=predicted.warp;return request;
    }
    // Input routes through the predicted active loadout's kit; loadout-select actions are kit independent.
    const CombatKitDefinition& routing_kit()const{if(!combat->loadouts.empty()&&ability_prediction){const auto& loadout=combat->loadouts[ability_prediction->view().loadout];if(loadout)return *loadout->kit;}return *combat->kit;}
    std::optional<unsigned> loadout_request(const InputEvent& event)const{if(!combat->loadouts.empty()&&event.action)for(unsigned index=0;index<loadout_slot_count;++index)if(combat->loadout_actions[index]==event.action)return index;return std::nullopt;}
    void enqueue(std::span<const InputEvent> events){if(!combat){require(events.empty(),"Combat input requires a configured kit");return;}auto staged=pending_events;for(const auto& event:events){bool routed=loadout_request(event).has_value();for(const auto& slot:routing_kit().slots)routed|=slot.input_action&&slot.input_action==event.action;if(routed){require(staged.size()<32,"Combat input queue full; resynchronize");staged.push_back(event);}}pending_events=std::move(staged);}
    void begin(std::uint64_t tick,const CharacterSceneInput& input){
        require(started,"Character scene requires a fresh synchronized session");require(!input.crouch,"Character scene posture animation is not in the initial standing profile");require(std::isfinite(input.x)&&std::isfinite(input.z)&&std::isfinite(input.yaw)&&std::isfinite(input.pitch),"Character scene input finite");
        const auto mark=std::chrono::steady_clock::now();
        MotorInput command{tick,tick,owner_motor->state().epoch,input.x,input.z,input.yaw,input.jump,input.crouch,input.pitch};validate_motor_input(command);
        require(client.collision_control_ready()&&client.submit_motor(player,command),"Character scene control unavailable; resynchronize");
        std::vector<AbilityPredictionInput> prediction_input;
        if(combat){require(client.ability_corrections().contains(player)&&!client.ability_correction_needs_resync(),"Combat correction unavailable; resynchronize");const auto& current=client.ability_corrections().at(player).ability;auto submit=[&](CombatSlot slot,const InputEvent& event){require(next_operation!=UINT64_MAX,"Combat operation identity exhausted");AbilityIntent intent{player,next_operation++,tick,owner_motor->state().epoch,current.grant_generation,slot,event.edge,event.cancelled,false};require(client.submit_ability_intent(intent),"Combat input delivery unavailable; resynchronize");prediction_input.push_back({intent});};
            for(const auto& event:pending_events){if(auto index=loadout_request(event)){if(event.edge==InputEdge::Pressed&&!event.cancelled)submit(loadout_select_slot(*index),event);continue;}for(const auto& slot:routing_kit().slots)if(slot.input_action&&slot.input_action==event.action)submit(slot.slot,event);}pending_events.clear();}
        // An interaction is a request like any intent: it names the object, and authority resolves it in its world step.
        if(combat&&input.interact)client.submit_interaction(player,*input.interact);
        predicted_request={};predicted_command=command;
        if(combat){auto predicted=ability_prediction->advance(tick,prediction_input);predicted_cue_definition=ability_prediction->view().action_definition;predicted_cue_action=ability_prediction->view().action;if(predicted.face_movement)scene_detail::face(predicted_command,owner_motor->state().yaw,predicted.started);predicted_request=predicted_motion(predicted,predicted_command);if(predicted_restricted(tick))scene_detail::restrict(predicted_command,predicted_request);const auto candidates=client_candidates();if(auto chosen=scene_detail::lock(predicted_request,owner_motor->state(),predicted_command,candidates)){predicted_locks[tick]=*chosen;soft_lock_tick=tick;soft_lock_actor=*chosen;}}
        semantic_commands.push_back(command);require(semantic_commands.size()<=30,"Character scene semantic history overflow; resynchronize");
        owner->predict(predicted_command,predicted_request);prediction->step();owner_motor->post_physics();prediction->finish_tick();
        profile.prediction+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-mark).count();
    }
    bool public_ready(std::uint64_t tick)const{if(!public_current(player,tick))return false;for(const auto& [id,remote]:remotes)if(!public_current(id,tick))return false;return true;}
    bool ready(std::uint64_t tick)const{return client.motors().contains(player)&&client.motors().at(player).tick==tick&&!client.collisions().empty()&&client.collisions().back().tick==tick&&client.collision_control_ready()&&(!combat||(client.ability_corrections().contains(player)&&client.ability_corrections().at(player).ability.tick==tick&&public_ready(tick)));}
    void finish(std::uint64_t tick){
        auto mark=std::chrono::steady_clock::now();auto lap=[&](double& phase){const auto now=std::chrono::steady_clock::now();phase+=std::chrono::duration<double,std::micro>(now-mark).count();mark=now;};
        require(client.motors().contains(player)&&client.motors().at(player).tick==tick&&!client.collisions().empty()&&client.collisions().back().tick==tick,"Character scene snapshot delivery bound");
        if(combat){const auto& correction=client.ability_corrections().at(player);require(correction.ability.tick==tick&&correction.motor.tick==tick,"Atomic combat correction clock");if(correction.ability.active)definition(correction.ability);}
        lap(profile.replication);
        if(combat){auto candidate=*ability_prediction;for(const auto& receipt:client.drain_ability_receipts())candidate.receipt(receipt);const auto& correction=client.ability_corrections().at(player);candidate.reconcile(correction.ability);std::vector<MotorCommand> regenerated;double replay_yaw=correction.motor.yaw;
            for(const auto& replay:candidate.replay_motion()){auto found=std::find_if(semantic_commands.begin(),semantic_commands.end(),[&](const auto& value){return value.tick==replay.tick;});require(found!=semantic_commands.end(),"Combat correction missing motor input history");auto replay_input=*found;if(replay.face_movement)scene_detail::face(replay_input,replay_yaw,replay.started);auto request=predicted_motion(replay,replay_input);replay_yaw=replay_input.yaw;if(predicted_restricted(replay.tick))scene_detail::restrict(replay_input,request);if(request.warp.active){const auto candidates=client_candidates();auto locked=predicted_locks.find(replay.tick);auto chosen=std::find_if(candidates.begin(),candidates.end(),[&](const auto& other){return locked!=predicted_locks.end()&&other.id==locked->second;});if(chosen!=candidates.end())request.warp.point=chosen->position;else request.warp={};}regenerated.push_back({replay_input,request});}
            require(owner->reconcile(correction.motor,history,regenerated)==ReplayResult::Applied&&!owner->needs_resync(),"Character scene atomic combat/motor correction failed; resynchronize");*ability_prediction=std::move(candidate);
        }else require(owner->reconcile(client.motors().at(player),history)==ReplayResult::Applied&&!owner->needs_resync(),"Character scene isolated correction failed; resynchronize");
        std::erase_if(semantic_commands,[&](const auto& input){return input.tick<=client.motors().at(player).tick;});std::erase_if(predicted_locks,[&](const auto& item){return item.first<=client.motors().at(player).tick;});
        lap(profile.prediction);
        if(combat){require(public_ready(tick),"Combat public snapshot delivery bound; resynchronize");for(auto& [id,remote]:remotes)if(!remote.pose)present(id,remote);for(auto& [id,observer]:observers)observer->push(shown(id));effect_cue_updates.clear();for(auto& [id,view]:effect_views){require(!client.effect_frame_needs_resync(id,AttributeVisibility::Public),"Scene effect state requires resynchronization");auto found=client.effect_frames(AttributeVisibility::Public).find(id);if(found!=client.effect_frames(AttributeVisibility::Public).end())view->push(found->second);auto cues=view->drain_cues();effect_cue_updates.insert(effect_cue_updates.end(),std::make_move_iterator(cues.begin()),std::make_move_iterator(cues.end()));require(effect_cue_updates.size()<=1024,"Scene persistent cue work bound");}
            // Action cues. The owner's action is seen twice, confirmed and predicted; every other character's once, from its public frame at this tick.
            {const auto& correction=client.ability_corrections().at(player);cue_tracker->observe(player,correction.motor.epoch,CueTiming::OwnerConfirmed,correction.ability.action_definition,correction.ability.action);cue_tracker->observe(player,correction.motor.epoch,CueTiming::OwnerPredicted,predicted_cue_definition,predicted_cue_action);
                for(const auto& [id,remote]:remotes)if(remote.pose){auto frame=shown(id);if(frame.ability.tick<tick)frame.ability=public_ability_at(frame.ability,tick);cue_tracker->observe(id,frame.avatar_epoch,CueTiming::Observed,frame.ability.action_definition,frame.ability.action);footstep_tracker.observe(id,frame.avatar_epoch,frame.motor.position.x,frame.motor.position.z,frame.motor.grounded);}
                // The owner hears its own steps from its predicted motor.
                {const auto& walked=owner_motor->state();footstep_tracker.observe(player,correction.motor.epoch,walked.position.x,walked.position.z,walked.grounded);}footstep_updates=footstep_tracker.drain();
                action_cue_updates=cue_tracker->drain_cues();action_cue_cuts=cue_tracker->drain_cuts();}
            if(cue_player){for(const auto& update:effect_cue_updates)cue_player->effect(update,tick);for(const auto& update:action_cue_updates)cue_player->action(update.network,update.avatar_epoch,update.event,tick,update.timing);for(const auto& cut:action_cue_cuts)cue_player->rejected(cut.network,cut.activation,tick);for(const auto& step:footstep_updates)cue_player->moment(step.network,step.avatar_epoch,footstep_cue_key,step.serial,tick);cue_player->advance(tick);}}
        // A far character's newest state can be a tick or two old; its pose still advances every tick, holding that state
        // while a running action's clock advances (the index has confirmed its content for this tick).
        for(auto& [id,remote]:remotes)if(remote.pose){auto frame=observers.at(id)->sample(double(tick)).frame;if(frame.ability.tick<tick){frame.ability=public_ability_at(frame.ability,tick);frame.motor.tick=tick;}remote.pose->sample(frame,effect_frame(id));}
        // The owner's own pose follows its confirmed motor state for the tick.
        const auto& state=client.motors().at(player);double x=state.achieved.x*60-state.support_velocity.x,z=state.achieved.z*60-state.support_velocity.z;
        GraphParameters parameters;parameters.speed=float(std::hypot(x,z));parameters.forward=float(std::sin(state.yaw)*x+std::cos(state.yaw)*z);parameters.lateral=float(std::cos(state.yaw)*x-std::sin(state.yaw)*z);
        if(graph.tag_dictionary())parameters.tags=&client.ability_corrections().at(player).ability.tags;
        auto selected=graph.evaluate(parameters);double stride{};
        for(const auto& layer:selected.span()){const auto& clip=layer.clip->definition();const auto& end=clip.root.back();stride+=layer.weight*std::hypot(end[0],end[2])*60/clip.ticks;}
        if(parameters.speed>.01&&stride>.01)parameters.playback_rate=float(std::clamp(parameters.speed/stride,0.,4.));
        graph_inputs=graph.advance(tick,parameters);
        if(combat){require(client.ability_corrections().contains(player)&&client.ability_corrections().at(player).ability.tick==tick,"Combat correction delivery bound");const auto& correction=client.ability_corrections().at(player);require(correction.motor.tick==tick,"Combat motor/action atomic clock");bool alive=true;for(const auto& attribute:correction.ability.attributes)if(attribute.id==correction.ability.health_attribute)alive=attribute.value>0;
            // Confirmed owner state and the checked public effect instances choose the reaction; gameplay interruption already arrived through the correction.
            player_reaction=reactions->select(effect_frame(player),player,correction.session_epoch,correction.motor.epoch,tick,alive,bool(correction.ability.active));
            player_death_tick=player_death?player_death->update(alive,tick,correction.motor.epoch):std::nullopt;
            if(player_death_tick)matrices=rig.sample(player_death->clip(),*player_death_tick);
            else if(correction.ability.active){const auto& ability=definition(correction.ability);matrices=action_mixer->sample(rig,graph_inputs,*ability.action,*combat->clips.at(ability.action->motion->clip.id),double(correction.ability.action->clock%ability.action->duration)/action_tick_units);}else if(player_reaction&&player_reaction->suppressed==ReactionSuppression::None)matrices=rig.sample(*player_reaction->clip,player_reaction->tick);else matrices=rig.blend(graph_inputs.span());client.acknowledge_ability_correction(player);lap(profile.presentation);client.drain_ability_receipts();
        }else{matrices=rig.blend(graph_inputs.span());lap(profile.presentation);}
        latest=tick;++profile.ticks;
    }
    // Authority's answers to the owner's interaction requests; an accepted single-use interactable is no longer offered.
    void settle(std::uint64_t tick){
        if(!combat)return;
        for(const auto& notice:client.drain_interaction_receipts()){
            // Not an answer: another member spent a single-use interactable, or it was spent before this session.
            if(static_cast<InteractionResult>(notice.result)==InteractionResult::UsedUp){interactables_seen_used.insert(notice.object);continue;}
            interaction_outcome=InteractionOutcome{notice.object,static_cast<InteractionResult>(notice.result),tick};if(interaction_outcome->result==InteractionResult::Accepted)for(const auto& item:combat->interactables)if(item.id==notice.object&&item.definition.once)interactables_seen_used.insert(notice.object);}
    }
};
SceneClient::SceneClient(SessionHandshake hello,Transport& transport,ConnectionHandle peer,StableId player,std::span<const StableId> party,const CollisionDefinition& collision,std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition rig,std::string_view archive,std::shared_ptr<const CharacterSceneCombat> combat)
    :impl_(std::make_unique<Impl>(std::move(hello),transport,peer,player,party,collision,std::move(plan),std::move(rig),archive,std::move(combat))){}
SceneClient::~SceneClient()=default;
void SceneClient::receive(){impl_->receive();}
bool SceneClient::bootstrapped()const{return impl_->bootstrapped();}
void SceneClient::start(){impl_->start();}
void SceneClient::enqueue(std::span<const InputEvent> events){impl_->enqueue(events);}
void SceneClient::begin(std::uint64_t tick,const CharacterSceneInput& input){impl_->begin(tick,input);}
bool SceneClient::ready(std::uint64_t tick){return impl_->ready(tick);}
void SceneClient::finish(std::uint64_t tick){impl_->finish(tick);}
void SceneClient::flush(){impl_->client.tick();}
void SceneClient::settle(std::uint64_t tick){impl_->settle(tick);}
StableId SceneClient::player()const{return impl_->player_identity;}
std::uint64_t SceneClient::network()const{return impl_->player;}
bool SceneClient::knows(std::uint64_t network)const{return network&&(network==impl_->player||impl_->remotes.contains(network));}
std::size_t SceneClient::characters()const{return impl_->remotes.size()+(impl_->player?1:0);}
const World& SceneClient::presentation()const{return impl_->client.world();}
const MotorState& SceneClient::predicted_motor()const{return impl_->owner_motor->state();}
const GraphState& SceneClient::graph()const{return impl_->graph.state();}
const GraphPoseInputs& SceneClient::graph_inputs()const{return impl_->graph_inputs;}
const std::vector<JointMatrix>& SceneClient::pose()const{return impl_->matrices;}
const std::vector<JointMatrix>* SceneClient::pose(StableId actor)const{if(actor==impl_->player_identity)return &impl_->matrices;if(const auto* found=impl_->find(actor))if(found->pose)return &found->pose->matrices();return nullptr;}
const GraphPoseInputs* SceneClient::graph_inputs(StableId actor)const{if(actor==impl_->player_identity)return &impl_->graph_inputs;if(const auto* found=impl_->find(actor))if(found->pose)return &found->pose->inputs();return nullptr;}
std::size_t SceneClient::pending_prediction()const{return impl_->owner->pending();}
unsigned SceneClient::resynchronizations()const{return impl_->resynchronizations;}
const AbilityOwnerSnapshot* SceneClient::ability()const{auto found=impl_->client.ability_corrections().find(impl_->player);return found==impl_->client.ability_corrections().end()?nullptr:&found->second.ability;}
const AbilityOwnerSnapshot* SceneClient::predicted_ability()const{return impl_->ability_prediction?&impl_->ability_prediction->view():nullptr;}
std::size_t SceneClient::pending_abilities()const{return impl_->ability_prediction?impl_->ability_prediction->pending():0;}
MotorVec SceneClient::prediction_visual_offset()const{return impl_->owner->visual_offset();}
ObserverAbilitySample SceneClient::observer(StableId identity,double render_tick)const{auto& p=*impl_;auto handle=p.client.world().find(identity);require(p.client.world().valid(handle),"Observer character lifecycle missing");auto id=p.client.world().read(handle).network.value;require(p.observers.contains(id)&&!p.client.public_ability_needs_resync(id),"Observer character state requires resynchronization");return p.observers.at(id)->sample(render_tick);}
const EffectPresentation* SceneClient::effects(StableId actor)const{for(const auto& [id,view]:impl_->effect_views){auto found=impl_->client.objects().find(id);if(found!=impl_->client.objects().end()&&found->second.id==actor)return view.get();}return nullptr;}
std::span<const EffectCueUpdate> SceneClient::effect_cues()const{return impl_->effect_cue_updates;}
std::span<const ActionCueUpdate> SceneClient::action_cues()const{return impl_->action_cue_updates;}
std::span<const ActionCueCut> SceneClient::action_cue_cuts()const{return impl_->action_cue_cuts;}
std::span<const FootstepUpdate> SceneClient::footsteps()const{return impl_->footstep_updates;}
void SceneClient::present_cues(CuePlayer* player){impl_->cue_player=player;}
std::optional<StableId> SceneClient::identity(std::uint64_t network)const{const auto& p=*impl_;if(network&&network==p.player)return p.player_identity;auto found=p.remotes.find(network);if(found!=p.remotes.end())return found->second.identity;return std::nullopt;}
const ReactionSample* SceneClient::reaction(StableId actor)const{const auto& p=*impl_;if(actor==p.player_identity)return p.player_reaction?&*p.player_reaction:nullptr;if(const auto* found=p.find(actor))if(found->pose&&found->pose->reaction())return &*found->pose->reaction();return nullptr;}
std::optional<double> SceneClient::death(StableId actor)const{const auto& p=*impl_;if(actor==p.player_identity)return p.player_death_tick;if(const auto* found=p.find(actor))if(found->pose)return found->pose->death();return std::nullopt;}
unsigned SceneClient::loadout()const{auto found=impl_->client.ability_corrections().find(impl_->player);return found==impl_->client.ability_corrections().end()?0:found->second.ability.loadout;}
unsigned SceneClient::predicted_loadout()const{return impl_->ability_prediction?impl_->ability_prediction->view().loadout:0;}
std::optional<StableId> SceneClient::soft_lock()const{const auto& p=*impl_;if(p.combat&&p.soft_lock_tick&&p.soft_lock_tick==p.latest){auto found=p.remotes.find(p.soft_lock_actor);if(found!=p.remotes.end())return found->second.identity;}return std::nullopt;}
const std::vector<ProjectileView>& SceneClient::projectiles()const{return impl_->client.projectiles();}
const CharacterSceneProfile& SceneClient::profile()const{return impl_->profile;}
AimSolution SceneClient::aim(MotorVec camera,MotorVec direction,double range,MotorVec muzzle)const{
    const auto& p=*impl_;const double length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
    require(std::isfinite(camera.x)&&std::isfinite(camera.y)&&std::isfinite(camera.z)&&std::isfinite(length)&&length>1e-6&&std::isfinite(range)&&range>0&&range<=200&&std::isfinite(muzzle.x)&&std::isfinite(muzzle.y)&&std::isfinite(muzzle.z),"Aim ray bounds");
    const MotorVec ray{direction.x/length,direction.y/length,direction.z/length};const auto& self=p.owner_motor->state();
    // Start level with the owner along the ray: the ground or a wall between the camera and the owner is not what the crosshair is on.
    const double skip=std::max(0.,(self.position.x-camera.x)*ray.x+(self.position.y+muzzle.y-camera.y)*ray.y+(self.position.z-camera.z)*ray.z);
    const MotorVec start{camera.x+ray.x*skip,camera.y+ray.y*skip,camera.z+ray.z*skip};double nearest=range;bool hit=false;
    const auto blockers=p.prediction->query_ray(start,{ray.x*range,ray.y*range,ray.z*range},{StaticCollision|KinematicCollision|DynamicCollision,p.player});
    for(const auto& blocker:blockers.hits){const double along=(blocker.point.x-start.x)*ray.x+(blocker.point.y-start.y)*ray.y+(blocker.point.z-start.z)*ray.z;if(along>=0&&along<nearest){nearest=along;hit=true;}}
    // Other characters as upright capsules at their confirmed public positions.
    constexpr double radius=.35,height=1.8;
    for(const auto& [id,remote]:p.remotes){auto motor=p.client.motors().find(id);if(motor==p.client.motors().end())continue;const auto& foot=motor->second.position;
        // Closest approach between the ray and the capsule's axis segment.
        const MotorVec base{foot.x,foot.y+radius,foot.z};const double axis=height-2*radius;const MotorVec w{start.x-base.x,start.y-base.y,start.z-base.z};
        const double b=ray.y,d=ray.x*w.x+ray.y*w.y+ray.z*w.z,e=w.y,denominator=1-b*b;double along=denominator>1e-9?(b*e-d)/denominator:0,up=denominator>1e-9?(e-b*d)/denominator:e;
        up=std::clamp(up,0.,axis);along=std::max(0.,up*b-d);up=std::clamp(e+along*b,0.,axis);
        const double dx=start.x+ray.x*along-base.x,dy=start.y+ray.y*along-(base.y+up),dz=start.z+ray.z*along-base.z,gap=dx*dx+dy*dy+dz*dz;
        if(gap>radius*radius)continue;const double surface=std::max(0.,along-std::sqrt(radius*radius-gap));if(surface<nearest){nearest=surface;hit=true;}}
    AimSolution solution;solution.hit=hit;solution.point={start.x+ray.x*nearest,start.y+ray.y*nearest,start.z+ray.z*nearest};
    // Angles from the muzzle, which itself turns with the yaw: one refinement from the owner's position is enough.
    solution.yaw=std::atan2(ray.x,ray.z);solution.pitch=std::clamp(std::asin(std::clamp(ray.y,-1.,1.)),-1.4,1.4);
    double yaw=std::atan2(solution.point.x-self.position.x,solution.point.z-self.position.z);
    for(unsigned pass=0;pass<2;++pass){const double c=std::cos(yaw),s=std::sin(yaw);const MotorVec origin{self.position.x+c*muzzle.x+s*muzzle.z,self.position.y+muzzle.y,self.position.z-s*muzzle.x+c*muzzle.z};
        const double dx=solution.point.x-origin.x,dy=solution.point.y-origin.y,dz=solution.point.z-origin.z,level=std::hypot(dx,dz);
        // A point at or behind the muzzle gives no usable angle; keep the camera's own direction.
        if(level<.5||dx*ray.x+dz*ray.z<=0)return solution;
        yaw=std::atan2(dx,dz);if(pass==1){solution.yaw=yaw;solution.pitch=std::clamp(std::atan2(dy,level),-1.4,1.4);}}
    return solution;
}
std::optional<InteractionPrompt> SceneClient::interaction_prompt()const{
    const auto& p=*impl_;if(!p.combat)return std::nullopt;const auto& self=p.owner_motor->state().position;std::optional<InteractionPrompt> best;double nearest{};
    for(std::size_t index=0;index<p.reach.size();++index){const auto& item=p.combat->interactables[index];if(p.interactables_seen_used.contains(item.id)||!p.reach[index].reaches(self))continue;const double dx=self.x-item.position.x,dy=self.y-item.position.y,dz=self.z-item.position.z,distance=dx*dx+dy*dy+dz*dz;if(!best||distance<nearest){best=InteractionPrompt{item.id,item.definition.prompt};nearest=distance;}}
    // A scene transition used on purpose is offered the same way.
    for(const auto& item:p.combat->transitions){if(item.definition.activation!=TransitionActivation::Interact)continue;const double dx=self.x-item.center.x,dy=self.y-item.center.y,dz=self.z-item.center.z,distance=dx*dx+dy*dy+dz*dz;if(distance>item.definition.radius*item.definition.radius)continue;if(!best||distance<nearest){best=InteractionPrompt{item.id,item.definition.prompt};nearest=distance;}}
    return best;
}
const InteractionOutcome* SceneClient::last_interaction()const{return impl_->interaction_outcome?&*impl_->interaction_outcome:nullptr;}
}
