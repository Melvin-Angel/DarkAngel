#include <darkangel/scene_authority.hpp>
#include <darkangel/assembly.hpp>
#include "scene_shared.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <iterator>
#include <set>

namespace darkangel {
using scene_detail::require;
struct SceneAuthority::Impl {
    WorldSession server;
    PhysicsWorld authoritative;
    // A party member: its character's authoritative motor and ability owner, the connection that owns it, and this tick's
    // accepted command and action motion. Kept in seat order, which is ascending network identity.
    struct Member {StableId identity;std::uint64_t id{};ConnectionHandle peer;MotorVec foot{};double yaw{};std::unique_ptr<CharacterMotor> motor;AbilityOwnerHandle owner;MotorInput accepted;MotionRequest motion;AbilityOwnerSnapshot before;};
    // A server-driven actor: its scene configuration, authoritative motor and ability owner, the latest controller
    // press and chosen target. Kept in ascending network identity.
    struct Actor {StableId identity;std::uint64_t id{};const CharacterSceneActor* config{};MotorVec foot{};double yaw{};std::uint32_t faction{};std::unique_ptr<CharacterMotor> motor;AbilityOwnerHandle owner;std::uint64_t operation{1};std::optional<AbilityReceipt> receipt;std::optional<StableId> chosen,locked;std::unique_ptr<Perception> perception;std::optional<std::size_t> encounter;std::uint64_t died{};bool recorded{};double hold_x{},hold_z{};std::optional<double> hold_face;};
    std::shared_ptr<const CharacterSceneCombat> combat;std::vector<Member> members;std::vector<StableId> member_identities;std::vector<Actor> actors;std::vector<StableId> actor_identities;std::uint32_t player_faction{1};std::vector<SensedSubject> seen;std::vector<MotorInput> actor_commands;std::vector<MotionRequest> actor_motions;std::vector<AbilityOwnerSnapshot> actor_before;std::vector<char> actor_requested;
    // World events, trigger volumes and encounters; enemies an encounter spawned keep their configuration here.
    WorldEvents world;std::vector<WorldInteractable> interactables;std::vector<WorldTrigger> triggers;std::vector<EncounterDirector> directors;std::vector<StableId> encounter_identities;std::deque<CharacterSceneActor> spawned;struct Entering {std::size_t first{};EncounterSpawn second;unsigned attempts{};};std::deque<Entering> spawn_queue;std::size_t sense_cursor{},decision_cursor{};std::vector<char> turn;std::map<std::string,std::uint32_t> factions;
    std::vector<AbilityCommitUpdate> commitment_updates;
    // Scene transition triggers: a box entered by walking in fires once it has seen itself empty. travel is the party
    // travel raised on the latest tick, until its owner takes it. director_phases: each encounter's phase as last recorded.
    struct Portal {EncounterVolume volume;bool armed{};};std::vector<Portal> portals;std::optional<SceneTransitionRequest> travel;std::vector<EncounterPhase> director_phases;
    CharacterSceneProfile profile;
    // Characters one session holds beside the party.
    std::size_t actor_limit()const{return session_character_capacity-members.size();}
    bool living(std::uint64_t id)const{return server.objects().at(id).health.current>0;}
    // The world ledger and this scene's half of a persistent identity; null when the session records nothing.
    WorldLedger* ledger{};
    PersistentId persistent(StableId object)const{return {combat->scene,object};}
    // The one mechanism by which this scene's objects reach the ledger: each kind reports its logical properties with
    // their default policy, and takes them back before the first tick. Defaults per kind; per-object overrides come with
    // the Inspector's policy editor.
    struct InteractableState final:PersistentObject {WorldInteractable& item;explicit InteractableState(WorldInteractable& held):item(held){}
        std::string_view kind()const override{return "interactable";}
        void capture(LedgerWriter& out)const override{if(item.definition().once&&item.used())out.set("used",std::int64_t(item.used()),{LedgerScope::Save});}
        void restore(const LedgerReader& in)override{if(const auto used=in.integer("used");used&&*used>0)item.restore(unsigned(std::min<std::int64_t>(*used,1000000)));}};
    struct TriggerState final:PersistentObject {WorldTrigger& trigger;explicit TriggerState(WorldTrigger& held):trigger(held){}
        std::string_view kind()const override{return "trigger";}
        void capture(LedgerWriter& out)const override{if(trigger.definition().once&&trigger.fired())out.set("fired",std::int64_t(trigger.fired()),{LedgerScope::Save});}
        void restore(const LedgerReader& in)override{if(const auto fired=in.integer("fired");fired&&*fired>0)trigger.restore(unsigned(std::min<std::int64_t>(*fired,1000000)));}};
    // A completed encounter stays completed until a rest or a party wipe. One that had started is recorded as started and
    // starts again when its scene is built again: a half-fought encounter restarts, it is not resumed mid-wave.
    struct EncounterState final:PersistentObject {EncounterDirector& director;explicit EncounterState(EncounterDirector& held):director(held){}
        std::string_view kind()const override{return "encounter";}
        void capture(LedgerWriter& out)const override{const LedgerPolicy policy{LedgerScope::Save,LedgerReset::Rest|LedgerReset::PartyWipe};const auto phase=director.status().phase;if(phase==EncounterPhase::Complete)out.set("completed",true,policy);else if(phase==EncounterPhase::Running)out.set("started",true,policy);}
        void restore(const LedgerReader& in)override{if(in.flag("completed").value_or(false))director.restore(EncounterPhase::Complete);else if(in.flag("started").value_or(false))director.restore(EncounterPhase::Running);}};
    // A scene actor (a spawn point's NPC, a placed dummy) that was defeated is not created again until a rest or a party wipe.
    struct ActorState final:PersistentObject {bool defeated{};
        std::string_view kind()const override{return "actor";}
        void capture(LedgerWriter& out)const override{if(defeated)out.set("defeated",true,{LedgerScope::Save,LedgerReset::Rest|LedgerReset::PartyWipe});}
        void restore(const LedgerReader& in)override{defeated=in.flag("defeated").value_or(false);}};
    void record_interactable(std::size_t index){if(ledger)ledger->capture(persistent(combat->interactables[index].id),InteractableState(interactables[index]));}
    void record_trigger(std::size_t index){if(ledger)ledger->capture(persistent(combat->triggers[index].id),TriggerState(triggers[index]));}
    void record_encounter(std::size_t index){if(ledger)ledger->capture(persistent(encounter_identities[index]),EncounterState(directors[index]));}
    // Every persistent object of the scene into the ledger. Each fact was already written in the tick it changed, so
    // this changes nothing in a healthy session; it is what an unload calls before the scene is destroyed.
    void capture(){
        if(!ledger)return;
        for(std::size_t index=0;index<interactables.size();++index)record_interactable(index);for(std::size_t index=0;index<triggers.size();++index)record_trigger(index);for(std::size_t index=0;index<directors.size();++index)record_encounter(index);
        for(auto& actor:actors)if(!actor.encounter&&actor.recorded){ActorState state;state.defeated=true;ledger->capture(persistent(actor.identity),state);}
    }
    // The party enters in a fixed formation: the first member on the point, the others on a ring of 1.2 m around it in
    // seat order, starting behind it. Each place is put on the ground under it and, while a scene actor or an earlier
    // member stands there, moved further out.
    MotorVec formation(const SceneArrival& arrival,std::size_t seat,std::span<const MotorVec> taken)const{
        MotorVec fallback=arrival.position;
        for(unsigned attempt=0;attempt<8;++attempt){
            MotorVec at=arrival.position;
            if(seat){const double angle=arrival.yaw+3.141592653589793+2.0943951023931953*double(seat-1),reach=1.2+.6*attempt;at.x+=std::sin(angle)*reach;at.z+=std::cos(angle)*reach;}
            else if(attempt){const double angle=2.399963229728653*attempt,reach=.45*attempt;at.x+=std::sin(angle)*reach;at.z+=std::cos(angle)*reach;}
            const auto found=authoritative.query_ray({at.x,arrival.position.y+2,at.z},{0,-6,0},{StaticCollision,0});const CollisionHit* nearest=nullptr;for(const auto& hit:found.hits)if(!nearest||hit.fraction<nearest->fraction)nearest=&hit;
            if(!nearest)continue;at.y=nearest->point.y;if(!attempt)fallback=at;
            if(std::none_of(taken.begin(),taken.end(),[&](const MotorVec& other){return std::hypot(other.x-at.x,other.z-at.z)<1.&&std::abs(other.y-at.y)<2.5;}))return at;}
        return fallback;
    }
    Impl(SessionHandshake hello,std::span<const ObjectData> objects,std::span<const SceneSeat> party,const CollisionDefinition& collision,std::shared_ptr<const CharacterSceneCombat> configured,const SceneStart& start)
        :server(SessionRole::Server,hello),combat(std::move(configured)){
        require((hello.protocol==2||hello.protocol==3)&&(!combat||hello.protocol==3),"Character scene requires wire2 and the initial human profile");require(!objects.empty()&&objects.size()<=128&&!party.empty()&&party.size()<=scene_party_limit&&party.front().id,"Character scene object/player bounds");
        if(combat&&combat->scene!=AssetId{})ledger=start.ledger;
        for(const auto& seat:party){require(seat.transport&&bool(seat.id)&&std::count_if(party.begin(),party.end(),[&](const auto& other){return other.id==seat.id;})==1,"Scene party seat");server.attach(*seat.transport,seat.peer);Member member;member.identity=seat.id;member.peer=seat.peer;member.foot=seat.position;member.yaw=seat.yaw;members.push_back(std::move(member));member_identities.push_back(seat.id);}
        for(const auto& box:collision.boxes)require(!box.moving&&!box.dynamic&&!box.sensor,"Character scene initial profile requires static collision");
        authoritative.load_scene(collision);
        const auto identity=party.front().id;const ObjectData* first=nullptr;
        if(combat)require(combat->actors.size()<=actor_limit()&&!combat->faction.empty(),"Combat scene actor/faction bounds");
        // A scene actor the ledger holds as defeated is not in this session: neither its object nor its character.
        std::set<StableId> absent;if(ledger)for(const auto& config:combat->actors){ActorState state;ledger->restore(persistent(config.id),state);if(state.defeated)absent.insert(config.id);}
        // With an arrival point the whole party stands in formation around it; otherwise the first member stands where
        // the scene's player object is and the others where their seats say.
        if(start.arrival){require(std::isfinite(start.arrival->position.x)&&std::isfinite(start.arrival->position.y)&&std::isfinite(start.arrival->position.z)&&std::isfinite(start.arrival->yaw),"Scene arrival place");
            std::vector<MotorVec> taken;if(combat)for(const auto& object:objects)if(!absent.contains(object.id)&&std::any_of(combat->actors.begin(),combat->actors.end(),[&](const auto& config){return config.id==object.id;}))taken.push_back({object.transform.x,object.transform.y,object.transform.z});
            for(std::size_t seat=0;seat<members.size();++seat){members[seat].foot=formation(*start.arrival,seat,taken);members[seat].yaw=start.arrival->yaw;taken.push_back(members[seat].foot);}}
        for(const auto& source:objects){if(absent.contains(source.id))continue;auto object=source;
            if(object.id==identity&&start.arrival){object.transform.x=members.front().foot.x;object.transform.y=members.front().foot.y;object.transform.z=members.front().foot.z;object.transform.yaw=members.front().yaw;}
            auto id=server.create(object);if(object.id==identity){require(!members.front().id,"Duplicate character scene player");require(object.transform.scale==1&&object.transform.pitch==0&&object.transform.roll==0&&object.health.current>0,"Character scene requires an upright, unscaled live player");members.front().id=id;members.front().foot={object.transform.x,object.transform.y,object.transform.z};members.front().yaw=object.transform.yaw;first=&source;}
            for(std::size_t seat=1;seat<party.size();++seat)require(object.id!=party[seat].id,"A later party member's identity is a scene object's");
            if(combat)for(const auto& config:combat->actors)if(config.id==object.id){require(object.id!=identity&&std::none_of(actors.begin(),actors.end(),[&](const auto& known){return known.identity==object.id;})&&object.transform.scale==1&&object.health.current>0,"Combat actor profile");Actor actor;actor.identity=object.id;actor.id=id;actor.config=&config;actor.foot={object.transform.x,object.transform.y,object.transform.z};actor.yaw=object.transform.yaw;actors.push_back(std::move(actor));}}
        require(first&&members.front().id,"Character scene player missing");
        // A later member's character is the player object's data at the member's own place.
        for(std::size_t seat=1;seat<members.size();++seat){auto& member=members[seat];require(std::isfinite(member.foot.x)&&std::isfinite(member.foot.y)&&std::isfinite(member.foot.z)&&std::isfinite(member.yaw),"Scene party seat place");ObjectData object;object.id=member.identity;object.transform=first->transform;object.transform.x=member.foot.x;object.transform.y=member.foot.y;object.transform.z=member.foot.z;object.transform.yaw=member.yaw;object.health=first->health;member.id=server.create(object);require(members[seat-1].id<member.id,"A party member's network identity is out of order");}
        if(combat){require(actors.size()+absent.size()==combat->actors.size(),"Combat actor has no scene object");std::sort(actors.begin(),actors.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            // Factions become session-local numbers in simulation order; the party's is 1.
            factions={{combat->faction,player_faction}};for(auto& actor:actors){actor.faction=factions.emplace(actor.config->faction,std::uint32_t(factions.size()+1)).first->second;actor_identities.push_back(actor.identity);}
            for(const auto& member:members)server.assign_team(member.id,player_faction);for(const auto& actor:actors)server.assign_team(actor.id,actor.faction);
            actor_commands.resize(actors.size());actor_motions.resize(actors.size());actor_before.resize(actors.size());actor_requested.resize(actors.size());seen.reserve(actors.size()+members.size());}
        for(auto& member:members)member.motor=std::make_unique<CharacterMotor>(authoritative,member.foot,member.id);
        if(combat){require(combat->kit&&combat->evaluator&&(bool(combat->damage)||bool(combat->combat_damage)),"Combat scene requires frozen kit/game evaluator");validate_combat_kit(*combat->kit,combat->input);for(const auto& ability:combat->abilities)require(ability&&ability->action->motion,"Scene action requires a frozen clip");
            server.bind_melee_query(std::make_shared<PhysicsMeleeQuery>(authoritative));if(combat->combat_damage)server.register_combat_evaluator(combat->evaluator,combat->combat_damage);else server.register_damage_evaluator(combat->evaluator,combat->damage);for(const auto& [id,evaluator]:combat->effect_evaluators)server.register_effect_evaluator(id,evaluator);
            if(!combat->loadouts.empty())require(combat->loadouts.size()==loadout_slot_count&&combat->active_loadout<loadout_slot_count&&combat->loadouts[combat->active_loadout],"Scene loadouts need four slots and an equipped active slot");
            // Every member is equipped from the session's player definition.
            for(auto& member:members){member.owner=server.configure_abilities(member.id,combat->attributes,combat->health,combat->maximum_health);if(combat->tags)server.configure_ability_tags(member.owner,combat->tags);if(combat->loadouts.empty())server.equip_combat_kit(member.owner,combat->kit,combat->input,combat->abilities);else server.configure_ability_loadouts(member.owner,combat->loadouts,combat->active_loadout);}
            // Every actor gets its own motor and ability owner; one with a kit is equipped with it.
            for(auto& actor:actors)arm(actor);
            {require(scene_actor_demand(*combat)<=actor_limit(),"The scene's spawn points and the most its encounters can have alive at once are more actors than one session holds");require(combat->decision_budget>=1&&combat->sense_budget>=1&&combat->spawn_budget>=1,"Time slicing budgets are at least 1");}
            for(const auto& encounter:combat->encounters){for(auto npc:encounter.definition.npcs()){auto found=encounter.npcs.find(npc);require(found!=encounter.npcs.end()&&!found->second.attributes.empty()&&!found->second.faction.empty(),"An encounter's NPC definition is not prepared");for(const auto& ability:found->second.abilities)require(ability&&std::any_of(combat->abilities.begin(),combat->abilities.end(),[&](const auto& known){return known->id==ability->id&&known->generation==ability->generation;}),"An encounter NPC's ability is not in the prepared scene abilities");}
                require(bool(encounter.id)&&std::find(encounter_identities.begin(),encounter_identities.end(),encounter.id)==encounter_identities.end(),"Encounter identity");directors.emplace_back(encounter.definition,encounter.volumes,encounter.points,encounter.id.high^encounter.id.low);encounter_identities.push_back(encounter.id);}
            for(const auto& trigger:combat->triggers)triggers.emplace_back(trigger.definition,trigger.center,trigger.yaw);
            require(combat->interactables.size()<=64,"Scene interactable limit");for(const auto& item:combat->interactables){require(bool(item.id)&&std::count_if(combat->interactables.begin(),combat->interactables.end(),[&](const auto& other){return other.id==item.id;})==1,"Interactable identity");interactables.emplace_back(item.definition,item.position);}
            require(combat->transitions.size()<=16,"Scene transition limit");for(const auto& item:combat->transitions){validate_transition(item.definition);require(bool(item.id)&&std::count_if(combat->transitions.begin(),combat->transitions.end(),[&](const auto& other){return other.id==item.id;})==1&&std::none_of(combat->interactables.begin(),combat->interactables.end(),[&](const auto& other){return other.id==item.id;}),"Scene transition identity");portals.push_back({EncounterVolume{item.center,item.yaw,item.definition.half},false});}
            // Persistent objects take their state from the ledger before the first tick.
            if(ledger){for(std::size_t index=0;index<interactables.size();++index){InteractableState state(interactables[index]);ledger->restore(persistent(combat->interactables[index].id),state);}for(std::size_t index=0;index<triggers.size();++index){TriggerState state(triggers[index]);ledger->restore(persistent(combat->triggers[index].id),state);}for(std::size_t index=0;index<directors.size();++index){EncounterState state(directors[index]);ledger->restore(persistent(encounter_identities[index]),state);}}
            for(const auto& director:directors)director_phases.push_back(director.status().phase);
            world.emit("play.started");
            server.drain_ability_actions();}
        for(auto& member:members){auto state=member.motor->state();state.topology=authoritative.topology();state.yaw=member.yaw;member.motor->restore(state);}
        for(const auto& member:members){server.own_motor(member.id,member.peer);server.publish_motor(member.id,member.motor->state());}
        // Clients are told which single-use interactables are already spent.
        for(std::size_t index=0;index<interactables.size();++index)if(interactables[index].definition().once&&interactables[index].used())for(const auto& member:members)server.answer_interaction(member.id,combat->interactables[index].id,std::uint8_t(InteractionResult::UsedUp));
        server.publish_collision(collision_stream(authoritative.capture()));
    }
    const AbilityDefinition& definition(const AbilityOwnerSnapshot& snapshot)const{return scene_detail::definition(combat.get(),snapshot);}
    MotionRequest motion(const AbilityOwnerSnapshot& snapshot,std::uint64_t from,std::uint64_t to,MotorInput& command)const{
        auto local=ability_motion_between(definition(snapshot),from,to);const auto& v=local.root.translation;auto yaw=command.yaw;command.yaw=std::remainder(command.yaw+local.root.yaw,6.283185307179586);MotionRequest request;request.root={std::cos(yaw)*v[0]+std::sin(yaw)*v[2],v[1],-std::sin(yaw)*v[0]+std::cos(yaw)*v[2]};request.action=snapshot.active->activation;request.lock=local.movement_lock;request.warp=ability_warp_between(definition(snapshot),from,to);return request;
    }
    // Root/lock request of an owner's action across one authoritative tick, including the
    // final partial step of an action that completed on this tick.
    MotionRequest action_motion(const AbilityOwnerSnapshot& before,const AbilityOwnerSnapshot& after,std::span<const AbilityActionUpdate> updates,MotorInput& command,double motor_yaw,bool requested=false)const{
        if(after.active){const bool continuing=before.active&&before.active==after.active;if(definition(after).face_movement)scene_detail::face(command,motor_yaw,!continuing||requested);auto from=continuing?before.action->clock:0;return motion(after,from,after.action->clock,command);}
        if(before.active)for(const auto& update:updates)if(update.handle==*before.active&&update.phase==ActionPhase::Completed){if(definition(before).face_movement)scene_detail::face(command,motor_yaw,false);auto total=std::uint64_t(definition(before).action->duration)*definition(before).action->loops;return motion(before,before.action->clock,total,command);}
        return {};
    }
    // Movement restriction from statuses and death. Authority reads its own state after effects advanced on this tick.
    bool restricted(AbilityOwnerHandle owner,std::uint64_t id)const{
        if(!living(id))return true;
        for(const auto& active:server.ability_effects(owner))if(!active.suppressed)for(const auto& effect:combat->effects)if(effect->id==active.definition&&effect->generation==active.generation&&effect->lock_movement)return true;
        return false;
    }
    // Server side of one actor: senses, authoritative motor, ability owner with its kit. Runs at session start for the scene's
    // actors and during play for an enemy an encounter spawns.
    void arm(Actor& actor){const auto& config=*actor.config;require(!config.attributes.empty(),"Combat actor needs resolved attributes");AttributeSet(config.attributes);
                actor.perception=std::make_unique<Perception>(config.senses);actor.motor=std::make_unique<CharacterMotor>(authoritative,actor.foot,actor.id);auto state=actor.motor->state();state.yaw=actor.yaw;state.tick=state.sequence=authoritative.tick();actor.motor->restore(state);
                actor.owner=server.configure_abilities(actor.id,config.attributes,combat->health,combat->maximum_health);if(combat->tags)server.configure_ability_tags(actor.owner,combat->tags);
                if(config.kit){validate_combat_kit(*config.kit,config.input);for(const auto& ability:config.abilities)require(ability&&std::any_of(combat->abilities.begin(),combat->abilities.end(),[&](const auto& known){return known->id==ability->id&&known->generation==ability->generation;}),"Actor ability is not in the prepared scene abilities");server.equip_combat_kit(actor.owner,config.kit,config.input,config.abilities);}
                else require(config.abilities.empty(),"An actor without a kit cannot carry abilities");
                server.publish_motor(actor.id,actor.motor->state());}
    // An enemy enters the session: a replicated object at the requested place, then the same setup as any actor.
    // False when the place is blocked by a body: nothing is left behind and the caller tries again.
    bool spawn(std::size_t index,const EncounterSpawn& request){
        const auto& encounter=combat->encounters[index];require(actors.size()<actor_limit(),"An encounter spawned more actors than one session holds");
        auto made=encounter.npcs.at(request.npc);made.id=encounter_actor(encounter.id,request.index);made.controller=encounter.controller?encounter.controller(request.npc):nullptr;spawned.push_back(std::move(made));const auto& config=spawned.back();
        ObjectData object;object.id=config.id;object.transform.x=request.position.x;object.transform.y=request.position.y;object.transform.z=request.position.z;object.transform.yaw=request.yaw;
        for(const auto& attribute:config.attributes){if(attribute.id==combat->health)object.health.current=attribute.base;if(attribute.id==combat->maximum_health)object.health.maximum=attribute.base;}
        Actor actor;actor.identity=config.id;actor.id=server.create(object);actor.config=&config;actor.foot=request.position;actor.yaw=request.yaw;actor.encounter=index;require(actors.empty()||actors.back().id<actor.id,"A spawned actor's network identity is out of order");
        actor.faction=factions.emplace(config.faction,std::uint32_t(factions.size()+1)).first->second;server.assign_team(actor.id,actor.faction);
        try{arm(actor);}catch(const std::exception&){actor.motor.reset();server.destroy(actor.id);spawned.pop_back();return false;}
        actors.push_back(std::move(actor));actor_identities.push_back(config.id);actor_commands.resize(actors.size());actor_motions.resize(actors.size());actor_before.resize(actors.size());actor_requested.resize(actors.size());return true;
    }
    // An actor leaves the session: its replicated object, motor and ability owner. Its clients drop what they prepared
    // for it when the object leaves their world.
    void retire(std::size_t position){
        const auto id=actors[position].id;actors.erase(actors.begin()+std::ptrdiff_t(position));actor_identities.erase(actor_identities.begin()+std::ptrdiff_t(position));server.destroy(id);
        actor_commands.resize(actors.size());actor_motions.resize(actors.size());actor_before.resize(actors.size());actor_requested.resize(actors.size());
    }
    // Round robin over the actors that have a controller: up to budget of them get this tick's turn, starting where the last tick stopped.
    void slice(std::size_t& cursor,unsigned budget){
        turn.assign(actors.size(),0);if(actors.empty())return;if(cursor>=actors.size())cursor=0;unsigned given=0;std::size_t last=cursor;
        for(std::size_t step=0;step<actors.size()&&given<budget;++step){const auto index=(cursor+step)%actors.size();if(!actors[index].config->controller)continue;turn[index]=1;++given;last=index;}
        if(given)cursor=(last+1)%actors.size();
    }
    const Member* member(StableId identity)const{for(const auto& known:members)if(known.identity==identity)return &known;return nullptr;}
    const Member* member(std::uint64_t network)const{for(const auto& known:members)if(known.id==network)return &known;return nullptr;}
    // A trigger volume and an encounter's bounds see the party through one member: the first living one inside,
    // otherwise the first member.
    std::pair<MotorVec,bool> witness(std::span<const EncounterVolume> volumes)const{
        for(const auto& known:members)if(living(known.id)&&encounter_contains(volumes,known.motor->state().position))return {known.motor->state().position,true};
        return {members.front().motor->state().position,living(members.front().id)};
    }
    // World step, before sensing: deliver last tick's events, remove the dead whose time is up, let trigger volumes and
    // encounters act on authoritative state, then bring in at most spawn_budget of the enemies that are waiting to enter.
    void run_world(std::uint64_t tick){
        world.begin_tick(tick);
        // Interaction requests, each resolved for the member that sent it from authoritative state. Every request is answered.
        for(const auto& request:server.drain_interactions()){auto result=InteractionResult::Unknown;if(const auto* sender=member(request.network)){
                // A single-use interactable that was just spent is recorded, and the other members' clients are told.
                for(std::size_t index=0;index<interactables.size();++index)if(combat->interactables[index].id==request.object){result=interactables[index].use(world,sender->motor->state().position,living(sender->id));if(result==InteractionResult::Accepted&&interactables[index].definition().once){record_interactable(index);for(const auto& other:members)if(other.id!=sender->id)server.answer_interaction(other.id,request.object,std::uint8_t(InteractionResult::UsedUp));}break;}
                // A scene transition used on purpose is the same request; what it raises is travel for the whole party.
                for(std::size_t index=0;index<portals.size();++index){const auto& item=combat->transitions[index];if(item.id!=request.object||item.definition.activation!=TransitionActivation::Interact)continue;const auto& at=sender->motor->state().position;const double dx=at.x-item.center.x,dy=at.y-item.center.y,dz=at.z-item.center.z;
                    result=!living(sender->id)?InteractionResult::Dead:dx*dx+dy*dy+dz*dz>item.definition.radius*item.definition.radius?InteractionResult::OutOfReach:travel?InteractionResult::Unavailable:InteractionResult::Accepted;if(result==InteractionResult::Accepted)travel=SceneTransitionRequest{item.id,item.definition.scene,item.definition.entry};break;}}
            server.answer_interaction(request.network,request.object,std::uint8_t(result));}
        for(auto& actor:actors)if(actor.encounter&&!actor.died&&!living(actor.id))actor.died=tick;
        for(auto& actor:actors)if(!actor.encounter&&!actor.recorded&&!living(actor.id)){actor.recorded=true;if(ledger){ActorState state;state.defeated=true;ledger->capture(persistent(actor.identity),state);}}
        for(std::size_t position=actors.size();position-->0;){const auto& actor=actors[position];if(!actor.encounter||!actor.died)continue;const auto keep=std::uint64_t(std::llround(combat->encounters[*actor.encounter].definition.corpse_seconds*60));if(keep&&tick-actor.died>=keep)retire(position);}
        for(std::size_t index=0;index<triggers.size();++index){auto& trigger=triggers[index];const auto seen_by=witness({&trigger.volume(),1});if(trigger.step(world,seen_by.first,seen_by.second)&&trigger.definition().once)record_trigger(index);}
        const auto ground=[this](double x,double from,double z)->std::optional<double>{const auto found=authoritative.query_ray({x,from,z},{0,-(std::abs(from)+500),0},{StaticCollision,0});const CollisionHit* nearest=nullptr;for(const auto& hit:found.hits)if(!nearest||hit.fraction<nearest->fraction)nearest=&hit;if(!nearest)return std::nullopt;return nearest->point.y;};
        const auto occupied=[this](MotorVec point){auto near=[&](const MotorVec& at){return std::hypot(at.x-point.x,at.z-point.z)<1.&&std::abs(at.y-point.y)<2.5;};for(const auto& known:members)if(living(known.id)&&near(known.motor->state().position))return true;for(const auto& actor:actors)if(near(actor.motor->state().position))return true;for(const auto& waiting:spawn_queue)if(near(waiting.second.position))return true;return false;};
        for(std::size_t index=0;index<directors.size();++index){unsigned alive=0;for(const auto& actor:actors)if(actor.encounter==index&&living(actor.id))++alive;for(const auto& waiting:spawn_queue)if(waiting.first==index)++alive;
            auto room=[&]{return unsigned(actor_limit()-std::min(actor_limit(),actors.size()+spawn_queue.size()));};
            // A spawn that needs room takes it from the dead, oldest first, before their time is up.
            for(const auto need=directors[index].need();room()<need;){std::size_t oldest=actors.size();for(std::size_t position=0;position<actors.size();++position)if(actors[position].encounter&&actors[position].died&&(oldest==actors.size()||actors[position].died<actors[oldest].died))oldest=position;if(oldest==actors.size())break;retire(oldest);}
            for(const auto& request:directors[index].step(tick,world,witness(directors[index].volumes()).first,alive,ground,occupied,room()))spawn_queue.push_back({index,request,0});
            if(const auto phase=directors[index].status().phase;phase!=director_phases[index]){director_phases[index]=phase;record_encounter(index);}}
        // A transition entered by walking in fires for any living member, once it has seen its box empty: a party that
        // arrives standing inside one is not sent straight back.
        for(std::size_t index=0;index<portals.size();++index){const auto& item=combat->transitions[index];if(item.definition.activation!=TransitionActivation::Enter)continue;bool inside=false;for(const auto& known:members)inside|=living(known.id)&&encounter_contains({&portals[index].volume,1},known.motor->state().position);
            if(!inside)portals[index].armed=true;else if(portals[index].armed&&!travel){travel=SceneTransitionRequest{item.id,item.definition.scene,item.definition.entry};portals[index].armed=false;}}
        for(unsigned entered=0;entered<combat->spawn_budget&&!spawn_queue.empty();++entered){auto next=spawn_queue.front();spawn_queue.pop_front();auto request=next.second;
            // A blocked place is tried again on later ticks, a little further out each time.
            if(next.attempts){const double angle=2.399963229728653*next.attempts,reach=.45*next.attempts;request.position.x+=std::sin(angle)*reach;request.position.z+=std::cos(angle)*reach;}
            if(!spawn(next.first,request)){require(next.attempts<12,"An encounter found no free place to bring an enemy in");++next.attempts;spawn_queue.push_back(next);break;}}
    }
    const Actor* find(StableId identity)const{for(const auto& actor:actors)if(actor.identity==identity)return &actor;return nullptr;}
    // Soft-lock candidates for a party member: every live actor of another faction, from authority's own state.
    std::vector<TargetCandidate> server_candidates()const{std::vector<TargetCandidate> result;for(const auto& actor:actors)if(actor.faction!=player_faction&&living(actor.id))result.push_back({actor.id,actor.motor->state().position});return result;}
    // The motor of the character an actor's controller chose: a party member or another actor.
    const MotorState* locked_motor(const Actor& actor,std::uint64_t& id)const{
        if(!actor.locked)return nullptr;
        if(const auto* chosen=member(*actor.locked)){id=chosen->id;return &chosen->motor->state();}
        if(const auto* other=find(*actor.locked)){id=other->id;return &other->motor->state();}
        return nullptr;
    }
    std::vector<TargetCandidate> locked_candidates(const Actor& actor)const{
        std::vector<TargetCandidate> result;std::uint64_t id{};if(const auto* state=locked_motor(actor,id))if(living(id))result.push_back({id,state->position});
        return result;
    }
    // Shared aim for NPCs: an aimed ability points from its muzzle at the body of the target the controller chose for this action,
    // raised by the drop a projectile will suffer on the way. A projectile is aimed at where the target will be when it arrives:
    // the target's current movement over the ground, held for the flight time, refined twice because the lead changes the distance.
    // An instant shot has no flight time and no lead. The controller only chooses the target and presses the slot.
    void aim_actor(const Actor& actor,const AbilityDefinition& ability,MotorInput& command)const{
        if(!ability.aim||ability.melee.empty())return;const auto candidates=locked_candidates(actor);if(candidates.empty())return;
        const auto& self=actor.motor->state();const auto& goal=candidates.front().position;MotorVec drift{};
        if(ability.aim->projectile){std::uint64_t id{};if(const auto* target=locked_motor(actor,id))drift={target->achieved.x*60,0,target->achieved.z*60};}
        double seconds=0,dx=0,dz=0,level=0;MotorVec muzzle{};
        for(unsigned pass=0;pass<3;++pass){const MotorVec ahead{goal.x+drift.x*seconds,goal.y,goal.z+drift.z*seconds};MotorState turned=self;turned.yaw=std::atan2(ahead.x-self.position.x,ahead.z-self.position.z);muzzle=melee_origin(turned,ability.melee.front());
            dx=ahead.x-muzzle.x;dz=ahead.z-muzzle.z;level=std::hypot(dx,dz);if(level<.3)return;if(!ability.aim->projectile)break;seconds=level/ability.aim->projectile->speed;}
        double height=goal.y+perception_body_height-muzzle.y;if(ability.aim->projectile)height+=.5*ability.aim->projectile->gravity*seconds*seconds;
        command.yaw=std::atan2(dx,dz);command.pitch=std::clamp(std::atan2(height,level),-1.4,1.4);
    }
    // Controller seam: an actor's controller sees a bounded view of authoritative state as it stood before this tick and asks for
    // move, face and a slot press. Activation, cost, cooldown, hits and effects are the ordinary authoritative ability path.
    std::shared_ptr<const AbilityDefinition> actor_ability(const Actor& actor,CombatSlot slot)const{if(!actor.config->kit)return {};const auto wanted=actor.config->kit->slots[static_cast<unsigned>(slot)].ability;if(wanted==AssetId{})return {};for(const auto& ability:actor.config->abilities)if(ability->id==wanted)return ability;return {};}
    // Sensing: every actor perceives the others, party members included, from authoritative state as it stood before this
    // tick, in ascending network identity, with at most one ray per other character against static collision. Nothing
    // from a client is read.
    bool sight(MotorVec from,MotorVec to)const{return authoritative.query_ray(from,{to.x-from.x,to.y-from.y,to.z-from.z},{StaticCollision,0}).hits.empty();}
    void sense(Actor& actor,std::uint64_t tick){
        seen.clear();std::size_t next=0;auto list_members=[&](std::uint64_t before){for(;next<members.size()&&members[next].id<before;++next)seen.push_back({members[next].identity,player_faction,members[next].motor->state(),living(members[next].id)});};
        for(const auto& other:actors){list_members(other.id);if(&other!=&actor)seen.push_back({other.identity,other.faction,other.motor->state(),living(other.id)});}
        list_members(UINT64_MAX);
        actor.perception->sense(tick,actor.motor->state(),living(actor.id),seen,[this](MotorVec from,MotorVec to){return sight(from,to);});
    }
    const PerceivedActor* perceived(const Actor& actor,StableId identity)const{for(const auto& other:actor.perception->actors())if(other.id==identity)return &other;return nullptr;}
    bool drive_controller(Actor& actor,std::uint64_t tick,MotorInput& command){
        ControllerView view;view.tick=tick;view.self=actor.motor->state();view.alive=living(actor.id);view.faction=actor.faction;view.perceived=actor.perception->actors();
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
        actor.hold_x=decision.move_x;actor.hold_z=decision.move_z;actor.hold_face=decision.face&&(!decision.press||pressed)?decision.face:std::optional<double>{};
        validate_motor_input(command);return pressed;
    }
    void step(std::uint64_t tick){
        require(tick==authoritative.tick()+1,"Scene authority tick order");
        auto mark=std::chrono::steady_clock::now();auto lap=[&](double& phase){const auto now=std::chrono::steady_clock::now();phase+=std::chrono::duration<double,std::micro>(now-mark).count();mark=now;};
        server.tick();
        for(auto& known:members){require(server.motor_input_ready(known.id,tick),"Character scene owner command unavailable");known.accepted=server.consume_motor(known.id,tick,known.motor->state().epoch);known.motion={};}
        lap(profile.replication);if(combat)run_world(tick);lap(profile.world);
        for(std::size_t index=0;index<actors.size();++index){const auto& motor=actors[index].motor->state();actor_commands[index]=MotorInput{tick,tick,motor.epoch,0,0,motor.yaw};actor_motions[index]={};}
        if(combat){
            // Controllers decide in ascending network identity from the state before this tick; their presses join the same ability advance as the members' intents.
            slice(sense_cursor,combat->sense_budget);for(std::size_t index=0;index<actors.size();++index)if(turn[index])sense(actors[index],tick);lap(profile.sensing);
            // An actor whose turn it is decides; the others keep moving and facing as they last decided.
            slice(decision_cursor,combat->decision_budget);
            for(std::size_t index=0;index<actors.size();++index){auto& actor=actors[index];actor_requested[index]=false;if(turn[index])actor_requested[index]=drive_controller(actor,tick,actor_commands[index]);else if(actor.config->controller){actor_commands[index].x=actor.hold_x;actor_commands[index].z=actor.hold_z;if(actor.hold_face)actor_commands[index].yaw=std::remainder(*actor.hold_face,6.283185307179586);}}
            lap(profile.decisions);for(auto& known:members)known.before=server.ability_snapshot(known.owner);for(std::size_t index=0;index<actors.size();++index)actor_before[index]=server.ability_snapshot(actors[index].owner);
            server.advance_abilities(tick);auto updates=server.drain_ability_actions();
            {const auto candidates=server_candidates();for(auto& known:members){auto after=server.ability_snapshot(known.owner);known.motion=action_motion(known.before,after,updates,known.accepted,known.motor->state().yaw);if(restricted(known.owner,known.id))scene_detail::restrict(known.accepted,known.motion);scene_detail::lock(known.motion,known.motor->state(),known.accepted,candidates);}}
            for(std::size_t index=0;index<actors.size();++index){auto& actor=actors[index];if(actor.config->kit){const auto now=server.ability_snapshot(actor.owner);if(now.active)aim_actor(actor,definition(now),actor_commands[index]);actor_motions[index]=action_motion(actor_before[index],now,updates,actor_commands[index],actor.motor->state().yaw,bool(actor_requested[index]));}if(restricted(actor.owner,actor.id))scene_detail::restrict(actor_commands[index],actor_motions[index]);
                // Shared soft lock: an NPC's only candidate is the live target its controller chose for this action.
                if(!server.ability_snapshot(actor.owner).active&&!actor_requested[index])actor.locked.reset();
                {const auto candidates=locked_candidates(actor);scene_detail::lock(actor_motions[index],actor.motor->state(),actor_commands[index],candidates);}}}
        lap(profile.abilities);
        for(auto& known:members)known.motor->step(known.accepted,known.motion);for(std::size_t index=0;index<actors.size();++index)actors[index].motor->step(actor_commands[index],actor_motions[index]);authoritative.step();for(auto& known:members)known.motor->post_physics();for(auto& actor:actors)actor.motor->post_physics();authoritative.finish_tick();lap(profile.motors);
        auto frame=authoritative.capture();for(const auto& known:members)server.publish_motor(known.id,known.motor->state());for(const auto& actor:actors)server.publish_motor(actor.id,actor.motor->state());lap(profile.replication);if(combat){server.resolve_ability_hits(tick);commitment_updates=server.drain_ability_commitments();server.drain_effect_outcomes();}lap(profile.hits);server.publish_collision(collision_stream(frame));lap(profile.replication);
        ++profile.ticks;
    }
};
std::size_t scene_actor_demand(const CharacterSceneCombat& combat){std::size_t planned=combat.actors.size();for(const auto& encounter:combat.encounters)planned+=encounter.definition.concurrent();return planned;}
SceneAuthority::SceneAuthority(SessionHandshake hello,std::span<const ObjectData> objects,std::span<const SceneSeat> party,const CollisionDefinition& collision,std::shared_ptr<const CharacterSceneCombat> combat,const SceneStart& start)
    :impl_(std::make_unique<Impl>(std::move(hello),objects,party,collision,std::move(combat),start)){}
SceneAuthority::~SceneAuthority()=default;
void SceneAuthority::step(std::uint64_t tick){impl_->step(tick);}
void SceneAuthority::pump(){impl_->server.tick();}
std::uint64_t SceneAuthority::tick()const{return impl_->authoritative.tick();}
std::optional<SceneTransitionRequest> SceneAuthority::take_transition(){auto asked=impl_->travel;impl_->travel.reset();return asked;}
void SceneAuthority::capture(){impl_->capture();}
void SceneAuthority::remove_collision(std::uint64_t id){impl_->authoritative.remove(id);}
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
std::optional<unsigned> SceneAuthority::interactable_used(StableId id)const{const auto& p=*impl_;for(std::size_t index=0;index<p.interactables.size();++index)if(p.combat->interactables[index].id==id)return p.interactables[index].used();return std::nullopt;}
std::uint64_t SceneAuthority::server_packets(StableId member)const{const auto* found=impl_->member(member);require(found,"Unknown party member");return impl_->server.packets_sent(found->peer);}
std::vector<std::uint64_t> SceneAuthority::server_packet_kinds(StableId member)const{const auto* found=impl_->member(member);require(found,"Unknown party member");return impl_->server.packets_by_kind(found->peer);}
const CharacterSceneProfile& SceneAuthority::profile()const{return impl_->profile;}
}
