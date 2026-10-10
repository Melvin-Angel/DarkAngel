#include <darkangel/animation_graph.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <locale>
#include <set>

namespace darkangel {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool bounded(float value){return std::isfinite(value)&&std::abs(value)<=100;}
double area(const GraphPoint& a,const GraphPoint& b,const GraphPoint& c){return double(b.x-a.x)*(c.y-a.y)-double(b.y-a.y)*(c.x-a.x);}
bool overlap(const std::vector<GraphPoint>& points,const std::array<unsigned,3>& first,const std::array<unsigned,3>& second){
    for(const auto& triangle:{first,second})for(unsigned edge=0;edge<3;++edge){
        const auto& a=points[triangle[edge]];const auto& b=points[triangle[(edge+1)%3]];
        double x=-(b.y-a.y),y=b.x-a.x;
        auto range=[&](const auto& value){std::array<double,3> projected;for(unsigned i=0;i<3;++i)projected[i]=points[value[i]].x*x+points[value[i]].y*y;auto bounds=std::minmax_element(projected.begin(),projected.end());return std::pair(*bounds.first,*bounds.second);};
        auto left=range(first),right=range(second);
        if(left.second<=right.first+1e-8||right.second<=left.first+1e-8)return false;
    }
    return true;
}
}
struct AnimationGraphPlan::Impl {
    using TagMask=std::array<std::uint64_t,2>;
    struct Node { GraphNode source; std::vector<unsigned> children; unsigned leaf{},active_bound{};std::uint32_t mask{};std::vector<TagMask> all;TagMask any{},none{}; };
    std::string generation;
    std::vector<Node> nodes;
    std::array<std::shared_ptr<const AnimationClip>,32> leaves;
    unsigned leaf_count{},root{};
    std::shared_ptr<const TagDictionary> tags;

    std::array<double,32> weights(GraphParameters parameters) const {
        require(bounded(parameters.speed)&&parameters.speed>=0&&bounded(parameters.forward)&&bounded(parameters.lateral)&&std::isfinite(parameters.playback_rate)&&parameters.playback_rate>=0&&parameters.playback_rate<=4,"Graph parameter bounds");
        TagMask present{};
        if(tags){
            require(parameters.tags,"Tagged graph requires an explicit actor tag snapshot");const auto& snapshot=*parameters.tags;
            require(snapshot.registry==tags->registry()&&snapshot.generation==tags->generation()&&snapshot.values.size()<=128,"Graph tag snapshot registry generation/bound");
            auto definitions=tags->definitions();
            for(auto id:snapshot.values){auto found=std::lower_bound(definitions.begin(),definitions.end(),id,[](const auto& field,TagId value){return field.id<value;});require(found!=definitions.end()&&found->id==id&&found->visibility!=AttributeVisibility::Server,"Graph tag snapshot unknown/private ID");auto index=static_cast<unsigned>(found-definitions.begin());auto bit=std::uint64_t{1}<<(index%64);require(!(present[index/64]&bit),"Graph tag snapshot duplicate ID");present[index/64]|=bit;}
        }
        std::array<std::array<double,32>,32> outputs{};
        for(unsigned index=0;index<nodes.size();++index){
            const auto& node=nodes[index];const auto& source=node.source;auto& result=outputs[index];
            if(source.kind==GraphNodeKind::Clip){result[node.leaf]=1;continue;}
            std::array<double,32> selected{};
            if(source.kind==GraphNodeKind::TagSelect){
                auto intersects=[&](const TagMask& mask){return (present[0]&mask[0])||(present[1]&mask[1]);};
                bool matches=std::all_of(node.all.begin(),node.all.end(),intersects)&&(source.requirements.any.empty()||intersects(node.any))&&!intersects(node.none);
                selected[matches?1:0]=1;
            }else if(source.kind==GraphNodeKind::Blend1D){
                const auto value=source.parameter==GraphParameter::Speed?parameters.speed:source.parameter==GraphParameter::Forward?parameters.forward:parameters.lateral;
                if(value<=source.points.front().x)selected[0]=1;
                else if(value>=source.points.back().x)selected[source.points.size()-1]=1;
                else for(unsigned point=1;point<source.points.size();++point)if(value<=source.points[point].x){
                    auto alpha=double(value-source.points[point-1].x)/(source.points[point].x-source.points[point-1].x);
                    selected[point-1]=1-alpha;selected[point]=alpha;break;
                }
            }else{
                GraphPoint query{0,parameters.lateral,parameters.forward};bool inside=false;
                for(const auto& triangle:source.triangles){
                    const auto& a=source.points[triangle[0]];const auto& b=source.points[triangle[1]];const auto& c=source.points[triangle[2]];
                    auto denominator=area(a,b,c);std::array<double,3> barycentric{area(query,b,c)/denominator,area(a,query,c)/denominator,area(a,b,query)/denominator};
                    if(*std::min_element(barycentric.begin(),barycentric.end())>=-1e-7){
                        double total{};for(auto& value:barycentric){value=std::max(value,0.0);total+=value;}
                        for(unsigned point=0;point<3;++point)selected[triangle[point]]=barycentric[point]/total;
                        inside=true;break;
                    }
                }
                if(!inside){
                    // Clamp outside the authored domain to its nearest triangle
                    // edge. Authored order resolves ties deterministically.
                    double nearest=std::numeric_limits<double>::infinity();
                    for(const auto& triangle:source.triangles)for(unsigned edge=0;edge<3;++edge){
                        auto first=triangle[edge],second=triangle[(edge+1)%3];const auto& a=source.points[first];const auto& b=source.points[second];
                        double dx=b.x-a.x,dy=b.y-a.y,alpha=std::clamp(((query.x-a.x)*dx+(query.y-a.y)*dy)/(dx*dx+dy*dy),0.0,1.0);
                        double x=a.x+alpha*dx-query.x,y=a.y+alpha*dy-query.y,distance=x*x+y*y;
                        if(distance<nearest){nearest=distance;selected.fill(0);selected[first]=1-alpha;selected[second]=alpha;}
                    }
                }
            }
            for(unsigned point=0;point<source.points.size();++point)for(unsigned leaf=0;leaf<leaf_count;++leaf)result[leaf]+=selected[point]*outputs[node.children[point]][leaf];
        }
        return outputs[root];
    }
    GraphPoseInputs pose(GraphParameters parameters,double phase)const {
        auto selected=weights(parameters);GraphPoseInputs result;
        for(unsigned leaf=0;leaf<leaf_count;++leaf)if(selected[leaf]>0){
            require(result.count<result.layers.size(),"Graph active pose budget");result.layers[result.count++]={leaves[leaf].get(),phase*leaves[leaf]->definition().ticks,float(std::clamp(selected[leaf],0.0,1.0)),{}};
        }
        return result;
    }
};
AnimationGraphPlan::AnimationGraphPlan(std::string generation,std::uint32_t root,std::vector<GraphNode> source,std::shared_ptr<const TagDictionary> tags):impl_(std::make_unique<Impl>()){
    auto& plan=*impl_;require(generation.size()==64&&generation.find_first_not_of("0123456789abcdef")==generation.npos,"Graph generation digest");
    require(!source.empty()&&source.size()<=32,"Graph node budget");plan.generation=std::move(generation);if(tags)plan.tags=std::make_shared<const TagDictionary>(*tags);bool tagged=false;
    for(unsigned index=0;index<source.size();++index){
        const auto& node=source[index];require(node.id,"Graph stable node ID");
        for(unsigned prior=0;prior<index;++prior)require(source[prior].id!=node.id,"Duplicate graph node ID");
        require(node.kind==GraphNodeKind::Clip||node.kind==GraphNodeKind::Blend1D||node.kind==GraphNodeKind::Blend2D||node.kind==GraphNodeKind::TagSelect,"Unknown graph node kind");
        if(node.kind==GraphNodeKind::TagSelect){
            tagged=true;require(plan.tags&&plan.tags->registry()!=AssetId{}&&plan.tags->generation().size()==64&&plan.tags->generation().find_first_not_of("0123456789abcdef")==std::string::npos&&node.points.size()==2&&node.triangles.empty()&&!node.clip,"Tag selector requires a frozen dictionary and two branches");plan.tags->validate(node.requirements);
            require(!node.requirements.all.empty()||!node.requirements.any.empty()||!node.requirements.none.empty(),"Tag selector needs a condition");
            for(const auto& list:{node.requirements.all,node.requirements.any,node.requirements.none})for(auto id:list)for(const auto& tag:plan.tags->definitions())if(tag.id==id)require(tag.visibility!=AttributeVisibility::Server,"Presentation graph cannot select server-only tags");
            for(const auto& point:node.points)require(point.input&&point.x==0&&point.y==0,"Tag selector branch contract");continue;
        }
        require(node.requirements.all.empty()&&node.requirements.any.empty()&&node.requirements.none.empty(),"Blend/clip node cannot own tag conditions");
        require(node.parameter==GraphParameter::Speed||node.parameter==GraphParameter::Forward||node.parameter==GraphParameter::Lateral,"Unknown graph parameter");
        if(node.kind==GraphNodeKind::Clip){require(node.clip&&node.clip->definition().loop&&node.points.empty()&&node.triangles.empty(),"Locomotion clip node contract");continue;}
        require(!node.clip&&node.points.size()>=2&&node.points.size()<=32,"Graph point budget");
        for(unsigned point=0;point<node.points.size();++point){
            const auto& value=node.points[point];require(value.input&&bounded(value.x)&&bounded(value.y),"Graph point bounds");
            if(node.kind==GraphNodeKind::Blend1D){require(value.y==0,"1D point has no second coordinate");if(point)require(value.x>node.points[point-1].x,"1D points strictly ordered");}
            for(unsigned prior=0;prior<point;++prior)require(value.x!=node.points[prior].x||value.y!=node.points[prior].y,"Duplicate graph point coordinate");
        }
        if(node.kind==GraphNodeKind::Blend1D)require(node.triangles.empty(),"1D node has no triangles");
        else{
            require(node.points.size()>=3&&!node.triangles.empty()&&node.triangles.size()<=64,"2D triangle budget");
            std::array<bool,32> used{};
            for(unsigned index_triangle=0;index_triangle<node.triangles.size();++index_triangle){
                auto triangle=node.triangles[index_triangle];
                for(auto point:triangle){require(point<node.points.size(),"2D triangle point index");used[point]=true;}
                require(area(node.points[triangle[0]],node.points[triangle[1]],node.points[triangle[2]])>1e-6,"2D triangles must be nondegenerate counterclockwise");
                std::sort(triangle.begin(),triangle.end());
                for(unsigned prior=0;prior<index_triangle;++prior){auto other=node.triangles[prior];std::sort(other.begin(),other.end());require(triangle!=other,"Duplicate 2D triangle");require(!overlap(node.points,node.triangles[index_triangle],node.triangles[prior]),"Overlapping 2D triangles");}
            }
            for(unsigned point=0;point<node.points.size();++point)require(used[point],"Unused 2D point");
        }
    }
    require(tagged==bool(plan.tags),"Graph tag dictionary must match its selectors");
    auto lookup=[&](std::uint32_t id){auto found=std::find_if(source.begin(),source.end(),[&](const auto& node){return node.id==id;});require(found!=source.end(),"Missing graph input node");return unsigned(found-source.begin());};
    std::array<unsigned,32> colors{},compiled{};
    std::function<unsigned(unsigned,unsigned)> visit=[&](unsigned index,unsigned depth){
        require(depth<=16,"Graph depth budget");require(colors[index]!=1,"Graph dependency cycle");if(colors[index]==2)return compiled[index];colors[index]=1;
        Impl::Node node;node.source=source[index];
        if(node.source.kind==GraphNodeKind::TagSelect){
            // Compile hierarchy matching once; sampling uses two-word masks and
            // fixed scratch storage rather than allocations or hierarchy walks.
            auto mask=[&](std::span<const TagId> requested){Impl::TagMask result{};auto definitions=plan.tags->definitions();for(unsigned index=0;index<definitions.size();++index)for(auto id:requested)if(plan.tags->descends(definitions[index].id,id))result[index/64]|=std::uint64_t{1}<<(index%64);return result;};
            for(auto id:node.source.requirements.all)node.all.push_back(mask(std::span<const TagId>(&id,1)));node.any=mask(node.source.requirements.any);node.none=mask(node.source.requirements.none);
        }
        for(const auto& point:node.source.points)node.children.push_back(visit(lookup(point.input),depth+1));
        if(node.source.kind==GraphNodeKind::Clip){
            const auto& definition=node.source.clip->definition();
            if(plan.leaf_count){const auto& first=plan.leaves[0]->definition();require(definition.skeleton==first.skeleton&&definition.signature==first.signature&&definition.joints==first.joints,"Graph clip skeleton/signature mismatch");}
            auto found=std::find(plan.leaves.begin(),plan.leaves.begin()+plan.leaf_count,node.source.clip);
            node.leaf=unsigned(found-plan.leaves.begin());
            if(node.leaf==plan.leaf_count){require(plan.leaf_count<32,"Graph distinct clip catalogue budget");plan.leaves[plan.leaf_count++]=node.source.clip;}
        }
        if(node.source.kind==GraphNodeKind::Clip){node.mask=std::uint32_t(1)<<node.leaf;node.active_bound=1;}
        else{for(auto child:node.children)node.mask|=plan.nodes[child].mask;
            auto bound=[&](std::span<const unsigned> points){unsigned total{};std::uint32_t mask{};std::set<unsigned> unique;for(auto point:points){auto child=node.children[point];if(unique.insert(child).second)total+=plan.nodes[child].active_bound;mask|=plan.nodes[child].mask;}return std::min(total,unsigned(std::popcount(mask)));};
            if(node.source.kind==GraphNodeKind::TagSelect)for(auto child:node.children)node.active_bound=std::max(node.active_bound,plan.nodes[child].active_bound);
            else if(node.source.kind==GraphNodeKind::Blend1D)for(unsigned point=1;point<node.children.size();++point){std::array<unsigned,2> pair{point-1,point};node.active_bound=std::max(node.active_bound,bound(pair));}
            else for(const auto& triangle:node.source.triangles)node.active_bound=std::max(node.active_bound,bound(triangle));
            require(node.active_bound<=4,"Graph simultaneous pose layer budget");}
        compiled[index]=unsigned(plan.nodes.size());plan.nodes.push_back(std::move(node));colors[index]=2;return compiled[index];
    };
    plan.root=visit(lookup(root),0);for(unsigned index=0;index<source.size();++index)require(colors[index]==2,"Unreachable graph node");
    // Caller source generation is only a seed. Include the compiled topology,
    // parameter layout and frozen dependency content to fence corrections even
    // when two plans accidentally receive the same source revision.
    std::ostringstream identity;identity.imbue(std::locale::classic());identity<<std::hexfloat;
    identity<<plan.generation<<':'<<root<<'\n';
    if(plan.tags)identity<<plan.tags->registry().text()<<':'<<plan.tags->generation()<<'\n';
    for(const auto& node:source){
        identity<<node.id<<':'<<int(node.kind)<<':'<<int(node.parameter)<<'\n';
        if(node.clip){const auto& clip=node.clip->definition();identity<<clip.id.text()<<':'<<clip.runtime.text()<<':'<<clip.skeleton.text()<<':'<<clip.signature<<':'<<clip.joints<<':'<<clip.ticks<<':'<<clip.loop<<':'<<node.clip->archive_generation()<<'\n';for(const auto& key:clip.root)for(auto value:key)identity<<value<<',';identity<<'\n';}
        for(const auto& point:node.points)identity<<point.input<<':'<<point.x<<':'<<point.y<<'\n';
        for(const auto& triangle:node.triangles)identity<<triangle[0]<<':'<<triangle[1]<<':'<<triangle[2]<<'\n';
        if(node.kind==GraphNodeKind::TagSelect)for(const auto& list:{node.requirements.all,node.requirements.any,node.requirements.none}){identity<<'[';for(auto id:list)identity<<id<<',';identity<<']';}
    }
    plan.generation=sha256(identity.str());
}
AnimationGraphPlan::~AnimationGraphPlan()=default;
const TagDictionary* AnimationGraphPlan::tag_dictionary()const{return impl_->tags.get();}
AnimationGraphInstance::AnimationGraphInstance(std::shared_ptr<const AnimationGraphPlan> plan):plan_(std::move(plan)){
    require(bool(plan_),"Graph plan is required");state_.generation=plan_->impl_->generation;
}
GraphPoseInputs AnimationGraphInstance::evaluate(GraphParameters parameters)const{return plan_->impl_->pose(parameters,state_.phase);}
GraphPoseInputs AnimationGraphInstance::advance(std::uint64_t tick,GraphParameters parameters){
    require(state_.tick!=std::numeric_limits<std::uint64_t>::max()&&tick==state_.tick+1,"Graph requires consecutive fixed ticks");
    auto weights=plan_->impl_->weights(parameters);double delta{};
    for(unsigned leaf=0;leaf<plan_->impl_->leaf_count;++leaf)delta+=weights[leaf]/plan_->impl_->leaves[leaf]->definition().ticks;
    const auto accumulated=state_.phase+delta*parameters.playback_rate;
    // Snap only roundoff at an exact cycle boundary. Otherwise repeated 1/N
    // increments can leave the Nth tick just below one instead of at zero.
    const auto phase=std::max(0.0,accumulated-std::floor(accumulated+1e-12));
    auto result=plan_->impl_->pose(parameters,phase);state_.tick=tick;state_.phase=phase;return result;
}
void AnimationGraphInstance::restore(const GraphState& state){
    require(state.generation==plan_->impl_->generation&&std::isfinite(state.phase)&&state.phase>=0&&state.phase<1,"Graph correction generation/phase mismatch");state_.tick=state.tick;state_.phase=state.phase;
}
}
