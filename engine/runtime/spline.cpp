#include <darkangel/spline.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
bool finite(const MotorVec& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
double apart(const MotorVec& a,const MotorVec& b){const double x=b.x-a.x,y=b.y-a.y,z=b.z-a.z;return std::sqrt(x*x+y*y+z*z);}
// A number in [0, 1) from a seed, an instance index and a channel: the same everywhere.
double chance(std::uint32_t seed,std::uint64_t index,std::uint64_t channel){
    std::uint64_t value=(std::uint64_t(seed)<<32)^(index*0x9e3779b97f4a7c15ull)^(channel*0xbf58476d1ce4e5b9ull);
    value+=0x9e3779b97f4a7c15ull;value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;value=(value^(value>>27))*0x94d049bb133111ebull;value^=value>>31;
    return double(value>>11)/9007199254740992.;
}
}
void validate_spline(const SplineDefinition& spline){
    if(!spline.scatter)return;const auto& scatter=*spline.scatter;
    require(scatter.model!=AssetId{},"A scatter needs a model");
    require(std::isfinite(scatter.spacing)&&scatter.spacing>=.25&&scatter.spacing<=50,"Scatter spacing is 0.25 to 50 m");
    require(std::isfinite(scatter.jitter)&&scatter.jitter>=0&&scatter.jitter<=5,"Scatter jitter is 0 to 5 m");
}
SplineDefinition decode_spline(std::string_view text){
    require(text.size()<=4096,"Spline component size bound");const auto value=Json::parse(text);require(value.is_object(),"Spline component type");
    for(const auto& [key,field]:value.items())require(key=="closed"||key=="scatter","Spline component has an unknown field");
    SplineDefinition result;if(value.contains("closed")){require(value.at("closed").is_boolean(),"Spline closed type");result.closed=value.at("closed").get<bool>();}
    if(value.contains("scatter")&&!value.at("scatter").is_null()){
        const auto& source=value.at("scatter");require(source.is_object(),"Scatter type");for(const auto& [key,field]:source.items())require(key=="model"||key=="spacing"||key=="jitter"||key=="seed"||key=="align","Scatter has an unknown field");
        require(source.contains("model")&&source.at("model").is_string(),"A scatter needs a model");ScatterDefinition scatter;scatter.model=AssetId::parse(source.at("model").get<std::string>());
        if(source.contains("spacing")){require(source.at("spacing").is_number(),"Scatter spacing type");scatter.spacing=source.at("spacing").get<double>();}
        if(source.contains("jitter")){require(source.at("jitter").is_number(),"Scatter jitter type");scatter.jitter=source.at("jitter").get<double>();}
        if(source.contains("seed")){require(source.at("seed").is_number_unsigned()&&source.at("seed").get<std::uint64_t>()<=UINT32_MAX,"Scatter seed is a whole number up to 4294967295");scatter.seed=source.at("seed").get<std::uint32_t>();}
        if(source.contains("align")){require(source.at("align").is_boolean(),"Scatter align type");scatter.align=source.at("align").get<bool>();}
        result.scatter=scatter;}
    validate_spline(result);return result;
}
Spline::Spline(std::vector<MotorVec> points,bool closed){
    require(points.size()>=2&&points.size()<=spline_point_limit,"A spline has 2 to 32 points");for(const auto& point:points)require(finite(point),"Spline point position");
    const auto count=points.size();const auto at=[&](std::ptrdiff_t index)->const MotorVec&{if(closed){const auto size=std::ptrdiff_t(count);return points[std::size_t(((index%size)+size)%size)];}return points[std::size_t(std::clamp<std::ptrdiff_t>(index,0,std::ptrdiff_t(count)-1))];};
    const std::size_t segments=closed?count:count-1;constexpr unsigned steps=12;line_.push_back(points.front());lengths_.push_back(0);
    for(std::size_t segment=0;segment<segments;++segment){const auto& a=at(std::ptrdiff_t(segment)-1);const auto& b=at(std::ptrdiff_t(segment));const auto& c=at(std::ptrdiff_t(segment)+1);const auto& d=at(std::ptrdiff_t(segment)+2);
        for(unsigned step=1;step<=steps;++step){const double t=double(step)/steps,t2=t*t,t3=t2*t;const auto axis=[&](double p0,double p1,double p2,double p3){return .5*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t2+(-p0+3*p1-3*p2+p3)*t3);};
            // Two points are a straight line whatever the curve's rule says.
            const MotorVec next=count==2&&!closed?MotorVec{b.x+(c.x-b.x)*t,b.y+(c.y-b.y)*t,b.z+(c.z-b.z)*t}:MotorVec{axis(a.x,b.x,c.x,d.x),axis(a.y,b.y,c.y,d.y),axis(a.z,b.z,c.z,d.z)};
            lengths_.push_back(lengths_.back()+apart(line_.back(),next));line_.push_back(next);}}
    require(lengths_.back()>1e-6,"A spline's points are all in one place");
}
SplineSample Spline::sample(double distance)const{
    require(std::isfinite(distance),"Spline sample distance");const double along=std::clamp(distance,0.,lengths_.back());
    auto upper=std::upper_bound(lengths_.begin(),lengths_.end(),along);std::size_t piece=upper==lengths_.end()?lengths_.size()-2:std::size_t(std::max<std::ptrdiff_t>(0,upper-lengths_.begin()-1));
    // Skip pieces of no length so the tangent is defined.
    while(piece+2<lengths_.size()&&lengths_[piece+1]-lengths_[piece]<1e-9)++piece;while(piece>0&&lengths_[piece+1]-lengths_[piece]<1e-9)--piece;
    const auto& a=line_[piece];const auto& b=line_[piece+1];const double span=lengths_[piece+1]-lengths_[piece],t=span>1e-9?std::clamp((along-lengths_[piece])/span,0.,1.):0;
    SplineSample result;result.position={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};const double length=apart(a,b);result.tangent=length>1e-9?MotorVec{(b.x-a.x)/length,(b.y-a.y)/length,(b.z-a.z)/length}:MotorVec{0,0,1};return result;
}
std::vector<ScatterInstance> scatter_along(const Spline& spline,const ScatterDefinition& scatter,bool closed,bool* truncated){
    SplineDefinition checked;checked.scatter=scatter;validate_spline(checked);if(truncated)*truncated=false;
    // An open curve has an instance at both ends when the spacing divides it; a closed one does not repeat its start.
    const double length=spline.length();std::size_t count=std::size_t(std::floor(length/scatter.spacing+1e-9))+(closed?0:1);
    if(count>scatter_instance_limit){count=scatter_instance_limit;if(truncated)*truncated=true;}
    std::vector<ScatterInstance> result;result.reserve(count);
    for(std::size_t index=0;index<count;++index){const auto at=spline.sample(double(index)*scatter.spacing);const double level=std::hypot(at.tangent.x,at.tangent.z);
        // Sideways is the tangent turned a quarter over the ground; a vertical piece has no side and gets no jitter.
        const double side=(chance(scatter.seed,index,1)*2-1)*scatter.jitter;MotorVec position=at.position;if(level>1e-6){position.x+=at.tangent.z/level*side;position.z-=at.tangent.x/level*side;}
        const double yaw=scatter.align&&level>1e-6?std::atan2(at.tangent.x,at.tangent.z):chance(scatter.seed,index,2)*6.283185307179586-3.141592653589793;result.push_back({position,yaw});}
    return result;
}
}
