#include <darkangel/animation.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <set>
#include <stdexcept>
namespace darkangel {
    ClipMotion clip_root_delta(const ClipDefinition& clip,double from,double to){
        if(!std::isfinite(from)||!std::isfinite(to)||from<0||to<from||to>1e9||to-from>8||!clip.ticks||clip.ticks>600||clip.root.size()!=clip.ticks+1)throw std::runtime_error("Clip root interval bounds");
        auto compose=[](ClipMotion a,ClipMotion b){
            const auto c=std::cos(a.yaw),s=std::sin(a.yaw);
            return ClipMotion{{a.translation[0]+c*b.translation[0]+s*b.translation[2],a.translation[1]+b.translation[1],a.translation[2]-s*b.translation[0]+c*b.translation[2]},a.yaw+b.yaw};
        };
        auto sample=[&](double tick){
            const auto phase=std::min(tick,double(clip.ticks));const auto i=std::min(static_cast<unsigned>(phase),clip.ticks-1);const auto ratio=phase-i;
            ClipMotion value;for(unsigned a=0;a<3;++a)value.translation[a]=clip.root[i][a]+(clip.root[i+1][a]-clip.root[i][a])*ratio;value.yaw=clip.root[i][3]+(clip.root[i+1][3]-clip.root[i][3])*ratio;return value;
        };
        auto cumulative=[&](double tick){
            if(!clip.loop)return sample(tick);
            auto loops=static_cast<std::uint64_t>(tick/clip.ticks);auto power=sample(clip.ticks);ClipMotion result;
            while(loops){if(loops&1)result=compose(result,power);power=compose(power,power);loops>>=1;}
            return compose(result,sample(std::fmod(tick,double(clip.ticks))));
        };
        auto a=cumulative(from),b=cumulative(to);auto c=std::cos(a.yaw),s=std::sin(a.yaw);auto x=b.translation[0]-a.translation[0],z=b.translation[2]-a.translation[2];
        return {{c*x-s*z,b.translation[1]-a.translation[1],s*x+c*z},b.yaw-a.yaw};
    }
    ClipDefinition decode_clip_manifest(std::string_view text){
        using Json=nlohmann::json;
        auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        require(text.size()<=256*1024,"Clip manifest byte limit");
        std::size_t events{};std::vector<std::set<std::string>> keys;
        auto j=Json::parse(text,[&](int depth,Json::parse_event_t event,Json& value){
            require(depth<=8&&++events<=8192,"Clip manifest work bounds");
            if(event==Json::parse_event_t::object_start)keys.emplace_back();
            if(event==Json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate clip field");
            if(event==Json::parse_event_t::object_end)keys.pop_back();return true;
        });
        require(j.is_object()&&j.size()==12&&j.at("schema")==1&&j.at("kind")=="clip"&&j.at("ozz")=="744eb9d99f606eda849acb0b1204f7a3dc20bca1"&&j.at("root_policy")=="stripped-translation-yaw-60hz","Clip manifest schema/profile");
        ClipDefinition clip;clip.id=AssetId::parse(j.at("id").get<std::string>());clip.runtime=AssetId::parse(j.at("runtime").get<std::string>());clip.skeleton=AssetId::parse(j.at("skeleton").get<std::string>());
        require(clip.id!=clip.runtime&&clip.id!=clip.skeleton&&clip.runtime!=clip.skeleton,"Clip identity collision");
        clip.signature=j.at("signature").get<std::string>();require(clip.signature.size()==64&&clip.signature.find_first_not_of("0123456789abcdef")==clip.signature.npos,"Clip signature");
        require(j.at("ticks").is_number_unsigned()&&j.at("joints").is_number_unsigned(),"Clip count types");
        require(j.at("ticks").get<std::uint64_t>()<=600&&j.at("joints").get<std::uint64_t>()<=256,"Clip count range");
        clip.ticks=j.at("ticks").get<unsigned>();clip.joints=j.at("joints").get<unsigned>();clip.loop=j.at("loop").get<bool>();
        require(clip.ticks>0&&clip.ticks<=600&&clip.joints>0&&clip.joints<=256&&j.at("root").is_array()&&j.at("root").size()==clip.ticks+1,"Clip duration/root bounds");
        for(const auto& key:j.at("root")){
            auto value=key.get<std::array<float,4>>();for(float n:value)require(std::isfinite(n)&&std::abs(n)<=100,"Clip root finite/range");clip.root.push_back(value);
        }
        for(float n:clip.root.front())require(std::abs(n)<.00001f,"Clip root starts at identity");
        return clip;
    }
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
