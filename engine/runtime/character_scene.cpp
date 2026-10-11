#include <darkangel/character_scene.hpp>
#include <darkangel/ability_prediction.hpp>
#include <darkangel/character_presentation.hpp>
#include <darkangel/targeting.hpp>
#include <darkangel/assembly.hpp>
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
    std::unique_ptr<CharacterMotor> host_motor,owner_motor;
    // A server-driven actor: its scene configuration, authoritative motor and ability owner, the latest controller
    // press and chosen target, and the pose observed from its public state. Kept in ascending network identity.
    struct Actor {StableId identity;std::uint64_t id{};const CharacterSceneActor* config{};MotorVec foot{};double yaw{};std::uint32_t faction{};std::unique_ptr<CharacterMotor> motor;AbilityOwnerHandle owner;std::uint64_t operation{1};std::optional<AbilityReceipt> receipt;std::optional<StableId> chosen,locked;std::unique_ptr<ObservedCharacterPose> pose;std::unique_ptr<Perception> perception;std::optional<std::size_t> encounter;};
    std::optional<CharacterSceneCombat> combat;AbilityOwnerHandle ability_owner;std::vector<Actor> actors;std::vector<StableId> actor_identities;std::uint32_t player_faction{1};std::vector<SensedSubject> seen;std::vector<MotorInput> actor_commands;std::vector<MotionRequest> actor_motions;std::vector<AbilityOwnerSnapshot> actor_before;std::vector<char> actor_requested;std::uint64_t next_operation{1};std::vector<InputEvent> pending_events;
    // World events, trigger volumes and encounters; enemies an encounter spawned keep their configuration here.
    WorldEvents world;std::vector<WorldTrigger> triggers;std::vector<EncounterDirector> directors;std::vector<StableId> encounter_identities;std::deque<CharacterSceneActor> spawned;std::map<std::string,std::uint32_t> factions;std::vector<std::shared_ptr<const ActionDefinition>> prepared_actions;std::string rig_archive;
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
    std::unique_ptr<ActionPoseMixer> action_mixer;
    // Shared read-only reaction selection for the owner's confirmed pose.
    std::unique_ptr<ReactionPresentation> reactions;std::optional<ReactionSample> player_reaction;
    std::unique_ptr<DeathPresentation> player_death;std::optional<double> player_death_tick;
    const EffectFrame* effect_frame(std::uint64_t id)const{auto found=effect_views.find(id);return found!=effect_views.end()&&found->second->current()?&*found->second->current():nullptr;}
    StableId player_identity;
    SimulationClock clock;
    std::uint64_t player{};
    bool pending_jump{},failed{};unsigned resynchronizations{};
    // Soft lock: the candidate the live prediction chose per unconfirmed tick (reused by replay) and the last tick the owner warped.
    std::map<std::uint64_t,std::uint64_t> predicted_locks;std::uint64_t soft_lock_tick{},soft_lock_actor{};
    Impl(SessionHandshake hello,std::span<const ObjectData> objects,StableId identity,const CollisionDefinition& collision,
         std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition definition,std::string_view archive,std::optional<CharacterSceneCombat> configured)
        :transport(create_loopback(hello)),server(SessionRole::Server,hello),client(SessionRole::Client,hello),presentation_plan(plan),graph(std::move(plan)),rig(std::move(definition),archive){
        player_identity=identity;combat=std::move(configured);require((hello.protocol==2||hello.protocol==3)&&(!combat||hello.protocol==3)&&rig.definition().human,"Character scene requires wire2 and the initial human profile");require(!objects.empty()&&objects.size()<=128&&identity,"Character scene object/player bounds");
        if(auto tags=graph.tag_dictionary())require(combat&&combat->tags&&combat->tags->registry()==tags->registry()&&combat->tags->generation()==tags->generation(),"Character graph/actor tag registry mismatch");
        host_peer=transport.host->open(hello);client_peer=transport.client->open(hello);
        server.attach(*transport.host,host_peer);client.attach(*transport.client,client_peer);
        for(const auto& box:collision.boxes)require(!box.moving&&!box.dynamic&&!box.sensor,"Character scene initial profile requires static collision");
        MotorVec foot;double yaw{};
        if(combat)require(combat->actors.size()<=character_scene_actor_limit&&!combat->faction.empty(),"Combat scene actor/faction bounds");
        for(const auto& object:objects){auto id=server.create(object);if(object.id==identity){require(!player,"Duplicate character scene player");require(object.transform.scale==1&&object.transform.pitch==0&&object.transform.roll==0&&object.health.current>0,"Character scene requires an upright, unscaled live player");player=id;foot={object.transform.x,object.transform.y,object.transform.z};yaw=object.transform.yaw;}
            if(combat)for(const auto& config:combat->actors)if(config.id==object.id){require(object.id!=identity&&std::none_of(actors.begin(),actors.end(),[&](const auto& known){return known.identity==object.id;})&&object.transform.scale==1&&object.health.current>0,"Combat actor profile");Actor actor;actor.identity=object.id;actor.id=id;actor.config=&config;actor.foot={object.transform.x,object.transform.y,object.transform.z};actor.yaw=object.transform.yaw;actors.push_back(std::move(actor));}}
        if(combat){require(actors.size()==combat->actors.size(),"Combat actor has no scene object");std::sort(actors.begin(),actors.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            // Factions become session-local numbers in simulation order; the player's is 1.
            factions={{combat->faction,player_faction}};for(auto& actor:actors){actor.faction=factions.emplace(actor.config->faction,std::uint32_t(factions.size()+1)).first->second;actor_identities.push_back(actor.identity);}
            server.assign_team(player,player_faction);for(const auto& actor:actors)server.assign_team(actor.id,actor.faction);
            actor_commands.resize(actors.size());actor_motions.resize(actors.size());actor_before.resize(actors.size());actor_requested.resize(actors.size());seen.reserve(actors.size()+1);}
        require(player,"Character scene player missing");authoritative.load_scene(collision);prediction->load_scene(collision);
        host_motor=std::make_unique<CharacterMotor>(authoritative,foot,player);owner_motor=std::make_unique<CharacterMotor>(*prediction,foot,player);
        if(combat){require(combat->kit&&combat->evaluator&&(bool(combat->damage)||bool(combat->combat_damage)),"Combat scene requires frozen kit/game evaluator");validate_combat_kit(*combat->kit,combat->input);for(const auto& ability:combat->abilities){require(ability&&ability->action->motion,"Scene action requires a frozen clip");auto clip=combat->clips.at(ability->action->motion->clip.id);require(clip&&clip->definition().skeleton==rig.definition().id&&clip->definition().signature==rig.definition().signature&&clip->archive_generation()==ability->action->motion->archive_generation,"Scene action clip generation/rig mismatch");}
            server.bind_melee_query(std::make_shared<PhysicsMeleeQuery>(authoritative));if(combat->combat_damage)server.register_combat_evaluator(combat->evaluator,combat->combat_damage);else server.register_damage_evaluator(combat->evaluator,combat->damage);for(const auto& [id,evaluator]:combat->effect_evaluators)server.register_effect_evaluator(id,evaluator);ability_owner=server.configure_abilities(player,combat->attributes,combat->health,combat->maximum_health);if(combat->tags)server.configure_ability_tags(ability_owner,combat->tags);if(combat->loadouts.empty())server.equip_combat_kit(ability_owner,combat->kit,combat->input,combat->abilities);else{require(combat->loadouts.size()==loadout_slot_count&&combat->active_loadout<loadout_slot_count&&combat->loadouts[combat->active_loadout],"Scene loadouts need four slots and an equipped active slot");server.configure_ability_loadouts(ability_owner,combat->loadouts,combat->active_loadout);}
            // Every actor gets its own motor and ability owner; one with a kit is equipped with it.
            for(auto& actor:actors)arm(actor);
            {std::size_t planned=actors.size();for(const auto& encounter:combat->encounters)planned+=encounter.definition.enemies();require(planned<=character_scene_actor_limit,"The scene's spawn points and encounters ask for more actors than one session holds");}
            for(const auto& encounter:combat->encounters){for(auto npc:encounter.definition.npcs()){auto found=encounter.npcs.find(npc);require(found!=encounter.npcs.end()&&!found->second.attributes.empty()&&!found->second.faction.empty(),"An encounter's NPC definition is not prepared");for(const auto& ability:found->second.abilities)require(ability&&std::any_of(combat->abilities.begin(),combat->abilities.end(),[&](const auto& known){return known->id==ability->id&&known->generation==ability->generation;}),"An encounter NPC's ability is not in the prepared scene abilities");}
                require(bool(encounter.id)&&std::find(encounter_identities.begin(),encounter_identities.end(),encounter.id)==encounter_identities.end(),"Encounter identity");directors.emplace_back(encounter.definition,encounter.volumes,encounter.points,encounter.id.high^encounter.id.low);encounter_identities.push_back(encounter.id);}
            for(const auto& trigger:combat->triggers)triggers.emplace_back(trigger.definition,trigger.center,trigger.yaw);world.emit("play.started");
            server.drain_ability_actions();}
        auto host_state=host_motor->state();host_state.topology=authoritative.topology();host_state.yaw=yaw;host_motor->restore(host_state);auto owner_state=owner_motor->state();owner_state.yaw=yaw;owner_motor->restore(owner_state);
        owner=std::make_unique<OwnerPrediction>(*prediction,*owner_motor);prepared_geometry=prediction->capture().meshes;
        server.own_motor(player,host_peer);server.publish_motor(player,host_motor->state());
        auto initial=authoritative.capture();history.retain(initial);server.publish_collision(collision_stream(initial));
        for(unsigned work=0;work<32&&(!client.motors().contains(player)||!client.collision_control_ready()||(combat&&(!client.ability_corrections().contains(player)||!client.public_abilities().contains(player)||!actors_public())));++work)pump();
        require(client.readiness(client_peer)==SessionReadiness::Ready&&client.collision_control_ready()&&client.motors().contains(player),"Character scene bootstrap/control work limit");
        if(combat){require(client.ability_corrections().contains(player),"Combat prediction bootstrap work limit");ability_prediction=std::make_unique<OwnerAbilityPrediction>(client.ability_corrections().at(player).ability,combat->attributes,combat->kit,combat->input,combat->abilities,client.ability_corrections().at(player).motor.epoch,combat->tags,combat->loadouts);}
        if(combat){std::vector<std::shared_ptr<const ActionDefinition>> actions;for(const auto& ability:combat->abilities)actions.push_back(ability->action);prepared_actions=actions;rig_archive=std::string(archive);
            auto observe=[&](std::uint64_t id,const std::vector<AttributeDefinition>& schema){require(client.public_abilities().contains(id),"Combat public bootstrap work limit");auto prepared=std::make_unique<ObserverAbility>(schema,combat->health,combat->maximum_health,actions,combat->tags);prepared->push(client.public_abilities().at(id));observers.emplace(id,std::move(prepared));if(!combat->effects.empty()){require(bool(combat->tags),"Scene effect preparation needs a tag registry");effect_views.emplace(id,std::make_unique<EffectPresentation>(AttributeVisibility::Public,AttributeSet(schema),combat->tags,combat->effects));}};
            observe(player,combat->attributes);for(const auto& actor:actors)observe(actor.id,actor.config->attributes);
            action_mixer=std::make_unique<ActionPoseMixer>(*presentation_plan,rig.definition(),actions);reactions=std::make_unique<ReactionPresentation>(rig.definition(),combat->effects,combat->clips);if(combat->death)player_death=std::make_unique<DeathPresentation>(rig.definition(),combat->death,combat->clips);
            for(auto& actor:actors){actor.pose=std::make_unique<ObservedCharacterPose>(presentation_plan,rig.definition(),archive,actions,combat->clips,combat->tags,combat->effects,combat->death);actor.pose->sample(observers.at(actor.id)->sample(0).frame,effect_frame(actor.id));}

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
        auto local=ability_motion_between(definition(snapshot),from,to);const auto& v=local.root.translation;auto yaw=command.yaw;command.yaw=std::remainder(command.yaw+local.root.yaw,6.283185307179586);MotionRequest request;request.root={std::cos(yaw)*v[0]+std::sin(yaw)*v[2],v[1],-std::sin(yaw)*v[0]+std::cos(yaw)*v[2]};request.action=snapshot.active->activation;request.lock=local.movement_lock;request.warp=ability_warp_between(definition(snapshot),from,to);return request;
    }
    // Facing rule for abilities authored with face_movement: on the activation tick the
    // actor turns to its world-space movement input (or keeps its yaw without input); afterwards
    // the yaw stays where the motor has it until the action ends. Input yaw then resumes.
    static void face(MotorInput& command,double motor_yaw,bool started){
        if(!started){command.yaw=motor_yaw;return;}
        if(std::hypot(command.x,command.z)>.1)command.yaw=std::atan2(command.x,command.z);
    }
    // Root/lock request of an owner's action across one authoritative tick, including the
    // final partial step of an action that completed on this tick.
    MotionRequest action_motion(const AbilityOwnerSnapshot& before,const AbilityOwnerSnapshot& after,std::span<const AbilityActionUpdate> updates,MotorInput& command,double motor_yaw,bool requested=false)const{
        if(after.active){const bool continuing=before.active&&before.active==after.active;if(definition(after).face_movement)face(command,motor_yaw,!continuing||requested);auto from=continuing?before.action->clock:0;return motion(after,from,after.action->clock,command);}
        if(before.active)for(const auto& update:updates)if(update.handle==*before.active&&update.phase==ActionPhase::Completed){if(definition(before).face_movement)face(command,motor_yaw,false);auto total=std::uint64_t(definition(before).action->duration)*definition(before).action->loops;return motion(before,before.action->clock,total,command);}
        return {};
    }
    // Movement restriction from statuses and death: the motor's existing lock request plus
    // no jump. Authority reads its own state after effects advanced on this tick.
    bool locks(const EffectDefinition& effect)const{return effect.lock_movement;}
    bool restricted(AbilityOwnerHandle owner,std::uint64_t id)const{
        if(server.objects().at(id).health.current<=0)return true;
        for(const auto& active:server.ability_effects(owner))if(!active.suppressed)for(const auto& effect:combat->effects)if(effect->id==active.definition&&effect->generation==active.generation&&locks(*effect))return true;
        return false;
    }
    // The owner predicts the same restriction for a tick from what it has confirmed: its
    // corrected Health and the instance clocks of its latest checked public effect frame.
    bool predicted_restricted(std::uint64_t tick)const{
        const auto& confirmed=client.ability_corrections().at(player).ability;for(const auto& attribute:confirmed.attributes)if(attribute.id==confirmed.health_attribute&&attribute.value<=0)return true;
        if(const auto* frame=effect_frame(player))for(const auto& active:frame->effects)if(!active.suppressed&&active.start<tick&&(!active.end||tick<active.end))for(const auto& effect:combat->effects)if(effect->id==active.definition&&effect->generation==active.generation&&locks(*effect))return true;
        return false;
    }
    static void restrict(MotorInput& command,MotionRequest& request){request.lock=true;request.warp={};command.jump=false;}
    // A character's public frame as observers take it. The server sends a public ability frame only when its content
    // changed; while the index says the held content is current, the frame is brought to the tick with the character's
    // own motor state from the motor stream.
    bool public_current(std::uint64_t id,std::uint64_t tick)const{auto listed=client.public_index().find(id);auto held=client.public_abilities().find(id);auto motor=client.motors().find(id);return listed!=client.public_index().end()&&held!=client.public_abilities().end()&&motor!=client.motors().end()&&listed->second.tick==tick&&motor->second.tick==tick&&(held->second.ability.tick==tick||(!held->second.ability.active&&listed->second.content==public_ability_content(held->second.ability)));}
    AbilityPublicFrame shown(std::uint64_t id)const{auto frame=client.public_abilities().at(id);auto motor=client.motors().find(id);if(!frame.ability.active&&motor!=client.motors().end()&&motor->second.tick>frame.ability.tick&&motor->second.epoch==frame.motor.epoch){frame.motor=motor->second;frame.ability.tick=motor->second.tick;}return frame;}
    // Server side of one actor: senses, authoritative motor, ability owner with its kit. Runs at session start for the scene's
    // actors and during play for an enemy an encounter spawns.
    void arm(Actor& actor){const auto& config=*actor.config;require(!config.attributes.empty(),"Combat actor needs resolved attributes");AttributeSet(config.attributes);
                actor.perception=std::make_unique<Perception>(config.senses);actor.motor=std::make_unique<CharacterMotor>(authoritative,actor.foot,actor.id);auto state=actor.motor->state();state.yaw=actor.yaw;state.tick=state.sequence=authoritative.tick();actor.motor->restore(state);
                actor.owner=server.configure_abilities(actor.id,config.attributes,combat->health,combat->maximum_health);if(combat->tags)server.configure_ability_tags(actor.owner,combat->tags);
                if(config.kit){validate_combat_kit(*config.kit,config.input);for(const auto& ability:config.abilities)require(ability&&std::any_of(combat->abilities.begin(),combat->abilities.end(),[&](const auto& known){return known->id==ability->id&&known->generation==ability->generation;}),"Actor ability is not in the prepared scene abilities");server.equip_combat_kit(actor.owner,config.kit,config.input,config.abilities);}
                else require(config.abilities.empty(),"An actor without a kit cannot carry abilities");
                server.publish_motor(actor.id,actor.motor->state());}
    // Client side of one actor: observer, effect view and pose, once its first public frame has arrived.
    void present(Actor& actor){
        require(client.public_abilities().contains(actor.id),"Combat public bootstrap work limit");auto prepared=std::make_unique<ObserverAbility>(actor.config->attributes,combat->health,combat->maximum_health,prepared_actions,combat->tags);prepared->push(client.public_abilities().at(actor.id));observers.emplace(actor.id,std::move(prepared));
        if(!combat->effects.empty())effect_views.emplace(actor.id,std::make_unique<EffectPresentation>(AttributeVisibility::Public,AttributeSet(actor.config->attributes),combat->tags,combat->effects));
        actor.pose=std::make_unique<ObservedCharacterPose>(presentation_plan,rig.definition(),rig_archive,prepared_actions,combat->clips,combat->tags,combat->effects,combat->death);actor.pose->sample(observers.at(actor.id)->sample(double(client.public_abilities().at(actor.id).ability.tick)).frame,effect_frame(actor.id));
    }
    // An enemy enters the session: a replicated object at the requested place, then the same setup as any actor.
    void spawn(std::size_t index,const EncounterSpawn& request){
        const auto& encounter=combat->encounters[index];require(actors.size()<character_scene_actor_limit,"An encounter spawned more actors than one session holds");
        auto made=encounter.npcs.at(request.npc);made.id=encounter_actor(encounter.id,request.index);made.controller=encounter.controller?encounter.controller(request.npc):nullptr;spawned.push_back(std::move(made));const auto& config=spawned.back();
        ObjectData object;object.id=config.id;object.transform.x=request.position.x;object.transform.y=request.position.y;object.transform.z=request.position.z;object.transform.yaw=request.yaw;
        for(const auto& attribute:config.attributes){if(attribute.id==combat->health)object.health.current=attribute.base;if(attribute.id==combat->maximum_health)object.health.maximum=attribute.base;}
        Actor actor;actor.identity=config.id;actor.id=server.create(object);actor.config=&config;actor.foot=request.position;actor.yaw=request.yaw;actor.encounter=index;require(actors.empty()||actors.back().id<actor.id,"A spawned actor's network identity is out of order");
        actor.faction=factions.emplace(config.faction,std::uint32_t(factions.size()+1)).first->second;server.assign_team(actor.id,actor.faction);arm(actor);
        actors.push_back(std::move(actor));actor_identities.push_back(config.id);actor_commands.resize(actors.size());actor_motions.resize(actors.size());actor_before.resize(actors.size());actor_requested.resize(actors.size());
    }
    // World step, before sensing: deliver last tick's events, let trigger volumes and encounters act on authoritative state.
    void run_world(std::uint64_t tick){
        world.begin_tick(tick);const auto& owner_state=host_motor->state();const bool owner_alive=server.objects().at(player).health.current>0;
        for(auto& trigger:triggers)trigger.step(world,owner_state.position,owner_alive);
        const auto ground=[this](double x,double from,double z)->std::optional<double>{const auto found=authoritative.query_ray({x,from,z},{0,-(std::abs(from)+500),0},{StaticCollision,0});const CollisionHit* nearest=nullptr;for(const auto& hit:found.hits)if(!nearest||hit.fraction<nearest->fraction)nearest=&hit;if(!nearest)return std::nullopt;return nearest->point.y;};
        const auto occupied=[this](MotorVec point){auto near=[&](const MotorVec& at){return std::hypot(at.x-point.x,at.z-point.z)<1.&&std::abs(at.y-point.y)<2.5;};if(server.objects().at(player).health.current>0&&near(host_motor->state().position))return true;for(const auto& actor:actors)if(server.objects().at(actor.id).health.current>0&&near(actor.motor->state().position))return true;return false;};
        for(std::size_t index=0;index<directors.size();++index){unsigned alive=0;for(const auto& actor:actors)if(actor.encounter==index&&server.objects().at(actor.id).health.current>0)++alive;
            for(const auto& request:directors[index].step(tick,world,owner_state.position,alive,ground,occupied))spawn(index,request);}
    }
    bool actors_public()const{for(const auto& actor:actors)if(!client.public_abilities().contains(actor.id))return false;return true;}
    const Actor* find(StableId identity)const{for(const auto& actor:actors)if(actor.identity==identity)return &actor;return nullptr;}
    // Soft-lock candidates for the player: every live actor of another faction. Authority reads its own
    // state; the owner reads its confirmed public view (motor snapshot and public Health).
    std::vector<TargetCandidate> server_candidates()const{std::vector<TargetCandidate> result;for(const auto& actor:actors)if(actor.faction!=player_faction&&server.objects().at(actor.id).health.current>0)result.push_back({actor.id,actor.motor->state().position});return result;}
    std::vector<TargetCandidate> client_candidates()const{
        std::vector<TargetCandidate> result;for(const auto& actor:actors){if(actor.faction==player_faction)continue;auto motor=client.motors().find(actor.id);auto shown=client.public_abilities().find(actor.id);if(motor==client.motors().end()||shown==client.public_abilities().end())continue;
            const auto& ability=shown->second.ability;for(const auto& attribute:ability.attributes)if(attribute.id==ability.health_attribute&&attribute.value>0)result.push_back({actor.id,motor->second.position});}
        return result;
    }
    // The targeting module proposes the point; the motor gates and applies the warp when it steps.
    static std::optional<std::uint64_t> lock(MotionRequest& request,const MotorState& state,const MotorInput& command,std::span<const TargetCandidate> candidates){
        if(!request.warp.active)return std::nullopt;if(auto chosen=select_soft_lock(request.warp,state,command,candidates)){request.warp.point=chosen->position;return chosen->id;}request.warp={};return std::nullopt;
    }
    std::vector<TargetCandidate> locked_candidates(const Actor& actor)const{
        std::vector<TargetCandidate> result;if(!actor.locked)return result;
        if(*actor.locked==player_identity){if(server.objects().at(player).health.current>0)result.push_back({player,host_motor->state().position});}
        else if(const auto* other=find(*actor.locked))if(server.objects().at(other->id).health.current>0)result.push_back({other->id,other->motor->state().position});
        return result;
    }
    // Shared aim for NPCs: an aimed ability points from its muzzle at the body of the target the controller chose for this action,
    // raised by the drop a projectile will suffer on the way. A projectile is aimed at where the target will be when it arrives:
    // the target's current movement over the ground, held for the flight time, refined twice because the lead changes the distance.
    // An instant shot has no flight time and no lead. The controller only chooses the target and presses the slot.
    void aim_actor(const Actor& actor,const AbilityDefinition& ability,MotorInput& command)const{
        if(!ability.aim||ability.melee.empty())return;const auto candidates=locked_candidates(actor);if(candidates.empty())return;
        const auto& self=actor.motor->state();const auto& goal=candidates.front().position;MotorVec drift{};
        if(ability.aim->projectile){const MotorState* target=nullptr;if(*actor.locked==player_identity)target=&host_motor->state();else if(const auto* other=find(*actor.locked))target=&other->motor->state();if(target)drift={target->achieved.x*60,0,target->achieved.z*60};}
        double seconds=0,dx=0,dz=0,level=0;MotorVec muzzle{};
        for(unsigned pass=0;pass<3;++pass){const MotorVec ahead{goal.x+drift.x*seconds,goal.y,goal.z+drift.z*seconds};MotorState turned=self;turned.yaw=std::atan2(ahead.x-self.position.x,ahead.z-self.position.z);muzzle=melee_origin(turned,ability.melee.front());
            dx=ahead.x-muzzle.x;dz=ahead.z-muzzle.z;level=std::hypot(dx,dz);if(level<.3)return;if(!ability.aim->projectile)break;seconds=level/ability.aim->projectile->speed;}
        double height=goal.y+perception_body_height-muzzle.y;if(ability.aim->projectile)height+=.5*ability.aim->projectile->gravity*seconds*seconds;
        command.yaw=std::atan2(dx,dz);command.pitch=std::clamp(std::atan2(height,level),-1.4,1.4);
    }
    // Controller seam: an actor's controller sees a bounded view of authoritative state as it stood before this tick and asks for
    // move, face and a slot press. Activation, cost, cooldown, hits and effects are the ordinary authoritative ability path.
    std::shared_ptr<const AbilityDefinition> actor_ability(const Actor& actor,CombatSlot slot)const{if(!actor.config->kit)return {};const auto wanted=actor.config->kit->slots[static_cast<unsigned>(slot)].ability;if(wanted==AssetId{})return {};for(const auto& ability:actor.config->abilities)if(ability->id==wanted)return ability;return {};}
    // Sensing: every actor perceives the others from authoritative state as it stood before this tick, in ascending network
    // identity, with at most one ray per other actor against static collision. Nothing from a client is read.
    bool sight(MotorVec from,MotorVec to)const{return authoritative.query_ray(from,{to.x-from.x,to.y-from.y,to.z-from.z},{StaticCollision,0}).hits.empty();}
    void sense(Actor& actor,std::uint64_t tick){
        seen.clear();bool owner_listed=false;auto list_owner=[&]{seen.push_back({player_identity,player_faction,host_motor->state(),server.objects().at(player).health.current>0});owner_listed=true;};
        for(const auto& other:actors){if(!owner_listed&&player<other.id)list_owner();if(&other!=&actor)seen.push_back({other.identity,other.faction,other.motor->state(),server.objects().at(other.id).health.current>0});}
        if(!owner_listed)list_owner();
        actor.perception->sense(tick,actor.motor->state(),server.objects().at(actor.id).health.current>0,seen,[this](MotorVec from,MotorVec to){return sight(from,to);});
    }
    const PerceivedActor* perceived(const Actor& actor,StableId identity)const{for(const auto& other:actor.perception->actors())if(other.id==identity)return &other;return nullptr;}
    bool drive_controller(Actor& actor,std::uint64_t tick,MotorInput& command){
        ControllerView view;view.tick=tick;view.self=actor.motor->state();view.alive=server.objects().at(actor.id).health.current>0;view.faction=actor.faction;view.perceived=actor.perception->actors();
        auto snapshot=server.ability_snapshot(actor.owner);view.busy=bool(snapshot.active);
        const auto decision=actor.config->controller->decide(view);
        require(std::isfinite(decision.move_x)&&std::isfinite(decision.move_z)&&decision.move_x*decision.move_x+decision.move_z*decision.move_z<=1.00001&&(!decision.face||std::isfinite(*decision.face))&&(!decision.press||static_cast<unsigned>(*decision.press)<combat_slot_count),"Controller command bounds");
        const auto* aimed_at=decision.target?perceived(actor,*decision.target):nullptr;require(!decision.target||(aimed_at&&aimed_at->detected),"Controller target is not an actor it detects");actor.chosen=decision.target;
        command.x=decision.move_x;command.z=decision.move_z;bool pressed=false;
        if(decision.press){
            require(actor.operation!=UINT64_MAX,"Controller operation identity exhausted");
            // An aimed ability is only loosed at a chosen target the actor has line of sight to; otherwise the press never becomes a request.
            bool withheld=false;
            if(const auto ability=actor_ability(actor,*decision.press);ability&&ability->aim&&aimed_at&&!aimed_at->visible)withheld=true;
            else if(ability){
                if(actor.operation>1)server.retire_ability_operations(actor.owner,actor.operation-1);
                const auto& slot=actor.config->kit->slots[static_cast<unsigned>(*decision.press)];
                actor.receipt=server.request_ability({actor.owner,actor.operation++,snapshot.grant_generation,*decision.press,{slot.input_action,ability->activate_on,tick*16667,ability->minimum_held_us,1,false},false});
            }else{AbilityReceipt refused;refused.failure=AbilityFailure::Unassigned;refused.tick=tick;actor.receipt=refused;}
            pressed=!withheld&&actor.receipt->failure==AbilityFailure::None;
            // The target an accepted press was aimed at stays the action's soft-lock target.
            if(pressed)actor.locked=decision.target;
        }
        if(decision.face&&(!decision.press||pressed))command.yaw=std::remainder(*decision.face,6.283185307179586);
        validate_motor_input(command);return pressed;
    }
    MotionRequest predicted_motion(const AbilityPredictionMotion& predicted,MotorInput& command)const{
        const auto& v=predicted.local.root.translation;const auto yaw=command.yaw;command.yaw=std::remainder(yaw+predicted.local.root.yaw,6.283185307179586);MotionRequest request;request.root={std::cos(yaw)*v[0]+std::sin(yaw)*v[2],v[1],-std::sin(yaw)*v[0]+std::cos(yaw)*v[2]};request.action=predicted.activation;request.lock=predicted.local.movement_lock;request.warp=predicted.warp;return request;
    }
    // Input routes through the predicted active loadout's kit; loadout-select actions are kit independent.
    const CombatKitDefinition& routing_kit()const{if(!combat->loadouts.empty()&&ability_prediction){const auto& loadout=combat->loadouts[ability_prediction->view().loadout];if(loadout)return *loadout->kit;}return *combat->kit;}
    std::optional<unsigned> loadout_request(const InputEvent& event)const{if(!combat->loadouts.empty()&&event.action)for(unsigned index=0;index<loadout_slot_count;++index)if(combat->loadout_actions[index]==event.action)return index;return std::nullopt;}
    void enqueue(std::span<const InputEvent> events){if(!combat){require(events.empty(),"Combat input requires a configured kit");return;}auto staged=pending_events;for(const auto& event:events){bool routed=loadout_request(event).has_value();for(const auto& slot:routing_kit().slots)routed|=slot.input_action&&slot.input_action==event.action;if(routed){require(staged.size()<32,"Combat input queue full; resynchronize");staged.push_back(event);}}pending_events=std::move(staged);}
    void step(CharacterSceneInput input){
        require(!failed,"Character scene requires a fresh synchronized session");require(!input.crouch,"Character scene posture animation is not in the initial standing profile");require(std::isfinite(input.x)&&std::isfinite(input.z)&&std::isfinite(input.yaw)&&std::isfinite(input.pitch),"Character scene input finite");
        const auto tick=authoritative.tick()+1;MotorInput command{tick,tick,owner_motor->state().epoch,input.x,input.z,input.yaw,input.jump,input.crouch,input.pitch};validate_motor_input(command);
        require(client.collision_control_ready()&&client.submit_motor(player,command),"Character scene control unavailable; resynchronize");
        std::vector<AbilityPredictionInput> prediction_input;
        if(combat){require(client.ability_corrections().contains(player)&&!client.ability_correction_needs_resync(),"Combat correction unavailable; resynchronize");const auto& current=client.ability_corrections().at(player).ability;auto submit=[&](CombatSlot slot,const InputEvent& event){require(next_operation!=UINT64_MAX,"Combat operation identity exhausted");AbilityIntent intent{player,next_operation++,tick,owner_motor->state().epoch,current.grant_generation,slot,event.edge,event.cancelled,false};require(client.submit_ability_intent(intent),"Combat input delivery unavailable; resynchronize");prediction_input.push_back({intent});};
            for(const auto& event:pending_events){if(auto index=loadout_request(event)){if(event.edge==InputEdge::Pressed&&!event.cancelled)submit(loadout_select_slot(*index),event);continue;}for(const auto& slot:routing_kit().slots)if(slot.input_action&&slot.input_action==event.action)submit(slot.slot,event);}pending_events.clear();}
        MotionRequest authoritative_motion,predicted_request;auto predicted_command=command;
        if(combat){auto predicted=ability_prediction->advance(tick,prediction_input);if(predicted.face_movement)face(predicted_command,owner_motor->state().yaw,predicted.started);predicted_request=predicted_motion(predicted,predicted_command);if(predicted_restricted(tick))restrict(predicted_command,predicted_request);const auto candidates=client_candidates();if(auto chosen=lock(predicted_request,owner_motor->state(),predicted_command,candidates)){predicted_locks[tick]=*chosen;soft_lock_tick=tick;soft_lock_actor=*chosen;}}
        semantic_commands.push_back(command);require(semantic_commands.size()<=30,"Character scene semantic history overflow; resynchronize");
        server.tick();require(server.motor_input_ready(player,tick),"Character scene owner command unavailable");
        auto accepted=server.consume_motor(player,tick,command.epoch);
        if(combat)run_world(tick);
        for(std::size_t index=0;index<actors.size();++index){const auto& motor=actors[index].motor->state();actor_commands[index]=MotorInput{tick,tick,motor.epoch,0,0,motor.yaw};actor_motions[index]={};}
        if(combat){
            // Controllers decide in ascending network identity from the state before this tick; their presses join the same ability advance as the player's intents.
            for(auto& actor:actors)sense(actor,tick);
            for(std::size_t index=0;index<actors.size();++index)actor_requested[index]=actors[index].config->controller&&drive_controller(actors[index],tick,actor_commands[index]);
            auto before=server.ability_snapshot(ability_owner);for(std::size_t index=0;index<actors.size();++index)actor_before[index]=server.ability_snapshot(actors[index].owner);
            server.advance_abilities(tick);auto after=server.ability_snapshot(ability_owner);auto updates=server.drain_ability_actions();authoritative_motion=action_motion(before,after,updates,accepted,host_motor->state().yaw);if(restricted(ability_owner,player))restrict(accepted,authoritative_motion);{const auto candidates=server_candidates();lock(authoritative_motion,host_motor->state(),accepted,candidates);}
            for(std::size_t index=0;index<actors.size();++index){auto& actor=actors[index];if(actor.config->kit){const auto now=server.ability_snapshot(actor.owner);if(now.active)aim_actor(actor,definition(now),actor_commands[index]);actor_motions[index]=action_motion(actor_before[index],now,updates,actor_commands[index],actor.motor->state().yaw,bool(actor_requested[index]));}if(restricted(actor.owner,actor.id))restrict(actor_commands[index],actor_motions[index]);
                // Shared soft lock: an NPC's only candidate is the live target its controller chose for this action.
                if(!server.ability_snapshot(actor.owner).active&&!actor_requested[index])actor.locked.reset();
                {const auto candidates=locked_candidates(actor);lock(actor_motions[index],actor.motor->state(),actor_commands[index],candidates);}}}
        owner->predict(predicted_command,predicted_request);prediction->step();owner_motor->post_physics();prediction->finish_tick();
        host_motor->step(accepted,authoritative_motion);for(std::size_t index=0;index<actors.size();++index)actors[index].motor->step(actor_commands[index],actor_motions[index]);authoritative.step();host_motor->post_physics();for(auto& actor:actors)actor.motor->post_physics();authoritative.finish_tick();
        auto frame=authoritative.capture();history.retain(frame);server.publish_motor(player,host_motor->state());for(const auto& actor:actors)server.publish_motor(actor.id,actor.motor->state());if(combat){server.resolve_ability_hits(tick);commitment_updates=server.drain_ability_commitments();server.drain_effect_outcomes();}server.publish_collision(collision_stream(frame));pump();
        auto public_ready=[&]{if(!public_current(player,tick))return false;for(const auto& actor:actors)if(!public_current(actor.id,tick))return false;return true;};
        auto snapshots_ready=[&]{return client.motors().contains(player)&&client.motors().at(player).tick==tick&&!client.collisions().empty()&&client.collisions().back().tick==tick&&client.collision_control_ready()&&(!combat||(client.ability_corrections().contains(player)&&client.ability_corrections().at(player).ability.tick==tick&&public_ready()));};
        for(unsigned work=0;work<32&&!snapshots_ready();++work)pump();
        require(client.motors().contains(player)&&client.motors().at(player).tick==tick&&!client.collisions().empty()&&client.collisions().back().tick==tick,"Character scene snapshot delivery bound");
        if(combat){const auto& correction=client.ability_corrections().at(player);require(correction.ability.tick==tick&&correction.motor.tick==tick,"Atomic combat correction clock");if(correction.ability.active)definition(correction.ability);}
        if(combat){auto prepared=*ability_prediction;for(const auto& receipt:client.drain_ability_receipts())prepared.receipt(receipt);const auto& correction=client.ability_corrections().at(player);prepared.reconcile(correction.ability);std::vector<MotorCommand> regenerated;double replay_yaw=correction.motor.yaw;
            for(const auto& replay:prepared.replay_motion()){auto found=std::find_if(semantic_commands.begin(),semantic_commands.end(),[&](const auto& value){return value.tick==replay.tick;});require(found!=semantic_commands.end(),"Combat correction missing motor input history");auto replay_input=*found;if(replay.face_movement)face(replay_input,replay_yaw,replay.started);auto request=predicted_motion(replay,replay_input);replay_yaw=replay_input.yaw;if(predicted_restricted(replay.tick))restrict(replay_input,request);if(request.warp.active){const auto candidates=client_candidates();auto locked=predicted_locks.find(replay.tick);auto chosen=std::find_if(candidates.begin(),candidates.end(),[&](const auto& candidate){return locked!=predicted_locks.end()&&candidate.id==locked->second;});if(chosen!=candidates.end())request.warp.point=chosen->position;else request.warp={};}regenerated.push_back({replay_input,request});}
            require(owner->reconcile(correction.motor,history,regenerated)==ReplayResult::Applied&&!owner->needs_resync(),"Character scene atomic combat/motor correction failed; resynchronize");*ability_prediction=std::move(prepared);
        }else require(owner->reconcile(client.motors().at(player),history)==ReplayResult::Applied&&!owner->needs_resync(),"Character scene isolated correction failed; resynchronize");
        std::erase_if(semantic_commands,[&](const auto& input){return input.tick<=client.motors().at(player).tick;});std::erase_if(predicted_locks,[&](const auto& item){return item.first<=client.motors().at(player).tick;});
        if(combat){require(public_ready(),"Combat public snapshot delivery bound; resynchronize");for(auto& actor:actors)if(!actor.pose)present(actor);for(auto& [id,observer]:observers)observer->push(shown(id));effect_cue_updates.clear();for(auto& [id,view]:effect_views){require(!client.effect_frame_needs_resync(id,AttributeVisibility::Public),"Scene effect state requires resynchronization");auto found=client.effect_frames(AttributeVisibility::Public).find(id);if(found!=client.effect_frames(AttributeVisibility::Public).end())view->push(found->second);auto cues=view->drain_cues();effect_cue_updates.insert(effect_cue_updates.end(),std::make_move_iterator(cues.begin()),std::make_move_iterator(cues.end()));require(effect_cue_updates.size()<=1024,"Scene persistent cue work bound");}}
        for(auto& actor:actors)if(actor.pose)actor.pose->sample(observers.at(actor.id)->sample(double(tick)).frame,effect_frame(actor.id));
        const auto& state=host_motor->state();double x=state.achieved.x*60-state.support_velocity.x,z=state.achieved.z*60-state.support_velocity.z;
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
            else if(correction.ability.active){const auto& ability=definition(correction.ability);matrices=action_mixer->sample(rig,graph_inputs,*ability.action,*combat->clips.at(ability.action->motion->clip.id),double(correction.ability.action->clock%ability.action->duration)/action_tick_units);}else if(player_reaction&&player_reaction->suppressed==ReactionSuppression::None)matrices=rig.sample(*player_reaction->clip,player_reaction->tick);else matrices=rig.blend(graph_inputs.span());client.acknowledge_ability_correction(player);client.drain_ability_receipts();client.tick();server.tick();}else matrices=rig.blend(graph_inputs.span());

    }
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
CharacterSceneSession::CharacterSceneSession(SessionHandshake hello,std::span<const ObjectData> objects,StableId player,const CollisionDefinition& collision,std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition rig,std::string_view archive,std::optional<CharacterSceneCombat> combat)
    :impl_(std::make_unique<Impl>(std::move(hello),objects,player,collision,std::move(plan),std::move(rig),archive,std::move(combat))){}
CharacterSceneSession::~CharacterSceneSession()=default;
unsigned CharacterSceneSession::advance(double seconds,CharacterSceneInput input){auto& p=*impl_;require(!p.failed,"Character scene requires a fresh synchronized session");require(std::isfinite(seconds)&&seconds>=0&&seconds<=10,"Character scene clock delta bounds");validate_motor_input({1,1,1,input.x,input.z,input.yaw,input.jump,input.crouch,input.pitch});p.enqueue(input.combat_events);input.combat_events.clear();p.pending_jump|=input.jump;auto steps=p.clock.advance(seconds);for(unsigned index=0;index<steps;++index){input.jump=p.pending_jump;try{p.step(input);}catch(...){p.failed=true;throw;}p.pending_jump=false;}return steps;}
void CharacterSceneSession::step(CharacterSceneInput input){try{impl_->enqueue(input.combat_events);input.combat_events.clear();impl_->step(input);}catch(...){impl_->failed=true;throw;}}
void CharacterSceneSession::remove_collision(std::uint64_t id){impl_->authoritative.remove(id);}
const World& CharacterSceneSession::presentation()const{return impl_->client.world();}
const MotorState& CharacterSceneSession::motor()const{return impl_->host_motor->state();}
const MotorState& CharacterSceneSession::predicted_motor()const{return impl_->owner_motor->state();}
const GraphState& CharacterSceneSession::graph()const{return impl_->graph.state();}
const GraphPoseInputs& CharacterSceneSession::graph_inputs()const{return impl_->graph_inputs;}
const std::vector<JointMatrix>& CharacterSceneSession::pose()const{return impl_->matrices;}
const std::vector<JointMatrix>* CharacterSceneSession::pose(StableId actor)const{if(actor==impl_->player_identity)return &impl_->matrices;if(const auto* found=impl_->find(actor))if(found->pose)return &found->pose->matrices();return nullptr;}
const GraphPoseInputs* CharacterSceneSession::graph_inputs(StableId actor)const{if(actor==impl_->player_identity)return &impl_->graph_inputs;if(const auto* found=impl_->find(actor))if(found->pose)return &found->pose->inputs();return nullptr;}
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
unsigned CharacterSceneSession::loadout()const{auto found=impl_->client.ability_corrections().find(impl_->player);return found==impl_->client.ability_corrections().end()?0:found->second.ability.loadout;}
std::optional<StableId> CharacterSceneSession::soft_lock()const{const auto& p=*impl_;if(p.combat&&p.soft_lock_tick&&p.soft_lock_tick==p.authoritative.tick())for(const auto& actor:p.actors)if(actor.id==p.soft_lock_actor)return actor.identity;return std::nullopt;}
std::span<const StableId> CharacterSceneSession::actors()const{return impl_->actor_identities;}
const std::vector<ProjectileView>& CharacterSceneSession::projectiles()const{return impl_->client.projectiles();}
std::uint64_t CharacterSceneSession::server_packets()const{return impl_->server.packets_sent(impl_->host_peer);}
std::vector<std::uint64_t> CharacterSceneSession::server_packet_kinds()const{return impl_->server.packets_by_kind(impl_->host_peer);}
AimSolution CharacterSceneSession::aim(MotorVec camera,MotorVec direction,double range,MotorVec muzzle)const{
    const auto& p=*impl_;const double length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
    require(std::isfinite(camera.x)&&std::isfinite(camera.y)&&std::isfinite(camera.z)&&std::isfinite(length)&&length>1e-6&&std::isfinite(range)&&range>0&&range<=200&&std::isfinite(muzzle.x)&&std::isfinite(muzzle.y)&&std::isfinite(muzzle.z),"Aim ray bounds");
    const MotorVec ray{direction.x/length,direction.y/length,direction.z/length};const auto& self=p.owner_motor->state();
    // Start level with the owner along the ray: the ground or a wall between the camera and the owner is not what the crosshair is on.
    const double skip=std::max(0.,(self.position.x-camera.x)*ray.x+(self.position.y+muzzle.y-camera.y)*ray.y+(self.position.z-camera.z)*ray.z);
    const MotorVec start{camera.x+ray.x*skip,camera.y+ray.y*skip,camera.z+ray.z*skip};double nearest=range;bool hit=false;
    const auto blockers=p.prediction->query_ray(start,{ray.x*range,ray.y*range,ray.z*range},{StaticCollision|KinematicCollision|DynamicCollision,p.player});
    for(const auto& blocker:blockers.hits){const double along=(blocker.point.x-start.x)*ray.x+(blocker.point.y-start.y)*ray.y+(blocker.point.z-start.z)*ray.z;if(along>=0&&along<nearest){nearest=along;hit=true;}}
    // Other actors as upright capsules at their confirmed public positions.
    constexpr double radius=.35,height=1.8;
    for(const auto& actor:p.actors){auto motor=p.client.motors().find(actor.id);if(motor==p.client.motors().end())continue;const auto& foot=motor->second.position;
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
std::string CharacterSceneSession::controller_status(StableId actor)const{const auto* found=impl_->find(actor);return found&&found->config->controller?found->config->controller->status():std::string{};}
const AbilityReceipt* CharacterSceneSession::controller_receipt(StableId actor)const{const auto* found=impl_->find(actor);return found&&found->receipt?&*found->receipt:nullptr;}
std::optional<StableId> CharacterSceneSession::controller_target(StableId actor)const{const auto* found=impl_->find(actor);return found?found->chosen:std::nullopt;}
void CharacterSceneSession::emit_world_event(std::string name){impl_->world.emit(std::move(name));}
const WorldEvents& CharacterSceneSession::world_events()const{return impl_->world;}
std::span<const StableId> CharacterSceneSession::encounters()const{return impl_->encounter_identities;}
std::optional<EncounterStatus> CharacterSceneSession::encounter(StableId id)const{const auto& p=*impl_;for(std::size_t index=0;index<p.directors.size();++index)if(p.encounter_identities[index]==id)return p.directors[index].status();return std::nullopt;}
std::optional<StableId> CharacterSceneSession::actor_encounter(StableId actor)const{const auto* found=impl_->find(actor);if(found&&found->encounter)return impl_->encounter_identities[*found->encounter];return std::nullopt;}
std::optional<unsigned> CharacterSceneSession::trigger_fired(StableId id)const{const auto& p=*impl_;for(std::size_t index=0;index<p.triggers.size();++index)if(p.combat->triggers[index].id==id)return p.triggers[index].fired();return std::nullopt;}
const CharacterSceneActor* CharacterSceneSession::actor(StableId id)const{const auto* found=impl_->find(id);return found?found->config:nullptr;}
const Perception* CharacterSceneSession::perception(StableId actor)const{const auto* found=impl_->find(actor);return found?found->perception.get():nullptr;}
unsigned CharacterSceneSession::predicted_loadout()const{return impl_->ability_prediction?impl_->ability_prediction->view().loadout:0;}
std::optional<double> CharacterSceneSession::death(StableId actor)const{const auto& p=*impl_;if(actor==p.player_identity)return p.player_death_tick;if(const auto* found=p.find(actor))if(found->pose)return found->pose->death();return std::nullopt;}
const ReactionSample* CharacterSceneSession::reaction(StableId actor)const{const auto& p=*impl_;if(actor==p.player_identity)return p.player_reaction?&*p.player_reaction:nullptr;if(const auto* found=p.find(actor))if(found->pose&&found->pose->reaction())return &*found->pose->reaction();return nullptr;}
const EffectPresentation* CharacterSceneSession::effects(StableId actor)const{for(const auto& [id,view]:impl_->effect_views)if(impl_->client.objects().at(id).id==actor)return view.get();return nullptr;}
}
