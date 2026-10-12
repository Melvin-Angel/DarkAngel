#include <darkangel/presentation_primitives.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
Json source(std::string_view text,std::initializer_list<const char*> fields,const char* kind,const char* error){
    require(text.size()<=4096,"Presentation primitive source size bound");const auto value=Json::parse(text);require(value.is_object()&&value.size()==fields.size(),error);for(auto field:fields)require(value.contains(field),error);
    require(value.at("schema")==1&&value.at("kind")==kind,error);return value;
}
AssetId identity(const Json& value){require(value.at("asset").is_string(),"Presentation primitive identity");const auto id=AssetId::parse(value.at("asset").get<std::string>());require(id!=AssetId{},"Presentation primitive identity");return id;}
std::string display(const Json& value){require(value.at("name").is_string(),"Presentation primitive name type");auto name=value.at("name").get<std::string>();require(!name.empty()&&name.size()<=64,"Presentation primitive name bounds (1 to 64 characters)");return name;}
float number(const Json& value,double least,double most,const char* error){require(value.is_number(),error);const auto read=value.get<double>();require(std::isfinite(read)&&read>=least&&read<=most,error);return float(read);}
unsigned count(const Json& value,unsigned least,unsigned most,const char* error){require(value.is_number_unsigned(),error);const auto read=value.get<std::uint64_t>();require(read>=least&&read<=most,error);return unsigned(read);}
template<std::size_t N>std::array<float,N> numbers(const Json& value,double least,double most,const char* error){require(value.is_array()&&value.size()==N,error);std::array<float,N> out{};for(std::size_t index=0;index<N;++index)out[index]=number(value[index],least,most,error);return out;}
std::array<float,3> sub(const std::array<float,3>& a,const std::array<float,3>& b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
float length(const std::array<float,3>& a){return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
std::array<float,3> cross(const std::array<float,3>& a,const std::array<float,3>& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
std::array<float,3> unit(const std::array<float,3>& a,const std::array<float,3>& fallback){const auto size=length(a);return size>1e-6f?std::array<float,3>{a[0]/size,a[1]/size,a[2]/size}:fallback;}
}
TrailDefinition decode_trail(std::string_view text){
    const auto value=source(text,{"schema","kind","asset","name","socket","base","tip","lifetime_ticks","max_points","color"},"trail","Trail source fields");TrailDefinition trail;trail.id=identity(value);trail.name=display(value);
    require(value.at("socket").is_string(),"Trail socket type");trail.socket=value.at("socket").get<std::string>();require(!trail.socket.empty()&&trail.socket.size()<=64&&trail.socket.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==trail.socket.npos,"Trail socket: 1 to 64 letters, digits, '_', '.' or '-'");
    trail.base=numbers<3>(value.at("base"),-5,5,"Trail base offset: three numbers within 5 metres");trail.tip=numbers<3>(value.at("tip"),-5,5,"Trail tip offset: three numbers within 5 metres");require(length(sub(trail.tip,trail.base))>=.01f,"A trail's base and tip are at least a centimetre apart");
    trail.lifetime_ticks=count(value.at("lifetime_ticks"),1,120,"Trail lifetime bounds (1 to 120 ticks)");trail.max_points=count(value.at("max_points"),2,64,"Trail point bounds (2 to 64)");trail.color=numbers<4>(value.at("color"),0,1,"Trail colour: four numbers from 0 to 1");return trail;
}
DecalDefinition decode_decal(std::string_view text){
    const auto value=source(text,{"schema","kind","asset","name","size","lifetime_ticks","fade_ticks","color"},"decal","Decal source fields");DecalDefinition decal;decal.id=identity(value);decal.name=display(value);
    decal.size=number(value.at("size"),.05,10,"Decal size bounds (0.05 to 10 metres)");decal.lifetime_ticks=count(value.at("lifetime_ticks"),1,3600,"Decal lifetime bounds (1 to 3600 ticks)");decal.fade_ticks=count(value.at("fade_ticks"),0,decal.lifetime_ticks,"A decal's fade is no longer than its lifetime");
    decal.color=numbers<4>(value.at("color"),0,1,"Decal colour: four numbers from 0 to 1");return decal;
}
WeaponTrails::WeaponTrails(std::size_t limit):limit_(limit){require(limit>=1&&limit<=256,"Trail set bounds");}
bool WeaponTrails::begin(std::uint64_t instance,std::uint64_t network,const TrailDefinition& definition){
    if(trails_.contains(instance))return true;if(trails_.size()>=limit_){++dropped_;return false;}trails_.emplace(instance,Trail{network,definition,{},true});return true;
}
void WeaponTrails::end(std::uint64_t instance){if(const auto found=trails_.find(instance);found!=trails_.end()){found->second.emitting=false;if(found->second.points.empty())trails_.erase(found);}}
void WeaponTrails::retire(std::uint64_t network){std::erase_if(trails_,[&](const auto& item){return item.second.network==network;});}
std::vector<WeaponTrails::Request> WeaponTrails::emitting()const{std::vector<Request> out;for(const auto& [instance,trail]:trails_)if(trail.emitting)out.push_back({instance,trail.network,&trail.definition});return out;}
void WeaponTrails::sample(std::uint64_t instance,std::uint64_t tick,std::array<float,3> base,std::array<float,3> tip){
    const auto found=trails_.find(instance);if(found==trails_.end()||!found->second.emitting)return;auto& trail=found->second;for(const auto value:{base[0],base[1],base[2],tip[0],tip[1],tip[2]})require(std::isfinite(value),"Trail sample is not finite");
    if(!trail.points.empty()&&trail.points.back().tick==tick)return;Point point{tick,base,tip,trail.points.empty()};
    // A jump breaks the strip: the new pair starts a piece of its own.
    if(!trail.points.empty()&&(length(sub(base,trail.points.back().base))>break_distance||length(sub(tip,trail.points.back().tip))>break_distance)){point.first=true;++breaks_;}
    trail.points.push_back(point);if(trail.points.size()>trail.definition.max_points){trail.points.erase(trail.points.begin());trail.points.front().first=true;}
}
void WeaponTrails::advance(std::uint64_t tick){
    for(auto at=trails_.begin();at!=trails_.end();){auto& trail=at->second;const auto old=std::find_if(trail.points.begin(),trail.points.end(),[&](const Point& point){return point.tick+trail.definition.lifetime_ticks>tick;});
        if(old!=trail.points.begin()){trail.points.erase(trail.points.begin(),old);if(!trail.points.empty())trail.points.front().first=true;}
        if(!trail.emitting&&trail.points.size()<2)at=trails_.erase(at);else ++at;}
}
std::size_t WeaponTrails::points()const{std::size_t all{};for(const auto& [instance,trail]:trails_)all+=trail.points.size();return all;}
void WeaponTrails::build(std::uint64_t tick,std::vector<PrimitiveVertex>& out)const{
    for(const auto& [instance,trail]:trails_){const auto& definition=trail.definition;
        const auto shade=[&](const Point& point,float edge){auto color=definition.color;const float age=float(tick>point.tick?tick-point.tick:0)/float(definition.lifetime_ticks);color[3]*=std::clamp(1.f-age,0.f,1.f)*edge;return color;};
        // The base edge is drawn fainter than the tip, so the sweep reads as a blade's arc.
        for(std::size_t index=1;index<trail.points.size();++index){const auto& from=trail.points[index-1];const auto& to=trail.points[index];if(to.first)continue;
            out.push_back({from.base,shade(from,.25f)});out.push_back({from.tip,shade(from,1)});out.push_back({to.base,shade(to,.25f)});out.push_back({to.tip,shade(to,1)});}}
}
DecalPool::DecalPool(std::size_t limit):limit_(limit){require(limit>=1&&limit<=1024,"Decal pool bounds");}
void DecalPool::spawn(const DecalDefinition& definition,std::array<float,3> position,std::array<float,3> normal,std::uint64_t tick){
    for(const auto value:{position[0],position[1],position[2],normal[0],normal[1],normal[2]})require(std::isfinite(value),"Decal placement is not finite");
    if(decals_.size()>=limit_){decals_.erase(decals_.begin());++replaced_;}decals_.push_back({definition,position,unit(normal,{0,1,0}),tick});
}
void DecalPool::advance(std::uint64_t tick){std::erase_if(decals_,[&](const Decal& decal){return tick>=decal.born+decal.definition.lifetime_ticks;});}
void DecalPool::build(std::uint64_t tick,std::vector<PrimitiveVertex>& out)const{
    constexpr int segments=12;constexpr float lift=.02f,turn=6.2831853f/segments;
    for(const auto& decal:decals_){const auto& definition=decal.definition;const auto age=tick>decal.born?tick-decal.born:0;if(age>=definition.lifetime_ticks)continue;const auto left=definition.lifetime_ticks-age;
        const float fade=definition.fade_ticks&&left<definition.fade_ticks?float(left)/float(definition.fade_ticks):1.f;auto centre=definition.color;centre[3]*=fade;auto rim=centre;rim[3]=0;
        // Two directions across the surface, from its normal.
        const auto& n=decal.normal;const auto side=unit(cross(std::abs(n[1])<.99f?std::array<float,3>{0,1,0}:std::array<float,3>{1,0,0},n),{1,0,0}),forward=cross(n,side);const float radius=definition.size*.5f;
        const std::array<float,3> middle{decal.position[0]+n[0]*lift,decal.position[1]+n[1]*lift,decal.position[2]+n[2]*lift};
        const auto edge=[&](int index){const float angle=turn*float(index),c=std::cos(angle)*radius,s=std::sin(angle)*radius;return std::array<float,3>{middle[0]+side[0]*c+forward[0]*s,middle[1]+side[1]*c+forward[1]*s,middle[2]+side[2]*c+forward[2]*s};};
        // A disc as a fan of quads whose first edge is the centre: opaque in the middle, clear at the rim.
        for(int index=0;index<segments;++index){out.push_back({middle,centre});out.push_back({middle,centre});out.push_back({edge(index),rim});out.push_back({edge(index+1),rim});}}
}
}
