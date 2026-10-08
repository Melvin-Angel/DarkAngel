#include "assets_internal.hpp"
#include <darkangel/hash.hpp>
#include <cmath>
#include <set>
namespace darkangel::assets_detail {
    Import import_collision(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
        auto bytes=read(source,1024*1024);
        auto data=json(bytes);
        require(data.is_object()&&data.size()==4&&data.at("kind")=="collision"&&data.at("schema")==1&&data.at("boxes").is_array()&&data.at("boxes").size()<=64,"Collision source schema/bounds");
        auto embedded=AssetId::parse(data.at("asset").get<std::string>());
        if(!inspect)require(ids.size()==1&&ids.at("$source")==embedded,"Collision source UUID mismatch");
        std::set<std::string> unique;
        Json boxes=Json::array();
        for(auto box:data.at("boxes")){
            require(box.is_object()&&box.size()==6,"Collision box schema");
            auto id=box.at("id").get<std::string>();
            require(!id.empty()&&id.size()<=19&&id[0]!='0'&&id.find_first_not_of("0123456789")==id.npos&&std::stoull(id)<(1ULL<<63)&&unique.insert(id).second,"Collision body identity");
            for(auto key:{
                "center","half"
            }){
                require(box.at(key).is_array()&&box.at(key).size()==3,"Collision vector dimension");
                for(auto number:box.at(key)){
                    auto value=number.get<double>();
                    require(std::isfinite(value)&&std::abs(value)<=100000&&(std::string_view(key)!="half"||value>0),"Collision vector value");
                }
            }for(auto key:{
                "yaw","roll"
            }){
                double value=box.at(key).get<double>();
                require(std::isfinite(value)&&std::abs(value)<=3.141593,"Collision orientation");
            }require(box.at("moving").is_boolean(),"Collision motion type");
            boxes.push_back(box);
        }
        Import out;
        out.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
        if(inspect)return out;
        auto id=ids.at("$source");
        Json product={
            {
                "schema",1
            },{
                "id",id.text()
            },{
                "kind","collision"
            },{
                "jolt","5.6.0"
            },{
                "axes","right-handed-y-up-metres"
            },{
                "boxes",boxes
            }
        };
        out.products.push_back({
            id,"collision","collision.json",product.dump(),{
            }
        });
        return out;
    }
}
