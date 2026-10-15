#include <darkangel/static_collision.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/assembly.hpp>
#include <darkangel/hash.hpp>
#include <DirectXMath.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
namespace darkangel {
namespace {
void require(bool b,const char* error){if(!b)throw std::runtime_error(error);}
bool finite(double n,double bound){return std::isfinite(n)&&std::abs(n)<=bound;}
void hex(std::string& out,std::uint64_t value){char text[17];std::snprintf(text,sizeof(text),"%016llx",static_cast<unsigned long long>(value));out.append(text,16);}
// One unit quaternion per rotation: q and -q describe the same rotation, so the sign is fixed.
std::array<double,4> canonical(std::array<double,4> q){
    double norm{};for(double n:q){require(std::isfinite(n),"Static body rotation finite");norm+=n*n;}require(std::abs(norm-1)<1e-4,"Static body rotation normalization");
    norm=std::sqrt(norm);for(auto& n:q)n/=norm;
    double lead=q[3]!=0?q[3]:q[0]!=0?q[0]:q[1]!=0?q[1]:q[2];if(lead<0)for(auto& n:q)n=-n;
    for(auto& n:q)if(n==0)n=0; // -0 and 0 encode differently
    return q;
}
std::string revision_of(std::span<const StaticCollisionBody> bodies){
    std::string text="darkangel.static-collision/1\n";text.reserve(text.size()+bodies.size()*260);
    for(const auto& body:bodies){
        const auto& geometry=body.geometry->definition();hex(text,body.id);text+='|';text+=body.object.text();text+='|';text+=geometry.runtime.text();text+='|';text+=geometry.signature;
        for(double n:{body.position.x,body.position.y,body.position.z,body.rotation[0],body.rotation[1],body.rotation[2],body.rotation[3],body.scale}){text+='|';hex(text,std::bit_cast<std::uint64_t>(n));}
        text+='\n';
    }
    return sha256(text);
}
}
std::uint64_t static_body_id(StableId object){
    require(bool(object),"A static body stands for a scene object");const auto digest=sha256("darkangel.static-body/"+object.text());
    return (std::stoull(digest.substr(0,16),nullptr,16)&((1ULL<<62)-1))|(1ULL<<62);
}
StaticCollisionLayer::StaticCollisionLayer(std::vector<StaticCollisionBody> bodies):bodies_(std::move(bodies)){
    require(bodies_.size()<=static_collision_body_limit,"Static collision body budget (16384) exceeded");
    for(auto& body:bodies_){
        require(body.id&&body.id<(1ULL<<63)&&body.geometry!=nullptr,"Static body identity/geometry");
        require(finite(body.position.x,100000)&&finite(body.position.y,100000)&&finite(body.position.z,100000),"Static body position bounds");
        require(std::isfinite(body.scale)&&body.scale>0&&body.scale<=1000,"Static body uniform scale must be in (0, 1000]");
        body.rotation=canonical(body.rotation);if(body.position.x==0)body.position.x=0;if(body.position.y==0)body.position.y=0;if(body.position.z==0)body.position.z=0;
    }
    std::sort(bodies_.begin(),bodies_.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(std::size_t i=1;i<bodies_.size();++i)require(bodies_[i-1].id!=bodies_[i].id,"Duplicate static body identity");
    revision_=revision_of(bodies_);
}
const std::shared_ptr<const StaticCollisionLayer>& StaticCollisionLayer::empty(){static const auto layer=std::make_shared<const StaticCollisionLayer>(std::vector<StaticCollisionBody>{});return layer;}
const StaticCollisionBody* StaticCollisionLayer::find(std::uint64_t id)const{
    auto found=std::lower_bound(bodies_.begin(),bodies_.end(),id,[](const auto& body,std::uint64_t value){return body.id<value;});
    return found!=bodies_.end()&&found->id==id?&*found:nullptr;
}
std::size_t StaticCollisionLayer::unique_geometry()const{std::set<const CollisionGeometry*> unique;for(const auto& body:bodies_)unique.insert(body.geometry.get());return unique.size();}
std::shared_ptr<const StaticCollisionLayer> StaticCollisionLayer::without(std::uint64_t id)const{
    require(find(id)!=nullptr,"Unknown static body");std::vector<StaticCollisionBody> kept;kept.reserve(bodies_.size()-1);
    for(const auto& body:bodies_)if(body.id!=id)kept.push_back(body);return std::make_shared<const StaticCollisionLayer>(std::move(kept));
}
std::array<double,4> placement_rotation(double pitch,double yaw,double roll){
    DirectX::XMFLOAT4 q;DirectX::XMStoreFloat4(&q,DirectX::XMQuaternionRotationRollPitchYaw(float(pitch),float(yaw),float(roll)));
    return canonical({q.x,q.y,q.z,q.w});
}
std::shared_ptr<const StaticCollisionLayer> scene_static_collision(const SpawnPlan& plan,const World& world,AssetId skin,const std::function<std::shared_ptr<const CollisionGeometry>(AssetId)>& geometry){
    std::vector<StaticCollisionBody> bodies;bodies.reserve(plan.models.size());
    for(const auto& [object,asset]:plan.models){
        if(asset==skin||plan.pickups.contains(object)||plan.altars.contains(object)||plan.movers.contains(object))continue;
        const auto handle=world.find(object);if(!world.valid(handle))continue;
        auto shape=geometry(asset);if(!shape)continue;
        const auto& pose=world.read(handle).transform;StaticCollisionBody body;body.object=object;body.id=static_body_id(object);body.geometry=std::move(shape);
        body.position={pose.x,pose.y,pose.z};body.rotation=placement_rotation(pose.pitch,pose.yaw,pose.roll);body.scale=pose.scale;bodies.push_back(std::move(body));
    }
    return std::make_shared<const StaticCollisionLayer>(std::move(bodies));
}
std::shared_ptr<const CollisionGeometry> CollisionGeometryCache::get(const std::shared_ptr<const CollisionMeshData>& data){
    require(data!=nullptr&&data->signature.size()==64,"Collision geometry needs signed mesh data");
    auto& slot=cache_[{data->runtime,data->signature}];if(auto held=slot.lock())return held;
    auto made=std::make_shared<const CollisionGeometry>(*data);slot=made;++builds_;return made;
}
}
