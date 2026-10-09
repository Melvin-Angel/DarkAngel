#include <darkangel/model_collision.hpp>
#include <DirectXMath.h>
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
namespace darkangel {
namespace {void require(bool b,const char* error){if(!b)throw std::runtime_error(error);}}
ModelCollisionBake bake_model_collision(const RuntimeModel& model,const Transform& pose,AssetId runtime){
    require(!model.meshes.empty()&&model.meshes.size()<=16,"Collision model mesh count");for(double value:{pose.x,pose.y,pose.z,pose.pitch,pose.yaw,pose.roll,pose.scale})require(std::isfinite(value),"Collision model transform finite");require(pose.scale>0&&pose.scale<=1000,"Collision model positive uniform scale");
    using namespace DirectX;auto transform=XMMatrixScaling(float(pose.scale),float(pose.scale),float(pose.scale))*XMMatrixRotationRollPitchYaw(float(pose.pitch),float(pose.yaw),float(pose.roll))*XMMatrixTranslation(float(pose.x),float(pose.y),float(pose.z));
    auto data=std::make_shared<CollisionMeshData>();data->runtime=runtime;std::map<std::array<float,3>,std::uint32_t> vertices;std::map<AssetId,std::uint32_t> materials;std::set<std::array<std::uint32_t,3>> faces;ModelCollisionBake output;
    for(const auto& mesh:model.meshes){require(mesh.vertices.size()<=2048&&mesh.indices.size()%3==0&&mesh.indices.size()/3<=4096,"Collision model source bounds");std::vector<std::uint32_t> remap;remap.reserve(mesh.vertices.size());
        for(const auto& vertex:mesh.vertices){auto p=vertex.position;XMFLOAT3 transformed;XMStoreFloat3(&transformed,XMVector3TransformCoord(XMVectorSet(p[0],p[1],p[2],1),transform));std::array<float,3> key{transformed.x,transformed.y,transformed.z};for(auto value:key)require(std::isfinite(value)&&std::abs(value)<=100000,"Collision transformed vertex bounds");auto [entry,inserted]=vertices.emplace(key,static_cast<std::uint32_t>(vertices.size()));if(inserted){require(data->vertices.size()<2048,"Collision combined vertex bound");data->vertices.push_back({key[0],key[1],key[2]});}remap.push_back(entry->second);}
        std::vector<bool> covered(mesh.indices.size()/3);
        for(const auto& part:mesh.parts){require(part.first%3==0&&part.count%3==0&&part.first<=mesh.indices.size()&&part.count<=mesh.indices.size()-part.first,"Collision material part bounds");auto [material,inserted]=materials.emplace(part.material,static_cast<std::uint32_t>(materials.size()));if(inserted){require(data->materials.size()<32,"Collision material bound");data->materials.push_back("material."+part.material.text());}
            for(std::size_t index=part.first;index<part.first+part.count;index+=3){require(!covered[index/3],"Collision overlapping material parts");covered[index/3]=true;std::array<std::uint32_t,3> triangle;for(unsigned corner=0;corner<3;++corner){require(mesh.indices[index+corner]<remap.size(),"Collision native model index");triangle[corner]=remap[mesh.indices[index+corner]];}auto a=data->vertices[triangle[0]],b=data->vertices[triangle[1]],c=data->vertices[triangle[2]];auto ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;auto x=uy*vz-uz*vy,y=uz*vx-ux*vz,z=ux*vy-uy*vx;
                if(x*x+y*y+z*z<=1e-12){++output.degenerate_faces;continue;}auto canonical=triangle;std::sort(canonical.begin(),canonical.end());if(!faces.insert(canonical).second){++output.duplicate_faces;continue;}require(data->triangles.size()<4096,"Collision combined triangle bound");data->triangles.push_back({triangle,static_cast<std::uint32_t>(data->triangles.size()),material->second});}
        }
        require(std::all_of(covered.begin(),covered.end(),[](bool value){return value;}),"Collision uncovered native triangles");
    }
    validate_collision_mesh(*data);data->signature=collision_mesh_signature(*data);output.data=std::move(data);return output;
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
}
