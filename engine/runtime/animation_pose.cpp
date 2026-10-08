#include <darkangel/animation.hpp>
#include <darkangel/hash.hpp>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/containers/vector.h>
#include <stdexcept>
#include <cmath>
#include <algorithm>
namespace darkangel {
    struct AnimationClip::Impl {
        ClipDefinition definition;ozz::animation::Animation animation;std::string generation;
        Impl(ClipDefinition d,std::string_view bytes):definition(std::move(d)),generation(sha256(bytes)){
            if(bytes.size()<32||bytes.size()>16*1024*1024||!definition.ticks||definition.ticks>600||definition.joints>256)throw std::runtime_error("Ozz clip archive bounds");
            ozz::io::MemoryStream stream;stream.Write(bytes.data(),bytes.size());stream.Seek(0,ozz::io::Stream::kSet);
            ozz::io::IArchive archive(&stream);if(!archive.TestTag<ozz::animation::Animation>())throw std::runtime_error("Ozz animation archive type");archive>>animation;
            if(animation.num_tracks()!=static_cast<int>(definition.joints)||std::abs(animation.duration()-definition.ticks/60.f)>.00001f)throw std::runtime_error("Ozz clip duration/track mismatch");
        }
    };
    AnimationClip::AnimationClip(ClipDefinition definition,std::string_view bytes):impl_(std::make_unique<Impl>(std::move(definition),bytes)){}
    AnimationClip::~AnimationClip()=default;
    const ClipDefinition& AnimationClip::definition()const{return impl_->definition;}
    struct RigPose::Impl {
        RigDefinition definition;
        ozz::animation::Skeleton skeleton;
        ozz::vector<ozz::math::Float4x4> models;
        std::vector<JointMatrix> matrices;
        ozz::vector<ozz::math::SoaTransform> locals;
        ozz::animation::SamplingJob::Context context;
        std::string generation;
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
            locals.resize(skeleton.num_soa_joints());context.Resize(skeleton.num_joints());
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
    const std::vector<JointMatrix>& RigPose::sample(const AnimationClip& clip,double tick){
        auto& s=*impl_;const auto& c=*clip.impl_;
        if(!std::isfinite(tick)||tick<0||tick>1e9||c.definition.signature!=s.definition.signature||c.definition.skeleton!=s.definition.id)throw std::runtime_error("Clip sampling clock/skeleton mismatch");
        if(s.generation!=c.generation){s.context.Invalidate();s.generation=c.generation;}
        const auto phase=c.definition.loop?std::fmod(tick,double(c.definition.ticks)):std::min(tick,double(c.definition.ticks));
        ozz::animation::SamplingJob sample;sample.animation=&c.animation;sample.context=&s.context;sample.ratio=float(phase/c.definition.ticks);sample.output=ozz::make_span(s.locals);
        if(!sample.Run())throw std::runtime_error("Ozz clip sampling failed");
        ozz::animation::LocalToModelJob job;job.skeleton=&s.skeleton;job.input=ozz::make_span(s.locals);job.output=ozz::make_span(s.models);
        if(!job.Run())throw std::runtime_error("Ozz sampled pose conversion failed");
        for(std::size_t i=0;i<s.models.size();++i)for(int column=0;column<4;++column)ozz::math::StorePtr(s.models[i].cols[column],s.matrices[i].values.data()+column*4);
        return s.matrices;
    }
}
