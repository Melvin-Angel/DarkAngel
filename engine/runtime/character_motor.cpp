#include <darkangel/character_motor.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace darkangel {
    namespace {
        constexpr float dt=1.0f/60;
        void check(bool b,const char* s){
            if(!b)throw std::runtime_error(s);
        }
        bool finite(double n){
            return std::isfinite(n)&&std::abs(n)<=100000;
        }
        bool valid(MotorVec v){
            return finite(v.x)&&finite(v.y)&&finite(v.z);
        }
        JPH::Vec3 v(MotorVec a){
            return {
                float(a.x),float(a.y),float(a.z)
            };
        }
        MotorVec m(JPH::Vec3 a){
            return {
                a.GetX(),a.GetY(),a.GetZ()
            };
        }
        MotorVec add(MotorVec a,MotorVec b){
            return {
                a.x+b.x,a.y+b.y,a.z+b.z
            };
        }
        MotorVec sub(MotorVec a,MotorVec b){
            return {
                a.x-b.x,a.y-b.y,a.z-b.z
            };
        }
        struct Layers: JPH::BroadPhaseLayerInterface {
            unsigned GetNumBroadPhaseLayers()const override{
                return 3;
            }
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l)const override{
                return JPH::BroadPhaseLayer((JPH::uint8)l);
            }
            #if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override{
                return "DarkAngel";
            }
            #endif
        };
        // Layer 2 is a query-only character proxy. Motors filter it and see each other
        // through CharacterVsCharacterCollisionSimple, never through both paths.
        struct Pair: JPH::ObjectLayerPairFilter {
            bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b)const override{
                return a!=2&&b!=2&&(a==1||b==1);
            }
        };
        struct Broad: JPH::ObjectVsBroadPhaseLayerFilter {
            bool ShouldCollide(JPH::ObjectLayer a,JPH::BroadPhaseLayer b)const override{
                return a!=2&&b.GetValue()!=2&&(a==1||b.GetValue()==1);
            }
        };
        struct MotorLayers:JPH::ObjectLayerFilter{
            bool ShouldCollide(JPH::ObjectLayer l)const override{
                return l!=2;
            }
        };
        struct JoltLifetime {
            JoltLifetime(){
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance=new JPH::Factory;
                JPH::RegisterTypes();
            }~JoltLifetime(){
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance=nullptr;
            }
        };
        void initialize(){
            static JoltLifetime lifetime;
        }
        JPH::RefConst<JPH::Shape> capsule(bool low){
            float height=low?1.0f:1.8f;
            return new JPH::RotatedTranslatedShape(JPH::Vec3(0,height/2,0),JPH::Quat::sIdentity(),new JPH::CapsuleShape(height/2-.3f,.3f));
        }
    }
    unsigned SimulationClock::advance(double seconds){
        check(std::isfinite(seconds)&&seconds>=0&&seconds<=10,"Invalid clock delta");
        debt_+=seconds;
        unsigned n=0;
        while(debt_+1e-10>=1.0/60&&n<8){
            debt_-=1.0/60;
            ++n;
            ++tick_;
        }return n;
    }
    struct PhysicsWorld::Impl {
        Layers layers;
        Pair pair;
        Broad broad;
        JPH::PhysicsSystem system;
        JPH::TempAllocatorImpl temp{
            8*1024*1024
        };
        JPH::JobSystemSingleThreaded jobs{
            1024
        };
        JPH::CharacterVsCharacterCollisionSimple characters;
        struct Box{
            CollisionBox data;
            JPH::BodyID body;
        };
        std::map<std::uint64_t,Box> boxes;
        std::uint64_t tick{
        },topology{
            1
        };
        unsigned motors{
        };
        std::uint64_t next_motor{
            1
        };
        std::thread::id owner=std::this_thread::get_id();
        Impl(){
            system.Init(256,0,512,512,layers,broad,pair);
            system.SetGravity({
                0,-20,0
            });
        }
        void thread()const{
            check(owner==std::this_thread::get_id(),"Physics owner thread mismatch");
        }
        void clear(){
            for(auto& [id,b]:boxes){
                system.GetBodyInterface().RemoveBody(b.body);
                system.GetBodyInterface().DestroyBody(b.body);
            }boxes.clear();
        }
        ~Impl(){
            clear();
        }
        void insert(CollisionBox b){
            check(b.id&&b.id<(1ULL<<63)&&boxes.size()<64&&!boxes.contains(b.id)&&valid(b.center)&&valid(b.half)&&b.half.x>0&&b.half.y>0&&b.half.z>0&&valid(b.velocity)&&valid(b.angular)&&b.angular.x==0&&b.angular.z==0&&finite(b.yaw)&&finite(b.roll),"Invalid collision box");
            auto q=JPH::Quat::sRotation(JPH::Vec3::sAxisY(),float(b.yaw))*JPH::Quat::sRotation(JPH::Vec3::sAxisZ(),float(b.roll));
            JPH::BodyCreationSettings settings(new JPH::BoxShape(v(b.half)),v(b.center),q,b.moving?JPH::EMotionType::Kinematic:JPH::EMotionType::Static,b.moving?1:0);
            settings.mUserData=b.id;
            settings.mLinearVelocity=v(b.velocity);
            settings.mAngularVelocity=v(b.angular);
            auto body=system.GetBodyInterface().CreateAndAddBody(settings,JPH::EActivation::Activate);
            check(!body.IsInvalid(),"Collision body budget exhausted");
            boxes.emplace(b.id,Box{
                b,body
            });
        }
    };
    PhysicsWorld::PhysicsWorld(){
        initialize();
        impl_=std::make_unique<Impl>();
    }PhysicsWorld::~PhysicsWorld()=default;
    void PhysicsWorld::add(CollisionBox b){
        auto& s=*impl_;
        s.thread();
        s.insert(b);
        ++s.topology;
    }
    void PhysicsWorld::remove(std::uint64_t id){
        auto& s=*impl_;
        s.thread();
        auto it=s.boxes.find(id);
        check(it!=s.boxes.end(),"Unknown collision identity");
        s.system.GetBodyInterface().RemoveBody(it->second.body);
        s.system.GetBodyInterface().DestroyBody(it->second.body);
        s.boxes.erase(it);
        ++s.topology;
    }
    void PhysicsWorld::set_platform(std::uint64_t id,MotorVec vel,MotorVec angular){
        auto& s=*impl_;
        s.thread();
        auto& b=s.boxes.at(id);
        check(b.data.moving&&valid(vel)&&valid(angular)&&angular.x==0&&angular.z==0,"Invalid platform target");
        b.data.velocity=vel;
        b.data.angular=angular;
        s.system.GetBodyInterface().SetLinearAndAngularVelocity(b.body,v(vel),v(angular));
    }
    void PhysicsWorld::step(){
        auto& s=*impl_;
        s.thread();
        auto error=s.system.Update(dt,1,&s.temp,&s.jobs);
        check(error==JPH::EPhysicsUpdateError::None,"Physics work overflow");
        ++s.tick;
    }
    CollisionFrame PhysicsWorld::capture()const{
        auto& s=*impl_;
        s.thread();
        CollisionFrame f{
            s.tick,s.topology,{
            }
        };
        for(const auto& [id,b]:s.boxes){
            auto d=b.data;
            d.center=m(s.system.GetBodyInterface().GetPosition(b.body));
            auto q=s.system.GetBodyInterface().GetRotation(b.body);
            auto angles=q.GetEulerAngles();
            d.yaw=angles.GetY();
            d.roll=angles.GetZ();
            f.boxes.push_back(d);
        }return f;
    }
    void PhysicsWorld::load(const CollisionFrame& f){
        auto& s=*impl_;
        s.thread();
        check(f.topology&&f.boxes.size()<=64,"Invalid history frame");
        check(s.motors<=1,"Cannot replace shared live motor topology");
        s.clear();
        for(auto b:f.boxes)s.insert(b);
        s.tick=f.tick;
        s.topology=f.topology;
    }
    void PhysicsWorld::load_cooked(std::string_view bytes){
        check(bytes.size()<=1024*1024,"Collision asset byte limit");
        auto json=nlohmann::json::parse(bytes);
        check(json.at("schema")==1&&json.at("kind")=="collision"&&json.at("jolt")=="5.6.0"&&json.at("axes")=="right-handed-y-up-metres"&&json.at("boxes").size()<=64,"Collision asset contract");
        CollisionFrame frame{
            0,1,{
            }
        };
        for(auto b:json.at("boxes")){
            auto center=b.at("center").get<std::vector<double>>(),half=b.at("half").get<std::vector<double>>();
            check(center.size()==3&&half.size()==3,"Collision vector dimensions");
            frame.boxes.push_back({
                std::stoull(b.at("id").get<std::string>()),{
                    center[0],center[1],center[2]
                },{
                    half[0],half[1],half[2]
                },{
                },{
                },b.at("yaw").get<double>(),b.at("roll").get<double>(),b.at("moving").get<bool>()
            });
        }load(frame);
    }
    std::uint64_t PhysicsWorld::tick()const{
        return impl_->tick;
    }std::uint64_t PhysicsWorld::topology()const{
        return impl_->topology;
    }
    bool PhysicsWorld::ray(MotorVec from,MotorVec delta,std::uint64_t& identity)const{
        auto& s=*impl_;
        s.thread();
        check(valid(from)&&valid(delta),"Invalid ray");
        JPH::RayCastResult hit;
        if(!s.system.GetNarrowPhaseQuery().CastRay(JPH::RRayCast(v(from),v(delta)),hit))return false;
        identity=s.system.GetBodyInterface().GetUserData(hit.mBodyID);
        return true;
    }
    namespace {
        template<class Base,class Hit> struct BoundedHits:Base {
            std::vector<Hit> values;
            unsigned limit;
            bool overflow{
            };
            explicit BoundedHits(unsigned n):limit(n){
                values.reserve(n);
            }void AddHit(const Hit& hit) override{
                if(values.size()==limit){
                    overflow=true;
                    this->ForceEarlyOut();
                    return;
                }values.push_back(hit);
            }
        };
    }
    CollisionQuery PhysicsWorld::overlap(MotorVec center,double radius,unsigned limit)const{
        auto& s=*impl_;
        s.thread();
        check(valid(center)&&radius>0&&radius<=10&&limit>0&&limit<=32,"Overlap query bounds");
        JPH::SphereShape shape{
            float(radius)
        };
        BoundedHits<JPH::CollideShapeCollector,JPH::CollideShapeResult> collector(limit);
        JPH::CollideShapeSettings settings;
        s.system.GetNarrowPhaseQuery().CollideShape(&shape,JPH::Vec3::sReplicate(1),JPH::RMat44::sTranslation(v(center)),settings,v(center),collector);
        CollisionQuery result;
        result.overflow=collector.overflow;
        for(auto hit:collector.values){
            auto id=s.system.GetBodyInterface().GetUserData(hit.mBodyID2);
            result.hits.push_back({
                id&~(1ULL<<63),0,(id&(1ULL<<63))!=0
            });
        }std::sort(result.hits.begin(),result.hits.end(),[](auto a,auto b){
            return a.identity<b.identity;
        });
        return result;
    }
    CollisionQuery PhysicsWorld::sweep(MotorVec from,MotorVec delta,double radius,unsigned limit)const{
        auto& s=*impl_;
        s.thread();
        check(valid(from)&&valid(delta)&&radius>0&&radius<=10&&limit>0&&limit<=32,"Sweep query bounds");
        JPH::SphereShape shape{
            float(radius)
        };
        BoundedHits<JPH::CastShapeCollector,JPH::ShapeCastResult> collector(limit);
        JPH::ShapeCastSettings settings;
        JPH::RShapeCast cast(&shape,JPH::Vec3::sReplicate(1),JPH::RMat44::sTranslation(v(from)),v(delta));
        s.system.GetNarrowPhaseQuery().CastShape(cast,settings,v(from),collector);
        CollisionQuery result;
        result.overflow=collector.overflow;
        for(auto hit:collector.values){
            auto id=s.system.GetBodyInterface().GetUserData(hit.mBodyID2);
            result.hits.push_back({
                id&~(1ULL<<63),hit.mFraction,(id&(1ULL<<63))!=0
            });
        }std::sort(result.hits.begin(),result.hits.end(),[](auto a,auto b){
            return a.fraction==b.fraction?a.identity<b.identity:a.fraction<b.fraction;
        });
        return result;
    }
    struct CharacterMotor::Impl {
        PhysicsWorld::Impl& world;
        JPH::RefConst<JPH::Shape> standing{
            capsule(false)
        },low{
            capsule(true)
        };
        JPH::Ref<JPH::CharacterVirtual> character;
        MotorState state;
        MotorLayers filter;
        std::uint64_t previous_support{
        };
        bool failed{};
        bool jumped{
        };
        Impl(PhysicsWorld::Impl& w,MotorVec foot,std::uint64_t identity):world(w){
            check(valid(foot)&&w.motors<4,"Invalid motor spawn/budget");
            JPH::CharacterVirtualSettings settings;
            settings.mShape=standing;
            settings.mInnerBodyShape=standing;
            settings.mInnerBodyLayer=2;
            settings.mMaxSlopeAngle=JPH::DegreesToRadians(45);
            settings.mMaxStrength=0;
            settings.mSupportingVolume=JPH::Plane(JPH::Vec3::sAxisY(),-.3f);
            settings.mMaxNumHits=32;
            check(identity<(1ULL<<63),"Invalid character identity");
            if(!identity)identity=w.next_motor++;
            character=new JPH::CharacterVirtual(&settings,v(foot),JPH::Quat::sIdentity(),identity|(1ULL<<63),&w.system);
            check(clear(foot),"Invalid spawn overlaps collision; choose validated safe pose");
            character->SetCharacterVsCharacterCollision(&w.characters);
            w.characters.Add(character);
            ++w.motors;
            state.position=foot;
            state.topology=w.topology;
            state.tick=w.tick;
            refresh();
        }
        ~Impl(){
            world.characters.Remove(character);
            character=nullptr;
            --world.motors;
        }
        void refresh(bool contacts=true){
            if(contacts)character->RefreshContacts(world.system.GetDefaultBroadPhaseLayerFilter(1),filter,{
            },{
            },world.temp);
            character->UpdateGroundVelocity();
            state.grounded=character->GetGroundState()==JPH::CharacterBase::EGroundState::OnGround;
            state.support=state.grounded?character->GetGroundUserData():0;
            state.support_velocity=state.grounded?m(character->GetGroundVelocity()):MotorVec{
            };
            state.support_local={
            };
            if(state.support&&world.boxes.contains(state.support)){
                auto body=world.boxes.at(state.support).body;
                auto pos=world.system.GetBodyInterface().GetPosition(body);
                auto q=world.system.GetBodyInterface().GetRotation(body);
                state.support_local=m(q.Conjugated()*JPH::Vec3(character->GetGroundPosition()-pos));
            }
        }
        bool shape(bool crouched,bool force=false){
            auto target=crouched?low:standing;
            if(!character->SetShape(target,force?FLT_MAX:.01f,world.system.GetDefaultBroadPhaseLayerFilter(1),filter,{
            },{
            },world.temp))return false;
            character->SetInnerBodyShape(target);
            state.crouched=crouched;
            return true;
        }
        bool clear(MotorVec foot){
            struct Clearance:JPH::CollideShapeCollector{
                bool hit{
                };
                void AddHit(const JPH::CollideShapeResult& r)override{
                    if(r.mPenetrationDepth>.01f){
                        hit=true;
                        ForceEarlyOut();
                    }
                }
            };
            Clearance collector;
            auto shape=state.crouched?low:standing;
            JPH::CollideShapeSettings settings;
            world.system.GetNarrowPhaseQuery().CollideShape(shape,JPH::Vec3::sReplicate(1),JPH::RMat44::sTranslation(v(foot)+shape->GetCenterOfMass()),settings,v(foot),collector,world.system.GetDefaultBroadPhaseLayerFilter(1),filter);
            return !collector.hit;
        }
    };
    CharacterMotor::CharacterMotor(PhysicsWorld& w,MotorVec f,std::uint64_t id):impl_(std::make_unique<Impl>(*w.impl_,f,id)){
    }CharacterMotor::~CharacterMotor()=default;
    MotorState CharacterMotor::step(const MotorInput& input,const MotionRequest& request){
        auto& s=*impl_;
        s.world.thread();
        check(!s.failed,"Motor requires validated teleport/resynchronization");
        validate_motor_input(input);
        check(input.tick==s.world.tick+1&&input.tick>s.state.tick&&input.sequence>=s.state.sequence&&input.epoch==s.state.epoch,"Motor input clock/epoch mismatch");
        check(valid(request.root)&&valid(request.impulse)&&std::sqrt(request.root.x*request.root.x+request.root.y*request.root.y+request.root.z*request.root.z)<=1&&std::sqrt(request.impulse.x*request.impulse.x+request.impulse.y*request.impulse.y+request.impulse.z*request.impulse.z)<=30,"Motion request bounds");
        auto& state=s.state;
        s.refresh();
        s.previous_support=state.support;
        if(input.crouch!=state.crouched)s.shape(input.crouch);
        state.coyote=state.grounded?6:(state.coyote?state.coyote-1:0);
        state.jump_buffer=input.jump?6:(state.jump_buffer?state.jump_buffer-1:0);
        bool jump=state.jump_buffer&&state.coyote;
        s.jumped=jump;
        auto before=s.character->GetPosition();
        auto current=s.character->GetLinearVelocity();
        auto ground=s.character->GetGroundVelocity();
        JPH::Vec3 vel(0,current.GetY(),0);
        if(state.grounded&&current.GetY()-ground.GetY()<.1f)vel=ground;
        // Acceleration/braking uses achieved prior velocity relative to support.
        double target_x=request.lock?0:input.x*(state.crouched?2:5),target_z=request.lock?0:input.z*(state.crouched?2:5);
        auto approach=[](double a,double b){
            return a+std::clamp(b-a,-30.0/60,30.0/60);
        };
        double px=state.momentum.x,pz=state.momentum.z;
        vel+=JPH::Vec3(float(approach(px,target_x)),0,float(approach(pz,target_z)));
        if(jump){
            vel.SetY(ground.GetY()+7);
            state.coyote=state.jump_buffer=0;
        }vel+=JPH::Vec3(0,-20*dt,0)+v(request.impulse);
        state.momentum=m(vel-ground);
        vel+=v(request.root)/dt;
        state.desired=m(vel);
        state.action=request.action;
        state.yaw=input.yaw;
        s.character->SetRotation(JPH::Quat::sRotation(JPH::Vec3::sAxisY(),float(input.yaw)));
        s.character->SetLinearVelocity(vel);
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        settings.mWalkStairsStepUp={
            0,.35f,0
        };
        settings.mStickToFloorStepDown={
            0,-.25f,0
        };
        if(jump||vel.GetY()-ground.GetY()>.1f){
            settings.mStickToFloorStepDown=JPH::Vec3::sZero();
            settings.mWalkStairsStepUp=JPH::Vec3::sZero();
        }s.character->ExtendedUpdate(dt,{
            0,-20,0
        },settings,s.world.system.GetDefaultBroadPhaseLayerFilter(1),s.filter,{
        },{
        },s.world.temp);
        check(!s.character->GetMaxHitsExceeded(),"Motor collision work overflow");
        state.position=m(s.character->GetPosition());
        state.achieved=m(JPH::Vec3(s.character->GetPosition()-before));
        state.velocity=m(s.character->GetLinearVelocity());
        state.tick=input.tick;
        state.sequence=input.sequence;
        state.topology=s.world.topology;
        s.refresh(false);
        return state;
    }
    void CharacterMotor::post_physics(){
        auto& s=*impl_;
        s.world.thread();
        check(s.state.tick==s.world.tick,"Post physics phase mismatch");
        if(s.state.support)s.previous_support=s.state.support;
        s.refresh();
        if(!s.state.grounded&&!s.jumped&&s.previous_support&&s.world.boxes.contains(s.previous_support)&&s.world.boxes.at(s.previous_support).data.moving&&s.state.desired.y<=s.world.system.GetBodyInterface().GetLinearVelocity(s.world.boxes.at(s.previous_support).body).GetY()+.1){
            s.character->StickToFloor({
                0,-.05f,0
            },s.world.system.GetDefaultBroadPhaseLayerFilter(1),s.filter,{
            },{
            },s.world.temp);
            s.refresh();
            auto corrected=m(s.character->GetPosition());
            s.state.achieved=add(s.state.achieved,sub(corrected,s.state.position));
            s.state.position=corrected;
        }
        if(!s.clear(s.state.position)){s.failed=true;throw std::runtime_error("Motor crushed/obstructed beyond recovery; resynchronize to validated safe pose");}
    }
    bool CharacterMotor::needs_resync()const{return impl_->failed;}
    const MotorState& CharacterMotor::state()const{
        return impl_->state;
    }
    void CharacterMotor::restore(const MotorState& state){
        validate_motor_state(state);
        auto& s=*impl_;
        s.world.thread();
        check(state.topology==s.world.topology&&state.tick==s.world.tick,"Restore history mismatch");
        s.shape(state.crouched,true);
        s.state=state;
        s.character->SetPosition(v(state.position));
        s.character->SetRotation(JPH::Quat::sRotation(JPH::Vec3::sAxisY(),float(state.yaw)));
        s.character->SetLinearVelocity(v(state.velocity));
        s.refresh();
    }
    bool CharacterMotor::teleport(MotorVec foot){
        auto& s=*impl_;
        s.world.thread();
        check(valid(foot),"Invalid teleport destination");
        if(!s.clear(foot))return false;
        check(s.state.epoch<UINT64_MAX,"Motor epoch exhausted");
        s.character->SetPosition(v(foot));
        s.character->SetLinearVelocity(JPH::Vec3::sZero());
        s.failed=false;
        auto epoch=s.state.epoch+1;
        auto low=s.state.crouched;
        s.state={
        };
        s.state.position=foot;
        s.state.epoch=epoch;
        s.state.tick=s.world.tick;
        s.state.topology=s.world.topology;
        s.shape(low,true);
        s.refresh();
        return true;
    }
    void CollisionHistory::retain(CollisionFrame f){
        check(f.topology&&f.boxes.size()<=64&&(frames_.empty()||f.tick>frames_.back().tick),"Invalid history ordering/bounds");
        frames_.push_back(std::move(f));
        while(frames_.size()>31)frames_.pop_front();
    }
    const CollisionFrame* CollisionHistory::find(std::uint64_t tick)const{
        for(const auto& f:frames_)if(f.tick==tick)return &f;
        return nullptr;
    }
    ReplayResult replay_motor(const MotorState& baseline,std::span<const MotorCommand> commands,const CollisionHistory& history,MotorState& output){
        if(commands.size()>30)return ReplayResult::WorkLimit;
        try{
            validate_motor_state(baseline);
            if(commands.empty()){
                output=baseline;
                return ReplayResult::Applied;
            }auto f=history.find(baseline.tick);
            if(!f)return ReplayResult::MissingHistory;
            if(f->topology!=baseline.topology)return ReplayResult::TopologyMismatch;
            PhysicsWorld isolated;
            isolated.load(*f);
            CharacterMotor motor(isolated,baseline.position);
            motor.restore(baseline);
            for(const auto& record:commands){
                const auto& command=record.input;
                if(command.epoch!=baseline.epoch)return ReplayResult::Discontinuity;
                f=history.find(command.tick-1);
                if(!f)return ReplayResult::MissingHistory;
                if(f->topology!=baseline.topology)return ReplayResult::TopologyMismatch;
                isolated.load(*f);
                motor.restore(motor.state());
                motor.step(command,record.motion);
                isolated.step();
                motor.post_physics();
            }output=motor.state();
            return ReplayResult::Applied;
        }catch(...){
            return ReplayResult::Invalid;
        }
    }
    ReplayResult replay_motor(const MotorState& baseline,std::span<const MotorInput> inputs,const CollisionHistory& history,MotorState& out){
        if(inputs.size()>30)return ReplayResult::WorkLimit;
        std::vector<MotorCommand> commands;
        for(auto input:inputs)commands.push_back({
            input,{
            }
        });
        return replay_motor(baseline,commands,history,out);
    }
    OwnerPrediction::OwnerPrediction(PhysicsWorld& world,CharacterMotor& motor):world_(world),motor_(motor){
    }
    MotorState OwnerPrediction::predict(const MotorInput& input,const MotionRequest& motion){
        check(!resync_,"Prediction requires fresh baseline");
        if(commands_.size()>=30){
            resync_=true;
            throw std::runtime_error("Owner history overflow; resynchronize");
        }history_.retain(world_.capture());
        auto state=motor_.step(input,motion);
        commands_.push_back({
            input,motion
        });
        return state;
    }
    ReplayResult OwnerPrediction::reconcile(const MotorState& baseline){
        auto before=motor_.state();
        if(baseline.epoch!=before.epoch){
            resync_=true;
            return ReplayResult::Discontinuity;
        }if(baseline.tick>before.tick||baseline.sequence>before.sequence){
            resync_=true;
            return ReplayResult::Invalid;
        }std::vector<MotorCommand> pending;
        for(auto command:commands_)if(command.input.tick>baseline.tick)pending.push_back(command);
        MotorState output;
        auto result=replay_motor(baseline,pending,history_,output);
        if(result!=ReplayResult::Applied){
            resync_=true;
            return result;
        }try{
            motor_.restore(output);
        }catch(...){
            resync_=true;
            return ReplayResult::TopologyMismatch;
        }commands_.assign(pending.begin(),pending.end());
        visual_offset_=sub(before.position,output.position);
        if(v(visual_offset_).Length()>2)visual_offset_={
        };
        return ReplayResult::Applied;
    }
    void ObserverMotor::push(MotorState state){
        validate_motor_state(state);
        if(!frames_.empty()){
            if(state.epoch!=frames_.back().epoch||state.topology!=frames_.back().topology)frames_.clear();
            else if(state.tick<=frames_.back().tick)return;
        }frames_.push_back(state);
        while(frames_.size()>8)frames_.pop_front();
    }
    MotorState ObserverMotor::sample(double tick)const{
        check(std::isfinite(tick)&&!frames_.empty(),"Invalid observer clock/state");
        auto result=frames_.back();
        for(std::size_t i=1;i<frames_.size();++i)if(tick<=double(frames_[i].tick)){
            auto a=frames_[i-1],b=frames_[i];
            double t=std::clamp((tick-double(a.tick))/double(b.tick-a.tick),0.0,1.0);
            result=a;
            result.position={
                a.position.x+(b.position.x-a.position.x)*t,a.position.y+(b.position.y-a.position.y)*t,a.position.z+(b.position.z-a.position.z)*t
            };
            result.yaw=a.yaw+std::remainder(b.yaw-a.yaw,2*3.141592653589793)*t;
            return result;
        }return result;
    }
}
