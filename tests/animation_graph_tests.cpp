#include <darkangel/animation_graph.hpp>
#include <darkangel/animation_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

using namespace darkangel;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F&& action){bool denied=false;try{action();}catch(const std::exception&){denied=true;}require(denied,"Invalid graph operation was accepted");}
using Clips=std::array<std::shared_ptr<const AnimationClip>,4>;
GraphNode clip_node(unsigned id,std::shared_ptr<const AnimationClip> clip){GraphNode node;node.id=id;node.kind=GraphNodeKind::Clip;node.clip=std::move(clip);return node;}
std::vector<GraphNode> linear(const Clips& clips){
    GraphNode root;root.id=10;root.kind=GraphNodeKind::Blend1D;root.points={{1,1,0},{2,5,0}};
    return {root,clip_node(2,clips[1]),clip_node(1,clips[0])};
}
std::vector<GraphNode> planar(const Clips& clips){
    GraphNode root;root.id=10;root.kind=GraphNodeKind::Blend2D;
    root.points={{3,1,0},{4,-1,0},{1,0,1},{2,0,2}};
    root.triangles={{{1,0,2}},{{0,3,2}},{{2,3,1}}};
    return {root,clip_node(1,clips[0]),clip_node(2,clips[1]),clip_node(3,clips[2]),clip_node(4,clips[3])};
}
float weight(const GraphPoseInputs& pose,const AnimationClip* clip){for(const auto& layer:pose.span())if(layer.clip==clip)return layer.weight;return 0;}
void same_pose(const std::vector<JointMatrix>& first,const std::vector<JointMatrix>& second){
    require(first.size()==second.size(),"Pose joint count");for(unsigned joint=0;joint<first.size();++joint)for(unsigned axis=0;axis<16;++axis)require(std::abs(first[joint].values[axis]-second[joint].values[axis])<.00002,"Graph endpoint disagrees with native clip sampling");
}
}
int main(){try{
    const auto generation=sha256("native initial graph fixture v1");auto project=std::filesystem::path(DAE_SOURCE_DIR);
    auto source=std::filesystem::path(DAE_BINARY_DIR)/("graph-fixture-"+AssetId::random().text());std::filesystem::create_directories(source);
    std::filesystem::copy_file(project/"content/animation/canonical_human.daskeleton",source/"human.daskeleton");AssetService assets(source,source/"cache");
    Clips clips;CookedRig rig;unsigned index{};
    for(auto name:{"omni-walk","omni-run","omni-left","omni-right"}){
        auto path=std::string(name)+".glb";std::filesystem::copy_file(project/".cache/fixtures"/path,source/path);
        auto id=assets.adopt_clip(path,"human.daskeleton",true);assets.cook(path);auto registry=source/(std::string(name)+".json");assets.package(id,registry);
        auto cooked=load_cooked_clip(registry,assets.cas_path(),id);if(!index)rig=load_cooked_rig(registry,assets.cas_path(),cooked.definition.skeleton);
        clips[index++]=std::make_shared<const AnimationClip>(cooked.definition,cooked.archive);
    }
    {
        auto dictionary=std::make_shared<TagDictionary>(std::vector<TagDefinition>{{1,0,"State",AttributeVisibility::Public},{2,1,"State.Moving",AttributeVisibility::Owner},{3,0,"Status.Blocked",AttributeVisibility::Public},{4,0,"Secret",AttributeVisibility::Server}},AssetId::random(),generation);
        GraphNode selector;selector.id=10;selector.kind=GraphNodeKind::TagSelect;selector.points={{1,0,0},{2,0,0}};selector.requirements={{1},{2,3},{3}};
        auto nodes=std::vector<GraphNode>{selector,clip_node(1,clips[0]),clip_node(2,clips[1])};
        auto plan=std::make_shared<const AnimationGraphPlan>(generation,10,nodes,dictionary);AnimationGraphInstance graph(plan);
        ActorTagSnapshot tags{dictionary->registry(),dictionary->generation(),{}};GraphParameters parameters;parameters.tags=&tags;
        require(graph.evaluate(parameters).layers[0].clip==clips[0].get(),"Unmatched tag selector branch");
        tags.values={2};require(graph.advance(1,parameters).layers[0].clip==clips[1].get(),"Matched tag hierarchy/all/any branch");auto saved=graph.state();
        tags.values={2,3};require(graph.advance(2,parameters).layers[0].clip==clips[0].get()&&graph.state().phase>saved.phase,"None condition returns to shared locomotion clock");
        auto terminal=graph.state();graph.restore(saved);require(graph.advance(2,parameters).layers[0].clip==clips[0].get()&&graph.state().phase==terminal.phase,"Tag selector correction replay consistency");
        parameters.tag_audience=AttributeVisibility::Public;tags.values={2};rejected([&]{graph.evaluate(parameters);});tags.values={};require(graph.evaluate(parameters).layers[0].clip==clips[0].get(),"Public graph view invented private tag state");parameters.tag_audience=AttributeVisibility::Owner;
        auto before=graph.state();tags.generation=sha256("stale registry");rejected([&]{graph.advance(3,parameters);});require(graph.state().tick==before.tick&&graph.state().phase==before.phase,"Stale tag snapshot mutated graph clock");
        {auto fields=std::vector<TagDefinition>(dictionary->definitions().begin(),dictionary->definitions().end());for(TagId id=5;id<=70;++id)fields.push_back({id,id==70?2u:0u,"Extra"+std::to_string(id),AttributeVisibility::Public});auto wide=std::make_shared<const TagDictionary>(fields,dictionary->registry(),sha256("wide tag dictionary"));auto plan=std::make_shared<const AnimationGraphPlan>(generation,10,nodes,wide);ActorTagSnapshot snapshot{wide->registry(),wide->generation(),{70}};GraphParameters input;input.tags=&snapshot;require(AnimationGraphInstance(plan).evaluate(input).layers[0].clip==clips[1].get(),"Tag hierarchy beyond the first 64 dictionary entries");snapshot.values={70,70};rejected([&]{AnimationGraphInstance(plan).evaluate(input);});}
        tags.generation=dictionary->generation();tags.values={4};rejected([&]{graph.evaluate(parameters);});tags.values={2,2};rejected([&]{graph.evaluate(parameters);});rejected([&]{graph.evaluate({});});
        nodes[0].requirements={{4},{},{}};rejected([&]{AnimationGraphPlan bad(generation,10,nodes,dictionary);});nodes[0].requirements={{2},{},{1}};rejected([&]{AnimationGraphPlan bad(generation,10,nodes,dictionary);});
        auto fields=std::vector<TagDefinition>(dictionary->definitions().begin(),dictionary->definitions().end());auto registry=dictionary->registry();*dictionary=TagDictionary(fields,registry,sha256("mutated caller dictionary"));tags={registry,generation,{2}};require(graph.evaluate(parameters).layers[0].clip==clips[1].get(),"Compiled graph borrowed mutable caller tag metadata");
        std::cout<<"Native tag selector: hierarchy/all/any/none, branch weights/shared clock, replay and stale/private/duplicate/missing snapshot rejection passed\n";
    }
    auto nodes=linear(clips);auto line=std::make_shared<const AnimationGraphPlan>(generation,10,nodes);nodes[0].points[0].x=-100;
    AnimationGraphInstance first(line),second(line);RigPose pose(rig.definition,rig.archive),oracle(rig.definition,rig.archive);
    auto low=first.evaluate({0,0,0}),high=first.evaluate({10,0,0}),middle=first.evaluate({3,0,0});
    require(low.count==1&&low.layers[0].clip==clips[0].get()&&high.count==1&&high.layers[0].clip==clips[1].get(),"1D clamps and frozen source nodes");
    require(std::abs(weight(middle,clips[0].get())-.5)<1e-6&&std::abs(weight(middle,clips[1].get())-.5)<1e-6,"1D numeric parameter interpolation");
    same_pose(pose.blend(low.span()),oracle.sample(*clips[0],0));same_pose(pose.blend(high.span()),oracle.sample(*clips[1],0));
    auto advanced=first.advance(1,{3,0,0});double expected=.5/70+.5/36;
    require(std::abs(first.state().phase-expected)<1e-12&&second.state().phase==0,"Weighted cycle rate and independent mutable graph clocks");
    for(const auto& layer:advanced.span())require(std::abs(layer.tick/layer.clip->definition().ticks-first.state().phase)<1e-12,"Different clip lengths share one normalized phase");
    AnimationGraphInstance cycle(line);for(unsigned tick=1;tick<=70;++tick)cycle.advance(tick,{1,0,0});require(cycle.state().phase==0&&cycle.evaluate({1,0,0}).layers[0].tick==0,"Exact single-clip cycle boundary wraps to zero");
    auto saved=first.state();rejected([&]{first.advance(3,{3,0,0});});rejected([&]{first.advance(2,{NAN,0,0});});require(first.state().phase==saved.phase&&first.state().tick==saved.tick,"Rejected graph tick/parameters preserve state");
    auto wrong=saved;wrong.generation=sha256("wrong");rejected([&]{first.restore(wrong);});wrong=saved;wrong.phase=NAN;rejected([&]{first.restore(wrong);});
    for(unsigned tick=2;tick<=30;++tick)first.advance(tick,{3,0,0});auto terminal=first.state();first.restore(saved);for(unsigned tick=2;tick<=30;++tick)first.advance(tick,{3,0,0});require(first.state().phase==terminal.phase,"Graph correction/replay phase equality");
    auto grid=std::make_shared<const AnimationGraphPlan>(generation,10,planar(clips));AnimationGraphInstance plane(grid);
    require(first.state().generation!=plane.state().generation,"Compiled topology contributes to graph generation");rejected([&]{plane.restore(first.state());});
    auto center=plane.evaluate({0,1.f/3,0});for(auto clip:{clips[0],clips[2],clips[3]})require(std::abs(weight(center,clip.get())-1.f/3)<1e-6,"Explicit 2D triangle barycentric weights");
    auto edge=plane.evaluate({0,.5f,.5f});require(edge.count==2&&std::abs(weight(edge,clips[0].get())-.5)<1e-6&&std::abs(weight(edge,clips[2].get())-.5)<1e-6,"Shared triangle edge continuity");
    auto outside=plane.evaluate({0,10,0});require(outside.count==1&&outside.layers[0].clip==clips[1].get(),"2D outside-domain nearest-edge clamp");
    auto left=plane.evaluate({0,0,1});same_pose(pose.blend(left.span()),oracle.sample(*clips[2],0));
    auto bad=linear(clips);bad[0].points[0].input=10;rejected([&]{AnimationGraphPlan plan(generation,10,bad);});
    bad=linear(clips);bad[0].points[0].input=99;rejected([&]{AnimationGraphPlan plan(generation,10,bad);});
    bad=linear(clips);bad[0].points[1].x=1;rejected([&]{AnimationGraphPlan plan(generation,10,bad);});
    bad=linear(clips);bad.push_back(clip_node(20,clips[2]));rejected([&]{AnimationGraphPlan plan(generation,10,bad);});
    bad=planar(clips);bad[0].triangles.push_back({{0,3,1}});rejected([&]{AnimationGraphPlan plan(generation,10,bad);});
    bad=planar(clips);bad[0].triangles[0]={{0,1,2}};rejected([&]{AnimationGraphPlan plan(generation,10,bad);});
    bad=planar(clips);bad[0].triangles[0][0]=99;rejected([&]{AnimationGraphPlan plan(generation,10,bad);});
    auto incompatible_definition=clips[0]->definition();incompatible_definition.signature=sha256("other skeleton");
    // Obtain the same verified archive solely to test incompatible metadata.
    auto cooked=load_cooked_clip(source/"omni-walk.json",assets.cas_path(),clips[0]->definition().id);
    auto incompatible=std::make_shared<const AnimationClip>(incompatible_definition,cooked.archive);bad=linear(clips);bad[1].clip=incompatible;rejected([&]{AnimationGraphPlan plan(generation,10,bad);});
    bad=linear(clips);bad[0].points[0].x=NAN;rejected([&]{AnimationGraphPlan plan(generation,10,bad);});
    bad=linear(clips);bad.resize(33);rejected([&]{AnimationGraphPlan plan(generation,10,bad);});
    bad={clip_node(1,clips[0])};for(unsigned id=2;id<=18;++id){GraphNode node;node.id=id;node.kind=GraphNodeKind::Blend1D;node.points={{id-1,0,0},{id-1,1,0}};bad.push_back(node);}rejected([&]{AnimationGraphPlan plan(generation,18,bad);});
    GraphNode excessive;excessive.id=10;excessive.kind=GraphNodeKind::Blend1D;
    bad={excessive};for(unsigned id=1;id<=5;++id){bad[0].points.push_back({id,float(id),0});bad.push_back(clip_node(id,id<=4?clips[id-1]:std::make_shared<const AnimationClip>(cooked.definition,cooked.archive)));}auto catalogue=std::make_shared<const AnimationGraphPlan>(generation,10,bad);AnimationGraphInstance selector(catalogue);require(selector.evaluate({5,0,0}).count==1,"Catalogue size does not enlarge simultaneous sampling work");
    GraphNode fan;fan.id=20;fan.kind=GraphNodeKind::Blend2D;fan.points={{1,0,0}};std::vector<GraphNode> fan_nodes;fan_nodes.push_back(clip_node(1,clips[0]));for(unsigned point=0;point<8;++point){double angle=point*6.283185307179586/8;fan.points.push_back({point+2,float(std::cos(angle)),float(std::sin(angle))});fan_nodes.push_back(clip_node(point+2,std::make_shared<const AnimationClip>(cooked.definition,cooked.archive)));fan.triangles.push_back({0,point+1,(point+1)%8+1});}fan_nodes.push_back(fan);auto fan_plan=std::make_shared<const AnimationGraphPlan>(generation,20,fan_nodes);AnimationGraphInstance directional(fan_plan);for(unsigned tick=1;tick<=120;++tick){double angle=tick*.15;auto selected=directional.advance(tick,{1,float(.8*std::sin(angle)),float(.8*std::cos(angle))});require(selected.count<=3,"Nine-point directional selector stays within three active layers");pose.blend(selected.span());}
    AnimationGraphInstance clock(line);clock.advance(1,{3,0,0,0});require(clock.state().phase==0,"Rate-zero sync freezes pose phase while simulation advances");clock.advance(2,{3,0,0,2});require(std::abs(clock.state().phase-2*expected)<1e-12,"Bounded locomotion playback rate");auto clock_saved=clock.state();rejected([&]{clock.advance(3,{3,0,0,5});});require(clock.state().tick==clock_saved.tick&&clock.state().phase==clock_saved.phase,"Invalid playback rate preserves graph state");
    GraphNode combined;combined.id=20;combined.kind=GraphNodeKind::Blend2D;combined.points={{11,0,0},{12,1,0},{13,0,1}};combined.triangles={{{0,1,2}}};bad={combined};for(unsigned pair=0;pair<3;++pair){GraphNode node;node.id=11+pair;node.kind=GraphNodeKind::Blend1D;node.points={{pair*2+1,0,0},{pair*2+2,1,0}};bad.push_back(node);for(unsigned child=1;child<=2;++child)bad.push_back(clip_node(pair*2+child,std::make_shared<const AnimationClip>(cooked.definition,cooked.archive)));}rejected([&]{AnimationGraphPlan plan(generation,20,bad);});
    // Render-only evaluations can happen at any rate. Tick inputs/clocks must
    // produce exactly the same trace, including after extra presentation reads.
    auto simulate=[&](double fps){AnimationGraphInstance instance(grid);double wall{},accumulator{};std::vector<double> trace;unsigned tick{};
        while(tick<180){double dt=fps?1/fps:1./60;wall+=dt;accumulator+=dt;while(accumulator+1e-12>=1./60&&tick<180){accumulator-=1./60;++tick;auto result=instance.advance(tick,{0,float(.5+.25*std::sin(tick*.1)),float(.25*std::cos(tick*.1))});trace.push_back(instance.state().phase);require(result.count<=4,"Compiled pose work remains bounded");for(const auto& matrix:pose.blend(result.span()))for(auto value:matrix.values)require(std::isfinite(value),"Native graph composition finite palette");}if(fps)instance.evaluate({0,.5f,.25f});}
        return trace;};
    auto baseline=simulate(0);for(auto fps:{30.,60.,144.})require(simulate(fps)==baseline,"Graph tick traces differ with rendering rate");
    std::cout<<"Native typed graph compile, 1D/2D clamping, isolated sync clocks, correction/replay and 30/60/144/headless tick traces passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
