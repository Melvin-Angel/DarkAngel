#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <meshoptimizer.h>
#include <DirectXTex.h>
#include <darkangel/hash.hpp>
#include "assets_internal.hpp"
#include <darkangel/material_assets.hpp>
#include <darkangel/model_collision.hpp>
#include <darkangel/smooth_normals.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
namespace darkangel::assets_detail {
namespace {
void hr(HRESULT result){require(SUCCEEDED(result),"DirectXTex conversion failed");}
void u32(std::string& out,std::uint32_t n){for(unsigned i=0;i<4;++i)out+=static_cast<char>(n>>(8*i));}
void f32(std::string& out,float n){require(std::isfinite(n),"Non-finite mesh value");u32(out,std::bit_cast<std::uint32_t>(n));}
struct Reader {std::string_view data;std::size_t at{};std::uint32_t u32(){require(at+4<=data.size(),"Truncated mesh product");std::uint32_t n{};for(unsigned i=0;i<4;++i)n|=static_cast<std::uint32_t>(static_cast<unsigned char>(data[at++]))<<(8*i);return n;}float f32(){auto n=std::bit_cast<float>(u32());require(std::isfinite(n),"Non-finite cooked mesh");return n;}AssetId id(){require(at+16<=data.size(),"Truncated material AssetID");AssetId result;std::memcpy(result.bytes.data(),data.data()+at,16);at+=16;AssetId::parse(result.text());return result;}};
std::string key(const cgltf_data* data,const cgltf_extras& extras){require(extras.end_offset>extras.start_offset && extras.end_offset<=data->json_size,"Source subassets require explicit darkangel_key extras");auto value=json(std::string_view(data->json+extras.start_offset,extras.end_offset-extras.start_offset));auto name=value.at("darkangel_key").get<std::string>();require(!name.empty() && name.size()<=128,"Subasset key limit");return name;}
std::string mesh_bytes(const CookedMesh& mesh){std::string out="DAMESH01";u32(out,static_cast<std::uint32_t>(mesh.vertices.size()));u32(out,static_cast<std::uint32_t>(mesh.indices.size()));u32(out,static_cast<std::uint32_t>(mesh.parts.size()));for(float n:mesh.minimum)f32(out,n);for(float n:mesh.maximum)f32(out,n);for(const auto& part:mesh.parts){out.append(reinterpret_cast<const char*>(part.material.bytes.data()),16);u32(out,part.first);u32(out,part.count);}for(const auto& v:mesh.vertices){for(float n:v.position)f32(out,n);for(float n:v.normal)f32(out,n);for(float n:v.uv)f32(out,n);}for(auto n:mesh.indices)u32(out,n);return out;}
std::string texture_bytes(std::string_view source,TextureSemantic semantic=TextureSemantic::Color){DirectX::TexMetadata metadata;hr(DirectX::GetMetadataFromWICMemory(reinterpret_cast<const std::uint8_t*>(source.data()),source.size(),DirectX::WIC_FLAGS_NONE,metadata));require(metadata.width>0 && metadata.height>0 && metadata.width<=4096 && metadata.height<=4096 && metadata.arraySize==1,"Texture dimension/array limit");DirectX::ScratchImage image,rgba,mips;hr(DirectX::LoadFromWICMemory(reinterpret_cast<const std::uint8_t*>(source.data()),source.size(),static_cast<DirectX::WIC_FLAGS>(DirectX::WIC_FLAGS_FORCE_RGB | (semantic==TextureSemantic::Color?DirectX::WIC_FLAGS_FORCE_SRGB:DirectX::WIC_FLAGS_IGNORE_SRGB)),&metadata,image));
    // Colour is stored sRGB; data and normal maps keep their bytes as linear values.
    const auto format=semantic==TextureSemantic::Color?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;auto* normalized=&image;
    if(image.GetMetadata().format!=format){hr(DirectX::Convert(image.GetImages(),image.GetImageCount(),image.GetMetadata(),format,DirectX::TEX_FILTER_DEFAULT,.5f,rgba));normalized=&rgba;}
    hr(DirectX::GenerateMipMaps(normalized->GetImages(),normalized->GetImageCount(),normalized->GetMetadata(),semantic==TextureSemantic::Color?DirectX::TEX_FILTER_DEFAULT:DirectX::TEX_FILTER_LINEAR,0,mips));
    // A filtered normal is shorter than unit length; each mip texel of a normal map is renormalised (z kept positive).
    if(semantic==TextureSemantic::Normal)for(std::size_t index=0;index<mips.GetImageCount();++index){const auto& level=mips.GetImages()[index];for(std::size_t y=0;y<level.height;++y){auto* row=level.pixels+y*level.rowPitch;for(std::size_t x=0;x<level.width;++x){auto* texel=row+x*4;float n[3];for(int c=0;c<3;++c)n[c]=texel[c]/127.5f-1.f;n[2]=std::max(n[2],0.f);const float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(length<1e-4f){n[0]=n[1]=0;n[2]=1;}else for(float& v:n)v/=length;for(int c=0;c<3;++c)texel[c]=static_cast<std::uint8_t>(std::clamp(std::lround((n[c]+1.f)*127.5f),0L,255L));}}}DirectX::Blob blob;hr(DirectX::SaveToDDSMemory(mips.GetImages(),mips.GetImageCount(),mips.GetMetadata(),DirectX::DDS_FLAGS_NONE,blob));return std::string(reinterpret_cast<const char*>(blob.GetBufferPointer()),blob.GetBufferSize());}
}
std::string cook_color_texture(std::string_view source){return texture_bytes(source);}
std::string cook_texture(std::string_view source,TextureSemantic semantic){return texture_bytes(source,semantic);}
std::string cook_hdr_texture(std::string_view source,std::string_view extension){
    if(extension==".dds"){
        DirectX::TexMetadata metadata;DirectX::ScratchImage image;hr(DirectX::LoadFromDDSMemory(reinterpret_cast<const std::uint8_t*>(source.data()),source.size(),DirectX::DDS_FLAGS_NONE,&metadata,image));
        require(metadata.width>0&&metadata.height>0&&metadata.width<=4096&&metadata.height<=4096&&metadata.depth==1,"HDR cubemap dimension limit");
        require(metadata.format==DXGI_FORMAT_R16G16B16A16_FLOAT,"HDR cubemap DDS must use RGBA16_FLOAT");
        return std::string(source);
    }
    require(extension==".hdr","HDR importer accepts .hdr or RGBA16_FLOAT .dds");
    DirectX::TexMetadata metadata;DirectX::ScratchImage image,converted,mips;hr(DirectX::LoadFromHDRMemory(reinterpret_cast<const std::uint8_t*>(source.data()),source.size(),&metadata,image));
    require(metadata.width>0&&metadata.height>0&&metadata.width<=4096&&metadata.height<=4096&&metadata.arraySize==1&&metadata.depth==1,"HDR equirectangular dimension limit");
    hr(DirectX::Convert(image.GetImages(),image.GetImageCount(),image.GetMetadata(),DXGI_FORMAT_R16G16B16A16_FLOAT,DirectX::TEX_FILTER_DEFAULT,.5f,converted));
    hr(DirectX::GenerateMipMaps(converted.GetImages(),converted.GetImageCount(),converted.GetMetadata(),DirectX::TEX_FILTER_DEFAULT,0,mips));
    DirectX::Blob blob;hr(DirectX::SaveToDDSMemory(mips.GetImages(),mips.GetImageCount(),mips.GetMetadata(),DirectX::DDS_FLAGS_NONE,blob));return std::string(reinterpret_cast<const char*>(blob.GetBufferPointer()),blob.GetBufferSize());
}
bool static_model_collision(const Json& sidecar){const auto setting=sidecar.value("collision","mesh");require(setting=="mesh"||setting=="none","A model's collision setting is \"mesh\" or \"none\"");return setting=="mesh";}
Import import_gltf(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect,bool collision,bool assignments){
    Import out;std::size_t input_bytes{};
    auto input=[&](const std::filesystem::path& p){auto bytes=read(p);input_bytes+=bytes.size();require(input_bytes<=128*1024*1024,"Import input closure limit");out.inputs[p.lexically_relative(root).generic_string()]=sha256(bytes);return bytes;};
    auto bytes=input(source);cgltf_options options{};cgltf_data* raw{};require(cgltf_parse(&options,bytes.data(),bytes.size(),&raw)==cgltf_result_success,"cgltf parse failed");std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
    json(std::string_view(data->json,data->json_size),64*1024*1024); // duplicate-key and bounded topology check
    require(data->nodes_count<=4096 && data->buffers_count<=128 && data->images_count<=128,"Static glTF topology limit");
    for(std::size_t i=0;i<data->nodes_count;++i){std::set<const cgltf_node*> parents;for(auto* node=&data->nodes[i];node;node=node->parent)require(parents.size()<128 && parents.insert(node).second,"Cyclic/deep glTF node hierarchy");}
    require(data->extensions_required_count==0 && data->skins_count==0 && data->animations_count==0 && data->cameras_count==0 && data->lights_count==0,"Unsupported glTF extension/skin/animation/camera/light in static profile");
    require(data->meshes_count>0 && data->meshes_count<=128 && data->materials_count>0 && data->materials_count<=128,"Mesh/material limit");
    auto external=[&](const char* uri){require(uri && *uri && std::string_view(uri).find_first_of("%:\\")==std::string_view::npos && uri[0]!='/',"Unsupported or unsafe external glTF URI");return input(within(root,source.parent_path().lexically_relative(root)/std::filesystem::path(uri)));};
    std::vector<std::string> buffers;buffers.reserve(data->buffers_count);
    for(std::size_t i=0;i<data->buffers_count;++i){auto& buffer=data->buffers[i];if(buffer.uri){buffers.push_back(external(buffer.uri));require(buffers.back().size()>=buffer.size,"Short external glTF buffer");buffer.data=buffers.back().data();}else {require(data->bin && data->bin_size>=buffer.size,"Missing GLB buffer");buffer.data=const_cast<void*>(data->bin);}buffer.data_free_method=cgltf_data_free_method_none;}
    require(cgltf_validate(data.get())==cgltf_result_success,"cgltf validation failed");
    std::set<std::string> keys;auto declare=[&](std::string name){require(keys.insert(name).second,"Duplicate/ambiguous subasset key");out.keys.push_back(name);if(inspect)return AssetId::random();auto it=ids.find(name);require(it!=ids.end(),"New/ambiguous subasset requires explicit adoption/remap");return it->second;};
    // R5: engine-owned material instances assigned to stable material slot keys in the model's import metadata. A slot key
    // the model no longer has is refused (repair the assignment), never remapped by order or name.
    std::map<std::string,MaterialInstanceDefinition> assigned;
    {auto metadata_path=source;metadata_path+=".daimport";if(std::filesystem::exists(metadata_path)){auto metadata=json(read(metadata_path,1024*1024),1024*1024);
        if(assignments&&metadata.contains("materials")){const auto& table=metadata.at("materials");require(table.is_object()&&table.size()<=128,"Model material assignment table");
            for(const auto& [slot,entry]:table.items()){require(slot.starts_with("material/")&&entry.is_object()&&entry.size()==2&&entry.contains("asset")&&entry.contains("source"),"Material assignment names a slot, a material and its source");
                auto path=within(root,entry.at("source").get<std::string>());require(path.extension()==".damaterial","Material assignment source type");auto material=decode_material_instance(input(path));
                require(material.id==AssetId::parse(entry.at("asset").get<std::string>()),"Material assignment source identity");assigned.emplace(slot,std::move(material));}}}}
    std::set<std::string> assigned_seen;
    std::map<const cgltf_material*,AssetId> material_ids;
    std::set<AssetId> texture_products; // a texture shared by several slots is one product
    for(std::size_t i=0;i<data->materials_count;++i){const auto& m=data->materials[i];auto name=key(data.get(),m.extras);auto id=declare(name);material_ids[&m]=id;
        // R5 A3 static profile: metallic-roughness materials with optional normal, packed occlusion/roughness/metallic and emissive
        // textures, and opaque, masked (cutout) or blended (transparent) alpha. Other extensions stay refused.
        require(m.has_pbr_metallic_roughness && !m.unlit && !m.has_pbr_specular_glossiness && !m.has_clearcoat && !m.has_transmission && !m.has_volume && !m.has_sheen && !m.has_ior && !m.has_specular && !m.has_iridescence && !m.has_emissive_strength,"Unsupported material feature in the static surface profile");
        const auto& pbr=m.pbr_metallic_roughness;
        for(float color:pbr.base_color_factor)require(std::isfinite(color) && color>=0 && color<=1,"Invalid material color factor");
        require(std::isfinite(pbr.roughness_factor) && pbr.roughness_factor>=0 && pbr.roughness_factor<=1 && std::isfinite(pbr.metallic_factor) && pbr.metallic_factor>=0 && pbr.metallic_factor<=1,"Invalid material surface factor");
        for(float color:m.emissive_factor)require(std::isfinite(color)&&color>=0&&color<=1,"Invalid material emissive factor");
        Json material={{"schema",1},{"id",id.text()},{"color",pbr.base_color_factor},{"roughness",pbr.roughness_factor},{"metallic",pbr.metallic_factor},{"double_sided",m.double_sided!=0},{"texture",nullptr},{"family",std::string(material_family_toon_standard)}};
        std::vector<AssetId> dependencies;
        // One texture reference of this material as a texture product of the model, cooked for its semantic. Base colour keeps
        // its existing key; the others are new keys that only models using them declare.
        auto texture_product=[&](const cgltf_texture_view& view,const std::string& kind,TextureSemantic semantic){
            require(view.texcoord==0 && !view.has_transform,"Unsupported texture coordinate/transform");auto* image=view.texture->image;require(image!=nullptr,"Missing texture image");auto texture_id=declare("texture/"+kind+"/"+name);dependencies.push_back(texture_id);
            if(auto* sampler=view.texture->sampler)require((sampler->mag_filter==0 || sampler->mag_filter==9729) && (sampler->min_filter==0 || sampler->min_filter==9987) && sampler->wrap_s==10497 && sampler->wrap_t==10497,"Only linear trilinear wrap sampler profile is supported");
            auto image_bytes=image->uri?external(image->uri):std::string{};
            if(!image->uri){require(image->buffer_view && image->buffer_view->buffer->data,"Missing embedded image buffer");const auto* ptr=static_cast<const char*>(image->buffer_view->buffer->data)+image->buffer_view->offset;image_bytes.assign(ptr,image->buffer_view->size);}
            if(!inspect)out.products.push_back({texture_id,"texture","dds",texture_bytes(image_bytes,semantic),{}});return texture_id;};
        if(pbr.base_color_texture.texture)material["texture"]=texture_product(pbr.base_color_texture,"base-color",TextureSemantic::Color).text();
        if(m.normal_texture.texture){material["normal_texture"]=texture_product(m.normal_texture,"normal",TextureSemantic::Normal).text();require(std::isfinite(m.normal_texture.scale)&&m.normal_texture.scale>=0&&m.normal_texture.scale<=2,"Normal scale is 0 to 2");material["normal_scale"]=m.normal_texture.scale;}
        // Occlusion must be packed in the metallic-roughness texture's red channel (glTF ORM); a separate occlusion image is refused.
        if(m.occlusion_texture.texture)require(pbr.metallic_roughness_texture.texture&&m.occlusion_texture.texture->image==pbr.metallic_roughness_texture.texture->image,"Occlusion must be packed in the metallic-roughness texture (R occlusion, G roughness, B metallic)");
        if(pbr.metallic_roughness_texture.texture){material["surface_texture"]=texture_product(pbr.metallic_roughness_texture,"surface",TextureSemantic::Data).text();
            material["occlusion_strength"]=m.occlusion_texture.texture?std::clamp(m.occlusion_texture.scale,0.f,1.f):0.f;}
        if(m.emissive_factor[0]>0||m.emissive_factor[1]>0||m.emissive_factor[2]>0||m.emissive_texture.texture){material["emissive"]=m.emissive_factor;if(m.emissive_texture.texture)material["emissive_texture"]=texture_product(m.emissive_texture,"emissive",TextureSemantic::Color).text();}
        if(m.alpha_mode==cgltf_alpha_mode_mask){require(std::isfinite(m.alpha_cutoff)&&m.alpha_cutoff>=0&&m.alpha_cutoff<=1,"Alpha cutoff is 0 to 1");material["alpha_mode"]="cutout";material["alpha_cutoff"]=m.alpha_cutoff;}
        else if(m.alpha_mode==cgltf_alpha_mode_blend){material["alpha_mode"]="transparent";material["alpha_cutoff"]=.5f;}
        if(auto found=assigned.find(name);found!=assigned.end()){const auto& instance=found->second;assigned_seen.insert(name);
            // The assigned instance replaces every value of the slot; its textures join the model's closure.
            for(auto key:{"texture","normal_texture","normal_scale","surface_texture","occlusion_strength","emissive","emissive_texture","alpha_mode","alpha_cutoff"})material.erase(key);dependencies.clear();
            const auto fields=material_product_fields(instance);for(const auto& [field,value]:fields.items())material[field]=value;
            for(const auto& [ref,semantic]:instance.textures()){if(std::find(dependencies.begin(),dependencies.end(),ref.id)==dependencies.end())dependencies.push_back(ref.id);
                auto texture=material_texture(root,ref,semantic,inspect);for(const auto& [path,hash]:texture.inputs)out.inputs[path]=hash;if(!texture_products.insert(ref.id).second)continue;for(auto& product:texture.products)out.products.push_back(std::move(product));}}
        if(!inspect)out.products.push_back({id,"material","material.json",material.dump(),dependencies});
    }
    for(const auto& [slot,unused]:assigned)require(assigned_seen.contains(slot),"A material assignment names a slot this model no longer has; repair or clear it");
    std::vector<AssetId> meshes;std::vector<CookedMesh> cooked;
    for(std::size_t i=0;i<data->meshes_count;++i){auto& mesh=data->meshes[i];auto id=declare(key(data.get(),mesh.extras));meshes.push_back(id);require(mesh.weights_count==0 && mesh.primitives_count<=128,"Unsupported morph weights/primitive limit");
        cgltf_node* node{};for(std::size_t n=0;n<data->nodes_count;++n)if(data->nodes[n].mesh==&mesh){require(!node,"Instanced meshes require explicit scene assembly import");node=&data->nodes[n];}require(node,"Unused mesh has no static node placement");
        float transform[16];cgltf_node_transform_world(node,transform);float lengths[3]{};for(unsigned c=0;c<3;++c){for(unsigned r=0;r<3;++r)lengths[c]+=transform[c*4+r]*transform[c*4+r];lengths[c]=std::sqrt(lengths[c]);}
        require(lengths[0]>1e-6f && std::abs(lengths[0]-lengths[1])<1e-4f*lengths[0] && std::abs(lengths[0]-lengths[2])<1e-4f*lengths[0],"Non-uniform/zero static scale unsupported");
        for(unsigned a=0;a<3;++a)for(unsigned b=a+1;b<3;++b){float dot{};for(unsigned r=0;r<3;++r)dot+=transform[a*4+r]*transform[b*4+r];require(std::isfinite(dot) && std::abs(dot)<1e-4f*lengths[a]*lengths[b],"Sheared static transform unsupported");}
        auto det=transform[0]*(transform[5]*transform[10]-transform[9]*transform[6])-transform[4]*(transform[1]*transform[10]-transform[9]*transform[2])+transform[8]*(transform[1]*transform[6]-transform[5]*transform[2]);require(det>0,"Negative/reflected transform unsupported");
        CookedMesh result;result.minimum.fill(std::numeric_limits<float>::max());result.maximum.fill(-std::numeric_limits<float>::max());std::vector<AssetId> dependencies;std::set<AssetId> slots;
        for(std::size_t p=0;p<mesh.primitives_count;++p){const auto& primitive=mesh.primitives[p];require(primitive.type==cgltf_primitive_type_triangles && primitive.targets_count==0 && primitive.indices && primitive.material,"Only indexed static triangles with declared materials supported");
            auto material=material_ids.at(primitive.material);require(slots.insert(material).second,"Duplicate material primitive needs an explicit stable partition key");dependencies.push_back(material);
            const cgltf_accessor *position{},*normal{},*uv{};
            for(std::size_t a=0;a<primitive.attributes_count;++a){const auto& attr=primitive.attributes[a];if(attr.type==cgltf_attribute_type_position)position=attr.data;else if(attr.type==cgltf_attribute_type_normal)normal=attr.data;else if(attr.type==cgltf_attribute_type_texcoord && attr.index==0)uv=attr.data;else require(attr.type==cgltf_attribute_type_texcoord,"Unsupported vertex stream");}
            require(position && normal && position->count==normal->count && (!uv || uv->count==position->count) && position->count<=1000000 && result.vertices.size()+position->count<=1000000,"Position/normal/UV count limit");
            require(!primitive.material->pbr_metallic_roughness.base_color_texture.texture || uv,"Textured primitive has no UV0");
            auto base=static_cast<std::uint32_t>(result.vertices.size());
            for(std::size_t v=0;v<position->count;++v){Vertex vertex{};float pos[3],nrm[3];require(cgltf_accessor_read_float(position,v,pos,3) && cgltf_accessor_read_float(normal,v,nrm,3),"Invalid vertex accessor");
                for(unsigned r=0;r<3;++r){vertex.position[r]=transform[r]*pos[0]+transform[4+r]*pos[1]+transform[8+r]*pos[2]+transform[12+r];vertex.normal[r]=transform[r]*nrm[0]+transform[4+r]*nrm[1]+transform[8+r]*nrm[2];require(std::isfinite(vertex.position[r]) && std::abs(vertex.position[r])<=1e6f && std::isfinite(vertex.normal[r]),"Invalid transformed vertex");result.minimum[r]=std::min(result.minimum[r],vertex.position[r]);result.maximum[r]=std::max(result.maximum[r],vertex.position[r]);}
                auto length=std::sqrt(vertex.normal[0]*vertex.normal[0]+vertex.normal[1]*vertex.normal[1]+vertex.normal[2]*vertex.normal[2]);require(length>1e-6f,"Invalid zero normal");for(auto& x:vertex.normal)x/=length;
                if(uv)require(cgltf_accessor_read_float(uv,v,vertex.uv.data(),2),"Invalid UV accessor");for(float x:vertex.uv)require(std::isfinite(x) && std::abs(x)<=1e5f,"Invalid UV value");result.vertices.push_back(vertex);
            }
            require(primitive.indices->count>0 && primitive.indices->count%3==0 && result.indices.size()+primitive.indices->count<=3000000,"Triangle/index limit");auto first=static_cast<std::uint32_t>(result.indices.size());
            for(std::size_t n=0;n<primitive.indices->count;++n){auto index=cgltf_accessor_read_index(primitive.indices,n);require(index<position->count,"Index outside vertex range");result.indices.push_back(base+static_cast<std::uint32_t>(index));}
            for(std::size_t n=first;n<result.indices.size();n+=3){const auto& a=result.vertices[result.indices[n]].position;const auto& b=result.vertices[result.indices[n+1]].position;const auto& c=result.vertices[result.indices[n+2]].position;float e[3],f[3];for(unsigned r=0;r<3;++r){e[r]=b[r]-a[r];f[r]=c[r]-a[r];}float cross[3]={e[1]*f[2]-e[2]*f[1],e[2]*f[0]-e[0]*f[2],e[0]*f[1]-e[1]*f[0]};require(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]>1e-20f,"Degenerate triangle");}
            if(!inspect)meshopt_optimizeVertexCache(result.indices.data()+first,result.indices.data()+first,primitive.indices->count,result.vertices.size());result.parts.push_back({material,first,static_cast<std::uint32_t>(primitive.indices->count)});
        }
        static_assert(sizeof(Vertex)==32);if(!inspect){smooth_normals(result.vertices, result.indices);std::vector<Vertex> optimized(result.vertices.size());auto count=meshopt_optimizeVertexFetch(optimized.data(),result.indices.data(),result.indices.size(),result.vertices.data(),result.vertices.size(),sizeof(Vertex));optimized.resize(count);result.vertices=std::move(optimized);}
        if(!inspect)out.products.push_back({id,"mesh","mesh",mesh_bytes(result),dependencies});if(!inspect&&collision)cooked.push_back(std::move(result));
    }
    if(!inspect){auto root_id=ids.at("$source");Json model={{"schema",1},{"id",root_id.text()},{"axes","right-handed-y-up"},{"units","metres"},{"meshes",Json::array()},{"collision",nullptr}};for(auto id:meshes)model["meshes"].push_back(id.text());auto required=meshes;
        // The model's own collision shape, welded in model space; its identity derives from the model's, so the sidecar adopts no new subasset.
        if(collision){RuntimeModel shape;shape.id=root_id;shape.meshes=std::move(cooked);ModelCollisionBake baked;
            try{baked=cook_model_collision(shape);}catch(const std::exception& error){throw std::runtime_error(std::string("Model collision: ")+error.what()+". Simplify the model or import it with \"collision\": \"none\"");}
            model["collision"]=baked.data->runtime.text();required.push_back(baked.data->runtime);out.products.push_back({baked.data->runtime,"model-collision","model-collision",encode_model_collision(*baked.data),{}});}
        out.products.push_back({root_id,"model","model.json",model.dump(),required});require(keys.size()+1==ids.size(),"Removed subassets require an explicit remap/retirement");}
    out.product_count=keys.size()+1+(collision?1:0);
    return out;
}
CookedMesh decode_mesh(std::string_view data){require(data.starts_with("DAMESH01"),"Cooked mesh schema mismatch");Reader in{data,8};auto vertices=in.u32(),indices=in.u32(),parts=in.u32();require(vertices>0 && vertices<=1000000 && indices>0 && indices<=3000000 && indices%3==0 && parts>0 && parts<=128,"Cooked mesh count limit");CookedMesh result;for(auto& x:result.minimum)x=in.f32();for(auto& x:result.maximum)x=in.f32();for(unsigned i=0;i<parts;++i){auto id=in.id();auto first=in.u32(),count=in.u32();require(first<=indices && count<=indices-first && count%3==0,"Invalid mesh part range");result.parts.push_back({id,first,count});}for(unsigned i=0;i<vertices;++i){Vertex v;for(auto& x:v.position)x=in.f32();for(auto& x:v.normal)x=in.f32();for(auto& x:v.uv)x=in.f32();result.vertices.push_back(v);}for(unsigned i=0;i<indices;++i){auto n=in.u32();require(n<vertices,"Cooked index out of range");result.indices.push_back(n);}require(in.at==data.size(),"Trailing cooked mesh data");return result;}
CookedTexture decode_texture(std::string_view bytes){
    DirectX::ScratchImage image;DirectX::TexMetadata metadata;
    hr(DirectX::LoadFromDDSMemory(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),DirectX::DDS_FLAGS_NONE,&metadata,image));
    const bool hdr=metadata.format==DXGI_FORMAT_R16G16B16A16_FLOAT;
    require((hdr||metadata.format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||metadata.format==DXGI_FORMAT_R8G8B8A8_UNORM)&&metadata.width>0&&metadata.height>0&&metadata.width<=4096&&metadata.height<=4096&&metadata.depth==1,"Cooked texture profile mismatch");
    const auto layers=metadata.IsCubemap()?6u:static_cast<std::uint32_t>(metadata.arraySize);
    require(layers==1||layers==6,"Cooked texture array/cubemap profile mismatch");
    const std::size_t bytes_per_pixel=hdr?8:4;
    CookedTexture result;result.srgb=!hdr&&metadata.format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;result.hdr=hdr;result.layers=layers;
    for(std::size_t mip=0;mip<metadata.mipLevels;++mip){
        auto* first=image.GetImage(mip,0,0);require(first,"Missing cooked mip");
        TextureMip level{static_cast<std::uint32_t>(first->width),static_cast<std::uint32_t>(first->height),std::vector<std::uint8_t>(first->width*first->height*bytes_per_pixel*layers)};
        for(std::uint32_t layer=0;layer<layers;++layer){auto* src=image.GetImage(mip,layer,0);require(src,"Missing cooked texture layer");for(std::size_t y=0;y<src->height;++y)std::memcpy(level.rgba.data()+(static_cast<std::size_t>(layer)*src->height+y)*src->width*bytes_per_pixel,src->pixels+y*src->rowPitch,src->width*bytes_per_pixel);}
        result.mips.push_back(std::move(level));
    }
    return result;
}
}
