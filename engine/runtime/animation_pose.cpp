#include <darkangel/animation.hpp>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/containers/vector.h>
#include <stdexcept>
namespace darkangel {
    struct RigPose::Impl {
        RigDefinition definition;
        ozz::animation::Skeleton skeleton;
        ozz::vector<ozz::math::Float4x4> models;
        std::vector<JointMatrix> matrices;
        Impl(RigDefinition rig,std::string_view bytes):definition(std::move(rig)){
            if(bytes.size()>1024*1024||bytes.size()<32)throw std::runtime_error("Ozz archive bounds");
            ozz::io::MemoryStream stream;
            stream.Write(bytes.data(),bytes.size());
            stream.Seek(0,ozz::io::Stream::kSet);
            ozz::io::IArchive archive(&stream);
            if(!archive.TestTag<ozz::animation::Skeleton>())throw std::runtime_error("Ozz skeleton archive type");
            archive>>skeleton;
            if(skeleton.num_joints()!=definition.joints.size())throw std::runtime_error("Ozz skeleton joint count");
            for(int i=0;i<skeleton.num_joints();++i)if(skeleton.joint_names()[i]!=definition.joints[i].key||skeleton.joint_parents()[i]!=definition.joints[i].parent)throw std::runtime_error("Ozz canonical joint order mismatch");
            models.resize(skeleton.num_joints());
            matrices.resize(skeleton.num_joints());
        }
    };
    RigPose::RigPose(RigDefinition definition,std::string_view bytes):impl_(std::make_unique<Impl>(std::move(definition),bytes)){
    }RigPose::~RigPose()=default;
    const RigDefinition& RigPose::definition()const{
        return impl_->definition;
    }
    const std::vector<JointMatrix>& RigPose::rest_pose(){
        auto& s=*impl_;
        ozz::animation::LocalToModelJob job;
        job.skeleton=&s.skeleton;
        job.input=s.skeleton.joint_rest_poses();
        job.output=ozz::make_span(s.models);
        if(!job.Run())throw std::runtime_error("Ozz rest pose job failed");
        for(std::size_t i=0;i<s.models.size();++i)for(int column=0;column<4;++column)ozz::math::StorePtr(s.models[i].cols[column],s.matrices[i].values.data()+column*4);
        return s.matrices;
    }
}
