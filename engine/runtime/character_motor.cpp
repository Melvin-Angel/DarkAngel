#include <darkangel/character_motor.hpp>
#include <darkangel/collision_asset.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/PhysicsMaterialSimple.h>
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
#include <atomic>
#include <set>
#include <tuple>
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
                return 4;
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
                return l!=2&&l!=3;
            }
        };
        std::atomic<bool> allocation_tracking{};
        std::atomic<std::uint64_t> sdk_allocations{},sdk_reallocations{},sdk_requested{},sdk_frees{};
        JPH::AllocateFunction original_allocate;JPH::ReallocateFunction original_reallocate;JPH::FreeFunction original_free;JPH::AlignedAllocateFunction original_aligned_allocate;JPH::AlignedFreeFunction original_aligned_free;
        void* counted_allocate(std::size_t size){if(allocation_tracking){++sdk_allocations;sdk_requested+=size;}return original_allocate(size);}
        void* counted_reallocate(void* pointer,std::size_t old_size,std::size_t size){if(allocation_tracking){++sdk_reallocations;sdk_requested+=size;}return original_reallocate(pointer,old_size,size);}
        void counted_free(void* pointer){if(pointer&&allocation_tracking)++sdk_frees;original_free(pointer);}
        void* counted_aligned_allocate(std::size_t size,std::size_t alignment){if(allocation_tracking){++sdk_allocations;sdk_requested+=size;}return original_aligned_allocate(size,alignment);}
        void counted_aligned_free(void* pointer){if(pointer&&allocation_tracking)++sdk_frees;original_aligned_free(pointer);}
        struct JoltLifetime {
            JoltLifetime(){
                JPH::RegisterDefaultAllocator();
                original_allocate=JPH::Allocate;original_reallocate=JPH::Reallocate;original_free=JPH::Free;original_aligned_allocate=JPH::AlignedAllocate;original_aligned_free=JPH::AlignedFree;
                JPH::Allocate=counted_allocate;JPH::Reallocate=counted_reallocate;JPH::Free=counted_free;JPH::AlignedAllocate=counted_aligned_allocate;JPH::AlignedFree=counted_aligned_free;
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
    void track_physics_allocations(bool enabled){initialize();allocation_tracking=enabled;}
    PhysicsAllocationCounters physics_allocation_counters(){return {sdk_allocations.load(),sdk_reallocations.load(),sdk_requested.load(),sdk_frees.load()};}
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
    struct CollisionGeometry::Impl {
        CollisionMeshData definition;JPH::RefConst<JPH::MeshShape> shape;
        explicit Impl(const CollisionMeshData& data):definition(data){
            validate_collision_mesh(definition);const auto signature=collision_mesh_signature(definition);
            check(definition.signature.empty()||definition.signature==signature,"Collision geometry signature mismatch");definition.signature=signature;
            JPH::VertexList vertices;JPH::IndexedTriangleList triangles;JPH::PhysicsMaterialList materials;
            vertices.reserve(data.vertices.size());triangles.reserve(data.triangles.size());materials.reserve(data.materials.size());
            for(auto p:data.vertices)vertices.emplace_back(float(p.x),float(p.y),float(p.z));
            for(const auto& t:data.triangles)triangles.emplace_back(t.vertices[0],t.vertices[1],t.vertices[2],t.material,t.key);
            for(const auto& key:data.materials)materials.push_back(new JPH::PhysicsMaterialSimple(key.c_str(),JPH::Color::sWhite));
            JPH::MeshShapeSettings settings(std::move(vertices),std::move(triangles),std::move(materials));settings.mPerTriangleUserData=true;
            check(settings.mIndexedTriangles.size()==data.triangles.size(),"Jolt sanitization changed approved triangle identities");
            auto result=settings.Create();check(!result.HasError(),"Jolt static mesh build failure");shape=static_cast<const JPH::MeshShape*>(result.Get().GetPtr());
        }
    };
    CollisionGeometry::CollisionGeometry(const CollisionMeshData& data){initialize();impl_=std::make_unique<Impl>(data);}
    CollisionGeometry::~CollisionGeometry()=default;
    const CollisionMeshData& CollisionGeometry::definition()const{return impl_->definition;}
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
            std::uint64_t generation;
        };
        std::map<std::uint64_t,Box> boxes;
        struct Mesh {CollisionMesh data;JPH::BodyID body;std::uint64_t generation;};
        std::map<std::uint64_t,Mesh> meshes;
        struct Actor {JPH::CharacterVirtual* character;const MotorState* state;std::uint64_t generation,post_tick;};
        std::map<std::uint64_t,Actor> actors;
        std::vector<JPH::Ref<JPH::CharacterVirtual>> historical_actors;
        std::map<std::uint64_t,std::pair<std::uint64_t,std::uint64_t>> historical_generations;
        std::map<std::uint64_t,CollisionActor> historical_states;
        using SensorPair=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t>;
        std::map<SensorPair,std::uint64_t> sensor_pairs;
        std::vector<SensorEvent> sensor_events;
        std::uint64_t last_sensor_tick=UINT64_MAX,next_token{},next_generation{},world_id;
        std::uint64_t tick{
        },topology{
            1
        };
        unsigned motors{
        };
        PhysicsWorld::Mode mode;
        bool failed{};
        std::uint64_t next_motor{
            1
        };
        std::thread::id owner=std::this_thread::get_id();
        explicit Impl(PhysicsWorld::Mode world_mode):mode(world_mode){
            static std::atomic<std::uint64_t> next_world{};world_id=++next_world;
            system.Init(256,0,512,512,layers,broad,pair);
            system.SetGravity({
                0,-20,0
            });
        }
        void thread()const{
            check(owner==std::this_thread::get_id(),"Physics owner thread mismatch");
        }
        void clear(){
            for(auto& actor:historical_actors)characters.Remove(actor);
            historical_actors.clear();
            historical_generations.clear();historical_states.clear();
            for(auto& [id,mesh]:meshes){system.GetBodyInterface().RemoveBody(mesh.body);system.GetBodyInterface().DestroyBody(mesh.body);}meshes.clear();
            for(auto& [id,b]:boxes){
                system.GetBodyInterface().RemoveBody(b.body);
                system.GetBodyInterface().DestroyBody(b.body);
            }boxes.clear();
        }
        ~Impl(){
            clear();
        }
        void insert_actor(const CollisionActor& actor){
            JPH::CharacterVirtualSettings settings;
            settings.mShape=capsule(actor.crouched);
            settings.mInnerBodyShape=settings.mShape;
            settings.mInnerBodyLayer=2;
            settings.mMaxStrength=0;
            JPH::Ref<JPH::CharacterVirtual> character=new JPH::CharacterVirtual(&settings,v(actor.foot),JPH::Quat::sRotation(JPH::Vec3::sAxisY(),float(actor.yaw)),actor.id|(1ULL<<63),&system);
            character->SetLinearVelocity(v(actor.velocity));
            characters.Add(character);
            historical_actors.push_back(character);
            historical_generations.emplace(actor.id,std::pair{++next_generation,actor.epoch});historical_states.emplace(actor.id,actor);
        }
        void insert(CollisionBox b){
            check(b.id&&b.id<(1ULL<<63)&&boxes.size()+meshes.size()<64&&!boxes.contains(b.id)&&!meshes.contains(b.id)&&valid(b.center)&&valid(b.half)&&b.half.x>0&&b.half.y>0&&b.half.z>0&&valid(b.velocity)&&valid(b.angular)&&(b.dynamic||(b.angular.x==0&&b.angular.z==0))&&finite(b.yaw)&&finite(b.roll)&&!(b.dynamic&&b.moving)&&(!b.sensor||(!b.dynamic&&!b.moving))&&finite(b.mass)&&b.mass>=1&&b.mass<=1000,"Invalid collision box");
            auto q=JPH::Quat::sRotation(JPH::Vec3::sAxisY(),float(b.yaw))*JPH::Quat::sRotation(JPH::Vec3::sAxisZ(),float(b.roll));
            double norm{};for(double n:b.rotation){check(finite(n),"Invalid collision rotation");norm+=n*n;}
            if(norm){check(std::abs(norm-1)<.0001,"Collision rotation normalization");q=JPH::Quat(float(b.rotation[0]),float(b.rotation[1]),float(b.rotation[2]),float(b.rotation[3]));}
            const auto moving=b.moving||b.dynamic;
            const auto type=b.dynamic&&mode==PhysicsWorld::Mode::Authoritative?JPH::EMotionType::Dynamic:moving?JPH::EMotionType::Kinematic:JPH::EMotionType::Static;
            JPH::BodyCreationSettings settings(new JPH::BoxShape(v(b.half)),v(b.center),q,type,b.sensor?3:moving?1:0);
            settings.mIsSensor=b.sensor;
            if(b.dynamic){settings.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia;settings.mMassPropertiesOverride.mMass=float(b.mass);settings.mMaxLinearVelocity=30;settings.mMaxAngularVelocity=10;}
            settings.mUserData=b.id;
            settings.mLinearVelocity=v(b.velocity);
            settings.mAngularVelocity=v(b.angular);
            auto body=system.GetBodyInterface().CreateAndAddBody(settings,JPH::EActivation::Activate);
            check(!body.IsInvalid(),"Collision body budget exhausted");
            boxes.emplace(b.id,Box{
                b,body,++next_generation
            });
        }
        void insert_mesh(CollisionMesh mesh){
            check(mesh.id&&mesh.id<(1ULL<<63)&&mesh.geometry&&boxes.size()+meshes.size()<64&&meshes.size()<16&&!boxes.contains(mesh.id)&&!meshes.contains(mesh.id),"Invalid static mesh body/budget");
            JPH::BodyCreationSettings settings(mesh.geometry->impl_->shape,JPH::RVec3::sZero(),JPH::Quat::sIdentity(),JPH::EMotionType::Static,0);settings.mUserData=mesh.id;
            auto body=system.GetBodyInterface().CreateAndAddBody(settings,JPH::EActivation::DontActivate);check(!body.IsInvalid(),"Static mesh body allocation");meshes.emplace(mesh.id,Mesh{mesh,body,++next_generation});
        }
        unsigned category(std::uint64_t user)const{
            if(user&(1ULL<<63))return CharacterCollision;
            if(meshes.contains(user))return StaticCollision;
            const auto& data=boxes.at(user).data;return data.sensor?SensorCollision:data.dynamic?DynamicCollision:data.moving?KinematicCollision:StaticCollision;
        }
        struct QueryBodies:JPH::BodyFilter {
            const Impl& world;QueryFilter filter;
            QueryBodies(const Impl& w,QueryFilter f):world(w),filter(f){check(f.layers&&!(f.layers&~AllCollision),"Invalid query layer mask");}
            bool ShouldCollideLocked(const JPH::Body& body)const override{
                const auto user=body.GetUserData(),id=user&~(1ULL<<63);const auto category=world.category(user);
                return (category&filter.layers)&&(!filter.owner||!(user&(1ULL<<63))||id!=filter.owner)&&(category!=SensorCollision||filter.sensors);
            }
        };
        CollisionHit hit(JPH::BodyID body,[[maybe_unused]] JPH::SubShapeID subshape,double fraction,MotorVec point,MotorVec normal)const{
            const auto user=system.GetBodyInterface().GetUserData(body),id=user&~(1ULL<<63);
            CollisionHit result;result.identity=id;result.character=(user&(1ULL<<63))!=0;result.fraction=fraction;result.world=world_id;result.tick=tick;result.topology=topology;
            result.point=point;result.normal=normal;result.subshape=0;result.sensor=category(user)==SensorCollision;
            if(result.character){if(actors.contains(id)){result.generation=actors.at(id).generation;result.epoch=actors.at(id).state->epoch;}else {auto version=historical_generations.at(id);result.generation=version.first;result.epoch=version.second;}}
            else if(meshes.contains(id)){
                const auto& mesh=meshes.at(id);result.generation=mesh.generation;result.subshape=mesh.data.geometry->impl_->shape->GetTriangleUserData(subshape);result.material=mesh.data.geometry->impl_->shape->GetMaterialIndex(subshape);
            }else result.generation=boxes.at(id).generation;
            return result;
        }
    };
    PhysicsWorld::PhysicsWorld(Mode mode){
        initialize();
        impl_=std::make_unique<Impl>(mode);
    }PhysicsWorld::~PhysicsWorld()=default;
    PhysicsWorld::Mode PhysicsWorld::mode()const{return impl_->mode;}
    bool PhysicsWorld::needs_resync()const{return impl_->failed;}
    bool PhysicsWorld::sleeping(std::uint64_t id)const{auto& s=*impl_;s.thread();check(s.boxes.contains(id),"Unknown body sleep query");return !s.system.GetBodyInterface().IsActive(s.boxes.at(id).body);}
    void PhysicsWorld::apply_impulse(std::uint64_t id,MotorVec impulse){
        auto& s=*impl_;s.thread();check(!s.failed&&s.mode==Mode::Authoritative&&s.boxes.contains(id)&&s.boxes.at(id).data.dynamic&&valid(impulse)&&v(impulse).Length()<=500,"Dynamic impulse authority/bounds");
        s.system.GetBodyInterface().AddImpulse(s.boxes.at(id).body,v(impulse));
    }
    void PhysicsWorld::add(CollisionBox b){
        auto& s=*impl_;
        s.thread();
        s.insert(b);
        ++s.topology;
    }
    void PhysicsWorld::add_mesh(CollisionMesh mesh){auto& s=*impl_;s.thread();s.insert_mesh(std::move(mesh));++s.topology;}
    CollisionStreamFrame collision_stream(const CollisionFrame& frame){
        CollisionStreamFrame stream{frame.tick,frame.topology,frame.boxes,frame.actors,{}};
        for(const auto& mesh:frame.meshes){check(bool(mesh.geometry),"Missing streamed geometry");const auto& data=mesh.geometry->definition();stream.meshes.push_back({mesh.id,data.runtime,data.signature});}validate_collision_stream(stream);return stream;
    }
    CollisionFrame prepare_collision_frame(const CollisionStreamFrame& stream,std::span<const CollisionMesh> prepared){
        validate_collision_stream(stream);check(prepared.size()<=16,"Prepared geometry inventory bound");CollisionFrame frame{stream.tick,stream.topology,stream.boxes,stream.actors,{}};
        for(const auto& reference:stream.meshes){auto found=std::find_if(prepared.begin(),prepared.end(),[&](const auto& mesh){return mesh.id==reference.id;});check(found!=prepared.end()&&found->geometry,"Collision geometry not prepared");const auto& data=found->geometry->definition();check(data.runtime==reference.runtime&&data.signature==reference.signature,"Collision geometry generation mismatch");frame.meshes.push_back(*found);}return frame;
    }
    void PhysicsWorld::load_scene(const CollisionDefinition& scene){
        CollisionFrame frame;frame.topology=1;frame.boxes=scene.boxes;
        for(const auto& mesh:scene.meshes){check(bool(mesh.data),"Missing collision geometry");frame.meshes.push_back({mesh.id,std::make_shared<const CollisionGeometry>(*mesh.data)});}
        load(frame);
    }
    void PhysicsWorld::remove(std::uint64_t id){
        auto& s=*impl_;
        s.thread();
        auto it=s.boxes.find(id);
        if(it==s.boxes.end()){
            auto mesh=s.meshes.find(id);check(mesh!=s.meshes.end(),"Unknown collision identity");s.system.GetBodyInterface().RemoveBody(mesh->second.body);s.system.GetBodyInterface().DestroyBody(mesh->second.body);s.meshes.erase(mesh);++s.topology;return;
        }
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
        check(!s.failed,"Physics world requires resynchronization");
        check(s.sensor_events.empty(),"Consume sensor events before advancing physics");
        auto error=s.system.Update(dt,1,&s.temp,&s.jobs);
        if(error!=JPH::EPhysicsUpdateError::None){s.failed=true;throw std::runtime_error("Physics work overflow; resynchronize");}
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
            d.velocity=m(s.system.GetBodyInterface().GetLinearVelocity(b.body));d.angular=m(s.system.GetBodyInterface().GetAngularVelocity(b.body));
            auto q=s.system.GetBodyInterface().GetRotation(b.body);
            d.rotation={q.GetX(),q.GetY(),q.GetZ(),q.GetW()};
            auto angles=q.GetEulerAngles();
            d.yaw=angles.GetY();
            d.roll=angles.GetZ();
            f.boxes.push_back(d);
        }
        check(s.historical_actors.empty()||s.mode==PhysicsWorld::Mode::Prediction,"Cannot publish a replay context as live history");
        for(const auto& [id,actor]:s.actors){
            f.actors.push_back({id,actor.state->epoch,m(actor.character->GetPosition()),m(actor.character->GetLinearVelocity()),actor.state->yaw,actor.state->crouched});
        }
        for(const auto& [id,actor]:s.historical_states)f.actors.push_back(actor);
        std::sort(f.actors.begin(),f.actors.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        for(const auto& [id,mesh]:s.meshes)f.meshes.push_back(mesh.data);
        return f;
    }
    void PhysicsWorld::load(const CollisionFrame& f,std::uint64_t replay_owner){
        auto& s=*impl_;
        s.thread();
        check(f.topology&&f.boxes.size()+f.meshes.size()<=64&&f.meshes.size()<=16&&f.actors.size()<=4,"Invalid history frame");
        check(s.motors<=1,"Cannot replace shared live motor topology");
        check(!s.motors||f.actors.empty()||(replay_owner&&s.actors.contains(replay_owner)),"History load requires an explicit matching replay owner");
        std::map<std::uint64_t,bool> identities;
        for(const auto& actor:f.actors){
            check(actor.id&&actor.id<(1ULL<<63)&&actor.epoch&&valid(actor.foot)&&valid(actor.velocity)&&finite(actor.yaw)&&identities.emplace(actor.id,true).second,"Invalid historical actor");
        }
        s.clear();
        for(auto b:f.boxes)s.insert(b);
        for(auto mesh:f.meshes)s.insert_mesh(mesh);
        for(const auto& actor:f.actors)if(actor.id!=replay_owner)s.insert_actor(actor);
        s.tick=f.tick;
        s.topology=f.topology;
    }
    void PhysicsWorld::load_cooked(std::string_view bytes){
        check(bytes.size()<=1024*1024,"Collision asset byte limit");
        auto json=nlohmann::json::parse(bytes);
        check((json.at("schema")==1||json.at("schema")==2)&&json.at("kind")=="collision"&&json.at("jolt")=="5.6.0"&&json.at("axes")=="right-handed-y-up-metres"&&json.at("boxes").size()<=64,"Collision asset contract");
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
            if(json.at("schema")==2){frame.boxes.back().dynamic=b.at("dynamic").get<bool>();frame.boxes.back().mass=b.at("mass").get<double>();frame.boxes.back().sensor=b.value("sensor",false);}
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
        auto result=query_ray(from,delta);check(!result.overflow,"Ray work overflow");if(result.hits.empty())return false;
        identity=result.hits.front().identity;
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
        void order(CollisionQuery& result){
            std::sort(result.hits.begin(),result.hits.end(),[](const auto& a,const auto& b){return std::tie(a.fraction,a.identity,a.character,a.subshape)<std::tie(b.fraction,b.identity,b.character,b.subshape);});
        }
    }
    CollisionQuery PhysicsWorld::query_ray(MotorVec from,MotorVec delta,QueryFilter filter)const{
        auto& s=*impl_;s.thread();check(valid(from)&&valid(delta),"Invalid ray");Impl::QueryBodies bodies(s,filter);
        BoundedHits<JPH::CastRayCollector,JPH::RayCastResult> collector(32);JPH::RayCastSettings settings;settings.SetBackFaceMode(filter.backfaces?JPH::EBackFaceMode::CollideWithBackFaces:JPH::EBackFaceMode::IgnoreBackFaces);
        s.system.GetNarrowPhaseQuery().CastRay(JPH::RRayCast(v(from),v(delta)),settings,collector,{},{},bodies);
        CollisionQuery result;result.overflow=collector.overflow;if(result.overflow)return result;
        for(const auto& hit:collector.values){
            auto point=v(from)+float(hit.mFraction)*v(delta);JPH::Vec3 normal=JPH::Vec3::sZero();
            JPH::BodyLockRead lock(s.system.GetBodyLockInterface(),hit.mBodyID);check(lock.Succeeded(),"Ray body retired during owner-thread query");
            if(hit.mFraction>0)normal=lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,point);
            result.hits.push_back(s.hit(hit.mBodyID,hit.mSubShapeID2,hit.mFraction,m(point),m(normal)));
        }
        order(result);if(result.hits.size()>1)result.hits.resize(1);return result;
    }
    bool PhysicsWorld::valid_hit(const CollisionHit& hit)const{
        auto& s=*impl_;s.thread();if(s.failed||hit.world!=s.world_id||hit.tick!=s.tick||hit.topology!=s.topology)return false;
        if(hit.character){if(s.actors.contains(hit.identity))return s.actors.at(hit.identity).generation==hit.generation&&s.actors.at(hit.identity).state->epoch==hit.epoch;
            return s.historical_generations.contains(hit.identity)&&s.historical_generations.at(hit.identity)==std::pair{hit.generation,hit.epoch};}
        return (s.boxes.contains(hit.identity)&&s.boxes.at(hit.identity).generation==hit.generation)||(s.meshes.contains(hit.identity)&&s.meshes.at(hit.identity).generation==hit.generation);
    }
    std::string PhysicsWorld::material_key(const CollisionHit& hit)const{check(valid_hit(hit),"Material lookup requires a current qualified hit");auto& s=*impl_;if(!s.meshes.contains(hit.identity)||hit.character)return "Default";const auto& materials=s.meshes.at(hit.identity).data.geometry->definition().materials;check(hit.material<materials.size(),"Invalid authored material key");return materials[hit.material];}
    CollisionQuery PhysicsWorld::overlap(MotorVec center,double radius,unsigned limit,QueryFilter filter)const{
        auto& s=*impl_;
        s.thread();
        check(valid(center)&&radius>0&&radius<=10&&limit>0&&limit<=32,"Overlap query bounds");
        JPH::SphereShape shape{
            float(radius)
        };
        BoundedHits<JPH::CollideShapeCollector,JPH::CollideShapeResult> collector(limit);
        JPH::CollideShapeSettings settings;
        settings.mBackFaceMode=filter.backfaces?JPH::EBackFaceMode::CollideWithBackFaces:JPH::EBackFaceMode::IgnoreBackFaces;Impl::QueryBodies bodies(s,filter);
        s.system.GetNarrowPhaseQuery().CollideShape(&shape,JPH::Vec3::sReplicate(1),JPH::RMat44::sTranslation(v(center)),settings,v(center),collector,{},{},bodies);
        CollisionQuery result;
        result.overflow=collector.overflow;
        for(auto hit:collector.values){
            auto normal=hit.mPenetrationAxis.LengthSq()>1e-10f?-hit.mPenetrationAxis.Normalized():JPH::Vec3::sZero();
            result.hits.push_back(s.hit(hit.mBodyID2,hit.mSubShapeID2,0,m(v(center)+hit.mContactPointOn2),m(normal)));
        }order(result);
        return result;
    }
    CollisionQuery PhysicsWorld::sweep(MotorVec from,MotorVec delta,double radius,unsigned limit,QueryFilter filter)const{
        auto& s=*impl_;
        s.thread();
        check(valid(from)&&valid(delta)&&radius>0&&radius<=10&&limit>0&&limit<=32,"Sweep query bounds");
        JPH::SphereShape shape{
            float(radius)
        };
        BoundedHits<JPH::CastShapeCollector,JPH::ShapeCastResult> collector(limit);
        JPH::ShapeCastSettings settings;
        settings.mBackFaceModeTriangles=filter.backfaces?JPH::EBackFaceMode::CollideWithBackFaces:JPH::EBackFaceMode::IgnoreBackFaces;
        settings.mBackFaceModeConvex=settings.mBackFaceModeTriangles;Impl::QueryBodies bodies(s,filter);
        JPH::RShapeCast cast(&shape,JPH::Vec3::sReplicate(1),JPH::RMat44::sTranslation(v(from)),v(delta));
        s.system.GetNarrowPhaseQuery().CastShape(cast,settings,v(from),collector,{},{},bodies);
        CollisionQuery result;
        result.overflow=collector.overflow;
        for(auto hit:collector.values){
            auto normal=hit.mPenetrationAxis.LengthSq()>1e-10f?-hit.mPenetrationAxis.Normalized():JPH::Vec3::sZero();
            result.hits.push_back(s.hit(hit.mBodyID2,hit.mSubShapeID2,hit.mFraction,m(v(from)+hit.mContactPointOn2),m(normal)));
        }order(result);
        return result;
    }
    void PhysicsWorld::finish_tick(){
        auto& s=*impl_;s.thread();if(s.mode!=Mode::Authoritative)return;check(!s.failed,"Physics world requires resynchronization");
        check(s.tick>0,"Sensors require a completed simulation tick");if(s.last_sensor_tick==s.tick)return;
        for(const auto& [id,actor]:s.actors)check(actor.post_tick==s.tick,"Sensors require every motor post-physics phase");
        check(s.sensor_events.empty(),"Consume the prior sensor batch before advancing");
        std::set<Impl::SensorPair> current;
        for(const auto& [id,box]:s.boxes)if(box.data.sensor){
            auto shape=s.system.GetBodyInterface().GetShape(box.body);auto matrix=s.system.GetBodyInterface().GetCenterOfMassTransform(box.body);
            BoundedHits<JPH::CollideShapeCollector,JPH::CollideShapeResult> collector(32);JPH::CollideShapeSettings settings;
            Impl::QueryBodies bodies(s,{KinematicCollision|DynamicCollision|CharacterCollision});
            s.system.GetNarrowPhaseQuery().CollideShape(shape,JPH::Vec3::sReplicate(1),matrix,settings,matrix.GetTranslation(),collector,{},{},bodies);
            if(collector.overflow){s.failed=true;throw std::runtime_error("Sensor overlap work overflow; reject tick batch/resynchronize");}
            for(const auto& hit:collector.values){
                auto result=s.hit(hit.mBodyID2,hit.mSubShapeID2,0,{},{});auto encoded=result.identity|(result.character?(1ULL<<63):0);
                current.emplace(id,box.generation,encoded,result.generation,result.epoch);
                if(current.size()>128){s.failed=true;throw std::runtime_error("Sensor lifetime budget exhausted; reject tick batch/resynchronize");}
            }
        }
        auto candidate=s.sensor_pairs;std::vector<SensorEvent> events;auto next_token=s.next_token;
        auto event=[&](const Impl::SensorPair& pair,std::uint64_t token,SensorPhase phase){
            if(events.size()==128){s.failed=true;throw std::runtime_error("Sensor event budget exhausted; reject tick batch/resynchronize");}
            auto [sensor,generation,encoded,other_generation,epoch]=pair;
            SensorEvent value;value.world=s.world_id;value.tick=s.tick;value.token=token;value.sensor=sensor;value.other=encoded&~(1ULL<<63);value.other_generation=other_generation;value.epoch=epoch;value.character=(encoded&(1ULL<<63))!=0;value.phase=phase;events.push_back(value);
        };
        // Ends precede replacement begins. Stable pair order is independent of
        // broad-phase traversal, sleep and native contact callback ordering.
        for(const auto& [pair,token]:s.sensor_pairs)if(!current.contains(pair)){event(pair,token,SensorPhase::End);candidate.erase(pair);}
        for(const auto& pair:current)if(!candidate.contains(pair)){check(next_token<UINT64_MAX,"Sensor token exhausted");auto token=++next_token;candidate.emplace(pair,token);event(pair,token,SensorPhase::Begin);}
        if(events.size()>128||candidate.size()>128){s.failed=true;throw std::runtime_error("Sensor lifetime/event budget exhausted; reject tick batch/resynchronize");}
        s.sensor_pairs=std::move(candidate);s.sensor_events=std::move(events);s.next_token=next_token;s.last_sensor_tick=s.tick;
    }
    std::vector<SensorEvent> PhysicsWorld::take_sensor_events(){auto& s=*impl_;s.thread();auto events=std::move(s.sensor_events);s.sensor_events.clear();return events;}
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
        Impl(PhysicsWorld::Impl& w,MotorVec foot,std::uint64_t identity,bool crouched):world(w){
            check(valid(foot)&&w.motors<4,"Invalid motor spawn/budget");
            JPH::CharacterVirtualSettings settings;
            state.crouched=crouched;settings.mShape=crouched?low:standing;
            settings.mInnerBodyShape=settings.mShape;
            settings.mInnerBodyLayer=2;
            settings.mMaxSlopeAngle=JPH::DegreesToRadians(45);
            settings.mMaxStrength=w.mode==PhysicsWorld::Mode::Authoritative?1000.f:0.f;
            settings.mSupportingVolume=JPH::Plane(JPH::Vec3::sAxisY(),-.3f);
            settings.mMaxNumHits=32;
            check(identity<(1ULL<<63),"Invalid character identity");
            if(!identity){while(w.actors.contains(w.next_motor))++w.next_motor;identity=w.next_motor++;}
            check(!w.actors.contains(identity),"Duplicate character identity");
            character=new JPH::CharacterVirtual(&settings,v(foot),JPH::Quat::sIdentity(),identity|(1ULL<<63),&w.system);
            check(clear(foot,true),"Invalid spawn overlaps collision; choose validated safe pose");
            character->SetCharacterVsCharacterCollision(&w.characters);
            w.characters.Add(character);
            w.actors.emplace(identity,PhysicsWorld::Impl::Actor{character,&state,++w.next_generation,w.tick});
            ++w.motors;
            ++w.topology;
            state.position=foot;
            state.topology=w.topology;
            state.tick=w.tick;
            refresh();
        }
        ~Impl(){
            world.actors.erase(character->GetUserData()&~(1ULL<<63));
            world.characters.Remove(character);
            character=nullptr;
            --world.motors;
            ++world.topology;
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
            if(state.support&&(world.boxes.contains(state.support)||world.meshes.contains(state.support))){
                auto body=world.boxes.contains(state.support)?world.boxes.at(state.support).body:world.meshes.at(state.support).body;
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
        bool clear(MotorVec foot,bool peers=false,const JPH::Shape* target=nullptr){
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
            if(target)shape=target;
            JPH::CollideShapeSettings settings;
            JPH::IgnoreSingleBodyFilter self(character->GetInnerBodyID());
            struct NoSensors:JPH::ObjectLayerFilter{bool ShouldCollide(JPH::ObjectLayer layer)const override{return layer!=3;}} no_sensors;
            if(peers)world.system.GetNarrowPhaseQuery().CollideShape(shape,JPH::Vec3::sReplicate(1),JPH::RMat44::sTranslation(v(foot)+shape->GetCenterOfMass()),settings,v(foot),collector,{},no_sensors,self);
            else world.system.GetNarrowPhaseQuery().CollideShape(shape,JPH::Vec3::sReplicate(1),JPH::RMat44::sTranslation(v(foot)+shape->GetCenterOfMass()),settings,v(foot),collector,world.system.GetDefaultBroadPhaseLayerFilter(1),filter,self);
            return !collector.hit;
        }
    };
    CharacterMotor::CharacterMotor(PhysicsWorld& w,MotorVec f,std::uint64_t id,bool crouched):impl_(std::make_unique<Impl>(*w.impl_,f,id,crouched)){
    }CharacterMotor::~CharacterMotor()=default;
    std::uint64_t CharacterMotor::identity()const{return impl_->character->GetUserData()&~(1ULL<<63);}
    MotorState CharacterMotor::step(const MotorInput& input,const MotionRequest& request){
        auto& s=*impl_;
        s.world.thread();
        check(!s.failed&&!s.world.failed,"Motor/world requires validated resynchronization");
        check(s.world.sensor_events.empty(),"Consume sensor events before advancing motors");
        validate_motor_input(input);
        check(input.tick==s.world.tick+1&&input.tick>s.state.tick&&input.sequence>=s.state.sequence&&input.epoch==s.state.epoch,"Motor input clock/epoch mismatch");
        check(valid(request.root)&&valid(request.impulse)&&std::sqrt(request.root.x*request.root.x+request.root.y*request.root.y+request.root.z*request.root.z)<=1&&std::sqrt(request.impulse.x*request.impulse.x+request.impulse.y*request.impulse.y+request.impulse.z*request.impulse.z)<=30,"Motion request bounds");
        auto& state=s.state;
        const auto was_grounded=state.grounded;const auto previous_ground=state.support_velocity;
        s.refresh();
        s.previous_support=state.support;
        if(input.crouch!=state.crouched)s.shape(input.crouch);
        state.coyote=state.grounded?6:(state.coyote?state.coyote-1:0);
        state.jump_buffer=input.jump?6:(state.jump_buffer?state.jump_buffer-1:0);
        bool jump=state.jump_buffer&&state.coyote;
        s.jumped=jump;
        auto before=s.character->GetPosition();
        auto ground=s.character->GetGroundVelocity();
        const auto vertical=was_grounded&&state.grounded?state.momentum.y-previous_ground.y+ground.GetY():state.momentum.y;
        JPH::Vec3 vel(0,float(vertical),0);
        if(state.grounded&&vel.GetY()-ground.GetY()<.1f)vel=ground;
        // Acceleration/braking uses achieved prior velocity relative to support.
        double target_x=request.lock?0:input.x*(state.crouched?2:5),target_z=request.lock?0:input.z*(state.crouched?2:5);
        auto approach=[](double a,double b){
            return a+std::clamp(b-a,-30.0/60,30.0/60);
        };
        double px=state.momentum.x,pz=state.momentum.z;
        // Horizontal movement memory is support-relative while grounded and
        // world-space while airborne. A detach inherits support once.
        if(was_grounded&&!state.grounded){px+=previous_ground.x;pz+=previous_ground.z;}
        else if(!was_grounded&&state.grounded){px-=ground.GetX();pz-=ground.GetZ();}
        vel+=JPH::Vec3(float(approach(px,target_x)),0,float(approach(pz,target_z)));
        if(jump){
            vel.SetY(ground.GetY()+7);
            state.coyote=state.jump_buffer=0;
        }vel+=JPH::Vec3(0,-20*dt,0)+v(request.impulse);
        auto native_velocity=vel;
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
        if(s.character->GetMaxHitsExceeded()){s.failed=true;throw std::runtime_error("Motor collision work overflow; resynchronize");}
        state.position=m(s.character->GetPosition());
        state.achieved=m(JPH::Vec3(s.character->GetPosition()-before));
        state.velocity=m(s.character->GetLinearVelocity());
        // Root requests must not become kinetic velocity after their interval.
        // Project only the native channel through the same actual contact
        // planes. Never dereference virtual-character pointers in contacts.
        const auto& contacts=s.character->GetActiveContacts();
        std::array<const JPH::CharacterContact*,32> planes{};std::size_t count{};
        for(const auto& contact:contacts)if(contact.mHadCollision&&!contact.mWasDiscarded&&!contact.mIsSensorB){
            if(count==planes.size()){s.failed=true;throw std::runtime_error("Native motion contact budget; resynchronize");}planes[count++]=&contact;
        }
        std::sort(planes.begin(),planes.begin()+count,[](const auto* a,const auto* b){return std::tuple{a->mUserData,a->mSubShapeIDB.GetValue()}<std::tuple{b->mUserData,b->mSubShapeIDB.GetValue()};});
        for(unsigned pass=0;pass<3;++pass)for(std::size_t i=0;i<count;++i){
            const auto& contact=*planes[i];auto normal=contact.mContactNormal;const auto length=normal.LengthSq();if(length<1e-10f)continue;normal/=std::sqrt(length);
            auto other=contact.mCanPushCharacter?contact.mLinearVelocity:JPH::Vec3::sZero();const auto closing=(native_velocity-other).Dot(normal);if(closing<0)native_velocity-=closing*normal;
        }
        state.momentum=m(native_velocity);
        state.tick=input.tick;
        state.sequence=input.sequence;
        state.topology=s.world.topology;
        s.refresh(false);
        if(state.grounded){state.momentum.x-=state.support_velocity.x;state.momentum.z-=state.support_velocity.z;}
        return state;
    }
    void CharacterMotor::post_physics(){
        auto& s=*impl_;
        s.world.thread();
        check(s.state.tick==s.world.tick,"Post physics phase mismatch");
        const auto was_grounded=s.state.grounded;const auto previous_ground=s.state.support_velocity;
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
        if(was_grounded&&!s.state.grounded){s.state.momentum.x+=previous_ground.x;s.state.momentum.z+=previous_ground.z;}
        else if(!was_grounded&&s.state.grounded){s.state.momentum.x-=s.state.support_velocity.x;s.state.momentum.z-=s.state.support_velocity.z;}
        s.world.actors.at(identity()).post_tick=s.world.tick;
    }
    bool CharacterMotor::needs_resync()const{return impl_->failed||impl_->world.failed;}
    const MotorState& CharacterMotor::state()const{
        return impl_->state;
    }
    void CharacterMotor::restore(const MotorState& state){
        validate_motor_state(state);
        auto& s=*impl_;
        s.world.thread();
        check(state.topology==s.world.topology&&state.tick==s.world.tick,"Restore history mismatch");
        check(!s.world.failed&&s.clear(state.position,true,state.crouched?s.low:s.standing),"Restore overlaps current collision; resynchronize to a validated pose");
        s.shape(state.crouched,true);
        s.state=state;
        s.character->SetPosition(v(state.position));
        s.character->SetRotation(JPH::Quat::sRotation(JPH::Vec3::sAxisY(),float(state.yaw)));
        s.character->SetLinearVelocity(v(state.velocity));
        s.refresh();
        s.failed=false;
    }
    bool CharacterMotor::teleport(MotorVec foot){
        auto& s=*impl_;
        s.world.thread();
        check(valid(foot),"Invalid teleport destination");
        if(!s.clear(foot,true))return false;
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
    bool PhysicsWorld::update_prediction(const CollisionFrame& frame,CharacterMotor& motor){
        auto& s=*impl_;s.thread();check(s.mode==Mode::Prediction && &motor.impl_->world==&s,"Prediction proxy owner/world mismatch");
        if(frame.tick!=s.tick||frame.topology!=s.topology){s.failed=true;return false;}
        try{
            collision_stream(frame);auto owner=std::find_if(frame.actors.begin(),frame.actors.end(),[&](const auto& actor){return actor.id==motor.identity();});check(owner!=frame.actors.end()&&owner->epoch==motor.state().epoch,"Prediction proxy owner epoch missing");
            check(frame.meshes.size()==s.meshes.size(),"Prediction mesh topology mismatch");for(const auto& mesh:frame.meshes){auto found=s.meshes.find(mesh.id);check(found!=s.meshes.end()&&found->second.data.geometry->definition().signature==mesh.geometry->definition().signature,"Prediction mesh generation mismatch");}
            load(frame,motor.identity());motor.impl_->refresh();return true;
        }catch(...){s.failed=true;return false;}
    }
    void CollisionHistory::retain(CollisionFrame f){
        check(f.topology&&f.boxes.size()+f.meshes.size()<=64&&f.meshes.size()<=16&&f.actors.size()<=4&&(frames_.empty()||f.tick>frames_.back().tick),"Invalid history ordering/bounds");
        frames_.push_back(std::move(f));
        while(frames_.size()>31)frames_.pop_front();
    }
    const CollisionFrame* CollisionHistory::find(std::uint64_t tick)const{
        for(const auto& f:frames_)if(f.tick==tick)return &f;
        return nullptr;
    }
    ReplayResult replay_motor(const MotorState& baseline,std::span<const MotorCommand> commands,const CollisionHistory& history,MotorState& output,std::uint64_t owner){
        if(commands.size()>30)return ReplayResult::WorkLimit;
        try{
            validate_motor_state(baseline);
            if(commands.empty()){
                output=baseline;
                return ReplayResult::Applied;
            }auto f=history.find(baseline.tick);
            if(!f)return ReplayResult::MissingHistory;
            if(f->topology!=baseline.topology)return ReplayResult::TopologyMismatch;
            if(!owner){
                if(f->actors.size()>1)return ReplayResult::Invalid;
                owner=f->actors.empty()?1:f->actors.front().id;
            }
            std::vector<std::uint64_t> retained_actors;
            for(const auto& actor:f->actors)retained_actors.push_back(actor.id);
            std::sort(retained_actors.begin(),retained_actors.end());
            std::map<std::uint64_t,std::string> retained_meshes;
            for(const auto& mesh:f->meshes){if(!mesh.geometry||!retained_meshes.emplace(mesh.id,mesh.geometry->definition().signature).second)return ReplayResult::Invalid;}
            auto validate_owner=[&](const CollisionFrame& frame){
                std::map<std::uint64_t,std::string> present_meshes;
                for(const auto& mesh:frame.meshes){if(!mesh.geometry||!present_meshes.emplace(mesh.id,mesh.geometry->definition().signature).second)return ReplayResult::Invalid;}
                if(present_meshes!=retained_meshes)return ReplayResult::TopologyMismatch;
                std::vector<std::uint64_t> present;
                for(const auto& a:frame.actors)present.push_back(a.id);
                std::sort(present.begin(),present.end());
                if(present!=retained_actors)return ReplayResult::MissingHistory;
                auto actor=std::find_if(frame.actors.begin(),frame.actors.end(),[&](const auto& a){return a.id==owner;});
                if(!frame.actors.empty()&&actor==frame.actors.end())return ReplayResult::MissingHistory;
                if(actor!=frame.actors.end()&&actor->epoch!=baseline.epoch)return ReplayResult::Discontinuity;
                return ReplayResult::Applied;
            };
            auto status=validate_owner(*f);
            if(status!=ReplayResult::Applied)return status;
            PhysicsWorld isolated(PhysicsWorld::Mode::Replay);
            isolated.load(*f,owner);
            CharacterMotor motor(isolated,baseline.position,owner,baseline.crouched);
            // Constructing a live motor advances topology; restore the retained
            // version only inside this disposable replay world.
            isolated.load(*f,owner);
            motor.restore(baseline);
            for(const auto& record:commands){
                const auto& command=record.input;
                if(command.epoch!=baseline.epoch)return ReplayResult::Discontinuity;
                f=history.find(command.tick-1);
                if(!f)return ReplayResult::MissingHistory;
                if(f->topology!=baseline.topology)return ReplayResult::TopologyMismatch;
                status=validate_owner(*f);
                if(status!=ReplayResult::Applied)return status;
                isolated.load(*f,owner);
                motor.restore(motor.state());
                motor.step(command,record.motion);
                isolated.step();
                // Dynamic trajectories are authoritative snapshots, never a
                // locally re-simulated rigid-body rollback. Post-step support
                // requires the matching completed-tick collision state.
                const bool dynamic=std::any_of(f->boxes.begin(),f->boxes.end(),[](const auto& box){return box.dynamic;});
                if(dynamic){
                    auto next=history.find(command.tick);if(!next)return ReplayResult::MissingHistory;
                    if(next->topology!=baseline.topology)return ReplayResult::TopologyMismatch;
                    status=validate_owner(*next);if(status!=ReplayResult::Applied)return status;
                    isolated.load(*next,owner);motor.restore(motor.state());
                }
                motor.post_physics();
            }output=motor.state();
            return ReplayResult::Applied;
        }catch(...){
            return ReplayResult::Invalid;
        }
    }
    ReplayResult replay_motor(const MotorState& baseline,std::span<const MotorInput> inputs,const CollisionHistory& history,MotorState& out,std::uint64_t owner){
        if(inputs.size()>30)return ReplayResult::WorkLimit;
        std::vector<MotorCommand> commands;
        for(auto input:inputs)commands.push_back({
            input,{
            }
        });
        return replay_motor(baseline,commands,history,out,owner);
    }
    OwnerPrediction::OwnerPrediction(PhysicsWorld& world,CharacterMotor& motor):world_(world),motor_(motor){
    }
    MotorState OwnerPrediction::predict(const MotorInput& input,const MotionRequest& motion){
        check(!resync_,"Prediction requires fresh baseline");
        if(commands_.size()>=30){
            resync_=true;
            throw std::runtime_error("Owner history overflow; resynchronize");
        }
        auto frame=world_.capture();
        if(world_.mode()==PhysicsWorld::Mode::Authoritative&&std::any_of(frame.boxes.begin(),frame.boxes.end(),[](const auto& box){return box.dynamic;})){
            resync_=true;throw std::runtime_error("Owner prediction needs authoritative dynamic proxies, not a server physics world");
        }
        if(!history_.find(frame.tick))history_.retain(std::move(frame));
        auto state=motor_.step(input,motion);
        commands_.push_back({
            input,motion
        });
        return state;
    }
    ReplayResult OwnerPrediction::reconcile(const MotorState& baseline){
        const auto& current=motor_.state();
        if(baseline.epoch!=current.epoch||baseline.tick>current.tick||baseline.sequence>current.sequence)return reconcile(baseline,history_);
        try{if(!history_.find(world_.tick()))history_.retain(world_.capture());}catch(...){resync_=true;return ReplayResult::Invalid;}
        return reconcile(baseline,history_);
    }
    ReplayResult OwnerPrediction::reconcile(const MotorState& baseline,const CollisionHistory& authoritative_history){
        auto before=motor_.state();
        if(baseline.epoch!=before.epoch){
            resync_=true;
            return ReplayResult::Discontinuity;
        }if(baseline.tick>before.tick||baseline.sequence>before.sequence){
            resync_=true;
            return ReplayResult::Invalid;
        }
        std::vector<MotorCommand> pending;
        for(auto command:commands_)if(command.input.tick>baseline.tick)pending.push_back(command);
        MotorState output;
        auto result=replay_motor(baseline,pending,authoritative_history,output,motor_.identity());
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
