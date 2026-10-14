#include <darkangel/world_session.hpp>
#include "ability_state.hpp"
#include "session_wire.hpp"
#include "owner_mailbox.hpp"
#include "ability_wire.hpp"
#include "effect_wire.hpp"
#include <algorithm>
#include <chrono>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>
#include <optional>
#include <tuple>
namespace darkangel {
namespace {
using namespace session_detail;
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
enum class Kind:std::uint64_t { Hello=1,ContentReady,Baseline,Ack,Ready,Resync,MotorCommand,MotorSnapshot,CollisionSnapshot,CollisionAck,AbilityCorrection,AbilityCorrectionAck,AbilityCommand,AbilityReceipt,AbilityPublic,EffectOwner,EffectPublic,Projectiles,MotorBatch,PublicIndex,PublicBatch,MotorRemote,Interact,InteractReceipt,OwnerService };
// Motor states packed into one state packet; three fit the packet limit.
constexpr std::size_t motor_batch_states=3;
// What an observer uses of a character it does not own is 104 bytes (128 on a moving support); nine fit a packet.
constexpr std::size_t motor_remote_states=9;
constexpr std::size_t packet_limit=1000,header_size=56,object_size=104,per_chunk=(packet_limit-header_size)/object_size;
Writer header(Kind kind,const SessionHandshake& h,std::uint64_t revision=0,std::uint64_t index=0,std::uint64_t chunks=0,std::uint64_t total=0){Writer w;w.u64(0x3253454144);w.u64(static_cast<std::uint64_t>(kind));w.u64(h.session_epoch);w.u64(revision);w.u64(index);w.u64(chunks);w.u64(total);return w;}
void write_object(Writer& w,const ObjectData& o){w.u64(o.network.value);w.u64(o.id.high);w.u64(o.id.low);for(double n:{o.transform.yaw,o.transform.x,o.transform.y,o.transform.z,o.transform.pitch,o.transform.roll,o.transform.scale,o.health.maximum,o.health.current})w.number(n);w.u64(0);}
ObjectData read_object(Reader& r){ObjectData o;o.network.value=r.u64();o.id={r.u64(),r.u64()};o.transform={r.number(),r.number(),r.number(),r.number(),r.number(),r.number(),r.number()};o.health={r.number(),r.number()};require(r.u64()==0,"Unsupported object wire schema");return o;}

constexpr unsigned collision_chunk_items=4;
std::size_t collision_items(const CollisionStreamFrame& frame){return frame.boxes.size()+frame.actors.size()+frame.meshes.size();}
void write_collision_item(Writer& w,const CollisionStreamFrame& frame,std::size_t item){
    if(item<frame.boxes.size()){const auto& b=frame.boxes[item];w.u64(1);w.u64(b.id);for(auto value:{b.center,b.half,b.velocity,b.angular})write_vector(w,value);w.number(b.yaw);w.number(b.roll);w.u64(unsigned(b.moving)|unsigned(b.dynamic)*2|unsigned(b.sensor)*4);w.number(b.mass);for(auto n:b.rotation)w.number(n);}
    else if((item-=frame.boxes.size())<frame.actors.size()){const auto& a=frame.actors[item];w.u64(2);w.u64(a.id);w.u64(a.epoch);write_vector(w,a.foot);write_vector(w,a.velocity);w.number(a.yaw);w.u64(a.crouched);}
    else{const auto& mesh=frame.meshes[item-frame.actors.size()];w.u64(3);w.u64(mesh.id);for(auto n:mesh.runtime.bytes)w.bytes.push_back(static_cast<std::byte>(n));for(char c:mesh.signature)w.bytes.push_back(static_cast<std::byte>(c));}
}
void read_collision_item(Reader& r,CollisionStreamFrame& frame){auto kind=r.u64();
    if(kind==1){CollisionBox b;b.id=r.u64();b.center=read_vector(r);b.half=read_vector(r);b.velocity=read_vector(r);b.angular=read_vector(r);b.yaw=r.number();b.roll=r.number();auto flags=r.u64();require(flags<=7,"Collision wire flags");b.moving=flags&1;b.dynamic=flags&2;b.sensor=flags&4;b.mass=r.number();for(auto& n:b.rotation)n=r.number();frame.boxes.push_back(b);}
    else if(kind==2){CollisionActor a;a.id=r.u64();a.epoch=r.u64();a.foot=read_vector(r);a.velocity=read_vector(r);a.yaw=r.number();auto stance=r.u64();require(stance<=1,"Collision wire stance");a.crouched=stance!=0;frame.actors.push_back(a);}
    else if(kind==3){CollisionGeometryReference mesh;mesh.id=r.u64();require(r.bytes.size()-r.at>=80,"Collision wire geometry reference");for(auto& n:mesh.runtime.bytes)n=std::to_integer<unsigned char>(r.bytes[r.at++]);for(unsigned n=0;n<64;++n)mesh.signature.push_back(static_cast<char>(std::to_integer<unsigned char>(r.bytes[r.at++])));frame.meshes.push_back(std::move(mesh));}
    else throw std::runtime_error("Collision wire item kind");
}

}
struct WorldSession::Impl {
    static constexpr std::size_t ability_chunk_bytes=896;
    struct QueuedAbility {AbilityIntent intent;ConnectionHandle peer;bool resync_notified{},expired_bootstrap{};};
    struct AbilitySending {std::uint64_t world_revision{},ability_revision{},highest_operation{};std::vector<std::byte> bytes;unsigned next{};};
    struct AbilityPartial {std::uint64_t world_revision{},ability_revision{},birth{},total{},chunks{};std::map<unsigned,std::vector<std::byte>> parts;};
    struct PublicSending {std::uint64_t world_revision{},ability_revision{},avatar_epoch{};std::vector<std::byte> bytes;unsigned next{};};
    struct PublicPartial {std::uint64_t world_revision{},ability_revision{},avatar_epoch{},birth{},total{},chunks{};std::map<unsigned,std::vector<std::byte>> parts;};
    using EffectKey=std::pair<std::uint64_t,AttributeVisibility>;
    struct EffectSent {std::uint64_t world_revision{},avatar_epoch{};std::vector<EffectReplica> effects;};
    struct EffectSending {std::uint64_t revision{};EffectSent state;std::vector<std::byte> bytes;unsigned next{};};

    SessionRole role;SessionHandshake hello;SessionLimits limits;std::thread::id owner=std::this_thread::get_id();
    std::unique_ptr<World> view;std::map<std::uint64_t,ObjectData> objects;std::uint64_t revision{},next_id{1},clock{};
    const std::chrono::steady_clock::time_point timer_origin=std::chrono::steady_clock::now();
    std::uint64_t timeout_clock()const{return limits.wall_clock_timeouts?std::uint64_t(std::chrono::duration<double>(std::chrono::steady_clock::now()-timer_origin).count()*60):clock;}
    // Server-only projectiles in flight (velocity and gravity per tick), and the public view both roles expose.
    struct Projectile {std::uint64_t id{};AbilityState::PendingHit hit;std::uint64_t source{};MotorVec position{},velocity{};double gravity{},radius{},remaining{};std::uint64_t expires{};};
    std::vector<Projectile> projectiles;std::vector<ProjectileView> projectile_views;std::uint64_t next_projectile{1},projectile_revision{},projectile_tick{};
    // Server-only: actors of one team never damage each other. Absent means no team.
    std::map<std::uint64_t,std::uint32_t> teams;
    std::map<std::uint32_t,StableId> owner_channels;std::vector<OwnerMessage> owner_messages;
    struct Peer {OwnerMailbox owner_mailbox;Transport* transport;ConnectionHandle handle;SessionReadiness state{SessionReadiness::Handshake};std::uint64_t progress{},sent_revision{},acked_revision{};bool greeting{},content_sent{},await_ack{},ready_sent{},collision_required{},ever_ready{};std::size_t next_chunk{},total{},chunks{};std::vector<ObjectData> sending;std::map<std::uint64_t,ObjectData> receiving;std::uint64_t receiving_revision{};std::map<std::uint64_t,std::pair<std::uint64_t,std::uint64_t>> motor_sent;
        std::optional<CollisionStreamFrame> collision_sending;unsigned collision_next{};std::uint64_t collision_revision{},collision_last_tick{},collision_last_revision{};bool collision_sent{};
        std::map<std::uint64_t,AbilitySending> ability_sending;std::map<std::uint64_t,std::pair<std::uint64_t,std::uint64_t>> ability_sent;std::map<std::uint64_t,AbilityPartial> ability_parts;std::deque<std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t>> ability_sent_frames;
        std::map<std::uint64_t,PublicSending> public_sending;std::map<std::uint64_t,std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>> public_sent;std::map<std::uint64_t,PublicPartial> public_parts;
        std::map<EffectKey,EffectSending> effect_sending;std::map<EffectKey,EffectSent> effect_sent;std::map<EffectKey,PublicPartial> effect_parts;EffectKey effect_cursor{};
        std::map<std::uint64_t,std::uint64_t> correction_tick;std::map<std::uint64_t,std::pair<std::uint64_t,std::uint64_t>> correction_motor;std::uint64_t projectile_sent{},packets_sent{};std::array<std::uint64_t,32> packets_by_kind{};std::map<std::uint64_t,std::uint64_t> public_content;std::vector<std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>> index_sent;unsigned packet_work{};CollisionStreamDiagnostics collision_stats;std::optional<std::uint64_t> collision_ack_pending;std::deque<std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>> collision_sent_frames;std::uint64_t collision_partial_birth{};
        std::uint64_t collision_receiving_tick{},collision_receiving_topology{},collision_receiving_total{},collision_receiving_chunks{};std::map<unsigned,std::vector<std::byte>> collision_parts;
    };
    std::map<ConnectionHandle,Peer> peers;
    struct MotorQueue {ConnectionHandle owner;std::map<std::uint64_t,MotorInput> pending;MotorInput last;std::uint64_t consumed_tick{},received_sequence{};};
    std::map<std::uint64_t,MotorQueue> motor_inputs;std::map<std::uint64_t,MotorState> motor_states;std::map<std::uint64_t,AbilityFailure> injected_refusals;
    std::optional<CollisionStreamFrame> latest_collision;std::deque<CollisionStreamFrame> collision_states;
    std::map<std::uint64_t,std::unique_ptr<AbilityState>> abilities;
    std::uint64_t ability_tick{},resolved_hit_tick{};
    std::map<std::uint32_t,CombatEvaluator> damage_evaluators;
    std::map<std::uint32_t,EffectEvaluator> effect_evaluators;std::vector<EffectOutcome> effect_outcomes;
    std::shared_ptr<const MeleeQuery> melee_query;
    std::map<std::uint64_t,AbilityCorrection> ability_corrections;
    std::map<std::uint64_t,AbilityPublicFrame> public_abilities;std::map<std::uint64_t,PublicIndexEntry> public_index;std::set<std::uint64_t> public_resync;
    std::map<std::uint64_t,EffectFrame> owner_effect_frames,public_effect_frames;std::set<EffectKey> effect_resync;
    std::map<std::uint64_t,std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>> ability_ack_pending;
    bool ability_resync{};
    std::map<std::uint64_t,std::map<std::uint64_t,QueuedAbility>> ability_inputs;
    std::map<ConnectionHandle,std::deque<AbilityOperationNotice>> ability_notices;
    std::vector<AbilityOperationNotice> received_ability_notices;
    std::vector<InteractionRequest> interaction_requests;std::map<ConnectionHandle,std::deque<InteractionNotice>> interaction_notices;std::vector<InteractionNotice> received_interaction_notices;
    std::vector<AbilityActionUpdate> ability_actions;
    std::vector<AbilityCommitUpdate> ability_commitments;

    const AbilityState& ability(AbilityOwnerHandle h)const{
        thread();require(role==SessionRole::Server,"Ability API requires server authority");
        auto it=abilities.find(h.network);
        require(h.session_epoch==hello.session_epoch&&view->valid(h.entity)&&it!=abilities.end()&&it->second->owner()==h,"Stale/wrong-domain ability owner");
        return *it->second;
    }
    std::vector<AbilityActionUpdate> queue_actions(std::span<const AbilityActionUpdate> updates)const{
        auto candidate=ability_actions;candidate.insert(candidate.end(),updates.begin(),updates.end());
        require(candidate.size()<=128,"Ability lifecycle queue full; drain or resync");
        std::size_t work{};for(const auto& update:candidate)work+=update.batch.events.size()+update.batch.traversed.size();
        require(work<=2048,"Ability action work queue full; drain or resync");return candidate;
    }
    std::vector<AbilityCommitUpdate> queue_commitments(std::span<AbilityState* const> states)const{
        auto candidate=ability_commitments;for(auto state:states){auto updates=state->take_commitments();candidate.insert(candidate.end(),updates.begin(),updates.end());}require(candidate.size()<=128,"Ability commitment queue full; drain before publication");return candidate;
    }
    std::vector<AbilityCommitUpdate> queue_commitments(const std::map<std::uint64_t,std::unique_ptr<AbilityState>>& states)const{std::vector<AbilityState*> values;for(const auto& [id,state]:states)values.push_back(state.get());return queue_commitments(values);}
    void commit_ability(std::uint64_t id,std::unique_ptr<AbilityState> candidate,std::span<const AbilityActionUpdate> updates){
        auto queue=queue_actions(updates);std::array<AbilityState*,1> states{candidate.get()};auto commitments=queue_commitments(states);auto health=candidate->health();auto& object=objects.at(id);
        if(object.health.maximum!=health.maximum||object.health.current!=health.current){
            view->apply_server_health(candidate->owner().entity,health);object.health=health;++revision;
        }
        abilities.at(id)=std::move(candidate);ability_actions=std::move(queue);ability_commitments=std::move(commitments);
    }

    void release_ability_control(ConnectionHandle peer){
        std::erase_if(owner_messages,[&](const auto& message){return message.source==peer;});
        if(role!=SessionRole::Server)return;std::map<std::uint64_t,std::unique_ptr<AbilityState>> prepared;std::vector<AbilityActionUpdate> updates;
        for(const auto& [id,input]:motor_inputs)if(input.owner==peer&&abilities.contains(id)){auto state=std::make_unique<AbilityState>(*abilities.at(id));auto cleanup=state->disconnect();updates.insert(updates.end(),cleanup.begin(),cleanup.end());prepared.emplace(id,std::move(state));}
        auto queue=queue_actions(updates);auto commitments=queue_commitments(prepared);bool changed=false;
        // Releasing action tags can suppress/remove modifiers and clamp Health.
        // Publish those resource changes with the prepared disconnect cleanup.
        for(const auto& [id,state]:prepared){auto health=state->health();auto& object=objects.at(id);if(object.health.maximum!=health.maximum||object.health.current!=health.current){view->apply_server_health(state->owner().entity,health);object.health=health;changed=true;}}
        for(auto& [id,state]:prepared)abilities.at(id)=std::move(state);ability_actions=std::move(queue);ability_commitments=std::move(commitments);ability_notices.erase(peer);interaction_notices.erase(peer);if(changed)++revision;
        for(auto& [id,input]:ability_inputs)std::erase_if(input,[&](const auto& record){return record.second.peer==peer;});
    }
    Impl(SessionRole r,SessionHandshake h,SessionLimits l):role(r),hello(std::move(h)),limits(l),view(std::make_unique<World>(r==SessionRole::Server?WorldDomain::Server:WorldDomain::ClientPresentation,l.objects)){
        require((r==SessionRole::Server || r==SessionRole::Client) && l.objects>0 && l.objects<=512 && l.peers>0 && l.peers<=4 && l.packets_per_tick>0 && l.packets_per_tick<=32 && l.timeout_ticks>0 && l.collision_timeout_ticks>0 && l.collision_timeout_ticks<=600,"Invalid session role/bounds");validate_session_handshake(hello);
    }
    void thread() const{require(owner==std::this_thread::get_id(),"Session owner thread mismatch");}
    void authority() const{thread();require(role==SessionRole::Server,"Client cannot mutate authoritative state");require(revision<std::numeric_limits<std::uint64_t>::max(),"Session revision exhausted");}
    std::unique_ptr<World> build(const std::map<std::uint64_t,ObjectData>& values) const{auto candidate=std::make_unique<World>(role==SessionRole::Server?WorldDomain::Server:WorldDomain::ClientPresentation,limits.objects);std::set<StableId> ids;for(const auto& [id,o]:values){require(id>0 && o.network.value==id && ids.insert(o.id).second,"Invalid/duplicate replicated identity");candidate->create(o);}return candidate;}
    void pump_owner(Peer& p){
        if(p.state!=SessionReadiness::Ready)return;
        const auto* value=p.owner_mailbox.front();if(!value)return;
        // Despawning/losing ownership must never disclose an old owner's payload.
        if(role==SessionRole::Server){auto owner=motor_inputs.find(value->network);require(owner!=motor_inputs.end()&&owner->second.owner==p.handle,"Queued owner service lost ownership");}
        auto w=header(Kind::OwnerService,hello,value->serial,value->network,(value->bytes.size()+owner_fragment_bytes-1)/owner_fragment_bytes,value->bytes.size());
        w.u64(value->channel);w.u64(value->part);const auto first=value->part*owner_fragment_bytes;
        w.bytes.insert(w.bytes.end(),value->bytes.begin()+first,value->bytes.begin()+std::min(first+owner_fragment_bytes,value->bytes.size()));
        if(send(p,std::move(w)))p.owner_mailbox.sent();
    }
    bool send(Peer& p,Writer w,Delivery delivery=Delivery::ReliableOrdered){if(p.packet_work>=limits.packets_per_tick)return false;if(!p.transport->send(p.handle,delivery,w.bytes))return false;++p.packet_work;++p.packets_sent;{Reader r{w.bytes};r.u64();const auto kind=r.u64();if(kind<p.packets_by_kind.size())++p.packets_by_kind[kind];}return true;}
    void receive(Peer& p,const TransportMessage& m){
        require(m.sender==p.handle && m.bytes.size()<=packet_limit,"Invalid session source/channel/size");Reader r{m.bytes};require(r.u64()==0x3253454144,"Session wire version mismatch");auto kind=static_cast<Kind>(r.u64());require(m.delivery==(kind==Kind::Baseline?Delivery::ReliableBootstrap:((kind==Kind::MotorSnapshot||kind==Kind::CollisionSnapshot||kind==Kind::AbilityCorrection||kind==Kind::AbilityPublic||kind==Kind::EffectOwner||kind==Kind::EffectPublic||kind==Kind::Projectiles||kind==Kind::MotorBatch||kind==Kind::MotorRemote||kind==Kind::PublicIndex||kind==Kind::PublicBatch)?Delivery::UnreliableState:Delivery::ReliableOrdered)),"Session traffic class mismatch");require(r.u64()==hello.session_epoch,"Stale gameplay session epoch");auto rev=r.u64(),index=r.u64(),chunks=r.u64(),total=r.u64();
        if(kind==Kind::Hello){require(rev==0 && index==0 && chunks==0 && total==0 && !p.greeting,"Duplicate/invalid gameplay handshake");require(r.u64()==hello.protocol,"Protocol mismatch");for(const auto* hash:{&hello.schema_hash,&hello.content_hash})for(char c:*hash){require(r.at<r.bytes.size(),"Truncated handshake digest");require(std::to_integer<unsigned char>(r.bytes[r.at++])==static_cast<unsigned char>(c),"Content/schema mismatch");};auto flags=r.u64();require(flags<=1&&(role==SessionRole::Client||flags==0),"Session collision readiness capability");p.collision_required=flags!=0;r.end();p.greeting=true;p.state=SessionReadiness::ContentReady;}
        else if(kind==Kind::ContentReady){require(role==SessionRole::Server && p.greeting && !p.content_sent && rev==0 && index==0 && chunks==0 && total==0,"Unexpected content readiness");r.end();p.content_sent=true;p.state=SessionReadiness::Bootstrap;}
        else if(kind==Kind::Baseline){
            require(role==SessionRole::Client && p.content_sent && total<=limits.objects && chunks==std::max<std::uint64_t>(1,(total+per_chunk-1)/per_chunk) && index<chunks,"Invalid baseline bounds/state");
            require(rev>=revision && (p.next_chunk==0 || (rev==p.receiving_revision && total==p.total && chunks==p.chunks)) && index==p.next_chunk,"Baseline order/revision mismatch");
            if(index==0){p.receiving.clear();p.receiving_revision=rev;p.total=static_cast<std::size_t>(total);p.chunks=static_cast<std::size_t>(chunks);p.state=SessionReadiness::Bootstrap;}
            auto count=std::min<std::uint64_t>(per_chunk,total-index*per_chunk);for(std::size_t i=0;i<count;++i){auto o=read_object(r);require(p.receiving.emplace(o.network.value,o).second,"Duplicate network identity");}r.end();++p.next_chunk;
            if(p.next_chunk==p.chunks){require(p.receiving.size()==total,"Incomplete baseline");auto candidate=build(p.receiving);view=std::move(candidate);objects=std::move(p.receiving);motor_states.clear();collision_states.clear();ability_corrections.clear();public_abilities.clear();public_resync.clear();owner_effect_frames.clear();public_effect_frames.clear();effect_resync.clear();ability_ack_pending.clear();p.ability_parts.clear();p.public_parts.clear();p.effect_parts.clear();p.collision_parts.clear();p.collision_ack_pending.reset();p.collision_stats.needs_resync=p.collision_stats.decoded>0;p.collision_stats.has_ack=false; // A new atomic baseline needs fresh motor state at its revision.
                revision=rev;p.next_chunk=0;p.state=SessionReadiness::CatchUp;p.await_ack=true;}
        }else if(kind==Kind::Ack){require(role==SessionRole::Server && p.await_ack && rev==p.sent_revision && index==0 && chunks==0 && total==0,"Unknown baseline ACK");r.end();p.await_ack=false;p.acked_revision=rev;p.state=SessionReadiness::CatchUp;}
        else if(kind==Kind::Ready){if(role==SessionRole::Client && rev<revision){require(index==0 && chunks==0 && total==0,"Invalid obsolete Ready");r.end();return;}require(role==SessionRole::Client && p.state==SessionReadiness::CatchUp && rev==revision && !p.await_ack && index==0 && chunks==0 && total==0,"Premature Ready");r.end();p.state=SessionReadiness::Ready;p.ever_ready=true;}
        else if(kind==Kind::MotorCommand){require(role==SessionRole::Server&&(p.state==SessionReadiness::Ready||p.ever_ready)&&index&&chunks==0&&total==0&&rev==0,"Invalid motor command state");auto it=motor_inputs.find(index);require(it!=motor_inputs.end()&&it->second.owner==p.handle,"Motor command ownership denied");MotorInput input;input.sequence=r.u64();input.tick=r.u64();input.epoch=r.u64();input.x=r.number();input.z=r.number();input.yaw=r.number();auto jump=r.u64(),low=r.u64();require(jump<=1&&low<=1,"Invalid input flags");input.jump=jump!=0;input.crouch=low!=0;input.pitch=r.number();r.end();validate_motor_input(input);require(input.sequence>0,"Invalid owner sequence");if(p.state!=SessionReadiness::Ready)return;auto& q=it->second;if(latest_collision&&(!p.collision_stats.has_ack||p.collision_stats.ack_topology!=latest_collision->topology))return;if(motor_states.contains(index)&&input.epoch!=motor_states.at(index).epoch)return;if(input.sequence<=q.received_sequence||input.tick<=q.consumed_tick)return;require(input.tick-q.consumed_tick<=8&&q.pending.size()<8,"Motor input lead/work limit");require(!q.pending.contains(input.tick),"Multiple movement commands for one tick");q.pending.emplace(input.tick,input);q.received_sequence=input.sequence;}
        else if(kind==Kind::MotorSnapshot||kind==Kind::MotorBatch){
            // One motor state per packet, or several packed into one; each is accepted by the same rule.
            auto accept=[&](std::uint64_t id,const MotorState& state){accept_motor(rev,id,state);};
            if(kind==Kind::MotorSnapshot){require(role==SessionRole::Client&&index&&chunks==0&&total==0,"Invalid motor snapshot direction/header");auto state=read_motor(r);r.end();accept(index,state);}
            else{require(role==SessionRole::Client&&index==0&&chunks==0&&total>=1&&total<=motor_batch_states,"Invalid motor batch direction/header");std::vector<std::pair<std::uint64_t,MotorState>> states;for(std::uint64_t item=0;item<total;++item){const auto id=r.u64();require(id!=0&&std::none_of(states.begin(),states.end(),[&](const auto& known){return known.first==id;}),"Motor batch identity");states.emplace_back(id,read_motor(r));}r.end();for(const auto& [id,state]:states)accept(id,state);}
        }
        else if(kind==Kind::MotorRemote){
            // Characters this peer does not own, as an observer uses them; the topology they share is in the header.
            require(role==SessionRole::Client&&index&&chunks==0&&total>=1&&total<=motor_remote_states,"Invalid remote motor direction/header");std::vector<std::pair<std::uint64_t,MotorState>> states;
            for(std::uint64_t item=0;item<total;++item){const auto id=r.u64();require(id!=0&&std::none_of(states.begin(),states.end(),[&](const auto& known){return known.first==id;}),"Remote motor identity");MotorState state;state.topology=index;state.tick=r.u64();state.epoch=r.u64();state.action=r.u64();const auto flags=r.u64();require(flags<=7,"Remote motor flags");state.grounded=flags&1;state.crouched=flags&2;state.position=read_vector(r);state.yaw=r.number();state.pitch=r.number();state.achieved=read_vector(r);if(flags&4)state.support_velocity=read_vector(r);validate_motor_state(state);states.emplace_back(id,state);}
            r.end();for(const auto& [id,state]:states)accept_motor(rev,id,state);
        }
        else if(kind==Kind::CollisionSnapshot){
            require(role==SessionRole::Client&&chunks>=1&&chunks<=33&&total<=64+session_character_capacity&&chunks<=std::max<std::uint64_t>(1,total),"Collision snapshot header bounds");
            p.collision_required=true;auto topology=r.u64(),part=r.u64();require(topology&&part<chunks,"Collision snapshot topology/chunk");if(rev!=revision||(p.state!=SessionReadiness::Ready&&p.state!=SessionReadiness::CatchUp)||(!collision_states.empty()&&index<=collision_states.back().tick))return;
            if(!p.collision_parts.empty()&&index<p.collision_receiving_tick)return;
            if(p.collision_parts.empty()||index>p.collision_receiving_tick){if(!p.collision_parts.empty()){++p.collision_stats.superseded;p.collision_stats.needs_resync=true;}p.collision_parts.clear();p.collision_partial_birth=timeout_clock();p.collision_receiving_tick=index;p.collision_receiving_topology=topology;p.collision_receiving_total=total;p.collision_receiving_chunks=chunks;}
            require(topology==p.collision_receiving_topology&&total==p.collision_receiving_total&&chunks==p.collision_receiving_chunks,"Collision snapshot inconsistent chunks");
            std::vector<std::byte> payload(r.bytes.begin()+r.at,r.bytes.end());auto existing=p.collision_parts.find(static_cast<unsigned>(part));if(existing!=p.collision_parts.end()){require(existing->second==payload,"Collision duplicate chunk differs");return;}p.collision_parts.emplace(static_cast<unsigned>(part),std::move(payload));
            if(p.collision_parts.size()==chunks){CollisionStreamFrame frame;frame.tick=index;frame.topology=topology;std::uint64_t read=0;for(unsigned n=0;n<chunks;++n){Reader chunk{p.collision_parts.at(n)};while(chunk.at<chunk.bytes.size()){require(read<total,"Collision snapshot item count");read_collision_item(chunk,frame);++read;}chunk.end();}require(read==total,"Collision snapshot item count");validate_collision_stream(frame);for(const auto& actor:frame.actors)require(objects.contains(actor.id),"Collision actor not replicated");++p.collision_stats.decoded;collision_states.push_back(std::move(frame));while(collision_states.size()>31)collision_states.pop_front();p.collision_parts.clear();}
        }
        else if(kind==Kind::CollisionAck){
            require(role==SessionRole::Server&&chunks==0&&total==0,"Collision ACK direction/header");auto topology=r.u64();r.end();if(rev<revision)return;require(rev==revision&&p.state==SessionReadiness::Ready,"Collision ACK lifecycle revision");
            if(!p.collision_sent_frames.empty()&&index<std::get<0>(p.collision_sent_frames.front()))return;
            auto found=std::find(p.collision_sent_frames.begin(),p.collision_sent_frames.end(),std::tuple{index,rev,topology});require(found!=p.collision_sent_frames.end(),"Collision ACK not sent/retained");if(!p.collision_stats.has_ack||index>p.collision_stats.ack_tick){p.collision_stats.has_ack=true;p.collision_stats.ack_tick=index;p.collision_stats.ack_topology=topology;}
        }
        else if(kind==Kind::OwnerService){
            require(p.state==SessionReadiness::Ready||p.ever_ready,"Owner service before bootstrap");
            const auto channel=r.u64(),part=r.u64();require(channel<=UINT32_MAX&&owner_channels.contains(std::uint32_t(channel)),"Unregistered owner service");
            if(role==SessionRole::Server){auto owner=motor_inputs.find(index);require(owner!=motor_inputs.end()&&owner->second.owner==p.handle,"Owner service ownership denied");}
            else require(objects.contains(index)&&objects.at(index).id==owner_channels.at(std::uint32_t(channel)),"Owner service audience denied");
            auto message=p.owner_mailbox.receive(p.handle,rev,index,std::uint32_t(channel),part,chunks,total,r.bytes.subspan(r.at));
            if(message){require(owner_messages.size()<32,"Owner service inbox exhausted; drain before receive");owner_messages.push_back(std::move(*message));}
        }
        else if(kind==Kind::Interact){
            // A request to use a placed interactable. It names the object and claims nothing; the host resolves it from its own state.
            require(role==SessionRole::Server&&(p.state==SessionReadiness::Ready||p.ever_ready)&&rev==0&&index&&chunks==0&&total==0,"Interaction request direction/readiness");const StableId object{r.u64(),r.u64()};r.end();require(bool(object),"Interaction request object identity");
            auto control=motor_inputs.find(index);require(control!=motor_inputs.end()&&control->second.owner==p.handle,"Interaction request ownership denied");
            if(p.state!=SessionReadiness::Ready){if(interaction_notices[p.handle].size()<64)interaction_notices[p.handle].push_back({index,object,3});return;}if(interaction_requests.size()<16)interaction_requests.push_back({index,object});else if(interaction_notices[p.handle].size()<64)interaction_notices[p.handle].push_back({index,object,3});
        }
        else if(kind==Kind::InteractReceipt){
            require(role==SessionRole::Client&&index&&chunks==0&&total==0,"Interaction receipt direction");InteractionNotice notice;notice.network=index;notice.object=StableId{r.u64(),r.u64()};const auto result=r.u64();r.end();require(bool(notice.object)&&result<=5&&received_interaction_notices.size()<64,"Interaction receipt bounds/backlog");notice.result=std::uint8_t(result);received_interaction_notices.push_back(notice);
        }
        else if(kind==Kind::AbilityCommand){
            require(role==SessionRole::Server&&hello.protocol>=3&&(p.state==SessionReadiness::Ready||p.ever_ready)&&rev==0&&index&&chunks==0&&total==0,"Ability intent direction/profile/readiness");require(r.u64()==1,"Ability intent schema");AbilityIntent intent;intent.network=index;intent.operation=r.u64();intent.tick=r.u64();intent.avatar_epoch=r.u64();intent.grant_generation=r.u64();auto slot=r.u64(),edge=r.u64(),flags=r.u64();r.end();
            require(intent.operation&&intent.tick&&intent.avatar_epoch&&slot<combat_slot_count+loadout_slot_count&&edge<=static_cast<unsigned>(InputEdge::Tapped)&&flags<=3&&intent.tick<=ability_tick+8,"Ability intent bounded identity/lead/flags");intent.slot=static_cast<CombatSlot>(slot);intent.edge=static_cast<InputEdge>(edge);intent.cancelled=flags&1;intent.replace_active=flags&2;
            require(motor_inputs.contains(index)&&motor_inputs.at(index).owner==p.handle&&abilities.contains(index),"Ability intent ownership denied");const bool expired=p.state!=SessionReadiness::Ready||(latest_collision&&(!p.collision_stats.has_ack||p.collision_stats.ack_topology!=latest_collision->topology));
            auto& pending=ability_inputs[index];auto found=pending.find(intent.operation);if(found!=pending.end()){require(found->second.intent==intent&&found->second.peer==p.handle,"Conflicting queued ability operation");return;}require(pending.size()<32,"Ability intent queue exhausted; resynchronize");pending.emplace(intent.operation,QueuedAbility{intent,p.handle,false,expired});
        }
        else if(kind==Kind::AbilityReceipt){
            require(role==SessionRole::Client&&hello.protocol>=3&&index&&chunks==0&&total==0,"Ability receipt direction/profile");require(r.u64()==1,"Ability receipt schema");AbilityOperationNotice notice;notice.network=index;auto& receipt=notice.receipt;receipt.operation=r.u64();receipt.tick=r.u64();receipt.inclusion_revision=r.u64();auto activation=r.u64(),failure=r.u64(),flags=r.u64();r.end();
            require(receipt.operation&&failure<=static_cast<unsigned>(AbilityFailure::TagRequirements)&&flags<=7&&(!(flags&1)||(failure==0&&activation)),"Ability receipt bounds");receipt.failure=static_cast<AbilityFailure>(failure);receipt.committed=flags&1;receipt.duplicate=flags&2;notice.terminal=flags&4;
            require(notice.terminal||receipt.failure==AbilityFailure::HistoryFull,"Nonterminal ability receipt policy");if(!objects.contains(index))return;receipt.handle.owner={view->find(objects.at(index).id),hello.session_epoch,index};receipt.handle.activation=activation;
            require(received_ability_notices.size()<128,"Ability receipt consumer backlog; drain/resynchronize");received_ability_notices.push_back(std::move(notice));if(!received_ability_notices.back().terminal)ability_resync=true;
        }
        else if(kind==Kind::AbilityCorrection){
            require(role==SessionRole::Client&&hello.protocol>=3&&index&&total>0&&total<=8192&&chunks==(total+ability_chunk_bytes-1)/ability_chunk_bytes&&chunks<=10,"Ability correction envelope bounds/profile");
            auto ability_revision=r.u64(),part=r.u64();require(part<chunks,"Ability correction part index");auto first=part*ability_chunk_bytes;auto count=std::min<std::uint64_t>(ability_chunk_bytes,total-first);require(r.bytes.size()-r.at==count,"Ability correction part size");
            if(rev!=revision||!objects.contains(index)||(p.state!=SessionReadiness::Ready&&p.state!=SessionReadiness::CatchUp))return;
            auto current=ability_corrections.find(index);if(current!=ability_corrections.end()&&ability_revision<=current->second.ability.revision)return;
            auto found=p.ability_parts.find(index);if(found!=p.ability_parts.end()&&ability_revision<found->second.ability_revision)return;
            if(found==p.ability_parts.end()||found->second.ability_revision!=ability_revision){require(found!=p.ability_parts.end()||p.ability_parts.size()<4,"Ability correction owner capacity");p.ability_parts[index]={rev,ability_revision,timeout_clock(),total,chunks,{}};}
            auto& partial=p.ability_parts.at(index);require(partial.world_revision==rev&&partial.total==total&&partial.chunks==chunks,"Conflicting ability correction fragments");std::vector<std::byte> bytes(r.bytes.begin()+r.at,r.bytes.end());auto [entry,inserted]=partial.parts.emplace(static_cast<unsigned>(part),bytes);require(inserted||entry->second==bytes,"Conflicting duplicate ability correction fragment");
            if(partial.parts.size()==chunks){std::vector<std::byte> complete;complete.reserve(static_cast<std::size_t>(total));for(unsigned n=0;n<chunks;++n){const auto& piece=partial.parts.at(n);complete.insert(complete.end(),piece.begin(),piece.end());}
                auto correction=session_detail::decode_ability_correction(complete);require(correction.network==index&&correction.session_epoch==hello.session_epoch&&correction.world_revision==rev&&correction.ability.revision==ability_revision,"Ability correction header/content identity");
                double health{},maximum{};for(const auto& attribute:correction.ability.attributes){if(attribute.id==correction.ability.health_attribute)health=attribute.value;if(attribute.id==correction.ability.maximum_health_attribute)maximum=attribute.value;}
                require(health==objects.at(index).health.current&&maximum==objects.at(index).health.maximum,"Ability correction Health/lifecycle baseline mismatch");correction.ability.owner.entity=view->find(objects.at(index).id);if(correction.ability.active)correction.ability.active->owner=correction.ability.owner;
                require(ability_corrections.contains(index)||ability_corrections.size()<4,"Ability correction publication capacity");ability_corrections[index]=std::move(correction);p.ability_parts.erase(index);
                // The owner's motor state travels inside its correction; the motor stream does not repeat it.
                accept_motor(rev,index,ability_corrections.at(index).motor);
            }
        }
        else if(kind==Kind::AbilityPublic){
            require(role==SessionRole::Client&&hello.protocol>=3&&index&&total>0&&total<=4096&&chunks==(total+ability_chunk_bytes-1)/ability_chunk_bytes&&chunks<=5,"Public ability envelope/profile/direction");auto ability_revision=r.u64(),avatar_epoch=r.u64(),part=r.u64();require(avatar_epoch&&part<chunks,"Public ability fragment identity");auto first=part*ability_chunk_bytes;auto count=std::min<std::uint64_t>(ability_chunk_bytes,total-first);require(r.bytes.size()-r.at==count,"Public ability fragment byte count");
            if(rev!=revision||!objects.contains(index)||(p.state!=SessionReadiness::Ready&&p.state!=SessionReadiness::CatchUp))return;
            auto current=public_abilities.find(index);if(current!=public_abilities.end()&&(avatar_epoch<current->second.avatar_epoch||(avatar_epoch==current->second.avatar_epoch&&ability_revision<=current->second.ability.revision)))return;
            auto found=p.public_parts.find(index);if(found!=p.public_parts.end()&&(avatar_epoch<found->second.avatar_epoch||(avatar_epoch==found->second.avatar_epoch&&ability_revision<found->second.ability_revision)))return;
            if(found==p.public_parts.end()||found->second.ability_revision!=ability_revision||found->second.avatar_epoch!=avatar_epoch){require(found!=p.public_parts.end()||p.public_parts.size()<session_character_capacity,"Public ability partial capacity");p.public_parts[index]={rev,ability_revision,avatar_epoch,timeout_clock(),total,chunks,{}};}
            auto& partial=p.public_parts.at(index);require(partial.world_revision==rev&&partial.total==total&&partial.chunks==chunks,"Conflicting public ability fragments");std::vector<std::byte> bytes(r.bytes.begin()+r.at,r.bytes.end());auto [entry,inserted]=partial.parts.emplace(static_cast<unsigned>(part),bytes);require(inserted||entry->second==bytes,"Conflicting duplicate public ability fragment");
            if(partial.parts.size()==chunks){std::vector<std::byte> complete;complete.reserve(static_cast<std::size_t>(total));for(unsigned n=0;n<chunks;++n){const auto& piece=partial.parts.at(n);complete.insert(complete.end(),piece.begin(),piece.end());}publish_public(index,rev,ability_revision,avatar_epoch,complete);p.public_parts.erase(index);}
        }
        else if(kind==Kind::EffectOwner||kind==Kind::EffectPublic){
            require(role==SessionRole::Client&&hello.protocol>=3&&index&&total>0&&total<=16384&&chunks==(total+ability_chunk_bytes-1)/ability_chunk_bytes&&chunks<=19,"Effect frame envelope/profile/direction");auto frame_revision=r.u64(),avatar_epoch=r.u64(),part=r.u64();require(avatar_epoch&&part<chunks,"Effect frame fragment identity");auto first=part*ability_chunk_bytes;auto count=std::min<std::uint64_t>(ability_chunk_bytes,total-first);require(r.bytes.size()-r.at==count,"Effect frame fragment size");
            if(rev!=revision||!objects.contains(index)||(p.state!=SessionReadiness::Ready&&p.state!=SessionReadiness::CatchUp))return;
            auto audience=kind==Kind::EffectOwner?AttributeVisibility::Owner:AttributeVisibility::Public;EffectKey key{index,audience};auto& frames=audience==AttributeVisibility::Owner?owner_effect_frames:public_effect_frames;auto current=frames.find(index);
            if(current!=frames.end()&&(avatar_epoch<current->second.avatar_epoch||(avatar_epoch==current->second.avatar_epoch&&frame_revision<=current->second.revision)))return;
            auto found=p.effect_parts.find(key);if(found!=p.effect_parts.end()&&(avatar_epoch<found->second.avatar_epoch||(avatar_epoch==found->second.avatar_epoch&&frame_revision<found->second.ability_revision)))return;
            if(found==p.effect_parts.end()||found->second.ability_revision!=frame_revision||found->second.avatar_epoch!=avatar_epoch){require(found!=p.effect_parts.end()||p.effect_parts.size()<8,"Effect partial actor/audience capacity");p.effect_parts[key]={rev,frame_revision,avatar_epoch,timeout_clock(),total,chunks,{}};}
            auto& partial=p.effect_parts.at(key);require(partial.world_revision==rev&&partial.total==total&&partial.chunks==chunks,"Conflicting effect fragments");std::vector<std::byte> bytes(r.bytes.begin()+r.at,r.bytes.end());auto [entry,inserted]=partial.parts.emplace(static_cast<unsigned>(part),bytes);require(inserted||entry->second==bytes,"Conflicting duplicate effect fragment");
            if(partial.parts.size()==chunks){std::vector<std::byte> complete;complete.reserve(static_cast<std::size_t>(total));for(unsigned n=0;n<chunks;++n){const auto& piece=partial.parts.at(n);complete.insert(complete.end(),piece.begin(),piece.end());}auto frame=session_detail::decode_effect_frame(complete);require(frame.owner.network==index&&frame.owner.session_epoch==hello.session_epoch&&frame.world_revision==rev&&frame.avatar_epoch==avatar_epoch&&frame.revision==frame_revision&&frame.audience==audience,"Effect frame header/content identity");frame.owner.entity=view->find(objects.at(index).id);require(frames.contains(index)||frames.size()<session_character_capacity,"Effect actor publication capacity");frames[index]=std::move(frame);effect_resync.erase(key);p.effect_parts.erase(key);}
        }
        else if(kind==Kind::AbilityCorrectionAck){
            require(role==SessionRole::Server&&hello.protocol>=3&&index&&chunks==0&&total==0,"Ability correction ACK direction/profile");auto ability_revision=r.u64(),through=r.u64();r.end();if(rev<revision)return;
            require(rev==revision&&p.state==SessionReadiness::Ready&&motor_inputs.contains(index)&&motor_inputs.at(index).owner==p.handle,"Ability correction ACK ownership/lifecycle");auto tuple=std::tuple{index,rev,ability_revision,through};
            auto sent=std::find(p.ability_sent_frames.begin(),p.ability_sent_frames.end(),tuple);if(sent==p.ability_sent_frames.end()){if(!p.ability_sent_frames.empty()&&ability_revision<std::get<2>(p.ability_sent_frames.front()))return;require(false,"Ability correction ACK not sent/retained");}
            require(abilities.contains(index),"Ability correction ACK owner retired");auto snapshot=abilities.at(index)->snapshot();if(through>snapshot.retired_through){auto candidate=std::make_unique<AbilityState>(*abilities.at(index));candidate->retire(through);commit_ability(index,std::move(candidate),{});}
        }
        else if(kind==Kind::PublicBatch){require(role==SessionRole::Client&&hello.protocol>=3&&chunks==0&&index==0&&total>=1&&total<=session_character_capacity,"Public batch direction/bounds");
            // Several whole public ability frames in one packet; each is taken exactly like a frame that arrived on its own.
            struct Entry {std::uint64_t id{},ability_revision{},avatar_epoch{};std::vector<std::byte> bytes;};std::vector<Entry> entries;
            for(std::uint64_t item=0;item<total;++item){Entry entry;entry.id=r.u64();entry.ability_revision=r.u64();entry.avatar_epoch=r.u64();const auto size=r.u64();require(entry.id&&entry.avatar_epoch&&size>0&&size<=r.bytes.size()-r.at,"Public batch entry bounds");entry.bytes.assign(r.bytes.begin()+r.at,r.bytes.begin()+r.at+size);r.at+=size;entries.push_back(std::move(entry));}r.end();
            if(rev!=revision||(p.state!=SessionReadiness::Ready&&p.state!=SessionReadiness::CatchUp))return;
            for(const auto& entry:entries){if(!objects.contains(entry.id))continue;auto current=public_abilities.find(entry.id);if(current!=public_abilities.end()&&(entry.avatar_epoch<current->second.avatar_epoch||(entry.avatar_epoch==current->second.avatar_epoch&&entry.ability_revision<=current->second.ability.revision)))continue;p.public_parts.erase(entry.id);publish_public(entry.id,rev,entry.ability_revision,entry.avatar_epoch,entry.bytes);}}
        else if(kind==Kind::PublicIndex){require(role==SessionRole::Client&&hello.protocol>=3&&chunks==0&&index==0&&total<=session_character_capacity,"Public index direction/bounds");std::vector<std::pair<std::uint64_t,PublicIndexEntry>> entries;for(std::uint64_t item=0;item<total;++item){const auto id=r.u64();PublicIndexEntry entry;entry.tick=r.u64();entry.content=r.u64();require(id!=0,"Public index identity");entries.push_back({id,entry});}r.end();if(rev!=revision)return;for(const auto& [id,entry]:entries){auto& held=public_index[id];if(entry.tick>=held.tick)held=entry;}}
        else if(kind==Kind::Projectiles){require(role==SessionRole::Client&&hello.protocol>=3&&chunks==0&&total<=projectile_capacity,"Projectile frame direction/bounds");std::vector<ProjectileView> frame;for(std::uint64_t item=0;item<total;++item){ProjectileView view;view.id=r.u64();view.position=read_vector(r);view.velocity=read_vector(r);for(double n:{view.position.x,view.position.y,view.position.z,view.velocity.x,view.velocity.y,view.velocity.z})require(std::isfinite(n)&&std::abs(n)<=100000,"Projectile frame value bounds");require(view.id!=0,"Projectile frame identity");frame.push_back(view);}r.end();if(rev!=revision||index<projectile_tick)return;projectile_tick=index;projectile_views=std::move(frame);}
        else throw std::runtime_error("Unapproved client operation/session message");p.progress=timeout_clock();
    }
    void pump_interaction_notices(Peer& p){
        auto found=interaction_notices.find(p.handle);if(found==interaction_notices.end())return;
        while(!found->second.empty()){const auto& notice=found->second.front();auto w=header(Kind::InteractReceipt,hello,0,notice.network);w.u64(notice.object.high);w.u64(notice.object.low);w.u64(notice.result);if(!send(p,std::move(w)))return;found->second.pop_front();}interaction_notices.erase(found);
    }
    void pump_ability_notices(Peer& p){
        auto found=ability_notices.find(p.handle);if(found==ability_notices.end())return;
        while(!found->second.empty()){const auto& notice=found->second.front();const auto& receipt=notice.receipt;auto w=header(Kind::AbilityReceipt,hello,revision,notice.network);w.u64(1);for(auto value:{receipt.operation,receipt.tick,receipt.inclusion_revision,receipt.handle.activation,std::uint64_t(receipt.failure),std::uint64_t(receipt.committed)|std::uint64_t(receipt.duplicate)*2|std::uint64_t(notice.terminal)*4})w.u64(value);if(!send(p,std::move(w)))return;found->second.pop_front();}ability_notices.erase(found);
    }
    // A motor state from the motor stream or from the owner's correction becomes the client's current one for that character when it is newer.
    void accept_motor(std::uint64_t rev,std::uint64_t id,const MotorState& state){
        if(rev!=revision||!objects.contains(id))return;auto it=motor_states.find(id);if(it!=motor_states.end()&&(state.epoch<it->second.epoch||(state.epoch==it->second.epoch&&state.tick<=it->second.tick)))return;
        require(motor_states.size()<session_character_capacity||it!=motor_states.end(),"Motor snapshot capacity");auto transform=objects.at(id).transform;transform.x=state.position.x;transform.y=state.position.y;transform.z=state.position.z;transform.yaw=state.yaw;view->apply_replica_transform(view->find(objects.at(id).id),transform);objects.at(id).transform=transform;motor_states[id]=state;
        // The public frame of the same tick is paired with it.
        auto shown=public_abilities.find(id);if(shown!=public_abilities.end()&&shown->second.avatar_epoch==state.epoch&&shown->second.ability.tick==state.tick)shown->second.motor=state;
    }
    // One complete public ability frame, checked against its envelope and the object it describes, becomes the client's current one.
    void publish_public(std::uint64_t index,std::uint64_t rev,std::uint64_t ability_revision,std::uint64_t avatar_epoch,const std::vector<std::byte>& complete){auto frame=session_detail::decode_ability_public(complete);require(frame.network==index&&frame.session_epoch==hello.session_epoch&&frame.world_revision==rev&&frame.avatar_epoch==avatar_epoch&&frame.ability.revision==ability_revision,"Public ability header/content identity");{auto held=motor_states.find(index);if(held!=motor_states.end())frame.motor=held->second;else{frame.motor.epoch=avatar_epoch;const auto& at=objects.at(index).transform;frame.motor.position={at.x,at.y,at.z};frame.motor.yaw=at.yaw;}}double health{},maximum{};for(const auto& value:frame.ability.attributes){if(value.id==frame.ability.health_attribute)health=value.value;if(value.id==frame.ability.maximum_health_attribute)maximum=value.value;}require(health==objects.at(index).health.current&&maximum==objects.at(index).health.maximum,"Public ability Health/lifecycle baseline mismatch");frame.ability.owner.entity=view->find(objects.at(index).id);if(frame.ability.active)frame.ability.active->owner=frame.ability.owner;require(public_abilities.contains(index)||public_abilities.size()<session_character_capacity,"Public ability publication capacity");public_abilities[index]=std::move(frame);public_resync.erase(index);}
    void pump_abilities(Peer& p){
        if(hello.protocol<3||p.state!=SessionReadiness::Ready)return;
        auto owned=[&](std::uint64_t id){return abilities.contains(id)&&motor_inputs.contains(id)&&motor_inputs.at(id).owner==p.handle;};
        std::erase_if(p.ability_sent,[&](const auto& item){return !owned(item.first);});std::erase_if(p.ability_sending,[&](const auto& item){return !owned(item.first);});
        for(const auto& [id,state]:abilities){auto control=motor_inputs.find(id);if(control==motor_inputs.end()||control->second.owner!=p.handle||!motor_states.contains(id))continue;auto snapshot=state->owner_snapshot();if(snapshot.tick!=motor_states.at(id).tick||!state->pending_hits.empty())continue;
            auto sending=p.ability_sending.find(id);if(sending!=p.ability_sending.end()&&sending->second.world_revision!=revision){p.ability_sending.erase(sending);sending=p.ability_sending.end();}
            if(sending==p.ability_sending.end()){auto sent=p.ability_sent.find(id);if(sent!=p.ability_sent.end()&&sent->second==std::pair{snapshot.revision,revision}&&clock%30!=0)continue;AbilityCorrection correction{id,hello.session_epoch,revision,motor_states.at(id),snapshot};auto bytes=session_detail::encode_ability_correction(correction);p.ability_sending[id]={revision,snapshot.revision,snapshot.highest_operation,std::move(bytes),0};p.correction_tick[id]=snapshot.tick;}
            auto& frame=p.ability_sending.at(id);auto chunks=(frame.bytes.size()+ability_chunk_bytes-1)/ability_chunk_bytes;
            for(;frame.next<chunks;++frame.next){auto w=header(Kind::AbilityCorrection,hello,frame.world_revision,id,chunks,frame.bytes.size());w.u64(frame.ability_revision);w.u64(frame.next);auto first=frame.next*ability_chunk_bytes;w.bytes.insert(w.bytes.end(),frame.bytes.begin()+first,frame.bytes.begin()+std::min(frame.bytes.size(),first+ability_chunk_bytes));require(w.bytes.size()<=packet_limit,"Ability correction packet budget");if(!send(p,std::move(w),Delivery::UnreliableState))return;}
            p.ability_sent[id]={frame.ability_revision,frame.world_revision};auto tuple=std::tuple{id,frame.world_revision,frame.ability_revision,frame.highest_operation};if(p.ability_sent_frames.empty()||p.ability_sent_frames.back()!=tuple)p.ability_sent_frames.push_back(tuple);while(p.ability_sent_frames.size()>64)p.ability_sent_frames.pop_front();p.correction_motor[id]={p.correction_tick[id],frame.world_revision};p.ability_sending.erase(id);
        }
    }
    // A public frame is read with the motor state of its tick: a far character's state, otherwise due every far_interval-th tick, goes out with its frame.
    void pair_motor(Peer& p,std::uint64_t id){auto sent=p.motor_sent.find(id);auto state=motor_states.find(id);if(sent!=p.motor_sent.end()&&state!=motor_states.end()&&sent->second.first!=state->second.tick)p.motor_sent.erase(sent);}
    void pump_public_abilities(Peer& p){
        if(hello.protocol<3||p.state!=SessionReadiness::Ready)return;
        std::erase_if(p.public_sent,[&](const auto& item){return !abilities.contains(item.first);});std::erase_if(p.public_content,[&](const auto& item){return !abilities.contains(item.first);});std::erase_if(p.public_sending,[&](const auto& item){return !abilities.contains(item.first);});
        // Frames that fit a packet together travel together; one is marked sent only when its packet has gone out.
        struct Packed {std::uint64_t id{},ability_revision{},avatar_epoch{},content{};std::vector<std::byte> bytes;};std::vector<Packed> batch;std::size_t batch_bytes=header_size;
        auto flush=[&]{if(batch.empty())return true;auto w=header(Kind::PublicBatch,hello,revision,0,0,batch.size());for(const auto& entry:batch){w.u64(entry.id);w.u64(entry.ability_revision);w.u64(entry.avatar_epoch);w.u64(entry.bytes.size());w.bytes.insert(w.bytes.end(),entry.bytes.begin(),entry.bytes.end());}require(w.bytes.size()<=packet_limit,"Public batch packet budget");if(!send(p,std::move(w),Delivery::UnreliableState))return false;for(const auto& entry:batch){p.public_sent[entry.id]={entry.ability_revision,revision,entry.avatar_epoch};p.public_content[entry.id]=entry.content;pair_motor(p,entry.id);}batch.clear();batch_bytes=header_size;return true;};
        for(const auto& [id,state]:abilities){if(!motor_states.contains(id)||!state->pending_hits.empty())continue;auto snapshot=state->public_snapshot();const auto& motor=motor_states.at(id);if(snapshot.tick!=motor.tick)continue;
            auto sending=p.public_sending.find(id);if(sending!=p.public_sending.end()&&(sending->second.world_revision!=revision||sending->second.avatar_epoch!=motor.epoch)){p.public_sending.erase(sending);sending=p.public_sending.end();}
            if(sending==p.public_sending.end()){const auto content=public_ability_content(snapshot);auto sent=p.public_sent.find(id);auto known=p.public_content.find(id);
                // A frame goes out when its content changed, not when only its clock did; the index tells the client the held frame is current.
                if(sent!=p.public_sent.end()&&std::get<1>(sent->second)==revision&&std::get<2>(sent->second)==motor.epoch&&known!=p.public_content.end()&&known->second==content&&clock%30!=0)continue;auto snapshot_revision=snapshot.revision;AbilityPublicFrame outgoing{id,hello.session_epoch,revision,{},std::move(snapshot)};outgoing.avatar_epoch=motor.epoch;auto bytes=session_detail::encode_ability_public(outgoing);
                if(header_size+32+bytes.size()<=packet_limit){if(batch_bytes+32+bytes.size()>packet_limit&&!flush())return;batch_bytes+=32+bytes.size();batch.push_back({id,snapshot_revision,motor.epoch,content,std::move(bytes)});continue;}
                p.public_content[id]=content;p.public_sending[id]={revision,snapshot_revision,motor.epoch,std::move(bytes),0};}
            auto& frame=p.public_sending.at(id);auto chunks=(frame.bytes.size()+ability_chunk_bytes-1)/ability_chunk_bytes;for(;frame.next<chunks;++frame.next){auto w=header(Kind::AbilityPublic,hello,frame.world_revision,id,chunks,frame.bytes.size());w.u64(frame.ability_revision);w.u64(frame.avatar_epoch);w.u64(frame.next);auto first=frame.next*ability_chunk_bytes;w.bytes.insert(w.bytes.end(),frame.bytes.begin()+first,frame.bytes.begin()+std::min(frame.bytes.size(),first+ability_chunk_bytes));require(w.bytes.size()<=packet_limit,"Public ability packet budget");if(!send(p,std::move(w),Delivery::UnreliableState))return;}p.public_sent[id]={frame.ability_revision,frame.world_revision,frame.avatar_epoch};pair_motor(p,id);p.public_sending.erase(id);
        }
        flush();
    }
    // One packet a tick: every character's public ability tick and content, so a client can tell a held frame is current.
    void pump_public_index(Peer& p){
        if(hello.protocol<3||p.state!=SessionReadiness::Ready)return;std::vector<std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>> entries;
        for(const auto& [id,state]:abilities){if(!motor_states.contains(id)||!state->pending_hits.empty())continue;auto snapshot=state->public_snapshot();if(snapshot.tick!=motor_states.at(id).tick)continue;entries.push_back({id,snapshot.tick,public_ability_content(snapshot)});}
        if(entries.empty()||(entries==p.index_sent&&clock%30!=0))return;require(entries.size()<=session_character_capacity,"Public index capacity");
        auto w=header(Kind::PublicIndex,hello,revision,0,0,entries.size());for(const auto& [id,tick,content]:entries){w.u64(id);w.u64(tick);w.u64(content);}
        require(w.bytes.size()<=packet_limit,"Public index packet budget");if(send(p,std::move(w),Delivery::UnreliableState))p.index_sent=std::move(entries);
    }
    void pump_effects(Peer& p){
        if(hello.protocol<3||p.state!=SessionReadiness::Ready)return;
        auto eligible=[&](const EffectKey& key){return abilities.contains(key.first)&&motor_states.contains(key.first)&&(key.second==AttributeVisibility::Public||(motor_inputs.contains(key.first)&&motor_inputs.at(key.first).owner==p.handle));};
        std::erase_if(p.effect_sent,[&](const auto& item){return !eligible(item.first);});std::erase_if(p.effect_sending,[&](const auto& item){return !eligible(item.first);});
        for(const auto& [id,state]:abilities){if(!motor_states.contains(id)||!state->pending_hits.empty())continue;auto snapshot=state->snapshot();const auto& motor=motor_states.at(id);if(snapshot.tick!=motor.tick)continue;
            for(auto audience:{AttributeVisibility::Owner,AttributeVisibility::Public}){EffectKey key{id,audience};if(!eligible(key))continue;auto sending=p.effect_sending.find(key);if(sending!=p.effect_sending.end()&&(sending->second.state.world_revision!=revision||sending->second.state.avatar_epoch!=motor.epoch)){p.effect_sending.erase(sending);sending=p.effect_sending.end();}if(sending!=p.effect_sending.end())continue;
                std::vector<EffectReplica> effects;for(const auto& effect:state->effects().snapshot(audience))effects.push_back({effect.handle.value,effect.definition,effect.generation,{effect.credit.session_epoch,effect.credit.source_network,effect.credit.activation},effect.start,effect.end,effect.next_period,effect.suppressed});auto sent=p.effect_sent.find(key);if(effects.empty()&&sent==p.effect_sent.end())continue;if(sent!=p.effect_sent.end()&&sent->second.world_revision==revision&&sent->second.avatar_epoch==motor.epoch&&sent->second.effects==effects&&clock%30!=0)continue;
                EffectFrame frame{state->owner(),revision,motor.epoch,snapshot.tick,snapshot.revision,audience,effects};auto bytes=session_detail::encode_effect_frame(frame);p.effect_sending[key]={snapshot.revision,{revision,motor.epoch,std::move(effects)},std::move(bytes),0};
            }
        }
        // At most a quarter of the shared packet budget; round-robin fragments
        // across actors/audiences prevent a dense status set starving another.
        auto quota=std::max<std::size_t>(1,limits.packets_per_tick/4);
        for(std::size_t work=0;work<quota&&!p.effect_sending.empty();++work){auto it=p.effect_sending.upper_bound(p.effect_cursor);if(it==p.effect_sending.end())it=p.effect_sending.begin();auto key=it->first;auto& frame=it->second;auto chunks=(frame.bytes.size()+ability_chunk_bytes-1)/ability_chunk_bytes;auto w=header(key.second==AttributeVisibility::Owner?Kind::EffectOwner:Kind::EffectPublic,hello,frame.state.world_revision,key.first,chunks,frame.bytes.size());w.u64(frame.revision);w.u64(frame.state.avatar_epoch);w.u64(frame.next);auto first=frame.next*ability_chunk_bytes;w.bytes.insert(w.bytes.end(),frame.bytes.begin()+first,frame.bytes.begin()+std::min(frame.bytes.size(),first+ability_chunk_bytes));require(w.bytes.size()<=packet_limit,"Effect fragment packet bound");if(!send(p,std::move(w),Delivery::UnreliableState))return;p.effect_cursor=key;if(++frame.next==chunks){p.effect_sent[key]=std::move(frame.state);p.effect_sending.erase(it);}}
    }
    // The whole public projectile list in one state packet whenever it changed, and periodically while any fly.
    void pump_projectiles(Peer& p){
        if(hello.protocol<3||p.state!=SessionReadiness::Ready||!projectile_revision||(p.projectile_sent==projectile_revision&&(projectile_views.empty()||clock%30!=0)))return;
        auto w=header(Kind::Projectiles,hello,revision,projectile_tick,0,projectile_views.size());for(const auto& view:projectile_views){w.u64(view.id);write_vector(w,view.position);write_vector(w,view.velocity);}
        require(w.bytes.size()<=packet_limit,"Projectile frame packet budget");if(send(p,std::move(w),Delivery::UnreliableState))p.projectile_sent=projectile_revision;
    }
    // Collision items are packed whole, as many as fit a packet: a character entry is 88 bytes and a box 176, so a scene of
    // characters takes far fewer packets than a fixed count per packet sized for the largest item.
    static std::vector<std::size_t> collision_chunk_starts(const CollisionStreamFrame& frame){
        constexpr std::size_t payload=packet_limit-header_size-16;std::vector<std::size_t> starts{0};std::size_t used=0;const auto total=collision_items(frame);
        for(std::size_t item=0;item<total;++item){Writer w;write_collision_item(w,frame,item);if(used&&used+w.bytes.size()>payload){starts.push_back(item);used=0;}used+=w.bytes.size();}
        return starts;
    }
    // The motor state of a character this peer owns, if it owns one.
    const MotorState* owned_motor(const Peer& p)const{for(const auto& [id,queue]:motor_inputs)if(queue.owner==p.handle){auto found=motor_states.find(id);if(found!=motor_states.end())return &found->second;}return nullptr;}
    void pump_collision(Peer& p){
        if(!latest_collision||p.state!=SessionReadiness::Ready)return;for(const auto& actor:latest_collision->actors)if(!objects.contains(actor.id))return;
        if(p.collision_sending&&p.collision_revision!=revision){p.collision_sending.reset();p.collision_next=0;}
        if(!p.collision_sending){if(p.collision_sent&&p.collision_last_tick==latest_collision->tick&&p.collision_last_revision==revision&&clock%30!=0)return;p.collision_sending=*latest_collision;p.collision_revision=revision;p.collision_next=0;
            // Character bodies by reach: a peer that owns a character gets the bodies near it, its own always.
            if(const auto* own=owned_motor(p))std::erase_if(p.collision_sending->actors,[&](const CollisionActor& actor){auto control=motor_inputs.find(actor.id);if(control!=motor_inputs.end()&&control->second.owner==p.handle)return false;const double dx=actor.foot.x-own->position.x,dy=actor.foot.y-own->position.y,dz=actor.foot.z-own->position.z;return std::sqrt(dx*dx+dy*dy+dz*dz)>limits.body_radius;});}
        const auto& frame=*p.collision_sending;auto total=collision_items(frame);const auto starts=collision_chunk_starts(frame);const auto chunks=starts.size();
        for(unsigned work=0;work<limits.packets_per_tick&&p.collision_next<chunks;++work){auto w=header(Kind::CollisionSnapshot,hello,p.collision_revision,frame.tick,chunks,total);w.u64(frame.topology);w.u64(p.collision_next);for(auto item=starts[p.collision_next];item<(p.collision_next+1<chunks?starts[p.collision_next+1]:total);++item)write_collision_item(w,frame,item);require(w.bytes.size()<=packet_limit,"Collision packet encoding budget");auto encoded_bytes=w.bytes.size();if(!send(p,std::move(w),Delivery::UnreliableState))return;++p.collision_stats.sent_packets;p.collision_stats.sent_bytes+=encoded_bytes;++p.collision_next;}
        if(p.collision_next==chunks){p.collision_last_tick=frame.tick;p.collision_last_revision=revision;p.collision_sent=true;auto sent=std::tuple{frame.tick,p.collision_revision,frame.topology};if(p.collision_sent_frames.empty()||p.collision_sent_frames.back()!=sent)p.collision_sent_frames.push_back(sent);while(p.collision_sent_frames.size()>31)p.collision_sent_frames.pop_front();p.collision_sending.reset();}
    }
    void pump(Peer& p){
        if(!p.ready_sent){auto w=header(Kind::Hello,hello);w.u64(hello.protocol);for(const auto* hash:{&hello.schema_hash,&hello.content_hash})for(char c:*hash)w.bytes.push_back(static_cast<std::byte>(c));w.u64(role==SessionRole::Server&&latest_collision.has_value()?1:0);if(send(p,std::move(w)))p.ready_sent=true;return;}if(!p.greeting)return;
        if(role==SessionRole::Client){if(p.state==SessionReadiness::Ready)for(auto it=ability_ack_pending.begin();it!=ability_ack_pending.end();){auto [world_revision,ability_revision,through]=it->second;if(world_revision!=revision){it=ability_ack_pending.erase(it);continue;}auto w=header(Kind::AbilityCorrectionAck,hello,world_revision,it->first);w.u64(ability_revision);w.u64(through);if(!send(p,std::move(w)))break;it=ability_ack_pending.erase(it);}if(p.collision_ack_pending&&p.state==SessionReadiness::Ready){auto frame=std::find_if(collision_states.begin(),collision_states.end(),[&](const auto& value){return value.tick==*p.collision_ack_pending;});require(frame!=collision_states.end(),"Prepared collision ACK expired before send");auto w=header(Kind::CollisionAck,hello,revision,frame->tick);w.u64(frame->topology);if(send(p,std::move(w))){p.collision_stats.has_ack=true;p.collision_stats.ack_tick=frame->tick;p.collision_stats.ack_topology=frame->topology;p.collision_stats.needs_resync=false;p.collision_ack_pending.reset();}}if(!p.content_sent){if(send(p,header(Kind::ContentReady,hello))){p.content_sent=true;p.state=SessionReadiness::Bootstrap;}return;}if(p.await_ack && send(p,header(Kind::Ack,hello,revision)))p.await_ack=false;return;}
        if(!p.content_sent || p.await_ack)return;
        if(p.sending.empty() && p.next_chunk==0 && p.state!=SessionReadiness::Bootstrap && p.acked_revision==revision){if(p.state!=SessionReadiness::Ready && send(p,header(Kind::Ready,hello,revision))){p.state=SessionReadiness::Ready;p.ever_ready=true;}return;}
        if(p.next_chunk==0){p.sending.clear();for(const auto& [id,o]:objects)p.sending.push_back(o);if(p.sent_revision!=revision)p.collision_stats.has_ack=false;p.sent_revision=revision;p.chunks=std::max<std::size_t>(1,(p.sending.size()+per_chunk-1)/per_chunk);p.state=SessionReadiness::Bootstrap;}
        for(std::size_t work=0;work<limits.packets_per_tick && p.next_chunk<p.chunks;++work){auto w=header(Kind::Baseline,hello,p.sent_revision,p.next_chunk,p.chunks,p.sending.size());auto first=p.next_chunk*per_chunk;for(auto i=first;i<std::min(first+per_chunk,p.sending.size());++i)write_object(w,p.sending[i]);if(!send(p,std::move(w),Delivery::ReliableBootstrap))break;++p.next_chunk;p.progress=timeout_clock();}
        if(p.next_chunk==p.chunks){p.await_ack=true;p.next_chunk=0;p.sending.clear();}
    }
};
WorldSession::WorldSession(SessionRole r,SessionHandshake h,SessionLimits l):impl_(std::make_unique<Impl>(r,std::move(h),l)){}
WorldSession::~WorldSession()=default;
void WorldSession::attach(Transport& t,ConnectionHandle h){auto& s=*impl_;s.thread();require(t.limits().payload_bytes>=packet_limit && t.limits().queued_bytes>=packet_limit,"Provider payload/queue too small for session protocol");for(const auto& [handle,peer]:s.peers)require(peer.transport!=&t,"Transport already attached to this session");require(t.valid(h) && h.transport && h.epoch && h.peer && !s.peers.contains(h) && s.peers.size()<s.limits.peers && (s.role==SessionRole::Server || s.peers.empty()),"Invalid session peer");s.peers.emplace(h,Impl::Peer{{},&t,h,SessionReadiness::Handshake,s.timeout_clock()});}
bool WorldSession::attached(ConnectionHandle h)const{impl_->thread();return impl_->peers.contains(h);}
void WorldSession::detach(ConnectionHandle h){impl_->thread();require(impl_->peers.contains(h),"Unknown session peer");impl_->release_ability_control(h);for(auto& [id,q]:impl_->motor_inputs)if(q.owner==h){q.pending.clear();q.last={};q.owner={};}require(impl_->peers.erase(h)==1,"Unknown session peer");if(impl_->role==SessionRole::Client){impl_->objects.clear();impl_->motor_states.clear();impl_->collision_states.clear();impl_->ability_corrections.clear();impl_->public_abilities.clear();impl_->public_resync.clear();impl_->owner_effect_frames.clear();impl_->public_effect_frames.clear();impl_->effect_resync.clear();impl_->ability_ack_pending.clear();impl_->received_ability_notices.clear();impl_->view=impl_->build({});impl_->revision=0;}}
void WorldSession::disconnect(ConnectionHandle h){auto& s=*impl_;s.thread();auto& peer=s.peers.at(h);if(peer.transport->valid(h))peer.transport->disconnect(h);detach(h);}
void WorldSession::tick(){auto& s=*impl_;s.thread();++s.clock;for(auto it=s.peers.begin();it!=s.peers.end();){auto& p=it->second;p.packet_work=0;try{require(p.transport->connected(),"Session peer disconnected");if(!p.collision_parts.empty()&&s.timeout_clock()-p.collision_partial_birth>s.limits.collision_timeout_ticks){p.collision_parts.clear();++p.collision_stats.expired;p.collision_stats.needs_resync=true;}for(auto partial=p.ability_parts.begin();partial!=p.ability_parts.end();)if(s.timeout_clock()-partial->second.birth>s.limits.collision_timeout_ticks){partial=p.ability_parts.erase(partial);s.ability_resync=true;}else ++partial;for(auto partial=p.public_parts.begin();partial!=p.public_parts.end();)if(s.timeout_clock()-partial->second.birth>s.limits.collision_timeout_ticks){s.public_resync.insert(partial->first);partial=p.public_parts.erase(partial);}else ++partial;for(auto partial=p.effect_parts.begin();partial!=p.effect_parts.end();)if(s.timeout_clock()-partial->second.birth>s.limits.collision_timeout_ticks){s.effect_resync.insert(partial->first);partial=p.effect_parts.erase(partial);}else ++partial;for(const auto& m:p.transport->poll(std::min(s.limits.packets_per_tick,p.transport->limits().packets)))s.receive(p,m);s.pump(p);s.pump_owner(p);require(p.state==SessionReadiness::Ready || s.timeout_clock()-p.progress<=s.limits.timeout_ticks,"Bootstrap timeout");if(s.role==SessionRole::Server&&p.state==SessionReadiness::Ready){s.pump_ability_notices(p);s.pump_interaction_notices(p);s.pump_effects(p);s.pump_abilities(p);s.pump_public_abilities(p);s.pump_public_index(p);s.pump_projectiles(p);{std::vector<std::uint64_t> due;
        // Relevance: the peer's own character and those near it are due every tick, the rest every far_interval-th tick. Near ones go first.
        const MotorState* own=s.owned_motor(p);
        std::vector<std::pair<double,std::uint64_t>> ordered;
        for(const auto& [id,state]:s.motor_states){auto sent=p.motor_sent.find(id);if(sent!=p.motor_sent.end()&&sent->second==std::pair{state.tick,s.revision}&&s.clock%30!=0)continue;double apart=0;
            if(own&&own!=&state){const double dx=state.position.x-own->position.x,dy=state.position.y-own->position.y,dz=state.position.z-own->position.z;apart=std::sqrt(dx*dx+dy*dy+dz*dz);
                if(apart>s.limits.relevance_radius&&s.limits.far_interval>1&&sent!=p.motor_sent.end()&&sent->second.second==s.revision&&(state.tick+id)%s.limits.far_interval!=0&&state.tick-sent->second.first<s.limits.far_interval)continue;}
            ordered.push_back({apart,id});}
        std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        // A character this peer owns travels as a full state, unless its correction for this tick already carried it; the others as what an observer uses.
        std::vector<std::uint64_t> remote;for(const auto& entry:ordered){const auto id=entry.second;const auto& state=s.motor_states.at(id);auto control=s.motor_inputs.find(id);
            if(control!=s.motor_inputs.end()&&control->second.owner==p.handle){auto carried=p.correction_motor.find(id);if(carried!=p.correction_motor.end()&&carried->second==std::pair{state.tick,s.revision}){p.motor_sent[id]={state.tick,s.revision};continue;}due.push_back(id);}
            else if(own)remote.push_back(id);else due.push_back(id);}
        // Motor states that are due travel packed, three to a packet, instead of one packet each.
        for(std::size_t first=0;first<due.size();first+=motor_batch_states){const auto count=std::min(motor_batch_states,due.size()-first);auto w=header(Kind::MotorBatch,s.hello,s.revision,0,0,count);for(std::size_t item=0;item<count;++item){w.u64(due[first+item]);write_motor(w,s.motor_states.at(due[first+item]));}require(w.bytes.size()<=packet_limit,"Motor batch packet budget");if(!s.send(p,std::move(w),Delivery::UnreliableState))break;for(std::size_t item=0;item<count;++item)p.motor_sent[due[first+item]]={s.motor_states.at(due[first+item]).tick,s.revision};}
        for(std::size_t first=0;first<remote.size();){auto w=header(Kind::MotorRemote,s.hello,s.revision,s.motor_states.at(remote[first]).topology,0,0);std::size_t count=0;
            for(;first+count<remote.size()&&count<motor_remote_states;++count){const auto id=remote[first+count];const auto& state=s.motor_states.at(id);if(count&&state.topology!=s.motor_states.at(remote[first]).topology)break;const bool carried=state.support_velocity.x!=0||state.support_velocity.y!=0||state.support_velocity.z!=0;if(w.bytes.size()+(carried?128:104)>packet_limit)break;
                w.u64(id);w.u64(state.tick);w.u64(state.epoch);w.u64(state.action);w.u64((state.grounded?1:0)|(state.crouched?2:0)|(carried?4:0));write_vector(w,state.position);w.number(state.yaw);w.number(state.pitch);write_vector(w,state.achieved);if(carried)write_vector(w,state.support_velocity);}
            // The count is known only after packing: it is the header's last field.
            for(unsigned byte=0;byte<8;++byte)w.bytes[48+byte]=static_cast<std::byte>((std::uint64_t(count)>>(byte*8))&255);
            require(count&&w.bytes.size()<=packet_limit,"Remote motor packet budget");if(!s.send(p,std::move(w),Delivery::UnreliableState))break;for(std::size_t item=0;item<count;++item)p.motor_sent[remote[first+item]]={s.motor_states.at(remote[first+item]).tick,s.revision};first+=count;}}s.pump_collision(p);}++it;}catch(...){if(p.transport->valid(p.handle))p.transport->disconnect(p.handle);s.release_ability_control(p.handle);for(auto& [id,q]:s.motor_inputs)if(q.owner==p.handle){q.pending.clear();q.last={};q.owner={};}it=s.peers.erase(it);if(s.role==SessionRole::Client){s.objects.clear();s.motor_states.clear();s.collision_states.clear();s.ability_corrections.clear();s.public_abilities.clear();s.public_index.clear();s.public_resync.clear();s.owner_effect_frames.clear();s.public_effect_frames.clear();s.effect_resync.clear();s.ability_ack_pending.clear();s.received_ability_notices.clear();s.view=s.build({});s.revision=0;}throw;}}}
std::uint64_t WorldSession::create(ObjectData o){auto& s=*impl_;s.authority();require(s.objects.size()<s.limits.objects && s.next_id<std::numeric_limits<std::uint64_t>::max(),"Session identity/capacity exhausted");require(!o.network.value && !o.target && o.scripts.records.empty() && o.scripts.count==0 && o.optional_json.empty(),"Unsupported replicated object fields");o.network.value=s.next_id;auto candidate=s.objects;candidate.emplace(s.next_id,o);s.view->create(o);s.objects=std::move(candidate);++s.revision;return s.next_id++;}
void WorldSession::move(std::uint64_t id,const Transform& t){auto& s=*impl_;s.authority();require(s.objects.contains(id),"Unknown network identity");require(!s.motor_states.contains(id),"Living character movement requires motor publication");auto candidate=s.objects;candidate.at(id).transform=t;s.view->set_transform(s.view->find(candidate.at(id).id),t,Authority::Server);s.objects=std::move(candidate);++s.revision;}
void WorldSession::destroy(std::uint64_t id){
    auto& s=*impl_;s.authority();auto candidate=s.objects;require(candidate.erase(id)==1,"Unknown network identity");s.teams.erase(id);
    std::vector<AbilityActionUpdate> cleanup_updates;auto ability=s.abilities.find(id);std::unique_ptr<AbilityState> retired;
    if(ability!=s.abilities.end()){
        retired=std::make_unique<AbilityState>(*ability->second);auto snapshot=retired->snapshot();
        if(snapshot.active){auto cleanup=retired->cancel(*snapshot.active,AbilityActionReason::Despawned);cleanup_updates=std::move(cleanup.second);}
    }
    std::map<std::uint64_t,std::unique_ptr<AbilityState>> affected;
    for(const auto& [network,old]:s.abilities)if(network!=id){auto state=std::make_unique<AbilityState>(*old);state->source_destroyed(s.hello.session_epoch,id);auto cleanup=state->damage(0);cleanup_updates.insert(cleanup_updates.end(),cleanup.begin(),cleanup.end());affected.emplace(network,std::move(state));}
    auto queue=s.queue_actions(cleanup_updates);std::vector<AbilityState*> states;for(const auto& [network,state]:affected)states.push_back(state.get());if(retired)states.push_back(retired.get());auto commitments=s.queue_commitments(states);
    s.view->destroy(s.view->find(s.objects.at(id).id),Authority::Server);
    for(auto& [network,state]:affected){auto health=state->health();s.view->apply_server_health(state->owner().entity,health);candidate.at(network).health=health;s.abilities.at(network)=std::move(state);}
    s.objects=std::move(candidate);std::erase_if(s.owner_messages,[&](const auto& message){return message.network==id;});s.motor_inputs.erase(id);s.motor_states.erase(id);s.abilities.erase(id);s.ability_inputs.erase(id);s.ability_actions=std::move(queue);s.ability_commitments=std::move(commitments);++s.revision;
}
const World& WorldSession::world() const{impl_->thread();return *impl_->view;}
const std::map<std::uint64_t,ObjectData>& WorldSession::objects() const{impl_->thread();return impl_->objects;}
std::uint64_t WorldSession::revision() const{impl_->thread();return impl_->revision;}
SessionReadiness WorldSession::readiness(ConnectionHandle h) const{impl_->thread();return impl_->peers.at(h).state;}
}

namespace darkangel {
void WorldSession::own_motor(std::uint64_t id,ConnectionHandle peer,std::uint64_t consumed_tick){auto& s=*impl_;s.authority();require(s.objects.contains(id)&&s.peers.contains(peer)&&s.motor_inputs.size()<4&&!s.motor_inputs.contains(id),"Invalid motor ownership");auto& queue=s.motor_inputs[id];queue.owner=peer;queue.consumed_tick=consumed_tick;queue.last.sequence=consumed_tick;}
bool WorldSession::submit_motor(std::uint64_t id,const MotorInput& input){auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.objects.contains(id)&&s.peers.size()==1,"Invalid client motor request");validate_motor_input(input);require(input.sequence>0,"Invalid owner sequence");auto& p=s.peers.begin()->second;require(p.state==SessionReadiness::Ready,"Motor request before Ready");if(!collision_control_ready())return false;auto w=header(Kind::MotorCommand,s.hello,0,id);w.u64(input.sequence);w.u64(input.tick);w.u64(input.epoch);w.number(input.x);w.number(input.z);w.number(input.yaw);w.u64(input.jump?1:0);w.u64(input.crouch?1:0);w.number(input.pitch);return s.send(p,std::move(w));}
bool WorldSession::motor_input_ready(std::uint64_t id,std::uint64_t tick)const{auto& s=*impl_;s.thread();return s.motor_inputs.at(id).pending.contains(tick);}
MotorInput WorldSession::consume_motor(std::uint64_t id,std::uint64_t tick,std::uint64_t epoch){auto& s=*impl_;s.authority();auto& q=s.motor_inputs.at(id);require(tick==q.consumed_tick+1&&epoch>0,"Movement consumes exactly one fixed step");q.consumed_tick=tick;MotorInput result{q.last.sequence,tick,epoch};auto it=q.pending.find(tick);if(it!=q.pending.end()){if(it->second.epoch==epoch)result=it->second;q.pending.erase(it);}for(auto i=q.pending.begin();i!=q.pending.end()&&i->first<tick;)i=q.pending.erase(i);q.last=result;return result;}
void WorldSession::publish_motor(std::uint64_t id,const MotorState& state){auto& s=*impl_;s.authority();require(s.objects.contains(id)&&(s.motor_states.contains(id)||s.motor_states.size()<session_character_capacity),"Invalid motor publication");validate_motor_state(state);if(s.motor_states.contains(id)){auto& old=s.motor_states.at(id);require(state.epoch>old.epoch||(state.epoch==old.epoch&&state.tick>old.tick),"Motor snapshot order");}auto transform=s.objects.at(id).transform;transform.x=state.position.x;transform.y=state.position.y;transform.z=state.position.z;transform.yaw=state.yaw;s.view->set_transform(s.view->find(s.objects.at(id).id),transform,Authority::Server);s.objects.at(id).transform=transform;if(s.motor_states.contains(id)&&state.epoch>s.motor_states.at(id).epoch&&s.motor_inputs.contains(id)){auto& q=s.motor_inputs.at(id);q.pending.clear();q.last={};q.received_sequence=0;}s.motor_states[id]=state;}
const std::map<std::uint64_t,MotorState>& WorldSession::motors()const{impl_->thread();return impl_->motor_states;}
}

namespace darkangel {
void WorldSession::publish_collision(CollisionStreamFrame frame){auto& s=*impl_;s.authority();validate_collision_stream(frame);for(const auto& actor:frame.actors)require(s.objects.contains(actor.id),"Collision actor not replicated");require(!s.latest_collision||frame.tick>s.latest_collision->tick,"Collision publication tick must advance");s.latest_collision=std::move(frame);}
const std::deque<CollisionStreamFrame>& WorldSession::collisions()const{impl_->thread();return impl_->collision_states;}
}

namespace darkangel {
void WorldSession::acknowledge_collision(std::uint64_t tick){auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.peers.size()==1,"Collision ACK requires client");auto& p=s.peers.begin()->second;require(p.state==SessionReadiness::Ready&&std::any_of(s.collision_states.begin(),s.collision_states.end(),[&](const auto& frame){return frame.tick==tick;}),"Collision ACK requires retained complete state");if(p.collision_stats.has_ack&&tick<=p.collision_stats.ack_tick)return;if(!p.collision_ack_pending||tick>*p.collision_ack_pending)p.collision_ack_pending=tick;}
CollisionStreamDiagnostics WorldSession::collision_diagnostics(ConnectionHandle handle)const{auto& s=*impl_;s.thread();const auto& p=s.peers.at(handle);auto stats=p.collision_stats;stats.partial_chunks=p.collision_parts.size();return stats;}
}

namespace darkangel {
bool WorldSession::collision_control_ready()const{auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.peers.size()==1,"Collision control readiness requires client");const auto& p=s.peers.begin()->second;if(p.state!=SessionReadiness::Ready)return false;if(!p.collision_required)return true;return !s.collision_states.empty()&&p.collision_stats.has_ack&&!p.collision_stats.needs_resync&&p.collision_stats.ack_topology==s.collision_states.back().topology;}
}

namespace darkangel {
AbilityOwnerHandle WorldSession::configure_abilities(std::uint64_t id,std::vector<AttributeDefinition> schema,AttributeId health,AttributeId maximum){
    auto& s=*impl_;s.authority();require(s.objects.contains(id)&&!s.abilities.contains(id)&&s.abilities.size()<session_character_capacity,"Ability owner missing/already configured/capacity");
    AbilityOwnerHandle handle{s.view->find(s.objects.at(id).id),s.hello.session_epoch,id};
    auto state=std::make_unique<AbilityState>(handle,s.ability_tick,std::move(schema),health,maximum);auto initial=state->health();
    require(initial.maximum==s.objects.at(id).health.maximum&&initial.current==s.objects.at(id).health.current,"Ability Health must match existing authoritative object");
    s.abilities.emplace(id,std::move(state));return handle;
}
void WorldSession::equip_combat_kit(AbilityOwnerHandle owner,std::shared_ptr<const CombatKitDefinition> kit,const InputProfile& input,std::span<const std::shared_ptr<const AbilityDefinition>> catalogue){
    auto& s=*impl_;s.authority();auto state=std::make_unique<AbilityState>(s.ability(owner));for(const auto& ability:catalogue)if(ability)for(const auto& hit:ability->melee)require(bool(s.melee_query)&&s.damage_evaluators.contains(hit.evaluator),"Missing authoritative query binding/game damage evaluator");auto updates=state->equip(std::move(kit),input,catalogue);s.commit_ability(owner.network,std::move(state),updates);
}
void WorldSession::configure_ability_loadouts(AbilityOwnerHandle owner,std::span<const std::optional<AbilityLoadout>> loadouts,unsigned active){
    auto& s=*impl_;s.authority();auto state=std::make_unique<AbilityState>(s.ability(owner));for(const auto& loadout:loadouts)if(loadout)for(const auto& ability:loadout->abilities)if(ability)for(const auto& hit:ability->melee)require(bool(s.melee_query)&&s.damage_evaluators.contains(hit.evaluator),"Missing authoritative query binding/game damage evaluator");auto updates=state->configure_loadouts(loadouts,active);s.commit_ability(owner.network,std::move(state),updates);
}
AbilityFailure WorldSession::can_activate(const AbilityRequest& request)const{return impl_->ability(request.owner).can_activate(request);}
AbilityReceipt WorldSession::request_ability(const AbilityRequest& request){
    auto& s=*impl_;s.authority();auto state=std::make_unique<AbilityState>(s.ability(request.owner));auto outcome=state->request(request);s.commit_ability(request.owner.network,std::move(state),outcome.second);return outcome.first;
}
void WorldSession::refuse_next_ability(std::uint64_t network,AbilityFailure reason){auto& s=*impl_;s.authority();require(reason!=AbilityFailure::None&&s.abilities.contains(network),"A refusal is injected for a known ability owner, with a reason");s.injected_refusals[network]=reason;}
AbilityFailure WorldSession::cancel_ability(AbilityActivationHandle handle){
    auto& s=*impl_;s.authority();auto state=std::make_unique<AbilityState>(s.ability(handle.owner));auto outcome=state->cancel(handle,AbilityActionReason::Cancelled);s.commit_ability(handle.owner.network,std::move(state),outcome.second);return outcome.first;
}
void WorldSession::advance_abilities(std::uint64_t tick,unsigned rate){
    auto& s=*impl_;s.authority();require(s.ability_tick!=std::numeric_limits<std::uint64_t>::max()&&tick==s.ability_tick+1&&rate<=4*action_tick_units,"Ability simulation must advance one fixed step");
    std::map<std::uint64_t,std::unique_ptr<AbilityState>> prepared;std::vector<AbilityActionUpdate> updates;auto pending=s.ability_inputs;auto notices=s.ability_notices;
    auto effects=s.effect_outcomes;
    // Expiry and periodic effects across all owners precede every activation.
    for(const auto& [id,old]:s.abilities){auto state=std::make_unique<AbilityState>(*old);state->begin_tick(tick);auto results=state->advance_effects([&](auto evaluator){return s.effect_evaluators.at(evaluator);},updates);for(auto& result:results){require(effects.size()<128,"Effect execution queue full; drain before advance");effects.push_back({state->owner(),std::move(result)});}prepared.emplace(id,std::move(state));}
    for(auto& [id,state]:prepared){
        auto input=pending.find(id);if(input!=pending.end())for(auto request=input->second.begin();request!=input->second.end();){auto& queued=request->second;if(queued.intent.tick>tick){++request;continue;}
            if(!s.peers.contains(queued.peer)||!s.motor_inputs.contains(id)||s.motor_inputs.at(id).owner!=queued.peer){request=input->second.erase(request);continue;}
            std::optional<AbilityFailure> rejection;if(!s.motor_states.contains(id)||queued.intent.avatar_epoch!=s.motor_states.at(id).epoch)rejection=AbilityFailure::AvatarMismatch;else if(queued.expired_bootstrap||queued.intent.tick<tick)rejection=AbilityFailure::InputExpired;
            else if(queued.intent.edge==InputEdge::Pressed&&!queued.intent.cancelled)if(const auto injected=s.injected_refusals.find(id);injected!=s.injected_refusals.end()){rejection=injected->second;s.injected_refusals.erase(injected);}
            auto outcome=state->request_wire(queued.intent,rejection);require(outcome.first.failure!=AbilityFailure::OperationConflict,"Changed wire operation payload; resynchronize");updates.insert(updates.end(),outcome.second.begin(),outcome.second.end());
            auto receipt=outcome.first;if(!receipt.operation){receipt.operation=queued.intent.operation;receipt.tick=tick;}
            const bool terminal=receipt.failure!=AbilityFailure::HistoryFull;
            if(terminal||!queued.resync_notified){auto& output=notices[queued.peer];require(output.size()<128,"Ability terminal receipt queue full; drain/resynchronize");output.push_back({id,receipt,terminal});}
            if(terminal)request=input->second.erase(request);else {queued.resync_notified=true;++request;}
        }
        auto batch=state->finish_tick(rate);for(auto& update:batch)if(update.phase!=ActionPhase::Active||!update.batch.events.empty()||!update.batch.traversed.empty())updates.push_back(std::move(update));
    }
    auto queue=s.queue_actions(updates);auto commitments=s.queue_commitments(prepared);bool changed=false;
    for(const auto& [id,state]:prepared){auto health=state->health();auto& object=s.objects.at(id);if(object.health.maximum!=health.maximum||object.health.current!=health.current){s.view->apply_server_health(state->owner().entity,health);object.health=health;changed=true;}}
    s.abilities=std::move(prepared);s.ability_inputs=std::move(pending);s.ability_notices=std::move(notices);s.ability_actions=std::move(queue);s.ability_commitments=std::move(commitments);s.effect_outcomes=std::move(effects);s.ability_tick=tick;if(changed)++s.revision;
}
AbilityOwnerSnapshot WorldSession::ability_snapshot(AbilityOwnerHandle owner)const{return impl_->ability(owner).snapshot();}
double WorldSession::ability_available(AbilityOwnerHandle owner,AttributeId id)const{return impl_->ability(owner).available(id);}
std::vector<AbilityCommitUpdate> WorldSession::drain_ability_commitments(){auto& s=*impl_;s.authority();auto result=std::move(s.ability_commitments);s.ability_commitments.clear();return result;}
std::vector<AbilityActionUpdate> WorldSession::drain_ability_actions(){auto& s=*impl_;s.authority();auto result=std::move(s.ability_actions);s.ability_actions.clear();return result;}
void WorldSession::retire_ability_operations(AbilityOwnerHandle owner,std::uint64_t through){
    auto& s=*impl_;s.authority();auto state=std::make_unique<AbilityState>(s.ability(owner));state->retire(through);s.commit_ability(owner.network,std::move(state),{});
}
}

namespace darkangel {
void WorldSession::configure_ability_tags(AbilityOwnerHandle owner,std::shared_ptr<const TagDictionary> dictionary){auto& s=*impl_;s.authority();auto state=std::make_unique<AbilityState>(s.ability(owner));state->configure_tags(std::move(dictionary));s.commit_ability(owner.network,std::move(state),{});}
void WorldSession::register_effect_evaluator(std::uint32_t id,EffectEvaluator evaluator){auto& s=*impl_;s.authority();require(id&&bool(evaluator)&&s.effect_evaluators.size()<16&&!s.effect_evaluators.contains(id),"Native effect evaluator registration");s.effect_evaluators.emplace(id,std::move(evaluator));}
EffectHandle WorldSession::apply_effect(AbilityOwnerHandle target,AbilityOwnerHandle source,const EffectDefinition& definition,double power,std::uint64_t activation){
 auto& s=*impl_;s.authority();const auto& origin=s.ability(source);require(!activation||activation<origin.snapshot().next_activation,"Invalid effect source activation credit");require(!definition.evaluator||s.effect_evaluators.contains(definition.evaluator),"Missing native effect evaluator");
 auto state=std::make_unique<AbilityState>(s.ability(target));std::vector<AbilityActionUpdate> updates;auto result=state->apply_effect(definition,{s.hello.session_epoch,source.network,activation,power,origin.attribute_values()},definition.evaluator?s.effect_evaluators.at(definition.evaluator):EffectEvaluator{},updates);
 // Reserve the worst simultaneous periodic batch before publishing an effect.
 // The fixed tick has one atomic outcome queue, so later draining cannot split it.
 std::size_t periodic{};for(const auto& [id,current]:s.abilities){const auto& actor=id==target.network?*state:*current;for(const auto& active:actor.effects().snapshot())periodic+=active.next_period!=0;}require(periodic<=128,"Global periodic effect reservation exhausted; remove an effect before applying another");
 auto outcomes=s.effect_outcomes;for(auto& execution:result.second){require(outcomes.size()<128,"Effect outcome queue full; drain before application");outcomes.push_back({target,std::move(execution)});}s.commit_ability(target.network,std::move(state),updates);s.effect_outcomes=std::move(outcomes);return result.first;
}
void WorldSession::remove_effect(AbilityOwnerHandle target,EffectHandle handle){auto& s=*impl_;s.authority();auto state=std::make_unique<AbilityState>(s.ability(target));state->remove_effect(handle);auto cleanup=state->damage(0);s.commit_ability(target.network,std::move(state),cleanup);}
void WorldSession::cleanse_effects(AbilityOwnerHandle target,const TagRequirement& tags){auto& s=*impl_;s.authority();auto state=std::make_unique<AbilityState>(s.ability(target));state->cleanse_effects(tags);auto cleanup=state->damage(0);s.commit_ability(target.network,std::move(state),cleanup);}
void WorldSession::damage(AbilityOwnerHandle target,double amount){auto& s=*impl_;s.authority();require(std::isfinite(amount)&&amount>=0,"Damage amount");auto state=std::make_unique<AbilityState>(s.ability(target));auto cleanup=state->damage(std::min(amount,state->health().current));s.commit_ability(target.network,std::move(state),cleanup);}
void WorldSession::heal(AbilityOwnerHandle target,double amount){auto& s=*impl_;s.authority();require(std::isfinite(amount)&&amount>=0,"Heal amount");auto state=std::make_unique<AbilityState>(s.ability(target));state->heal(amount);s.commit_ability(target.network,std::move(state),{});}
std::vector<EffectSnapshot> WorldSession::ability_effects(AbilityOwnerHandle owner)const{auto result=impl_->ability(owner).effects().snapshot();for(auto& state:result)state.handle.owner=owner;return result;}
std::vector<TagId> WorldSession::ability_tags(AbilityOwnerHandle owner,AttributeVisibility audience)const{return impl_->ability(owner).effects().tags().values(audience);}
std::vector<EffectOutcome> WorldSession::drain_effect_outcomes(){auto& s=*impl_;s.authority();auto results=std::move(s.effect_outcomes);s.effect_outcomes.clear();return results;}
void WorldSession::bind_melee_query(std::shared_ptr<const MeleeQuery> query){auto& s=*impl_;s.authority();require(bool(query)&&!s.melee_query,"Melee query is a single server-owned collision binding");s.melee_query=std::move(query);}
void WorldSession::register_damage_evaluator(std::uint32_t id,DamageEvaluator evaluator){
    require(bool(evaluator),"Missing damage evaluator");register_combat_evaluator(id,[evaluator=std::move(evaluator)](const DamageContext& context){return CombatEvaluation{evaluator(context),{}};});
}
void WorldSession::register_combat_evaluator(std::uint32_t id,CombatEvaluator evaluator){auto& s=*impl_;s.authority();require(id&&bool(evaluator)&&s.damage_evaluators.size()<16&&!s.damage_evaluators.contains(id),"Combat evaluator registration");s.damage_evaluators.emplace(id,std::move(evaluator));}
void WorldSession::assign_team(std::uint64_t id,std::uint32_t team){auto& s=*impl_;s.authority();require(s.objects.contains(id)&&s.teams.size()<=session_character_capacity,"Invalid team assignment");if(team)s.teams[id]=team;else s.teams.erase(id);}
std::vector<DamageResult> WorldSession::resolve_ability_hits(std::uint64_t tick){
    auto& s=*impl_;s.authority();require(tick&&tick==s.ability_tick&&bool(s.melee_query),"Ability hit resolution fixed tick/query binding");if(s.resolved_hit_tick==tick)return {};const auto& query=*s.melee_query;
    std::map<std::uint64_t,std::unique_ptr<AbilityState>> prepared;for(const auto& [id,state]:s.abilities)prepared.emplace(id,std::make_unique<AbilityState>(*state));
    std::vector<AbilityActionUpdate> updates;std::vector<DamageResult> results;std::size_t work{};auto effects=s.effect_outcomes;
    // One hit of one source on one candidate: team and liveness checks, the game evaluator, damage and on-hit effects.
    // Melee targets and projectile impacts both land through here.
    auto flying=s.projectiles;
    auto strike=[&](std::uint64_t id,AbilityState& source,const AbilityState::PendingHit& hit,const MeleeTarget& target){if(target.network==id||!prepared.contains(target.network)||!s.motor_states.contains(target.network))return;
                // No friendly fire: a hit never lands on an actor of the attacker's own team.
                if(auto mine=s.teams.find(id),theirs=s.teams.find(target.network);mine!=s.teams.end()&&theirs!=s.teams.end()&&mine->second==theirs->second)return;
                auto& victim=*prepared.at(target.network);auto& motor=s.motor_states.at(target.network);
                if(motor.tick!=tick||motor.epoch!=target.epoch||!query.matches(target.network,motor)||victim.health().current<=0)return;
                if(!source.remember_hit(hit,target.network,target.epoch))return;
                auto source_attributes=source.attribute_values(),target_attributes=victim.attribute_values();
                DamageContext context{hit.handle,victim.owner(),tick,hit.interval.block,hit.interval.loop,hit.profile.damage_type,hit.profile.power,source_attributes,target_attributes,&source.effects().tags(),&victim.effects().tags(),hit.definition->id};
                auto evaluation=s.damage_evaluators.at(hit.profile.evaluator)(context);require(std::isfinite(evaluation.damage)&&evaluation.damage>=0&&evaluation.damage<=1e9&&evaluation.effects.size()<=8,"Game combat evaluator output bounds");
                for(auto& request:evaluation.effects){require(request.definition&&std::isfinite(request.power)&&request.power>=0&&request.power<=1e9&&(!request.definition->evaluator||s.effect_evaluators.contains(request.definition->evaluator)),"Game combat effect request bounds/evaluator");request.definition=victim.prepare_effect(*request.definition);}
                auto before=victim.health().current;auto death=victim.damage(evaluation.damage);updates.insert(updates.end(),death.begin(),death.end());auto direct_after=victim.health().current;
                // On-hit statuses target surviving actors only. Every request is
                // prepared in this same batch; no damage or ledger publishes early.
                for(const auto& request:evaluation.effects){if(victim.health().current<=0)break;std::vector<AbilityActionUpdate> cleanup;auto applied=victim.apply_effect(*request.definition,{s.hello.session_epoch,id,hit.handle.activation,request.power,source_attributes},request.definition->evaluator?s.effect_evaluators.at(request.definition->evaluator):EffectEvaluator{},cleanup);updates.insert(updates.end(),cleanup.begin(),cleanup.end());for(auto& outcome:applied.second){require(effects.size()<128,"Combat effect execution queue full; drain before resolution");effects.push_back({victim.owner(),std::move(outcome)});}}
                auto after=victim.health().current;require(results.size()<128,"Damage result batch bound");results.push_back({hit.handle,victim.owner(),tick,hit.interval.block,hit.interval.loop,hit.profile.damage_type,before,after,before-direct_after,after<=0});
    };
    // Stable owner/activation/block/loop/target order, independent of query insertion.
    for(auto& [id,source]:prepared){auto pending=source->pending_hits;
        std::sort(pending.begin(),pending.end(),[](const auto& a,const auto& b){return std::tie(a.handle.activation,a.interval.block,a.interval.loop,a.interval.from)<std::tie(b.handle.activation,b.interval.block,b.interval.loop,b.interval.from);});
        for(const auto& hit:pending){
            if(source->health().current<=0)break;
            require(hit.tick==tick&&s.motor_states.contains(id)&&s.motor_states.at(id).tick==tick&&query.matches(id,s.motor_states.at(id)),"Melee requires current achieved authoritative motor pose");
            // A projectile ability launches once per hit block and loop; the projectile resolves its own hit on a later tick.
            if(hit.definition->aim&&hit.definition->aim->projectile){if(source->remember_hit(hit,0,0)&&flying.size()<projectile_capacity){const auto& shot=*hit.definition->aim->projectile;const auto& motor=s.motor_states.at(id);const double level=std::cos(motor.pitch)*shot.speed/60;
                    flying.push_back({s.next_projectile++,hit,id,melee_origin(motor,hit.profile),{std::sin(motor.yaw)*level,std::sin(motor.pitch)*shot.speed/60,std::cos(motor.yaw)*level},shot.gravity/3600,hit.profile.radius,hit.definition->aim->range,tick+shot.lifetime});}continue;}
            auto candidates=query.query(id,s.motor_states.at(id),hit.profile);require(!candidates.overflow&&candidates.targets.size()<=16,"Melee query overflow");
            std::sort(candidates.targets.begin(),candidates.targets.end());candidates.targets.erase(std::unique(candidates.targets.begin(),candidates.targets.end()),candidates.targets.end());
            for(const auto& target:candidates.targets){
                require(++work<=256,"Melee batch work bound");strike(id,*source,hit,target);
            }
        }
        source->pending_hits.clear();
    }
    // Projectiles in flight take one swept step each, in launch order. The first character in the path takes the hit through
    // the same evaluation as a melee hit; static collision, the aim range or the lifetime ends the flight.
    for(auto shot=flying.begin();shot!=flying.end();){bool spent=!prepared.contains(shot->source);
        if(!spent){shot->velocity.y-=shot->gravity;MotorVec delta=shot->velocity;double length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);if(length>shot->remaining&&length>1e-9){const double scale=shot->remaining/length;delta={delta.x*scale,delta.y*scale,delta.z*scale};length=shot->remaining;}
            const auto traced=query.trace(shot->position,delta,shot->radius,shot->source);
            if(traced.target){require(++work<=256,"Melee batch work bound");strike(shot->source,*prepared.at(shot->source),shot->hit,*traced.target);spent=true;}
            else if(traced.blocked)spent=true;
            else{shot->position={shot->position.x+delta.x,shot->position.y+delta.y,shot->position.z+delta.z};shot->remaining-=length;spent=shot->remaining<=1e-6||tick>=shot->expires;}}
        shot=spent?flying.erase(shot):shot+1;}
    std::size_t periodic{};for(const auto& [id,state]:prepared)for(const auto& effect:state->effects().snapshot())periodic+=effect.next_period!=0;require(periodic<=128,"Combat global periodic effect reservation exhausted");
    auto queue=s.queue_actions(updates);auto commitments=s.queue_commitments(prepared);bool changed=false;
    // Preparation, evaluator calls and all bounded queue checks completed first.
    for(const auto& [id,state]:prepared){auto health=state->health();auto& object=s.objects.at(id);if(object.health.current!=health.current||object.health.maximum!=health.maximum){s.view->apply_server_health(state->owner().entity,health);object.health=health;changed=true;}}
    s.abilities=std::move(prepared);s.ability_actions=std::move(queue);s.ability_commitments=std::move(commitments);s.effect_outcomes=std::move(effects);s.resolved_hit_tick=tick;if(changed)++s.revision;
    if(!flying.empty()||!s.projectiles.empty()){s.projectiles=std::move(flying);s.projectile_views.clear();for(const auto& shot:s.projectiles)s.projectile_views.push_back({shot.id,shot.position,{shot.velocity.x*60,shot.velocity.y*60,shot.velocity.z*60}});++s.projectile_revision;s.projectile_tick=tick;}
    return results;
}
}

namespace darkangel {
std::uint64_t WorldSession::packets_sent(ConnectionHandle peer)const{impl_->thread();auto found=impl_->peers.find(peer);require(found!=impl_->peers.end(),"Unknown session peer");return found->second.packets_sent;}
const std::vector<ProjectileView>& WorldSession::projectiles()const{impl_->thread();return impl_->projectile_views;}
const std::map<std::uint64_t,AbilityCorrection>& WorldSession::ability_corrections()const{impl_->thread();return impl_->ability_corrections;}
const std::map<std::uint64_t,AbilityPublicFrame>& WorldSession::public_abilities()const{impl_->thread();return impl_->public_abilities;}
const std::map<std::uint64_t,PublicIndexEntry>& WorldSession::public_index()const{impl_->thread();return impl_->public_index;}
std::vector<std::uint64_t> WorldSession::packets_by_kind(ConnectionHandle peer)const{impl_->thread();auto found=impl_->peers.find(peer);require(found!=impl_->peers.end(),"Unknown session peer");return {found->second.packets_by_kind.begin(),found->second.packets_by_kind.end()};}
std::uint64_t public_ability_content(const AbilityPublicSnapshot& snapshot){
    std::uint64_t hash=1469598103934665603ull;auto mix=[&](std::uint64_t value){for(unsigned byte=0;byte<8;++byte){hash^=(value>>(byte*8))&0xff;hash*=1099511628211ull;}};
    mix(snapshot.loadout);for(auto mask:snapshot.equipped_masks)for(auto byte:mask.bytes)mix(byte);mix(snapshot.attributes.size());for(const auto& attribute:snapshot.attributes){mix(attribute.id);mix(std::bit_cast<std::uint64_t>(attribute.value));}
    mix(snapshot.tags.values.size());for(auto tag:snapshot.tags.values)mix(std::uint64_t(tag));
    mix(snapshot.active?snapshot.active->activation:0);if(snapshot.action){mix(snapshot.action->activation);mix(snapshot.action->clock-std::uint64_t(snapshot.action->rate)*snapshot.action->tick);mix(snapshot.action->rate);mix(std::uint64_t(snapshot.action->phase));}
    return hash;
}
AbilityPublicSnapshot public_ability_at(const AbilityPublicSnapshot& snapshot,std::uint64_t tick){
    auto result=snapshot;if(tick<=result.tick)return result;if(result.action){result.action->clock+=std::uint64_t(result.action->rate)*(tick-result.tick);result.action->tick=tick;}result.tick=tick;return result;
}
const std::map<std::uint64_t,EffectFrame>& WorldSession::effect_frames(AttributeVisibility audience)const{impl_->thread();require(audience==AttributeVisibility::Owner||audience==AttributeVisibility::Public,"Effect frame audience");return audience==AttributeVisibility::Owner?impl_->owner_effect_frames:impl_->public_effect_frames;}
bool WorldSession::effect_frame_needs_resync(std::uint64_t network,AttributeVisibility audience)const{impl_->thread();require(audience==AttributeVisibility::Owner||audience==AttributeVisibility::Public,"Effect resync audience");return impl_->effect_resync.contains({network,audience});}
bool WorldSession::public_ability_needs_resync(std::uint64_t network)const{impl_->thread();return !impl_->public_abilities.contains(network)||impl_->public_resync.contains(network);}
bool WorldSession::ability_correction_needs_resync()const{impl_->thread();return impl_->ability_resync;}
void WorldSession::acknowledge_ability_correction(std::uint64_t network){
    auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.hello.protocol>=3&&s.ability_corrections.contains(network)&&!s.peers.empty()&&s.peers.begin()->second.state==SessionReadiness::Ready,"Ability correction ACK requires a complete owner bundle");const auto& correction=s.ability_corrections.at(network);require(correction.world_revision==s.revision&&s.view->valid(correction.ability.owner.entity),"Ability correction ACK stale lifecycle");s.ability_ack_pending[network]={correction.world_revision,correction.ability.revision,correction.ability.highest_operation};s.ability_resync=false;
}
}

namespace darkangel {
bool WorldSession::submit_interaction(std::uint64_t network,StableId object){auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.peers.size()==1&&network&&bool(object),"An interaction request needs a client, its character and an object");auto& peer=s.peers.begin()->second;if(peer.state!=SessionReadiness::Ready||!s.objects.contains(network))return false;auto w=header(Kind::Interact,s.hello,0,network);w.u64(object.high);w.u64(object.low);return s.send(peer,std::move(w));}
std::vector<InteractionRequest> WorldSession::drain_interactions(){auto& s=*impl_;s.authority();auto result=std::move(s.interaction_requests);s.interaction_requests.clear();return result;}
void WorldSession::answer_interaction(std::uint64_t network,StableId object,std::uint8_t result){auto& s=*impl_;s.authority();require(result<=5&&bool(object),"Interaction answer bounds");auto control=s.motor_inputs.find(network);if(control==s.motor_inputs.end()||!s.peers.contains(control->second.owner))return;auto& queue=s.interaction_notices[control->second.owner];require(queue.size()<64,"Interaction receipt backlog");queue.push_back({network,object,result});}
std::vector<InteractionNotice> WorldSession::drain_interaction_receipts(){auto& s=*impl_;s.thread();require(s.role==SessionRole::Client,"Interaction receipts are a client view");auto result=std::move(s.received_interaction_notices);s.received_interaction_notices.clear();return result;}
bool WorldSession::submit_ability_intent(const AbilityIntent& intent){
    auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.hello.protocol>=3&&s.peers.size()==1,"Ability intent requires a protocol-3 client");auto& peer=s.peers.begin()->second;
    require(intent.network&&intent.operation&&intent.tick&&intent.avatar_epoch&&static_cast<unsigned>(intent.slot)<combat_slot_count+loadout_slot_count&&static_cast<unsigned>(intent.edge)<=static_cast<unsigned>(InputEdge::Tapped),"Ability intent identity/edge");
    if(peer.state!=SessionReadiness::Ready||s.ability_resync||!s.ability_corrections.contains(intent.network))return false;const auto& correction=s.ability_corrections.at(intent.network);
    if(correction.world_revision!=s.revision||intent.operation<=correction.ability.retired_through||intent.tick>correction.ability.tick+8)return false;if(!collision_control_ready())return false;
    auto w=header(Kind::AbilityCommand,s.hello,0,intent.network);w.u64(1);for(auto value:{intent.operation,intent.tick,intent.avatar_epoch,intent.grant_generation,std::uint64_t(intent.slot),std::uint64_t(intent.edge),std::uint64_t(intent.cancelled)|std::uint64_t(intent.replace_active)*2})w.u64(value);return s.send(peer,std::move(w));
}
std::vector<AbilityOperationNotice> WorldSession::drain_ability_receipts(){auto& s=*impl_;s.thread();require(s.role==SessionRole::Client,"Ability wire receipts are a client view");auto result=std::move(s.received_ability_notices);s.received_ability_notices.clear();for(auto& notice:result){auto object=s.objects.find(notice.network);notice.receipt.handle.owner.entity=object==s.objects.end()?EntityHandle{}:s.view->find(object->second.id);}return result;}
}

namespace darkangel {
void WorldSession::register_owner_channel(std::uint32_t channel,StableId avatar){auto& s=*impl_;s.thread();require(s.peers.empty()&&channel&&s.owner_channels.size()<8&&(s.role==SessionRole::Client?bool(avatar):!bool(avatar)),"Owner channel registration role/lifetime/bounds");require(s.owner_channels.emplace(channel,avatar).second,"Duplicate owner channel");}
bool WorldSession::send_owner_message(std::uint64_t network,std::uint32_t channel,std::span<const std::byte> bytes){
    auto& s=*impl_;s.thread();require(s.owner_channels.contains(channel),"Unregistered owner channel");
    require(network&&!bytes.empty()&&bytes.size()<=session_detail::owner_message_bytes,"Owner message identity/size");
    if(s.role==SessionRole::Server){auto owner=s.motor_inputs.find(network);if(owner==s.motor_inputs.end()||!s.peers.contains(owner->second.owner))return false;auto& peer=s.peers.at(owner->second.owner);if(peer.state!=SessionReadiness::Ready)return false;return peer.owner_mailbox.queue(network,channel,bytes);}
    if(s.peers.empty()||s.peers.begin()->second.state!=SessionReadiness::Ready)return false;
    require(s.objects.contains(network)&&s.objects.at(network).id==s.owner_channels.at(channel),"Owner message avatar denied");
    return s.peers.begin()->second.owner_mailbox.queue(network,channel,bytes);
}
std::vector<OwnerMessage> WorldSession::drain_owner_messages(std::uint32_t channel){auto& s=*impl_;s.thread();require(s.owner_channels.contains(channel),"Unregistered owner channel");std::vector<OwnerMessage> result;for(auto it=s.owner_messages.begin();it!=s.owner_messages.end();)if(it->channel==channel){result.push_back(std::move(*it));it=s.owner_messages.erase(it);}else ++it;return result;}
}
