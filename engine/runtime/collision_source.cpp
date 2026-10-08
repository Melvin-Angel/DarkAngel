#include <darkangel/collision_asset.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <set>
#include <stdexcept>
namespace darkangel {
CollisionDefinition decode_collision_source(std::string_view bytes){
    using Json=nlohmann::json;auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    require(bytes.size()<=1024*1024,"Collision source byte limit");std::size_t events{};std::vector<std::set<std::string>> keys;
    auto data=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){
        require(depth<=16&&++events<=16384,"Collision source work bounds");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate collision source field");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;
    });
    require(data.is_object()&&data.size()==4&&data.at("kind")=="collision"&&(data.at("schema")==1||data.at("schema")==2)&&data.at("boxes").is_array()&&data.at("boxes").size()<=64,"Collision source schema/bounds");
    CollisionDefinition definition;definition.id=AssetId::parse(data.at("asset").get<std::string>());definition.schema=data.at("schema").get<unsigned>();std::set<std::uint64_t> identities;
    for(const auto& box:data.at("boxes")){
        require(box.is_object(),"Collision box schema");CollisionBox value;
        const auto id=box.at("id").get<std::string>();require(!id.empty()&&id.size()<=19&&id[0]!='0'&&id.find_first_not_of("0123456789")==id.npos,"Collision identity encoding");value.id=std::stoull(id);require(value.id<(1ULL<<63)&&identities.insert(value.id).second,"Collision identity range/uniqueness");
        auto center=box.at("center").get<std::array<double,3>>(),half=box.at("half").get<std::array<double,3>>();
        for(unsigned i=0;i<3;++i)require(std::isfinite(center[i])&&std::abs(center[i])<=100000&&std::isfinite(half[i])&&half[i]>0&&half[i]<=100000,"Collision geometry bounds");
        value.center={center[0],center[1],center[2]};value.half={half[0],half[1],half[2]};value.yaw=box.at("yaw").get<double>();value.roll=box.at("roll").get<double>();
        require(std::isfinite(value.yaw)&&std::abs(value.yaw)<=3.141593&&std::isfinite(value.roll)&&std::abs(value.roll)<=3.141593,"Collision orientation bounds");
        if(definition.schema==1){require(box.size()==6&&box.at("moving").is_boolean(),"Collision legacy box schema");value.moving=box.at("moving").get<bool>();}
        else{
            const auto motion=box.at("motion").get<std::string>();require(motion=="static"||motion=="kinematic"||motion=="dynamic"||motion=="sensor","Collision motion profile");value.moving=motion=="kinematic";value.dynamic=motion=="dynamic";value.sensor=motion=="sensor";
            require(box.size()==(value.dynamic?7:6),"Collision extended box schema");if(value.dynamic)value.mass=box.at("mass").get<double>();require(std::isfinite(value.mass)&&value.mass>=1&&value.mass<=1000,"Collision mass bounds");
        }
        definition.boxes.push_back(value);
    }
    return definition;
}
}
