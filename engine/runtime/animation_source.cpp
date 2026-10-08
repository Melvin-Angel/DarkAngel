#include <darkangel/animation.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <set>
#include <stdexcept>
namespace darkangel {
    RigDefinition decode_rig_source(std::string_view text){
        using Json=nlohmann::json;
        auto require=[](bool b,const char* s){
            if(!b)throw std::runtime_error(s);
        };
        require(text.size()<=1024*1024,"Rig source byte limit");
        std::size_t events{
        };
        std::vector<std::set<std::string>> object_keys;
        auto j=Json::parse(text,[&](int depth,Json::parse_event_t event,Json& value){
            require(depth<=16&&++events<=32768,"Rig source parse bounds");if(event==Json::parse_event_t::object_start)object_keys.emplace_back();if(event==Json::parse_event_t::key)require(object_keys.back().insert(value.get<std::string>()).second,"Duplicate rig source field");if(event==Json::parse_event_t::object_end)object_keys.pop_back();return true;
        });
        require(j.is_object()&&j.size()==8&&j.at("schema")==1&&j.at("kind")=="skeleton"&&j.at("axes")=="right-handed-y-up-metres","Rig source schema/axes");
        RigDefinition rig;
        rig.id=AssetId::parse(j.at("asset").get<std::string>());
        rig.runtime=AssetId::parse(j.at("runtime").get<std::string>());
        require(rig.id!=rig.runtime,"Rig product identity collision");
        rig.human=j.at("human").get<bool>();
        require(j.at("joints").is_array()&&!j.at("joints").empty()&&j.at("joints").size()<=256,"Rig joint bounds");
        std::map<std::string,int> keys;
        unsigned roots{
        };
        for(auto joint:j.at("joints")){
            require(joint.is_object()&&joint.size()==5,"Rig joint schema");
            RigJoint value;
            value.key=joint.at("key").get<std::string>();
            require(!value.key.empty()&&value.key.size()<=64&&keys.emplace(value.key,static_cast<int>(rig.joints.size())).second,"Rig joint key uniqueness");
            if(joint.at("parent").is_null())++roots;
            else{
                auto key=joint.at("parent").get<std::string>();
                require(key!=value.key&&keys.contains(key),"Rig hierarchy requires preceding parent");
                value.parent=keys.at(key);
            }
            value.translation=joint.at("translation").get<std::array<float,3>>();
            value.rotation=joint.at("rotation").get<std::array<float,4>>();
            value.scale=joint.at("scale").get<std::array<float,3>>();
            for(float n:value.translation)require(std::isfinite(n)&&std::abs(n)<=100,"Rig translation bounds");
            float length{
            };
            for(float n:value.rotation){
                require(std::isfinite(n),"Rig rotation finite");
                length+=n*n;
            }require(std::abs(length-1)<.0001f,"Rig quaternion normalization");
            for(float n:value.scale)require(std::isfinite(n)&&std::abs(n-1)<.0001f,"Only normalized unit-scale rigs supported");
            rig.joints.push_back(value);
        }
        require(roots==1,"Rig requires one root");
        std::vector<std::vector<int>> children(rig.joints.size());
        int root{
        };
        for(int i=0;i<rig.joints.size();++i){
            if(rig.joints[i].parent<0)root=i;
            else children[rig.joints[i].parent].push_back(i);
        }int next{
        };
        std::function<void(int,unsigned)> visit=[&](int i,unsigned depth){
            require(depth<=64&&i==next++,"Rig canonical depth-first order/depth");
            for(int child:children[i])visit(child,depth+1);
        };
        visit(root,0);
        require(j.at("sockets").is_object()&&j.at("sockets").size()<=32,"Rig socket bounds");
        for(auto [name,key]:j.at("sockets").items()){
            require(!name.empty()&&name.size()<=64&&key.is_string()&&keys.contains(key.get<std::string>()),"Rig socket joint reference");
            rig.sockets[name]=key.get<std::string>();
        }
        if(rig.human)require(keys.contains("Root_M")&&keys.contains("Wrist_R")&&keys.contains("Wrist_L")&&rig.sockets.contains("weapon_right")&&rig.sockets.contains("weapon_left"),"Canonical human joint/socket contract");
        rig.signature=sha256(Json{
            {
                "axes",j.at("axes")
            },{
                "human",rig.human
            },{
                "joints",j.at("joints")
            },{
                "sockets",j.at("sockets")
            }
        }.dump());
        return rig;
    }
}
