#include <darkangel/model_collision.hpp>
#include "assets_internal.hpp"
#include <DirectXMath.h>
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
namespace darkangel {
namespace {void require(bool b,const char* error){if(!b)throw std::runtime_error(error);}}
namespace {
// Welds the model's render meshes into one collision mesh through transform. Render vertices repeat a position once per normal/UV
// seam; the welded positions and kept triangles carry the collision bounds, so the source bound only caps the work.
struct WeldBounds {std::size_t meshes,source_vertices,source_triangles,vertices,triangles;};
ModelCollisionBake weld(const RuntimeModel& model,const DirectX::XMMATRIX& transform,AssetId runtime,const WeldBounds& bounds){
    using namespace DirectX;require(!model.meshes.empty()&&model.meshes.size()<=bounds.meshes,"Collision model mesh count");
    auto data=std::make_shared<CollisionMeshData>();data->runtime=runtime;std::map<std::array<float,3>,std::uint32_t> vertices;std::map<AssetId,std::uint32_t> materials;std::set<std::array<std::uint32_t,3>> faces;ModelCollisionBake output;
    for(const auto& mesh:model.meshes){require(mesh.vertices.size()<=bounds.source_vertices&&mesh.indices.size()%3==0&&mesh.indices.size()/3<=bounds.source_triangles,"Collision model source bounds");std::vector<std::uint32_t> remap;remap.reserve(mesh.vertices.size());
        for(const auto& vertex:mesh.vertices){auto p=vertex.position;XMFLOAT3 transformed;XMStoreFloat3(&transformed,XMVector3TransformCoord(XMVectorSet(p[0],p[1],p[2],1),transform));std::array<float,3> key{transformed.x,transformed.y,transformed.z};for(auto value:key)require(std::isfinite(value)&&std::abs(value)<=100000,"Collision transformed vertex bounds");auto [entry,inserted]=vertices.emplace(key,static_cast<std::uint32_t>(vertices.size()));if(inserted){require(data->vertices.size()<bounds.vertices,"Collision combined vertex bound");data->vertices.push_back({key[0],key[1],key[2]});}remap.push_back(entry->second);}
        std::vector<bool> covered(mesh.indices.size()/3);
        for(const auto& part:mesh.parts){require(part.first%3==0&&part.count%3==0&&part.first<=mesh.indices.size()&&part.count<=mesh.indices.size()-part.first,"Collision material part bounds");auto [material,inserted]=materials.emplace(part.material,static_cast<std::uint32_t>(materials.size()));if(inserted){require(data->materials.size()<32,"Collision material bound");data->materials.push_back("material."+part.material.text());}
            for(std::size_t index=part.first;index<part.first+part.count;index+=3){require(!covered[index/3],"Collision overlapping material parts");covered[index/3]=true;std::array<std::uint32_t,3> triangle;for(unsigned corner=0;corner<3;++corner){require(mesh.indices[index+corner]<remap.size(),"Collision native model index");triangle[corner]=remap[mesh.indices[index+corner]];}auto a=data->vertices[triangle[0]],b=data->vertices[triangle[1]],c=data->vertices[triangle[2]];auto ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;auto x=uy*vz-uz*vy,y=uz*vx-ux*vz,z=ux*vy-uy*vx;
                if(x*x+y*y+z*z<=1e-12){++output.degenerate_faces;continue;}auto canonical=triangle;std::sort(canonical.begin(),canonical.end());if(!faces.insert(canonical).second){++output.duplicate_faces;continue;}require(data->triangles.size()<bounds.triangles,"Collision combined triangle bound");data->triangles.push_back({triangle,static_cast<std::uint32_t>(data->triangles.size()),material->second});}
        }
        require(std::all_of(covered.begin(),covered.end(),[](bool value){return value;}),"Collision uncovered native triangles");
    }
    validate_collision_mesh(*data);data->signature=collision_mesh_signature(*data);output.data=std::move(data);return output;
}
void put(std::string& out,const void* value,std::size_t size){out.append(static_cast<const char*>(value),size);}
template<class T> T take(std::string_view bytes,std::size_t& at){require(at+sizeof(T)<=bytes.size(),"Model collision product truncated");T value;std::memcpy(&value,bytes.data()+at,sizeof(T));at+=sizeof(T);return value;}
}
ModelCollisionBake bake_model_collision(const RuntimeModel& model,const Transform& pose,AssetId runtime){
    for(double value:{pose.x,pose.y,pose.z,pose.pitch,pose.yaw,pose.roll,pose.scale})require(std::isfinite(value),"Collision model transform finite");require(pose.scale>0&&pose.scale<=1000,"Collision model positive uniform scale");
    using namespace DirectX;auto transform=XMMatrixScaling(float(pose.scale),float(pose.scale),float(pose.scale))*XMMatrixRotationRollPitchYaw(float(pose.pitch),float(pose.yaw),float(pose.roll))*XMMatrixTranslation(float(pose.x),float(pose.y),float(pose.z));
    return weld(model,transform,runtime,{16,65536,65536,2048,4096});
}
AssetId model_collision_id(AssetId model){
    auto hex=sha256("darkangel.model-collision/"+model.text()).substr(0,32);hex[12]='4';hex[16]="89ab"[std::string_view("0123456789abcdef").find(hex[16])%4];
    hex.insert(20,1,'-');hex.insert(16,1,'-');hex.insert(12,1,'-');hex.insert(8,1,'-');return AssetId::parse(hex);
}
ModelCollisionBake cook_model_collision(const RuntimeModel& model){
    return weld(model,DirectX::XMMatrixIdentity(),model_collision_id(model.id),{128,1000000,1000000,model_collision_vertex_limit,model_collision_triangle_limit});
}
std::string encode_model_collision(const CollisionMeshData& data){
    validate_collision_mesh(data);std::string out="DAMCOL01";
    const std::uint32_t counts[3]={static_cast<std::uint32_t>(data.vertices.size()),static_cast<std::uint32_t>(data.triangles.size()),static_cast<std::uint32_t>(data.materials.size())};put(out,counts,sizeof(counts));
    for(const auto& key:data.materials){require(key.starts_with("material.")&&key.size()==45,"Model collision material key");const auto text=AssetId::parse(key.substr(9)).text();put(out,text.data(),text.size());}
    for(const auto& vertex:data.vertices){const float point[3]={float(vertex.x),float(vertex.y),float(vertex.z)};require(double(point[0])==vertex.x&&double(point[1])==vertex.y&&double(point[2])==vertex.z,"Model collision vertex precision");put(out,point,sizeof(point));}
    for(std::size_t i=0;i<data.triangles.size();++i){const auto& triangle=data.triangles[i];require(triangle.key==i,"Model collision triangle keys are their order");const std::uint32_t record[4]={triangle.vertices[0],triangle.vertices[1],triangle.vertices[2],triangle.material};put(out,record,sizeof(record));}
    return out;
}
std::shared_ptr<const CollisionMeshData> decode_model_collision(std::string_view bytes,AssetId runtime){
    require(bytes.size()>=20&&bytes.substr(0,8)=="DAMCOL01","Model collision product schema");std::size_t at=8;
    const auto vertices=take<std::uint32_t>(bytes,at),triangles=take<std::uint32_t>(bytes,at),materials=take<std::uint32_t>(bytes,at);
    require(vertices>=3&&vertices<=model_collision_vertex_limit&&triangles>=1&&triangles<=model_collision_triangle_limit&&materials>=1&&materials<=32&&bytes.size()==20+std::size_t(materials)*36+std::size_t(vertices)*12+std::size_t(triangles)*16,"Model collision product size");
    auto data=std::make_shared<CollisionMeshData>();data->runtime=runtime;
    for(std::uint32_t i=0;i<materials;++i){data->materials.push_back("material."+AssetId::parse(bytes.substr(at,36)).text());at+=36;}
    for(std::uint32_t i=0;i<vertices;++i){const auto x=take<float>(bytes,at),y=take<float>(bytes,at),z=take<float>(bytes,at);data->vertices.push_back({x,y,z});}
    for(std::uint32_t i=0;i<triangles;++i){CollisionTriangle triangle;for(auto& vertex:triangle.vertices)vertex=take<std::uint32_t>(bytes,at);triangle.material=take<std::uint32_t>(bytes,at);triangle.key=i;data->triangles.push_back(triangle);}
    validate_collision_mesh(*data);data->signature=collision_mesh_signature(*data);return data;
}
std::string encode_collision_source(const CollisionDefinition& definition){
    using Json=nlohmann::json;require(definition.boxes.empty()&&!definition.meshes.empty()&&definition.meshes.size()<=16,"Model collision source static mesh profile");Json source={{"schema",3},{"kind","collision"},{"asset",definition.id.text()},{"boxes",Json::array()},{"meshes",Json::array()}};
    for(const auto& mesh:definition.meshes){require(bool(mesh.data),"Collision source geometry missing");Json vertices=Json::array(),triangles=Json::array();for(auto v:mesh.data->vertices)vertices.push_back({v.x,v.y,v.z});for(const auto& triangle:mesh.data->triangles)triangles.push_back({triangle.key,triangle.vertices,triangle.material});source["meshes"].push_back({{"id",std::to_string(mesh.id)},{"runtime",mesh.data->runtime.text()},{"vertices",vertices},{"triangles",triangles},{"materials",mesh.data->materials}});}
    auto encoded=source.dump(2)+"\n";decode_collision_source(encoded);return encoded;
}
void publish_collision_source(const std::filesystem::path& path,std::string_view bytes){
    require(path.extension()==".dacollision","Collision publication requires native source extension");decode_collision_source(bytes);if(std::filesystem::exists(path)){std::ifstream current(path,std::ios::binary);std::string previous{std::istreambuf_iterator<char>(current),{}};if(previous==bytes)return;}
    auto staged=path;staged+="."+AssetId::random().text()+".tmp";if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());try{std::ofstream output(staged,std::ios::binary);output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));output.close();require(output.good(),"Collision source staging failed");require(MoveFileExW(staged.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Collision source publication failed");}catch(...){std::error_code error;std::filesystem::remove(staged,error);throw;}
}
std::shared_ptr<const CollisionMeshData> load_cooked_model_collision(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId model){
    const auto manifest_held=assets_detail::registry_json(registry);const auto& manifest=*manifest_held;require(manifest.at("schema")==1&&manifest.at("assets").size()<=runtime_package_product_limit,"Runtime registry mismatch/limit");
    auto product=[&](AssetId id,const char* kind,const char* extension,std::size_t limit){
        for(const auto& record:manifest.at("assets"))if(record.at("id")==id.text()){
            require(record.at("kind")==kind&&record.at("extension")==extension,"Cooked AssetRef type mismatch");auto hash=record.at("sha256").get<std::string>();
            require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Cooked artifact digest");auto bytes=assets_detail::read(cas/(hash+"."+extension),limit);require(sha256(bytes)==hash,"Cooked artifact digest mismatch");return bytes;}
        throw std::runtime_error("Missing required cooked AssetID");};
    auto fields=assets_detail::json(product(model,"model","model.json",1024*1024));require(fields.at("id")==model.text(),"Cooked model contract mismatch");
    if(!fields.contains("collision")||fields.at("collision").is_null())return nullptr;
    const auto id=AssetId::parse(fields.at("collision").get<std::string>());require(id==model_collision_id(model),"Model collision identity");
    return decode_model_collision(product(id,"model-collision","model-collision",16*1024*1024),id);
}
}
