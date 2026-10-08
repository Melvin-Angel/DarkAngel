#include <darkangel/joint_mask.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <set>
#include <stdexcept>
namespace darkangel {
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
JointMaskSource decode_joint_mask_source(std::string_view bytes){
    using Json=nlohmann::json;require(bytes.size()<=65536,"Mask source byte budget");unsigned work{};std::vector<std::set<std::string>> keys;
    auto source=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){require(depth<=8&&++work<=4096,"Mask source work budget");if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate mask field");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});
    require(source.is_object()&&source.size()==6&&source.at("schema")==1&&source.at("kind")=="joint_mask","Mask source schema");
    JointMaskSource result;result.id=AssetId::parse(source.at("asset").get<std::string>());result.skeleton=AssetId::parse(source.at("skeleton").get<std::string>());result.canonical_source=source.at("canonical_source").get<std::string>();
    require(!result.canonical_source.empty()&&result.canonical_source.size()<=256&&result.canonical_source.front()!='/'&&result.canonical_source.find("..") == std::string::npos&&result.canonical_source.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-")==std::string::npos&&result.canonical_source.ends_with(".daskeleton"),"Mask canonical source path");
    require(source.at("weights").is_object()&&source.at("weights").size()<=256,"Mask joint budget");
    for(const auto& [key,value]:source.at("weights").items()){
        require(!key.empty()&&key.size()<=128&&key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string::npos&&value.is_number(),"Mask joint key/weight type");
        auto weight=value.get<float>();require(std::isfinite(weight)&&weight>=0&&weight<=1,"Mask weight bounds");result.weights.emplace(key,weight);
    }
    result.generation=sha256(source.dump());return result;
}
JointMaskDefinition compile_joint_mask(const JointMaskSource& source,const RigDefinition& rig){
    require(source.skeleton==rig.id&&!rig.joints.empty()&&rig.joints.size()<=256&&rig.signature.size()==64,"Mask skeleton compatibility");
    JointMaskDefinition result{source.id,rig.id,rig.signature,sha256(source.generation+rig.signature),std::vector<float>(rig.joints.size(),0)};
    std::map<std::string,unsigned> order;for(unsigned index=0;index<rig.joints.size();++index)require(order.emplace(rig.joints[index].key,index).second,"Mask duplicate canonical key");
    for(const auto& [key,weight]:source.weights){require(order.contains(key)&&std::isfinite(weight)&&weight>=0&&weight<=1,"Mask unknown joint/weight");result.weights[order.at(key)]=weight;}
    return result;
}
}
