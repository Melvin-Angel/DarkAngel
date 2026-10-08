#include <darkangel/collision_asset.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <set>
#include <stdexcept>
#include <algorithm>
#include <map>
namespace darkangel {
void validate_collision_stream(const CollisionStreamFrame& frame){
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    auto vector=[&](MotorVec v,double bound=100000){for(auto n:{v.x,v.y,v.z})require(std::isfinite(n)&&std::abs(n)<=bound,"Collision stream vector bounds");};
    require(frame.topology&&frame.tick<UINT64_MAX&&frame.boxes.size()+frame.meshes.size()<=64&&frame.meshes.size()<=16&&frame.actors.size()<=4,"Collision stream inventory/clock bounds");std::set<std::uint64_t> bodies,actors;std::set<AssetId> products;
    for(const auto& box:frame.boxes){require(box.id&&box.id<(1ULL<<63)&&bodies.insert(box.id).second,"Collision stream body identity");vector(box.center);vector(box.half);require(box.half.x>0&&box.half.y>0&&box.half.z>0,"Collision stream extents");vector(box.velocity);vector(box.angular);require(std::isfinite(box.yaw)&&std::isfinite(box.roll)&&std::abs(box.yaw)<=3.141593&&std::abs(box.roll)<=3.141593&&!(box.dynamic&&box.moving)&&(!box.sensor||(!box.dynamic&&!box.moving))&&(box.dynamic||(box.angular.x==0&&box.angular.z==0))&&std::isfinite(box.mass)&&box.mass>=1&&box.mass<=1000,"Collision stream box profile");double norm{};for(auto n:box.rotation){require(std::isfinite(n),"Collision stream quaternion");norm+=n*n;}require(!norm||std::abs(norm-1)<.0001,"Collision stream quaternion norm");}
    for(const auto& actor:frame.actors){require(actor.id&&actor.id<(1ULL<<63)&&actor.epoch&&actors.insert(actor.id).second&&std::isfinite(actor.yaw)&&std::abs(actor.yaw)<=100000,"Collision stream actor identity/epoch/yaw");vector(actor.foot);vector(actor.velocity);}
    for(const auto& mesh:frame.meshes){require(mesh.id&&mesh.id<(1ULL<<63)&&bodies.insert(mesh.id).second&&mesh.signature.size()==64&&mesh.signature.find_first_not_of("0123456789abcdef")==mesh.signature.npos&&products.insert(mesh.runtime).second,"Collision stream geometry identity/signature");AssetId::parse(mesh.runtime.text());}
}
void validate_collision_mesh(const CollisionMeshData& data){
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    AssetId::parse(data.runtime.text());
    require(data.vertices.size()>=3&&data.vertices.size()<=2048&&!data.triangles.empty()&&data.triangles.size()<=4096&&!data.materials.empty()&&data.materials.size()<=32,"Static mesh geometry limits");
    std::set<std::string> materials;for(const auto& key:data.materials)require(!key.empty()&&key.size()<=64&&key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==key.npos&&materials.insert(key).second,"Static mesh material keys");
    std::map<std::array<float,3>,unsigned> positions;std::vector<unsigned> canonical;
    for(auto vertex:data.vertices){for(double n:{vertex.x,vertex.y,vertex.z})require(std::isfinite(n)&&std::abs(n)<=100000,"Mesh vertex bounds");auto point=std::array{float(vertex.x),float(vertex.y),float(vertex.z)};auto [it,inserted]=positions.emplace(point,static_cast<unsigned>(positions.size()));canonical.push_back(it->second);}
    std::set<std::uint32_t> keys;std::set<std::array<unsigned,3>> faces;
    for(const auto& triangle:data.triangles){
        require(triangle.material<data.materials.size()&&keys.insert(triangle.key).second,"Mesh triangle material/key");
        auto indices=triangle.vertices;for(auto index:indices)require(index<data.vertices.size(),"Mesh triangle vertex reference");
        auto face=std::array{canonical[indices[0]],canonical[indices[1]],canonical[indices[2]]};std::sort(face.begin(),face.end());require(faces.insert(face).second,"Duplicate/opposite mesh face");
        auto a=data.vertices[indices[0]],b=data.vertices[indices[1]],c=data.vertices[indices[2]];
        double ux=float(b.x)-float(a.x),uy=float(b.y)-float(a.y),uz=float(b.z)-float(a.z),vx=float(c.x)-float(a.x),vy=float(c.y)-float(a.y),vz=float(c.z)-float(a.z);
        auto x=uy*vz-uz*vy,y=uz*vx-ux*vz,z=ux*vy-uy*vx;require(x*x+y*y+z*z>1e-12,"Degenerate/quantized mesh triangle");
    }
}
std::string collision_mesh_signature(const CollisionMeshData& data){
    nlohmann::json vertices=nlohmann::json::array(),triangles=nlohmann::json::array();for(auto v:data.vertices)vertices.push_back({v.x,v.y,v.z});for(const auto& t:data.triangles)triangles.push_back({t.key,t.vertices,t.material});
    return sha256(nlohmann::json{{"runtime",data.runtime.text()},{"vertices",vertices},{"triangles",triangles},{"materials",data.materials}}.dump());
}
CollisionDefinition decode_collision_source(std::string_view bytes){
    using Json=nlohmann::json;auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    require(bytes.size()<=1024*1024,"Collision source byte limit");std::size_t events{};std::vector<std::set<std::string>> keys;
    auto data=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){
        require(depth<=16&&++events<=65536,"Collision source work bounds");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate collision source field");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;
    });
    require(data.is_object()&&data.size()==(data.value("schema",0)==3?5:4)&&data.at("kind")=="collision"&&(data.at("schema")==1||data.at("schema")==2||data.at("schema")==3)&&data.at("boxes").is_array()&&data.at("boxes").size()<=64,"Collision source schema/bounds");
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
    if(definition.schema==3){
        require(data.at("meshes").is_array()&&data.at("meshes").size()<=16&&data.at("meshes").size()+definition.boxes.size()<=64,"Collision mesh/body inventory bound");std::set<AssetId> products{definition.id};
        for(const auto& source:data.at("meshes")){
            require(source.is_object()&&source.size()==5,"Static mesh source schema");const auto encoded=source.at("id").get<std::string>();require(!encoded.empty()&&encoded.size()<=19&&encoded[0]!='0'&&encoded.find_first_not_of("0123456789")==encoded.npos,"Mesh identity encoding");
            const auto id=std::stoull(encoded);require(id<(1ULL<<63)&&identities.insert(id).second,"Mesh body identity collision");auto mesh=std::make_shared<CollisionMeshData>();mesh->runtime=AssetId::parse(source.at("runtime").get<std::string>());require(products.insert(mesh->runtime).second,"Duplicate mesh product UUID");
            require(source.at("vertices").is_array()&&source.at("vertices").size()<=2048&&source.at("triangles").is_array()&&source.at("triangles").size()<=4096,"Mesh source work limit");
            for(const auto& point:source.at("vertices")){auto v=point.get<std::array<double,3>>();mesh->vertices.push_back({v[0],v[1],v[2]});}
            for(const auto& triangle:source.at("triangles")){
                require(triangle.is_array()&&triangle.size()==3&&triangle[0].is_number_unsigned()&&triangle[0].get<std::uint64_t>()<=UINT32_MAX&&triangle[2].is_number_unsigned()&&triangle[2].get<std::uint64_t>()<32,"Mesh triangle key/material encoding");
                auto indices=triangle[1].get<std::array<std::uint64_t,3>>();for(auto index:indices)require(index<mesh->vertices.size(),"Mesh source index bounds");
                mesh->triangles.push_back({{static_cast<std::uint32_t>(indices[0]),static_cast<std::uint32_t>(indices[1]),static_cast<std::uint32_t>(indices[2])},triangle[0].get<std::uint32_t>(),triangle[2].get<std::uint32_t>()});
            }
            require(source.at("materials").is_array()&&source.at("materials").size()<=32,"Mesh material inventory bound");mesh->materials=source.at("materials").get<std::vector<std::string>>();validate_collision_mesh(*mesh);mesh->signature=collision_mesh_signature(*mesh);definition.meshes.push_back({id,std::move(mesh)});
        }
    }
    return definition;
}
}
