#include <darkangel/primitive_assets.hpp>
#include <darkangel/cue_assets.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>bool refused(F call){try{call();}catch(const std::exception&){return true;}return false;}
struct Recorder final:CueSink {
    unsigned starts{},stops{},releases{};
    void start(const CueStart&)override{++starts;}
    void stop(std::uint64_t,unsigned)override{++stops;}
    void release(std::uint64_t)override{++releases;}
};
}
int main(){try{
    const auto trail_id=AssetId::random(),decal_id=AssetId::random();
    const Json trail_source={{"schema",1},{"kind","trail"},{"asset",trail_id.text()},{"name","Swing"},{"socket","weapon_right"},{"base",{0.0,0.0,0.1}},{"tip",{0.0,0.0,0.9}},{"lifetime_ticks",10},{"max_points",6},{"color",{1.0,0.9,0.6,0.8}}};
    const Json decal_source={{"schema",1},{"kind","decal"},{"asset",decal_id.text()},{"name","Scuff"},{"size",1.2},{"lifetime_ticks",100},{"fade_ticks",20},{"color",{0.1,0.1,0.1,0.5}}};
    const auto trail=decode_trail(trail_source.dump());const auto decal=decode_decal(decal_source.dump());
    check(trail.id==trail_id&&trail.socket=="weapon_right"&&trail.max_points==6&&trail.lifetime_ticks==10&&std::abs(trail.tip[2]-.9f)<1e-6f&&decal.id==decal_id&&std::abs(decal.size-1.2f)<1e-6f&&decal.fade_ticks==20,"A decoded trail or decal lost a field");
    {const auto bad_trail=[&](auto change){auto value=trail_source;change(value);return refused([&]{decode_trail(value.dump());});};const auto bad_decal=[&](auto change){auto value=decal_source;change(value);return refused([&]{decode_decal(value.dump());});};
        check(bad_trail([](Json& v){v["tip"]=v["base"];})&&bad_trail([](Json& v){v["lifetime_ticks"]=0;})&&bad_trail([](Json& v){v["max_points"]=1;})&&bad_trail([](Json& v){v["color"]={1.0,1.0,2.0,1.0};})&&bad_trail([](Json& v){v["socket"]="";})&&bad_trail([](Json& v){v["extra"]=1;})&&bad_trail([](Json& v){v["kind"]="decal";}),"An invalid trail was accepted");
        check(bad_decal([](Json& v){v["size"]=0.0;})&&bad_decal([](Json& v){v["fade_ticks"]=200;})&&bad_decal([](Json& v){v["lifetime_ticks"]=4000;})&&bad_decal([](Json& v){v.erase("color");}),"An invalid decal was accepted");}
    // A trail: one pair a tick while it emits, a strip between consecutive pairs, fading by age, bounded in points.
    {WeaponTrails trails(2);check(trails.begin(1,7,trail)&&trails.begin(1,7,trail)&&trails.live()==1&&trails.emitting().size()==1&&trails.emitting()[0].network==7,"A trail did not begin once");
        for(std::uint64_t tick=0;tick<4;++tick)trails.sample(1,tick,{float(tick)*.2f,1,0},{float(tick)*.2f,2,0});trails.sample(1,3,{9,9,9},{9,9,9});check(trails.points()==4,"A trail did not hold one pair a tick");
        std::vector<PrimitiveVertex> built;trails.build(3,built);check(built.size()==3*4,"Four pairs did not build three quads");
        check(built[1].color[3]>built[0].color[3]&&built[11].color[3]>built[1].color[3]&&std::abs(built[11].color[3]-.8f)<1e-5f,"A trail does not fade from its tip to its base and from new to old");
        // A jump breaks the strip; the piece before it stays and nothing spans the gap.
        trails.sample(1,4,{50,1,0},{50,2,0});trails.sample(1,5,{50.2f,1,0},{50.2f,2,0});built.clear();trails.build(5,built);check(trails.breaks()==1&&trails.points()==6&&built.size()==4*4,"A teleport did not break the strip");
        for(const auto& vertex:built)check(vertex.position[0]<1||vertex.position[0]>49,"A quad spans the break");
        // The point cap drops the oldest pair; lifetime drops what has aged out; an ended trail goes when it has faded.
        trails.sample(1,6,{50.4f,1,0},{50.4f,2,0});check(trails.points()==6,"A trail grew past its point cap");trails.advance(12);check(trails.points()==4,"Aged pairs were kept");
        trails.end(1);check(trails.emitting().empty()&&trails.live()==1,"An ended trail stopped being drawn at once");trails.sample(1,13,{0,0,0},{0,1,0});check(trails.points()==4,"An ended trail took a sample");trails.advance(40);check(trails.live()==0,"A faded trail was kept");
        // Bounds, and an actor leaving.
        check(trails.begin(2,7,trail)&&trails.begin(3,8,trail)&&!trails.begin(4,8,trail)&&trails.dropped()==1,"The trail set is not bounded");trails.retire(8);check(trails.live()==1,"A leaving actor kept its trail");trails.end(2);check(trails.live()==0,"A trail that never drew anything was kept");}
    // Decals: a soft disc on the surface, fading at the end of its life, in a pool that replaces its oldest.
    {DecalPool pool(2);pool.spawn(decal,{1,0,2},{0,1,0},10);std::vector<PrimitiveVertex> built;pool.build(10,built);check(built.size()==12*4,"A decal is not twelve slices");
        for(std::size_t index=0;index<built.size();index+=4){check(std::abs(built[index].color[3]-.5f)<1e-5f&&built[index+2].color[3]==0&&built[index+3].color[3]==0,"A decal is not opaque in the middle and clear at its rim");
            for(std::size_t corner=0;corner<4;++corner)check(std::abs(built[index+corner].position[1]-.02f)<1e-5f,"A decal on a floor is not flat on it");
            const auto dx=built[index+2].position[0]-1,dz=built[index+2].position[2]-2;check(std::abs(std::sqrt(dx*dx+dz*dz)-.6f)<1e-4f,"A decal's rim is not at half its size");}
        built.clear();pool.build(100,built);check(std::abs(built[0].color[3]-.25f)<1e-5f,"A decal does not fade at the end of its life");pool.advance(110);check(pool.live()==0,"An expired decal was kept");
        // On a wall the disc stands in the wall's plane.
        pool.spawn(decal,{0,1,0},{1,0,0},0);built.clear();pool.build(0,built);for(const auto& vertex:built)check(std::abs(vertex.position[0]-.02f)<1e-5f,"A decal on a wall is not in the wall's plane");
        pool.spawn(decal,{0,0,0},{0,1,0},1);pool.spawn(decal,{5,0,0},{0,1,0},2);check(pool.live()==2&&pool.replaced()==1,"The decal pool is not bounded");}
    // Assets: a trail and a decal are native sources; a cue may name them, and its cooked product names them by identity.
    {const auto workspace=std::filesystem::path(DAE_BINARY_DIR)/("primitive-"+AssetId::random().text().substr(0,12)),mount=workspace/"s";std::filesystem::create_directories(mount);AssetService assets(mount,workspace/"c");
        check(assets.create_native("swing.datrail",trail_source.dump(2)+"\n").root==trail_id&&assets.create_native("scuff.dadecal",decal_source.dump(2)+"\n").root==decal_id,"Trail and decal creation");
        const auto cue_id=AssetId::random();const Json cue={{"schema",1},{"kind","cue"},{"asset",cue_id.text()},{"name","Swing"},{"key","Attack.Start"},{"visual",""},{"audio",""},{"trail","swing.datrail"},{"decal","scuff.dadecal"},{"tail_ticks",24},{"predictable",true}};
        assets.create_native("swing.dacue",cue.dump(2)+"\n");const std::array<AssetId,2> more{trail_id,decal_id};assets.package(cue_id,workspace/"registry.json",more);
        const auto cues=load_cooked_cues(workspace/"registry.json",assets.cas_path());check(cues.size()==1&&cues[0].trail_asset==trail_id&&cues[0].decal_asset==decal_id&&cues[0].visible()&&cues[0].visual_asset==AssetId{},"A cooked cue did not resolve its trail and decal");
        const auto trails=load_cooked_trails(workspace/"registry.json",assets.cas_path());const auto decals=load_cooked_decals(workspace/"registry.json",assets.cas_path());check(trails.size()==1&&trails[0].id==trail_id&&trails[0].socket=="weapon_right"&&decals.size()==1&&decals[0].id==decal_id,"A package did not give back its trail and decal");
        auto missing=cue;missing["asset"]=AssetId::random().text();missing["key"]="Other";missing["trail"]="none.datrail";check(refused([&]{assets.create_native("missing.dacue",missing.dump(2)+"\n");}),"A cue naming a trail that is not in the project cooked");
        auto wrong=cue;wrong["asset"]=AssetId::random().text();wrong["key"]="Other";wrong["trail"]="scuff.dadecal";check(refused([&]{assets.create_native("wrong.dacue",wrong.dump(2)+"\n");}),"A cue naming a decal as its trail cooked");
        auto broken=trail_source;broken["max_points"]=500;check(refused([&]{assets.edit_native("swing.datrail",sha256(trail_source.dump(2)+"\n"),broken.dump(2)+"\n");}),"An invalid trail edit was accepted");
        // The cue player sends a cue that only has a trail to the visual side.
        Recorder visual,audio;CuePlayer player([&](std::string_view,AssetId)->std::optional<CueDefinition>{return cues[0];},&visual,&audio);
        player.moment(3,1,"Attack.Start",1,5);check(visual.starts==1&&audio.starts==0&&player.live()==1,"A cue with only a trail and a decal did not go to the visual side alone");
        player.advance(5+24+1);check(visual.releases==1&&player.live()==0,"A trail cue was not released when its tail was over");}
    std::cout<<"Presentation primitives: trail and decal sources and refusals, trail sampling, fading, breaks, caps and retirement, decal discs, fading and pool bounds, cues naming a trail and a decal passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
