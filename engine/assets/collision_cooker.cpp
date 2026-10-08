#include "assets_internal.hpp"
#include <darkangel/hash.hpp>
#include <darkangel/collision_asset.hpp>
#include <cmath>
#include <set>
namespace darkangel::assets_detail {
    Import import_collision(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
        auto bytes=read(source,1024*1024);
        auto definition=decode_collision_source(bytes);
        if(!inspect)require(ids.size()==definition.meshes.size()+1&&ids.at("$source")==definition.id,"Collision source UUID mismatch");
        Json boxes=Json::array();
        for(const auto& box:definition.boxes){
            Json record={{"id",std::to_string(box.id)},{"center",{box.center.x,box.center.y,box.center.z}},{"half",{box.half.x,box.half.y,box.half.z}},{"yaw",box.yaw},{"roll",box.roll},{"moving",box.moving}};
            if(definition.schema>=2){record["dynamic"]=box.dynamic;record["mass"]=box.mass;record["sensor"]=box.sensor;}
            boxes.push_back(std::move(record));
        }
        Import out;
        out.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
        for(const auto& mesh:definition.meshes)out.keys.push_back("mesh/"+std::to_string(mesh.id));
        out.product_count=definition.meshes.size()+1;
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
        std::vector<AssetId> required;
        if(definition.schema==3){
            product["meshes"]=Json::array();auto source_json=json(bytes);
            for(std::size_t i=0;i<definition.meshes.size();++i){
                const auto& mesh=definition.meshes[i];require(ids.at("mesh/"+std::to_string(mesh.id))==mesh.data->runtime,"Mesh product embedded UUID mismatch");
#if defined(DAE_COLLISION_MESH)
                CollisionGeometry validated(*mesh.data);
#else
                throw std::runtime_error("Static mesh cook requires explicit pinned physics tools");
#endif
                Json geometry={{"schema",1},{"kind","collision-geometry"},{"id",mesh.data->runtime.text()},{"jolt","5.6.0"},{"axes","right-handed-y-up-metres"},{"signature",mesh.data->signature},{"definition",source_json.at("meshes")[i]}};
                auto encoded=geometry.dump();json(encoded);out.products.push_back({mesh.data->runtime,"collision-geometry","collision-mesh.json",std::move(encoded),{}});
                product["meshes"].push_back({{"id",std::to_string(mesh.id)},{"runtime",mesh.data->runtime.text()},{"signature",mesh.data->signature}});required.push_back(mesh.data->runtime);
            }
        }
        auto encoded=product.dump();json(encoded);out.products.push_back({id,"collision","collision.json",std::move(encoded),std::move(required)});
        return out;
    }
}
namespace darkangel {
CollisionDefinition load_cooked_collision_scene(const std::filesystem::path& path,const std::filesystem::path& cas,AssetId root){
    using namespace assets_detail;auto registry=json(read(path,1024*1024));require(registry.at("schema")==1&&registry.at("root")==root.text()&&registry.at("assets").size()<=17,"Collision scene registry schema/bounds");
    std::map<AssetId,Json> records;for(const auto& record:registry.at("assets"))require(records.emplace(AssetId::parse(record.at("id").get<std::string>()),record).second,"Duplicate collision scene product");
    auto load=[&](AssetId id,const char* kind,const char* extension){require(records.contains(id),"Missing collision shape product");auto record=records.at(id);require(record.at("kind")==kind&&record.at("extension")==extension,"Collision shape product type");auto hash=record.at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Collision shape product digest");auto bytes=read(cas/(hash+"."+extension),1024*1024);require(sha256(bytes)==hash,"Collision shape product hash mismatch");return json(bytes);};
    auto product=load(root,"collision","collision.json");require(product.at("id")==root.text()&&product.at("jolt")=="5.6.0"&&product.at("axes")=="right-handed-y-up-metres"&&product.at("schema")>=1&&product.at("schema")<=3,"Collision scene product profile");
    Json source={{"schema",3},{"asset",root.text()},{"kind","collision"},{"boxes",Json::array()},{"meshes",Json::array()}};
    for(const auto& box:product.at("boxes")){
        auto record=box;const bool dynamic=box.value("dynamic",false),sensor=box.value("sensor",false);record.erase("moving");record.erase("dynamic");record.erase("sensor");record.erase("mass");
        record["motion"]=sensor?"sensor":dynamic?"dynamic":box.at("moving").get<bool>()?"kinematic":"static";if(dynamic)record["mass"]=box.at("mass");source["boxes"].push_back(record);
    }
    std::map<AssetId,std::string> signatures;
    if(product.at("schema")==3)for(const auto& record:product.at("meshes")){
        auto id=AssetId::parse(record.at("runtime").get<std::string>());auto geometry=load(id,"collision-geometry","collision-mesh.json");require(geometry.at("schema")==1&&geometry.at("id")==id.text()&&geometry.at("jolt")=="5.6.0"&&geometry.at("axes")=="right-handed-y-up-metres"&&geometry.at("definition").at("id")==record.at("id")&&geometry.at("definition").at("runtime")==id.text()&&geometry.at("signature")==record.at("signature"),"Frozen collision shape identity/signature");
        require(signatures.emplace(id,record.at("signature").get<std::string>()).second,"Duplicate collision shape reference");source["meshes"].push_back(geometry.at("definition"));
    }
    auto result=decode_collision_source(source.dump());for(const auto& mesh:result.meshes)require(mesh.data->signature==signatures.at(mesh.data->runtime),"Collision geometry content signature");return result;
}
}
