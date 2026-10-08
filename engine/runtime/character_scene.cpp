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
    std::unique_ptr<CharacterMotor> host_motor,owner_motor;
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
         std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition definition,std::string_view archive)
        :transport(create_loopback(hello)),server(SessionRole::Server,hello),client(SessionRole::Client,hello),graph(std::move(plan)),rig(std::move(definition),archive){
        require(hello.protocol==2&&rig.definition().human,"Character scene requires wire2 and the initial human profile");require(!objects.empty()&&objects.size()<=128&&identity,"Character scene object/player bounds");
        host_peer=transport.host->open(hello);client_peer=transport.client->open(hello);
        server.attach(*transport.host,host_peer);client.attach(*transport.client,client_peer);
        for(const auto& box:collision.boxes)require(!box.moving&&!box.dynamic&&!box.sensor,"Character scene initial profile requires static collision");
        MotorVec foot;double yaw{};
        for(const auto& object:objects){auto id=server.create(object);if(object.id==identity){require(!player,"Duplicate character scene player");require(object.transform.scale==1&&object.transform.pitch==0&&object.transform.roll==0&&object.health.current>0,"Character scene requires an upright, unscaled live player");player=id;foot={object.transform.x,object.transform.y,object.transform.z};yaw=object.transform.yaw;}}
        require(player,"Character scene player missing");authoritative.load_scene(collision);prediction->load_scene(collision);
        host_motor=std::make_unique<CharacterMotor>(authoritative,foot,player);owner_motor=std::make_unique<CharacterMotor>(*prediction,foot,player);
        auto host_state=host_motor->state();host_state.yaw=yaw;host_motor->restore(host_state);auto owner_state=owner_motor->state();owner_state.yaw=yaw;owner_motor->restore(owner_state);
        owner=std::make_unique<OwnerPrediction>(*prediction,*owner_motor);prepared_geometry=prediction->capture().meshes;
        server.own_motor(player,host_peer);server.publish_motor(player,host_motor->state());
        auto initial=authoritative.capture();history.retain(initial);server.publish_collision(collision_stream(initial));
        for(unsigned work=0;work<32&&(!client.motors().contains(player)||!client.collision_control_ready());++work)pump();
        require(client.readiness(client_peer)==SessionReadiness::Ready&&client.collision_control_ready()&&client.motors().contains(player),"Character scene bootstrap/control work limit");
        matrices=rig.blend(graph.evaluate({}).span());
    }
    void pump(){server.tick();client.tick();
        if(client.readiness(client_peer)==SessionReadiness::Ready&&!client.collisions().empty()){
            // Native geometry is prepared before acknowledging any frame.
            auto frame=prepare_collision_frame(client.collisions().back(),prepared_geometry);
            auto snapshot=client.motors().find(player);if(snapshot==client.motors().end()||snapshot->second.tick!=frame.tick)return;
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
    void step(CharacterSceneInput input){
        require(!failed,"Character scene requires a fresh synchronized session");require(!input.crouch,"Character scene posture animation is not in the initial standing profile");require(std::isfinite(input.x)&&std::isfinite(input.z)&&std::isfinite(input.yaw),"Character scene input finite");
        const auto tick=authoritative.tick()+1;MotorInput command{tick,tick,host_motor->state().epoch,input.x,input.z,input.yaw,input.jump,input.crouch};validate_motor_input(command);
        require(client.collision_control_ready()&&client.submit_motor(player,command),"Character scene control unavailable; resynchronize");
        server.tick();require(server.motor_input_ready(player,tick),"Character scene owner command unavailable");
        auto accepted=server.consume_motor(player,tick,command.epoch);
        owner->predict(command);prediction->step();owner_motor->post_physics();prediction->finish_tick();
        host_motor->step(accepted);authoritative.step();host_motor->post_physics();authoritative.finish_tick();
        auto frame=authoritative.capture();history.retain(frame);server.publish_motor(player,host_motor->state());server.publish_collision(collision_stream(frame));pump();
        require(client.motors().contains(player)&&client.motors().at(player).tick==tick&&!client.collisions().empty()&&client.collisions().back().tick==tick,"Character scene snapshot delivery bound");
        require(owner->reconcile(client.motors().at(player),history)==ReplayResult::Applied&&!owner->needs_resync(),"Character scene isolated correction failed; resynchronize");
        const auto& state=host_motor->state();double x=state.achieved.x*60-state.support_velocity.x,z=state.achieved.z*60-state.support_velocity.z;
        GraphParameters parameters;parameters.speed=float(std::hypot(x,z));parameters.forward=float(std::sin(state.yaw)*x+std::cos(state.yaw)*z);parameters.lateral=float(std::cos(state.yaw)*x-std::sin(state.yaw)*z);
        auto selected=graph.evaluate(parameters);double stride{};
        for(const auto& layer:selected.span()){const auto& clip=layer.clip->definition();const auto& end=clip.root.back();stride+=layer.weight*std::hypot(end[0],end[2])*60/clip.ticks;}
        if(parameters.speed>.01&&stride>.01)parameters.playback_rate=float(std::clamp(parameters.speed/stride,0.,4.));
        matrices=rig.blend(graph.advance(tick,parameters).span());
    }
};
CharacterSceneSession::CharacterSceneSession(SessionHandshake hello,std::span<const ObjectData> objects,StableId player,const CollisionDefinition& collision,std::shared_ptr<const AnimationGraphPlan> plan,RigDefinition rig,std::string_view archive)
    :impl_(std::make_unique<Impl>(std::move(hello),objects,player,collision,std::move(plan),std::move(rig),archive)){}
CharacterSceneSession::~CharacterSceneSession()=default;
unsigned CharacterSceneSession::advance(double seconds,CharacterSceneInput input){auto& p=*impl_;require(!p.failed,"Character scene requires a fresh synchronized session");require(std::isfinite(seconds)&&seconds>=0&&seconds<=10,"Character scene clock delta bounds");validate_motor_input({1,1,1,input.x,input.z,input.yaw,input.jump,input.crouch});p.pending_jump|=input.jump;auto steps=p.clock.advance(seconds);for(unsigned index=0;index<steps;++index){input.jump=p.pending_jump;try{p.step(input);}catch(...){p.failed=true;throw;}p.pending_jump=false;}return steps;}
void CharacterSceneSession::step(CharacterSceneInput input){try{impl_->step(input);}catch(...){impl_->failed=true;throw;}}
void CharacterSceneSession::remove_collision(std::uint64_t id){impl_->authoritative.remove(id);}
const World& CharacterSceneSession::presentation()const{return impl_->client.world();}
const MotorState& CharacterSceneSession::motor()const{return impl_->host_motor->state();}
const MotorState& CharacterSceneSession::predicted_motor()const{return impl_->owner_motor->state();}
const GraphState& CharacterSceneSession::graph()const{return impl_->graph.state();}
const std::vector<JointMatrix>& CharacterSceneSession::pose()const{return impl_->matrices;}
std::size_t CharacterSceneSession::pending_prediction()const{return impl_->owner->pending();}
unsigned CharacterSceneSession::resynchronizations()const{return impl_->resynchronizations;}
double CharacterSceneSession::debt()const{return impl_->clock.debt();}
}
