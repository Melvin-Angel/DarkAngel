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
#include <variant>

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
bool compare_value(const GraphParameterValue& actual,GraphParameterType type,GraphCompareOperator op,const GraphParameterValue& expected){
    auto result=[&](auto a,auto b){switch(op){case GraphCompareOperator::Equal:return a==b;case GraphCompareOperator::NotEqual:return a!=b;case GraphCompareOperator::Less:return a<b;case GraphCompareOperator::LessEqual:return a<=b;case GraphCompareOperator::Greater:return a>b;case GraphCompareOperator::GreaterEqual:return a>=b;}return false;};
    if(type==GraphParameterType::Float)return result(std::get<float>(actual),std::get<float>(expected));
    if(type==GraphParameterType::Integer)return result(std::get<std::int32_t>(actual),std::get<std::int32_t>(expected));
    return op==GraphCompareOperator::Equal?std::get<bool>(actual)==std::get<bool>(expected):op==GraphCompareOperator::NotEqual?std::get<bool>(actual)!=std::get<bool>(expected):false;
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
    GraphMachineDefinition machine;
    std::map<std::uint32_t,unsigned> state_roots;

    GraphParameterValue parameter(GraphParameters parameters,std::string_view name,GraphParameterType type) const {
        if(parameters.values){if(type==GraphParameterType::Float)return parameters.values->get_float(name);if(type==GraphParameterType::Integer)return parameters.values->get_int(name);return parameters.values->get_bool(name);}
        if(name=="Speed")return parameters.speed;if(name=="Forward")return parameters.forward;if(name=="Lateral")return parameters.lateral;return type==GraphParameterType::Float?GraphParameterValue{0.f}:type==GraphParameterType::Integer?GraphParameterValue{std::int32_t{0}}:GraphParameterValue{false};
    }
    bool condition(const GraphCondition& value,GraphParameters parameters,const TagMask& present) const {
        auto has=[&](TagId tag){if(!tags)return false;auto definitions=tags->definitions();auto found=std::find_if(definitions.begin(),definitions.end(),[&](const auto& item){return item.id==tag;});if(found==definitions.end())return false;auto index=static_cast<unsigned>(found-definitions.begin());return (present[index/64]&(std::uint64_t{1}<<(index%64)))!=0;};
        switch(value.kind){case GraphConditionKind::Always:return true;case GraphConditionKind::HasTag:return has(value.tag);case GraphConditionKind::Compare:return compare_value(parameter(parameters,value.parameter,value.parameter_type),value.parameter_type,value.comparison,value.value);case GraphConditionKind::Not:return value.children.size()==1&&!condition(value.children.front(),parameters,present);case GraphConditionKind::All:return !value.children.empty()&&std::all_of(value.children.begin(),value.children.end(),[&](const auto& child){return condition(child,parameters,present);});case GraphConditionKind::Any:return !value.children.empty()&&std::any_of(value.children.begin(),value.children.end(),[&](const auto& child){return condition(child,parameters,present);});case GraphConditionKind::None:return !value.children.empty()&&std::none_of(value.children.begin(),value.children.end(),[&](const auto& child){return condition(child,parameters,present);});case GraphConditionKind::Xor:return std::count_if(value.children.begin(),value.children.end(),[&](const auto& child){return condition(child,parameters,present);})==1;}return false;
    }

    std::array<double,32> weights(GraphParameters parameters,unsigned selected_root) const {
        require(bounded(parameters.speed)&&parameters.speed>=0&&bounded(parameters.forward)&&bounded(parameters.lateral)&&std::isfinite(parameters.playback_rate)&&parameters.playback_rate>=0&&parameters.playback_rate<=4,"Graph parameter bounds");
        TagMask present{};
        if(tags){
            require(parameters.tags,"Tagged graph requires an explicit actor tag snapshot");const auto& snapshot=*parameters.tags;
            require(snapshot.registry==tags->registry()&&snapshot.generation==tags->generation()&&snapshot.values.size()<=128&&(parameters.tag_audience==AttributeVisibility::Public||parameters.tag_audience==AttributeVisibility::Owner),"Graph tag snapshot registry generation/bound");
            auto definitions=tags->definitions();
            for(auto id:snapshot.values){auto found=std::lower_bound(definitions.begin(),definitions.end(),id,[](const auto& field,TagId value){return field.id<value;});require(found!=definitions.end()&&found->id==id&&(found->visibility==AttributeVisibility::Public||(parameters.tag_audience==AttributeVisibility::Owner&&found->visibility==AttributeVisibility::Owner)),"Graph tag snapshot unknown/private ID");auto index=static_cast<unsigned>(found-definitions.begin());auto bit=std::uint64_t{1}<<(index%64);require(!(present[index/64]&bit),"Graph tag snapshot duplicate ID");present[index/64]|=bit;}
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
        return outputs[selected_root];
    }
    GraphPoseInputs pose(GraphParameters parameters,double phase,unsigned selected_root)const {
        auto selected=weights(parameters,selected_root);GraphPoseInputs result;
        for(unsigned leaf=0;leaf<leaf_count;++leaf)if(selected[leaf]>0){
            require(result.count<result.layers.size(),"Graph active pose budget");result.layers[result.count++]={leaves[leaf].get(),phase*leaves[leaf]->definition().ticks,float(std::clamp(selected[leaf],0.0,1.0)),{}};
        }
        return result;
    }
};
AnimationGraphPlan::AnimationGraphPlan(std::string generation,std::uint32_t root,std::vector<GraphNode> source,std::shared_ptr<const TagDictionary> tags)
    : AnimationGraphPlan(std::move(generation),GraphMachineDefinition{{},{GraphStateDefinition{1,"Default",root}},{},1},std::move(source),std::move(tags)) {}
AnimationGraphPlan::AnimationGraphPlan(std::string generation,GraphMachineDefinition machine,std::vector<GraphNode> source,std::shared_ptr<const TagDictionary> tags):impl_(std::make_unique<Impl>()){
    auto& plan=*impl_;require(generation.size()==64&&generation.find_first_not_of("0123456789abcdef")==generation.npos,"Graph generation digest");
    require(!source.empty()&&source.size()<=32,"Graph node budget");plan.generation=std::move(generation);plan.machine=std::move(machine);require(!plan.machine.states.empty()&&plan.machine.states.size()<=32&&plan.machine.entry,"Graph state machine needs an entry state");if(tags)plan.tags=std::make_shared<const TagDictionary>(*tags);bool tagged=false;
    auto state_lookup=[&](std::uint32_t id){auto found=std::find_if(plan.machine.states.begin(),plan.machine.states.end(),[&](const auto& state){return state.id==id;});require(found!=plan.machine.states.end(),"Graph state reference is missing");return found;};
    std::set<std::uint32_t> state_ids;for(const auto& state:plan.machine.states){require(state.id&&state.root&&state_ids.insert(state.id).second,"Graph state identity/root or duplicate ID");}
    state_lookup(plan.machine.entry);
    for(const auto& transition:plan.machine.transitions){require(transition.id&&transition.from!=transition.to,"Graph transition identity");state_lookup(transition.to);if(transition.from)state_lookup(transition.from);require(transition.crossfade_ticks<=600&&(!transition.has_exit_time||transition.exit_time>=0&&transition.exit_time<=1),"Graph transition timing");}
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
    auto parameter_lookup=[&](std::string_view name){return std::find_if(plan.machine.parameters.begin(),plan.machine.parameters.end(),[&](const auto& parameter){return parameter.name==name;});};
    for(const auto& parameter:plan.machine.parameters){require(!parameter.name.empty()&&parameter.name.size()<=64&&parameter.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")==std::string::npos,"Graph parameter name");if(parameter.type==GraphParameterType::Float)require(std::holds_alternative<float>(parameter.default_value)&&std::isfinite(std::get<float>(parameter.default_value)),"Graph float parameter default");if(parameter.type==GraphParameterType::Integer)require(std::holds_alternative<std::int32_t>(parameter.default_value),"Graph integer parameter default");if(parameter.type==GraphParameterType::Boolean)require(std::holds_alternative<bool>(parameter.default_value),"Graph boolean parameter default");}
    std::function<void(const GraphCondition&)> validate_condition=[&](const GraphCondition& condition){
        const auto count=condition.children.size();
        if(condition.kind==GraphConditionKind::Always)require(count==0,"Always condition cannot have children");
        else if(condition.kind==GraphConditionKind::HasTag){require(count==0&&plan.tags&&plan.tags->contains(condition.tag),"HasTag condition requires a registered tag");for(const auto& definition:plan.tags->definitions())if(definition.id==condition.tag)require(definition.visibility!=AttributeVisibility::Server,"Presentation graph cannot read server tags");}
        else if(condition.kind==GraphConditionKind::Compare){auto parameter=parameter_lookup(condition.parameter);require(parameter!=plan.machine.parameters.end()&&parameter->type==condition.parameter_type&&((condition.parameter_type==GraphParameterType::Float&&std::holds_alternative<float>(condition.value))||(condition.parameter_type==GraphParameterType::Integer&&std::holds_alternative<std::int32_t>(condition.value))||(condition.parameter_type==GraphParameterType::Boolean&&std::holds_alternative<bool>(condition.value))),"Typed graph comparison parameter mismatch");if(condition.parameter_type==GraphParameterType::Boolean)require(condition.comparison==GraphCompareOperator::Equal||condition.comparison==GraphCompareOperator::NotEqual,"Boolean graph comparison operator");}
        else if(condition.kind==GraphConditionKind::Not)require(count==1,"Not condition arity");
        else require(count>0&&count<=16,"Logical graph condition group");
        for(const auto& child:condition.children)validate_condition(child);
    };
    for(const auto& transition:plan.machine.transitions)validate_condition(transition.condition);
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
    for(const auto& state:plan.machine.states){auto compiled_root=visit(lookup(state.root),0);plan.state_roots.emplace(state.id,compiled_root);}
    plan.root=plan.state_roots.at(plan.machine.entry);for(unsigned index=0;index<source.size();++index)require(colors[index]==2,"Unreachable graph node");
    // Caller source generation is only a seed. Include the compiled topology,
    // parameter layout and frozen dependency content to fence corrections even
    // when two plans accidentally receive the same source revision.
    std::ostringstream identity;identity.imbue(std::locale::classic());identity<<std::hexfloat;
    identity<<plan.generation<<':'<<plan.machine.entry<<'\n';for(const auto& parameter:plan.machine.parameters)identity<<"param:"<<parameter.name<<':'<<int(parameter.type)<<':'<<parameter.default_value.index()<<'\n';for(const auto& state:plan.machine.states)identity<<"state:"<<state.id<<':'<<state.name<<':'<<state.root<<'\n';for(const auto& transition:plan.machine.transitions)identity<<"transition:"<<transition.id<<':'<<transition.from<<':'<<transition.to<<':'<<transition.priority<<':'<<transition.crossfade_ticks<<':'<<transition.has_exit_time<<':'<<transition.exit_time<<'\n';
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
unsigned AnimationGraphPlan::active_layer_bound()const{return impl_->nodes[impl_->root].active_bound;}
const TagDictionary* AnimationGraphPlan::tag_dictionary()const{return impl_->tags.get();}
const std::vector<GraphParameterDefinition>& AnimationGraphPlan::parameters()const{return impl_->machine.parameters;}
const GraphMachineDefinition* AnimationGraphPlan::machine()const{return impl_->machine.states.size()>1||!impl_->machine.transitions.empty()?&impl_->machine:nullptr;}
AnimationGraphInstance::AnimationGraphInstance(std::shared_ptr<const AnimationGraphPlan> plan):plan_(std::move(plan)){
    require(bool(plan_),"Graph plan is required");state_.generation=plan_->impl_->generation;state_.active_state=plan_->impl_->machine.entry;for(const auto& parameter:plan_->parameters())parameters_.values[parameter.name]=parameter.default_value;
}
GraphPoseInputs AnimationGraphInstance::evaluate(GraphParameters parameters)const{auto& impl=*plan_->impl_;return impl.pose(parameters,state_.phase,impl.state_roots.at(state_.active_state));}
GraphPoseInputs AnimationGraphInstance::advance(std::uint64_t tick,GraphParameters parameters){
    require(state_.tick!=std::numeric_limits<std::uint64_t>::max()&&tick==state_.tick+1,"Graph requires consecutive fixed ticks");
    auto& impl=*plan_->impl_;(void)impl.weights(parameters,impl.state_roots.at(state_.active_state));if(!impl.machine.transitions.empty()){
        std::array<std::uint64_t,2> present{};if(impl.tags&&parameters.tags)for(auto id:parameters.tags->values){auto definitions=impl.tags->definitions();auto found=std::find_if(definitions.begin(),definitions.end(),[&](const auto& item){return item.id==id;});if(found!=definitions.end()){auto index=static_cast<unsigned>(found-definitions.begin());present[index/64]|=std::uint64_t{1}<<(index%64);}}
        const GraphTransitionDefinition* selected=nullptr;for(const auto& transition:impl.machine.transitions)if((transition.from==0||transition.from==state_.active_state)&&(!transition.has_exit_time||state_.phase>=transition.exit_time)&&impl.condition(transition.condition,parameters,present)){if(!selected||std::tie(transition.priority,transition.id)>std::tie(selected->priority,selected->id))selected=&transition;}
        if(selected){state_.previous_state=state_.active_state;state_.active_state=selected->to;state_.phase=0;state_.transition_ticks=selected->crossfade_ticks;state_.transition_total=selected->crossfade_ticks;}
    }
    auto weights=impl.weights(parameters,impl.state_roots.at(state_.active_state));double delta{};
    for(unsigned leaf=0;leaf<plan_->impl_->leaf_count;++leaf)delta+=weights[leaf]/plan_->impl_->leaves[leaf]->definition().ticks;
    const auto accumulated=state_.phase+delta*parameters.playback_rate;
    // Snap only roundoff at an exact cycle boundary. Otherwise repeated 1/N
    // increments can leave the Nth tick just below one instead of at zero.
    const auto phase=std::max(0.0,accumulated-std::floor(accumulated+1e-12));
    auto result=impl.pose(parameters,phase,impl.state_roots.at(state_.active_state));
    if(state_.transition_ticks){const auto alpha=1.f-float(state_.transition_ticks)/std::max(1u,state_.transition_total);auto previous=impl.pose(parameters,phase,impl.state_roots.at(state_.previous_state));for(const auto& layer:previous.span()){if(result.count>=result.layers.size())break;auto copy=layer;copy.weight*=1-alpha;result.layers[result.count++]=copy;}for(unsigned i=0;i<result.count;++i)result.layers[i].weight*=alpha;--state_.transition_ticks;}
    state_.tick=tick;state_.phase=phase;return result;
}
GraphPoseInputs AnimationGraphInstance::evaluate()const{return evaluate(GraphParameters{.values=&parameters_});}
GraphPoseInputs AnimationGraphInstance::advance(std::uint64_t tick){return advance(tick,GraphParameters{.values=&parameters_});}
void AnimationGraphInstance::set_float(std::string_view name,float value){auto it=std::find_if(plan_->parameters().begin(),plan_->parameters().end(),[&](const auto& parameter){return parameter.name==name;});require(it!=plan_->parameters().end()&&it->type==GraphParameterType::Float,"Unknown float animation parameter");parameters_.set_float(name,value);}
void AnimationGraphInstance::set_int(std::string_view name,std::int32_t value){auto it=std::find_if(plan_->parameters().begin(),plan_->parameters().end(),[&](const auto& parameter){return parameter.name==name;});require(it!=plan_->parameters().end()&&it->type==GraphParameterType::Integer,"Unknown integer animation parameter");parameters_.set_int(name,value);}
void AnimationGraphInstance::set_bool(std::string_view name,bool value){auto it=std::find_if(plan_->parameters().begin(),plan_->parameters().end(),[&](const auto& parameter){return parameter.name==name;});require(it!=plan_->parameters().end()&&it->type==GraphParameterType::Boolean,"Unknown boolean animation parameter");parameters_.set_bool(name,value);}
void AnimationGraphInstance::restore(const GraphState& state){
    require(state.generation==plan_->impl_->generation&&std::isfinite(state.phase)&&state.phase>=0&&state.phase<1&&plan_->impl_->state_roots.contains(state.active_state),"Graph correction generation/phase mismatch");state_=state;
}
}
