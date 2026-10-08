#include <darkangel/animation.hpp>
#include <darkangel/hash.hpp>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/blending_job.h>
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
        struct LayerBuffers {ozz::vector<ozz::math::SoaTransform> locals;ozz::vector<ozz::math::SimdFloat4> weights;ozz::animation::SamplingJob::Context context;std::string generation;};
        std::array<LayerBuffers,4> layers;
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
            for(auto& layer:layers){layer.locals.resize(skeleton.num_soa_joints());layer.weights.resize(skeleton.num_soa_joints());layer.context.Resize(skeleton.num_joints());}
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
        if(!std::isfinite(tick)||tick<0||tick>1e9||c.definition.signature!=s.definition.signature||c.definition.skeleton!=s.definition.id||c.definition.joints!=s.definition.joints.size())throw std::runtime_error("Clip sampling clock/skeleton mismatch");
        if(s.generation!=c.generation){s.context.Invalidate();s.generation=c.generation;}
        const auto phase=c.definition.loop?std::fmod(tick,double(c.definition.ticks)):std::min(tick,double(c.definition.ticks));
        ozz::animation::SamplingJob sample;sample.animation=&c.animation;sample.context=&s.context;sample.ratio=float(phase/c.definition.ticks);sample.output=ozz::make_span(s.locals);
        if(!sample.Run())throw std::runtime_error("Ozz clip sampling failed");
        ozz::animation::LocalToModelJob job;job.skeleton=&s.skeleton;job.input=ozz::make_span(s.locals);job.output=ozz::make_span(s.models);
        if(!job.Run())throw std::runtime_error("Ozz sampled pose conversion failed");
        for(std::size_t i=0;i<s.models.size();++i)for(int column=0;column<4;++column)ozz::math::StorePtr(s.models[i].cols[column],s.matrices[i].values.data()+column*4);
        return s.matrices;
    }
    const std::vector<JointMatrix>& RigPose::blend(std::span<const PoseLayer> layers){
        auto& s=*impl_;if(layers.size()>s.layers.size())throw std::runtime_error("Pose layer budget");
        // Preflight every descriptor before any output/context mutation.
        for(const auto& layer:layers){if(!layer.clip||!std::isfinite(layer.tick)||layer.tick<0||layer.tick>1e9||!std::isfinite(layer.weight)||layer.weight<0||layer.weight>1||(!layer.mask.empty()&&layer.mask.size()!=s.definition.joints.size())||layer.clip->definition().signature!=s.definition.signature||layer.clip->definition().skeleton!=s.definition.id||layer.clip->definition().joints!=s.definition.joints.size())throw std::runtime_error("Pose layer clock/weight/mask/skeleton contract");for(auto weight:layer.mask)if(!std::isfinite(weight)||weight<0||weight>1)throw std::runtime_error("Pose joint weight bounds");}
        std::array<ozz::animation::BlendingJob::Layer,4> jobs;
        for(std::size_t n=0;n<layers.size();++n){const auto& layer=layers[n];const auto& clip=*layer.clip->impl_;auto& buffers=s.layers[n];if(buffers.generation!=clip.generation){buffers.context.Invalidate();buffers.generation=clip.generation;}
            auto phase=clip.definition.loop?std::fmod(layer.tick,double(clip.definition.ticks)):std::min(layer.tick,double(clip.definition.ticks));ozz::animation::SamplingJob sample;sample.animation=&clip.animation;sample.context=&buffers.context;sample.ratio=float(phase/clip.definition.ticks);sample.output=ozz::make_span(buffers.locals);if(!sample.Run())throw std::runtime_error("Ozz layer sample failed");jobs[n].weight=layer.weight;jobs[n].transform=ozz::make_span(buffers.locals);
            if(!layer.mask.empty()){for(std::size_t group=0;group<buffers.weights.size();++group){float values[4]{};for(unsigned lane=0;lane<4;++lane)if(group*4+lane<layer.mask.size())values[lane]=layer.mask[group*4+lane];buffers.weights[group]=ozz::math::simd_float4::LoadPtrU(values);}jobs[n].joint_weights=ozz::make_span(buffers.weights);}
        }
        ozz::animation::BlendingJob blend;blend.layers={jobs.data(),layers.size()};blend.rest_pose=s.skeleton.joint_rest_poses();blend.output=ozz::make_span(s.locals);if(!blend.Run())throw std::runtime_error("Ozz pose blend failed");
        ozz::animation::LocalToModelJob models;models.skeleton=&s.skeleton;models.input=ozz::make_span(s.locals);models.output=ozz::make_span(s.models);if(!models.Run())throw std::runtime_error("Ozz blended pose conversion failed");for(std::size_t i=0;i<s.models.size();++i)for(int column=0;column<4;++column)ozz::math::StorePtr(s.models[i].cols[column],s.matrices[i].values.data()+column*4);return s.matrices;
    }

}
