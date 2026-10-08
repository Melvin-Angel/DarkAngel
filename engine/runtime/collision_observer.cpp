#include <darkangel/collision_asset.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <map>
namespace darkangel {
namespace {
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
using Quaternion=std::array<double,4>;
Quaternion rotation(const CollisionBox& box){double norm{};for(auto n:box.rotation)norm+=n*n;if(norm)return box.rotation;double y=box.yaw/2,z=box.roll/2;return {std::sin(y)*std::sin(z),std::sin(y)*std::cos(z),std::cos(y)*std::sin(z),std::cos(y)*std::cos(z)};}
Quaternion rotation(const CollisionActor& actor){return {0,std::sin(actor.yaw/2),0,std::cos(actor.yaw/2)};}
Quaternion blend(Quaternion a,Quaternion b,double t){double dot{};for(unsigned i=0;i<4;++i)dot+=a[i]*b[i];if(dot<0){for(auto& n:b)n=-n;dot=-dot;}dot=std::clamp(dot,0.0,1.0);double x=1-t,y=t;if(dot<.9995){double angle=std::acos(dot),sine=std::sin(angle);x=std::sin((1-t)*angle)/sine;y=std::sin(t*angle)/sine;}Quaternion result;double norm{};for(unsigned i=0;i<4;++i){result[i]=a[i]*x+b[i]*y;norm+=result[i]*result[i];}for(auto& n:result)n/=std::sqrt(norm);return result;}
MotorVec blend(MotorVec a,MotorVec b,double t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
using Identity=std::pair<bool,std::uint64_t>;
std::map<Identity,CollisionPresentationPose> poses(const CollisionStreamFrame& frame){std::map<Identity,CollisionPresentationPose> result;for(const auto& box:frame.boxes)result.emplace(Identity{false,box.id},CollisionPresentationPose{box.id,0,false,box.center,rotation(box)});for(const auto& mesh:frame.meshes)result.emplace(Identity{false,mesh.id},CollisionPresentationPose{mesh.id,0,false,{},{0,0,0,1}});for(const auto& actor:frame.actors)result.emplace(Identity{true,actor.id},CollisionPresentationPose{actor.id,actor.epoch,true,actor.foot,rotation(actor),actor.crouched});return result;}
bool compatible(const CollisionStreamFrame& a,const CollisionStreamFrame& b){if(a.topology!=b.topology)return false;auto x=poses(a),y=poses(b);if(x.size()!=y.size())return false;for(const auto& [id,pose]:x){auto found=y.find(id);if(found==y.end()||pose.epoch!=found->second.epoch)return false;}std::map<std::uint64_t,std::pair<AssetId,std::string>> meshes;for(const auto& mesh:a.meshes)meshes[mesh.id]={mesh.runtime,mesh.signature};if(meshes.size()!=b.meshes.size())return false;for(const auto& mesh:b.meshes){auto found=meshes.find(mesh.id);if(found==meshes.end()||found->second!=std::pair{mesh.runtime,mesh.signature})return false;}return true;}
}
void ObserverCollision::push(CollisionStreamFrame frame){validate_collision_stream(frame);if(!frames_.empty()){if(frame.tick<=frames_.back().tick)return;if(!compatible(frames_.back(),frame))frames_.clear();}frames_.push_back(std::move(frame));while(frames_.size()>8)frames_.pop_front();}
CollisionPresentationFrame ObserverCollision::sample(double tick)const{
    require(std::isfinite(tick)&&tick>=0&&tick<=1e9&&!frames_.empty(),"Observer collision clock/state");const auto* a=&frames_.front();const auto* b=a;bool held=tick<=a->tick;double t=0;
    if(tick>=frames_.back().tick){a=b=&frames_.back();held=true;}else if(tick>frames_.front().tick){for(std::size_t n=1;n<frames_.size();++n)if(tick<=frames_[n].tick){a=&frames_[n-1];b=&frames_[n];t=(tick-a->tick)/(b->tick-a->tick);held=false;break;}}
    CollisionPresentationFrame result{double(a->tick)+(double(b->tick)-a->tick)*t,a->topology,held,{}};auto first=poses(*a),second=poses(*b);for(const auto& [id,pose]:first){auto value=pose;const auto& other=second.at(id);value.crouched=t<1?pose.crouched:other.crouched;value.position=blend(pose.position,other.position,t);value.rotation=blend(pose.rotation,other.rotation,t);result.poses.push_back(value);}return result;
}
}
