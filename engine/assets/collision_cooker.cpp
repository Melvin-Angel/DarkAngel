#include "assets_internal.hpp"
#include <darkangel/hash.hpp>
#include <darkangel/collision_asset.hpp>
#include <cmath>
#include <set>
namespace darkangel::assets_detail {
    Import import_collision(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
        auto bytes=read(source,1024*1024);
        auto definition=decode_collision_source(bytes);
        if(!inspect)require(ids.size()==1&&ids.at("$source")==definition.id,"Collision source UUID mismatch");
        Json boxes=Json::array();
        for(const auto& box:definition.boxes){
            Json record={{"id",std::to_string(box.id)},{"center",{box.center.x,box.center.y,box.center.z}},{"half",{box.half.x,box.half.y,box.half.z}},{"yaw",box.yaw},{"roll",box.roll},{"moving",box.moving}};
            if(definition.schema==2){record["dynamic"]=box.dynamic;record["mass"]=box.mass;record["sensor"]=box.sensor;}
            boxes.push_back(std::move(record));
        }
        Import out;
        out.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
        if(inspect)return out;
        auto id=ids.at("$source");
        Json product={
            {
                "schema",definition.schema
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
