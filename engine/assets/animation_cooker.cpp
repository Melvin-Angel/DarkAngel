#include "assets_internal.hpp"
#include <darkangel/animation_assets.hpp>
#include <darkangel/hash.hpp>
#include <cgltf.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/quaternion.h>
#include <cmath>
#include <set>
#include <cstring>
namespace darkangel::assets_detail {
    namespace {
        constexpr auto ozz_pin="744eb9d99f606eda849acb0b1204f7a3dc20bca1";
        using Matrix=ozz::math::Float4x4;
        Matrix transform(const RigJoint& joint){
            return Matrix::FromAffine(ozz::math::Float3(joint.translation[0],joint.translation[1],joint.translation[2]),ozz::math::Quaternion(joint.rotation[0],joint.rotation[1],joint.rotation[2],joint.rotation[3]),ozz::math::Float3(1,1,1));
        }
        Matrix load_matrix(const float* values){
            Matrix matrix;
            for(int i=0;i<4;++i)matrix.cols[i]=ozz::math::simd_float4::LoadPtrU(values+i*4);
            return matrix;
        }
        std::array<float,3> point(const Matrix& matrix,const float* values){
            float result[4];
            ozz::math::StorePtrU(ozz::math::TransformPoint(matrix,ozz::math::simd_float4::Load3PtrU(values)),result);
            return {
                result[0],result[1],result[2]
            };
        }
        void close(float a,float b,float tolerance,const char* diagnostic){
            require(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance,diagnostic);
        }
    }
    Import import_clip(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,const std::filesystem::path& canonical,bool loop,bool inspect){
        require(source.extension()==".glb"&&canonical.extension()==".daskeleton","Initial clip profile requires normalized GLB and native skeleton");
        auto rig_bytes=read(canonical,1024*1024);auto rig=decode_rig_source(rig_bytes);
        auto bytes=read(source,16*1024*1024);Import out;out.keys={"runtime"};out.product_count=4;
        out.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);out.inputs[canonical.lexically_relative(root).generic_string()]=sha256(rig_bytes);
        require(bytes.size()>=20,"Clip GLB header bounds");std::uint32_t header[5];std::memcpy(header,bytes.data(),20);
        require(header[0]==0x46546c67&&header[1]==2&&header[2]==bytes.size()&&header[4]==0x4e4f534a&&header[3]<=bytes.size()-20,"Clip GLB container profile");
        // Bound JSON work before cgltf allocates its node/channel structures.
        auto document=json(std::string_view(bytes.data()+20,header[3]),16*1024*1024);
        cgltf_options options{};cgltf_data* pointer{};require(cgltf_parse(&options,bytes.data(),bytes.size(),&pointer)==cgltf_result_success,"Clip cgltf parse");
        std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(pointer,cgltf_free);
        require(document.at("extras").at("darkangel_axes")=="right-handed-y-up-metres"&&document.at("extras").at("darkangel_canonical")==rig.id.text(),"Clip conversion axes/canonical contract");
        require(data->animations_count==1&&data->nodes_count==rig.joints.size()&&data->meshes_count==0&&data->skins_count==0&&data->extensions_required_count==0&&data->buffers_count==1&&!data->buffers[0].uri&&data->bin&&data->bin_size>=data->buffers[0].size,"Normalized clip GLB profile");
        data->buffers[0].data=const_cast<void*>(data->bin);data->buffers[0].data_free_method=cgltf_data_free_method_none;
        require(cgltf_validate(data.get())==cgltf_result_success,"Clip glTF validation");
        std::map<std::string,unsigned> keys;for(unsigned i=0;i<rig.joints.size();++i)keys.emplace(rig.joints[i].key,i);
        std::vector<unsigned> remap(data->nodes_count);std::set<unsigned> unique;
        for(unsigned n=0;n<data->nodes_count;++n){
            const auto& node=data->nodes[n];require(node.name&&keys.contains(node.name)&&!node.has_matrix,"Clip canonical node key/TRS");
            auto index=keys.at(node.name);require(unique.insert(index).second,"Duplicate clip joint");remap[n]=index;const auto& joint=rig.joints[index];
            require((joint.parent<0&&!node.parent)||(joint.parent>=0&&node.parent&&node.parent->name&&rig.joints[joint.parent].key==node.parent->name),"Clip canonical hierarchy mismatch");
            for(int i=0;i<3;++i){close(node.translation[i],joint.translation[i],.0001f,"Clip canonical rest translation");close(node.scale[i],1,.0001f,"Clip canonical rest scale");}
            float dot{};for(int i=0;i<4;++i)dot+=node.rotation[i]*joint.rotation[i];close(std::abs(dot),1,.0001f,"Clip canonical rest rotation");
        }
        const auto& animation=data->animations[0];require(animation.channels_count==2*rig.joints.size(),"Clip requires complete translation/rotation tracks");
        auto* first=animation.channels[0].sampler->input;require(first&&!first->is_sparse&&first->count>=2&&first->count<=601,"Clip key count bounds");
        unsigned ticks=static_cast<unsigned>(first->count-1);float duration=ticks/60.f;
        require(std::uint64_t(ticks+1)*rig.joints.size()<=50000,"Clip cook key work limit");
        ozz::animation::offline::RawAnimation raw;raw.duration=duration;raw.name=animation.name?animation.name:"clip";raw.tracks.resize(rig.joints.size());
        std::vector<unsigned> seen(rig.joints.size());
        for(const auto& channel:std::span(animation.channels,animation.channels_count)){
            require(channel.target_node&&channel.sampler&&channel.sampler->interpolation==cgltf_interpolation_type_linear,"Only normalized LINEAR clip tracks supported");
            auto index=remap.at(static_cast<std::size_t>(channel.target_node-data->nodes));auto& track=raw.tracks[index];const auto& sampler=*channel.sampler;
            const auto* input=sampler.input;const auto* output=sampler.output;
            unsigned width=channel.target_path==cgltf_animation_path_type_translation?3:channel.target_path==cgltf_animation_path_type_rotation?4:0;
            require(width&&input&&output&&!input->is_sparse&&!output->is_sparse&&input->component_type==cgltf_component_type_r_32f&&input->type==cgltf_type_scalar&&output->component_type==cgltf_component_type_r_32f&&input->count==ticks+1&&output->count==ticks+1&&output->type==(width==3?cgltf_type_vec3:cgltf_type_vec4),"Clip channel dimensions/profile");
            const unsigned flag=width==3?1:2;require(!(seen[index]&flag),"Duplicate clip channel");seen[index]|=flag;
            for(unsigned k=0;k<=ticks;++k){
                float time{},value[4]{};require(cgltf_accessor_read_float(input,k,&time,1)&&cgltf_accessor_read_float(output,k,value,width),"Clip accessor read");close(time,k/60.f,.00001f,"Clip keys require exact normalized 60 Hz grid");
                for(unsigned c=0;c<width;++c)require(std::isfinite(value[c])&&std::abs(value[c])<=100,"Clip transform bounds");
                if(width==3)track.translations.push_back({time,{value[0],value[1],value[2]}});
                else {float norm{};for(float v:value)norm+=v*v;close(norm,1,.0001f,"Clip quaternion normalization");track.rotations.push_back({time,{value[0],value[1],value[2],value[3]}});}
            }
        }
        for(auto count:seen)require(count==3,"Missing clip track");
        auto& root_track=raw.tracks[0];const auto& rest=rig.joints[0];
        ozz::math::Quaternion rest_rotation(rest.rotation[0],rest.rotation[1],rest.rotation[2],rest.rotation[3]);
        const auto origin=root_track.translations.front().value;double previous{},first_yaw{};Json roots=Json::array();
        for(unsigned k=0;k<=ticks;++k){
            auto q=root_track.rotations[k].value;auto delta=q*ozz::math::Conjugate(rest_rotation);
            require(delta.y*delta.y+delta.w*delta.w>.00001f,"Clip root yaw singularity");
            double yaw=2*std::atan2(delta.y,delta.w);if(k)yaw=previous+std::remainder(yaw-previous,2*3.141592653589793);else first_yaw=yaw;previous=yaw;
            auto t=root_track.translations[k].value;const auto dx=t.x-origin.x,dz=t.z-origin.z;
            roots.push_back({float(std::cos(first_yaw)*dx-std::sin(first_yaw)*dz),t.y-origin.y,float(std::sin(first_yaw)*dx+std::cos(first_yaw)*dz),float(yaw-first_yaw)});
            root_track.translations[k].value={rest.translation[0],rest.translation[1],rest.translation[2]};
            auto rotation=ozz::math::Quaternion::FromAxisAngle(ozz::math::Float3(0,1,0),float(-yaw));root_track.rotations[k].value=rotation*q;
        }
        require(raw.Validate(),"Ozz raw clip validation");
        if(inspect)return out;
        require(ids.size()==2,"Clip owned UUID count");const auto id=ids.at("$source"),runtime=ids.at("runtime");
        Json manifest={{"schema",1},{"kind","clip"},{"id",id.text()},{"runtime",runtime.text()},{"skeleton",rig.id.text()},{"signature",rig.signature},{"ticks",ticks},{"joints",static_cast<unsigned>(rig.joints.size())},{"loop",loop},{"root",roots},{"root_policy","stripped-translation-yaw-60hz"},{"ozz",ozz_pin}};
        auto manifest_bytes=manifest.dump();decode_clip_manifest(manifest_bytes);
        ozz::animation::offline::AnimationBuilder builder;auto cooked=builder(raw);require(bool(cooked),"Ozz animation build failure");
        ozz::io::MemoryStream stream;{ozz::io::OArchive archive(&stream);archive<<*cooked;}
        std::string archive(stream.Size(),'\0');stream.Seek(0,ozz::io::Stream::kSet);require(stream.Read(archive.data(),archive.size())==archive.size(),"Ozz clip archive read");
        out.products.push_back({id,"clip","clip.json",std::move(manifest_bytes),{runtime,rig.id}});
        out.products.push_back({runtime,"ozz-animation","ozzanim",std::move(archive),{}});
        auto closure=import_skeleton(root,canonical,{{"$source",rig.id},{"runtime",rig.runtime}},false);
        for(auto& product:closure.products){require(product.id!=id&&product.id!=runtime,"Clip and rig UUID collision");out.products.push_back(std::move(product));}
        return out;
    }
    Import import_skeleton(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
        auto bytes=read(source,1024*1024);
        auto rig=decode_rig_source(bytes);
        Import out;
        out.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
        out.keys={
            "runtime"
        };
        if(inspect)return out;
        require(ids.size()==2&&ids.at("$source")==rig.id&&ids.at("runtime")==rig.runtime,"Native skeleton source/product identity");
        ozz::animation::offline::RawSkeleton raw;
        std::vector<std::vector<int>> children(rig.joints.size());
        int root_index=-1;
        for(int i=0;i<rig.joints.size();++i){
            auto parent=rig.joints[i].parent;
            if(parent<0)root_index=i;
            else children[parent].push_back(i);
        }
        std::function<ozz::animation::offline::RawSkeleton::Joint(int,unsigned)> build=[&](int i,unsigned depth){
            require(depth<=64,"Rig hierarchy depth limit");
            const auto& source=rig.joints[i];
            ozz::animation::offline::RawSkeleton::Joint joint;
            joint.name=source.key.c_str();
            joint.transform.translation={
                source.translation[0],source.translation[1],source.translation[2]
            };
            joint.transform.rotation={
                source.rotation[0],source.rotation[1],source.rotation[2],source.rotation[3]
            };
            joint.transform.scale={
                1,1,1
            };
            for(auto child:children[i])joint.children.push_back(build(child,depth+1));
            return joint;
        };
        raw.roots.push_back(build(root_index,0));
        ozz::animation::offline::SkeletonBuilder builder;
        auto skeleton=builder(raw);
        require(skeleton&&skeleton->num_joints()==rig.joints.size(),"Ozz skeleton cook failure");
        for(int i=0;i<skeleton->num_joints();++i)require(skeleton->joint_names()[i]==rig.joints[i].key&&skeleton->joint_parents()[i]==rig.joints[i].parent,"Source joints must use canonical depth-first order");
        ozz::io::MemoryStream stream;
        {
            ozz::io::OArchive archive(&stream);
            archive<<*skeleton;
        }std::string archive(stream.Size(),'\0');
        stream.Seek(0,ozz::io::Stream::kSet);
        require(stream.Read(archive.data(),archive.size())==archive.size(),"Ozz archive read");
        Json manifest={
            {
                "schema",1
            },{
                "kind","skeleton"
            },{
                "id",rig.id.text()
            },{
                "signature",rig.signature
            },{
                "ozz",ozz_pin
            },{
                "definition",json(bytes)
            },{
                "runtime",rig.runtime.text()
            }
        };
        out.products.push_back({
            rig.id,"skeleton","skeleton.json",manifest.dump(),{
                rig.runtime
            }
        });
        out.products.push_back({
            rig.runtime,"ozz-skeleton","ozz",archive,{
            }
        });
        return out;
    }
    Import import_human(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,const std::filesystem::path& canonical,bool inspect){
        require(canonical.extension()==".daskeleton","Human canonical source must be native skeleton");
        auto rig_bytes=read(canonical,1024*1024);
        auto rig=decode_rig_source(rig_bytes);
        require(rig.human,"Human skin requires canonical human rig");
        Import out;
        out.keys={
            "skin"
        };
        out.product_count=4;
        out.inputs[canonical.lexically_relative(root).generic_string()]=sha256(rig_bytes);
        auto bytes=read(source,64*1024*1024);
        out.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
        cgltf_options options{
        };
        cgltf_data* pointer{
        };
        require(cgltf_parse(&options,bytes.data(),bytes.size(),&pointer)==cgltf_result_success,"Human cgltf parse");
        std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(pointer,cgltf_free);
        json(std::string_view(data->json,data->json_size),64*1024*1024);
        require(data->skins_count==1&&data->nodes_count<=1024&&data->animations_count==0&&data->extensions_required_count==0,"Initial human interchange profile: one skin and no clips/required extensions");
        std::vector<std::string> buffers;
        buffers.reserve(data->buffers_count);
        require(data->buffers_count<=16,"Human buffer limit");
        std::size_t total=bytes.size()+rig_bytes.size();
        for(std::size_t i=0;i<data->buffers_count;++i){
            auto& buffer=data->buffers[i];
            if(buffer.uri){
                auto uri=std::string_view(buffer.uri);
                require(!uri.empty()&&uri.find_first_of("%:\\")==uri.npos&&uri[0]!='/',"Unsafe human buffer URI");
                auto path=within(root,source.parent_path().lexically_relative(root)/std::filesystem::path(uri));
                buffers.push_back(read(path,64*1024*1024));
                out.inputs[path.lexically_relative(root).generic_string()]=sha256(buffers.back());
                total+=buffers.back().size();
                require(total<=128*1024*1024&&buffers.back().size()>=buffer.size,"Human buffer closure limit");
                buffer.data=buffers.back().data();
            }else{
                require(data->bin&&data->bin_size>=buffer.size,"Missing human GLB buffer");
                buffer.data=const_cast<void*>(data->bin);
            }buffer.data_free_method=cgltf_data_free_method_none;
        }
        require(cgltf_validate(data.get())==cgltf_result_success,"Human cgltf validation");
        auto& skin=data->skins[0];
        require(skin.joints_count==rig.joints.size()&&skin.inverse_bind_matrices&&skin.inverse_bind_matrices->count==skin.joints_count&&!skin.inverse_bind_matrices->is_sparse,"Canonical human joint/bind count");
        std::map<std::string,unsigned> keys;
        std::vector<Matrix> canonical_world;
        for(unsigned i=0;i<rig.joints.size();++i){
            keys[rig.joints[i].key]=i;
            auto local=transform(rig.joints[i]);
            canonical_world.push_back(rig.joints[i].parent<0?local:canonical_world[rig.joints[i].parent]*local);
        }
        std::vector<unsigned> remap;
        std::set<unsigned> unique;
        std::vector<Matrix> original_world,bind;
        for(unsigned i=0;i<skin.joints_count;++i){
            auto* node=skin.joints[i];
            require(node->name&&keys.contains(node->name),"Canonical human joint key missing");
            auto index=keys.at(node->name);
            require(unique.insert(index).second,"Duplicate human skin joint");
            std::string parent;
            for(auto* ancestor=node->parent;ancestor;ancestor=ancestor->parent)if(ancestor->name&&keys.contains(ancestor->name)){
                parent=ancestor->name;
                break;
            }require((rig.joints[index].parent<0&&parent.empty())||(rig.joints[index].parent>=0&&parent==rig.joints[rig.joints[index].parent].key),"Canonical hierarchy mismatch");
            float matrix[16],inverse[16];
            cgltf_node_transform_world(node,matrix);
            require(cgltf_accessor_read_float(skin.inverse_bind_matrices,i,inverse,16),"Human inverse bind matrix read");
            auto world=load_matrix(matrix);
            ozz::math::Transform normalized;
            require(ozz::math::ToAffine(world,&normalized),"Human joint matrix decomposition");
            close(normalized.scale.x,normalized.scale.y,.0001f,"Nonuniform skin joint scale");
            close(normalized.scale.x,normalized.scale.z,.0001f,"Nonuniform skin joint scale");
            require(normalized.scale.x>0,"Reflected skin joint scale");
            auto rigid=Matrix::FromAffine(normalized.translation,normalized.rotation,ozz::math::Float3(1,1,1));
            for(int column=0;column<4;++column){
                float actual[4],expected[4];
                ozz::math::StorePtrU(rigid.cols[column],actual);
                ozz::math::StorePtrU(canonical_world[index].cols[column],expected);
                for(int row=0;row<4;++row)close(actual[row],expected[row],.0003f,"Canonical rest/bind mismatch; offline conversion required");
            }remap.push_back(index);
            original_world.push_back(world);
            bind.push_back(load_matrix(inverse));
        }
        Json meshes=Json::array();
        unsigned vertices_total{
        };
        for(std::size_t node_index=0;node_index<data->nodes_count;++node_index){
            auto* node=&data->nodes[node_index];
            if(!node->mesh)continue;
            require(node->skin==&skin,"All human fixture meshes must bind the canonical skin");
            float mesh_matrix[16];
            cgltf_node_transform_world(node,mesh_matrix);
            // Skinned mesh node transforms cancel out in glTF; use joint matrices only.
            for(std::size_t primitive_index=0;primitive_index<node->mesh->primitives_count;++primitive_index){
                auto& primitive=node->mesh->primitives[primitive_index];
                require(primitive.type==cgltf_primitive_type_triangles&&primitive.targets_count==0&&primitive.indices&&!primitive.indices->is_sparse,"Human triangles/morph profile");
                const cgltf_accessor *positions{
                },*weights{
                },*joints{
                };
                for(std::size_t a=0;a<primitive.attributes_count;++a){
                    auto& attribute=primitive.attributes[a];
                    if(attribute.type==cgltf_attribute_type_position)positions=attribute.data;
                    else if(attribute.type==cgltf_attribute_type_weights){
                        require(attribute.index==0,"More than four influences unsupported");
                        weights=attribute.data;
                    }else if(attribute.type==cgltf_attribute_type_joints){
                        require(attribute.index==0,"More than four influences unsupported");
                        joints=attribute.data;
                    }
                }
                require(positions&&weights&&joints&&positions->count==weights->count&&positions->count==joints->count&&!positions->is_sparse&&!weights->is_sparse&&!joints->is_sparse&&joints->type==cgltf_type_vec4&&weights->type==cgltf_type_vec4,"Human skin accessor contract");
                vertices_total+=static_cast<unsigned>(positions->count);
                require(vertices_total<=100000&&primitive.indices->count<=300000&&primitive.indices->count%3==0,"Human mesh bounds");
                Json vertices=Json::array();
                for(std::size_t vertex=0;vertex<positions->count;++vertex){
                    float position[3],weight[4];
                    cgltf_uint indices[4];
                    require(cgltf_accessor_read_float(positions,vertex,position,3)&&cgltf_accessor_read_float(weights,vertex,weight,4)&&cgltf_accessor_read_uint(joints,vertex,indices,4),"Human vertex read");
                    std::array<float,3> expected{
                        position[0],position[1],position[2]
                    };
                    std::array<float,3> restored{
                    };
                    float sum{
                    };
                    Json mapped=Json::array(),values=Json::array();
                    for(unsigned influence=0;influence<4;++influence){
                        require(indices[influence]<remap.size()&&std::isfinite(weight[influence])&&weight[influence]>=0&&weight[influence]<=1,"Human joint/weight range");
                        sum+=weight[influence];
                        auto posed=point(original_world[indices[influence]]*bind[indices[influence]],position);
                        for(unsigned axis=0;axis<3;++axis)restored[axis]+=posed[axis]*weight[influence];
                        mapped.push_back(remap[indices[influence]]);
                        values.push_back(weight[influence]);
                    }close(sum,1,.002f,"Human weights must sum to one");
                    for(unsigned axis=0;axis<3;++axis)close(restored[axis],expected[axis],.0005f,"glTF inverse bind pose mismatch");
                    vertices.push_back({
                        {
                            "position",expected
                        },{
                            "joints",mapped
                        },{
                            "weights",values
                        }
                    });
                }
                Json indices=Json::array();
                for(std::size_t i=0;i<primitive.indices->count;++i){
                    auto index=cgltf_accessor_read_index(primitive.indices,i);
                    require(index<positions->count,"Human index bounds");
                    indices.push_back(index);
                }meshes.push_back({
                    {
                        "vertices",vertices
                    },{
                        "indices",indices
                    }
                });
            }
        }require(!meshes.empty()&&meshes.size()<=32,"Human skin mesh bounds");
        if(inspect)return out;
        require(ids.size()==2&&ids.contains("skin"),"Human source product mapping");
        auto id=ids.at("$source"),skin_id=ids.at("skin");
        Json inverse_bind=Json::array();
        for(auto world:canonical_world){
            auto inverse=ozz::math::Invert(world);
            std::array<float,16> matrix;
            for(int column=0;column<4;++column)ozz::math::StorePtrU(inverse.cols[column],matrix.data()+column*4);
            inverse_bind.push_back(matrix);
        }
        Json skin_product={
            {
                "inverse_bind",inverse_bind
            },{
                "schema",1
            },{
                "kind","skin-binding"
            },{
                "id",skin_id.text()
            },{
                "signature",rig.signature
            },{
                "meshes",meshes
            }
        };
        Json manifest={
            {
                "schema",1
            },{
                "kind","human-binding"
            },{
                "id",id.text()
            },{
                "skeleton",rig.id.text()
            },{
                "skin",skin_id.text()
            },{
                "signature",rig.signature
            },{
                "axes","right-handed-y-up-metres"
            }
        };
        out.products.push_back({
            id,"human-binding","human.json",manifest.dump(),{
                rig.id,skin_id
            }
        });
        auto skin_bytes=skin_product.dump();
        json(skin_bytes,16*1024*1024); // Apply the runtime decode work budget before publishing a generation.
        out.products.push_back({
            skin_id,"skin-binding","skin.json",std::move(skin_bytes),{
                rig.id
            }
        });
        auto closure=import_skeleton(root,canonical,{
            {
                "$source",rig.id
            },{
                "runtime",rig.runtime
            }
        },false);
        for(auto& product:closure.products)out.products.push_back(std::move(product));
        return out;
    }
}
namespace darkangel {
    namespace {
        struct Registry {
            std::filesystem::path cas;
            std::map<AssetId,assets_detail::Json> records;
            Registry(const std::filesystem::path& path,const std::filesystem::path& cache):cas(cache){
                using namespace assets_detail;
                auto manifest=json(read(path,1024*1024));
                require(manifest.at("schema")==1&&manifest.at("assets").size()<=512,"Animation registry schema/bounds");
                for(auto record:manifest.at("assets"))require(records.emplace(AssetId::parse(record.at("id").get<std::string>()),record).second,"Duplicate animation product ID");
            }std::string load(AssetId id,std::string_view kind,std::string_view extension){
                using namespace assets_detail;
                require(records.contains(id),"Missing required animation product");
                auto record=records.at(id);
                require(record.at("kind").get<std::string>()==kind&&record.at("extension").get<std::string>()==extension,"Animation product type mismatch");
                auto hash=record.at("sha256").get<std::string>();
                require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Animation artifact hash");
                auto bytes=read(cas/(hash+"."+std::string(extension)),16*1024*1024);
                require(sha256(bytes)==hash,"Animation artifact digest mismatch");
                return bytes;
            }
        };
    }
    CookedRig load_cooked_rig(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
        using namespace assets_detail;
        Registry files(registry,cas);
        auto manifest=json(files.load(id,"skeleton","skeleton.json"));
        require(manifest.at("schema")==1&&manifest.at("id")==id.text()&&manifest.at("ozz")=="744eb9d99f606eda849acb0b1204f7a3dc20bca1","Cooked rig schema/codec pin");
        auto definition=decode_rig_source(manifest.at("definition").dump());
        require(definition.id==id&&definition.signature==manifest.at("signature").get<std::string>()&&definition.runtime.text()==manifest.at("runtime").get<std::string>(),"Cooked rig identity/signature");
        return {
            definition,files.load(definition.runtime,"ozz-skeleton","ozz")
        };
    }
    CookedClip load_cooked_clip(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
        using namespace assets_detail;Registry files(registry,cas);auto definition=decode_clip_manifest(files.load(id,"clip","clip.json"));
        require(definition.id==id,"Cooked clip identity");auto rig=load_cooked_rig(registry,cas,definition.skeleton);
        require(definition.signature==rig.definition.signature&&definition.joints==rig.definition.joints.size(),"Cooked clip skeleton compatibility");
        return {definition,files.load(definition.runtime,"ozz-animation","ozzanim")};
    }
    std::string load_cooked_human_binding(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
        using namespace assets_detail;
        Registry files(registry,cas);
        auto manifest=json(files.load(id,"human-binding","human.json"));
        require(manifest.at("schema")==1&&manifest.at("id")==id.text()&&manifest.at("axes")=="right-handed-y-up-metres","Cooked human schema/axes");
        auto rig=load_cooked_rig(registry,cas,AssetId::parse(manifest.at("skeleton").get<std::string>()));
        require(manifest.at("signature")==rig.definition.signature,"Cooked human canonical compatibility");
        auto bytes=files.load(AssetId::parse(manifest.at("skin").get<std::string>()),"skin-binding","skin.json");
        auto skin=json(bytes,16*1024*1024);
        require(skin.at("schema")==1&&skin.at("signature")==rig.definition.signature,"Cooked skin compatibility");
        return bytes;
    }
}
