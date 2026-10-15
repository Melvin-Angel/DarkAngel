// Incremental scene document edits (SCENE_SCALE_PLAN.md phase 8): for random edit sequences, an EditorDocument that edits one
// entity record at a time has the plan, world and saved output of a document that resolves the whole scene again.
#include <darkangel/editor_document.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void check(bool test,const std::string& error){if(!test)throw std::runtime_error(error);}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
const auto scene=AssetId::parse("22222222-2222-4222-8222-222222222222"),hut=AssetId::parse("11111111-1111-4111-8111-111111111111");
const auto model=AssetId::parse("44444444-4444-4444-8444-444444444444"),npc=AssetId::parse("55555555-5555-4555-8555-555555555555");
const auto item=AssetId::parse("66666666-6666-4666-8666-666666666666"),script=AssetId::parse("33333333-3333-4333-8333-333333333333");
const StableId placement{0,7};
const std::string mount=StableId{0,900}.text(),door=StableId{0,901}.text(),exported=StableId{0,902}.text();
Json types(double x,bool legacy=false){if(legacy)return {{"1",{{"version",1},{"fields",{{"1",x/10}}}}},{"2",{{"version",1},{"fields",{{"1",100},{"2",100}}}}}};
    return {{"1",{{"version",2},{"fields",{{"1",0.0},{"2",x},{"3",0.0},{"4",0.0},{"5",0.0},{"6",0.0},{"7",1.0}}}}},{"2",{{"version",1},{"fields",{{"1",100},{"2",100}}}}}};}
// A mounted assembly with one exported door, so root entities can address a mount's export.
std::string hut_source(){return Json{{"schema",1},{"asset",hut.text()},{"entities",{{door,{{"name","Door"},{"types",types(1)}}}}},{"exports",{{exported,door}}}}.dump();}
struct Random {
    std::mt19937 engine;
    std::size_t below(std::size_t count){return std::uniform_int_distribution<std::size_t>(0,count-1)(engine);}
    double between(double low,double high){return std::uniform_real_distribution<double>(low,high)(engine);}
};
// A random entity of one of the kinds the editor places, sometimes addressing another entity of the scene (or one that is missing).
Json entity(Random& random,const std::vector<std::string>& locals,std::size_t index){
    auto other=[&]{return locals.empty()||random.below(8)==0?StableId{0,777}.text():locals[random.below(locals.size())];};
    Json made={{"name","Object "+std::to_string(index)},{"types",types(random.between(-50,50),random.below(10)==0)}};
    switch(random.below(10)){
        case 0:case 1:made["model"]=model.text();break;
        case 2:made["target"]=other();break;
        case 3:made["scripts"]={{StableId{0,3}.text(),{{"asset",script.text()},{"config",{{"speed",1}}},{"entity_refs",{{"follow",other()}}}}}};break;
        case 4:made["spawn"]={{"npc",npc.text()}};if(random.below(2))made["spawn"]["encounter"]=other();break;
        case 5:made["encounter"]={{"trigger",{{"enter",true}}},{"mode","waves"},{"waves",Json::array({Json{{"npc",npc.text()},{"count",2u}}})},{"placement","random"}};break;
        case 6:made["volume"]={{"encounter",other()},{"half",Json::array({5.0,2.0,5.0})}};break;
        case 7:made["pickup"]={{"item",item.text()},{"quantity",1}};made["model"]=model.text();break;
        case 8:made["target"]={{"mount",mount},{"export",exported}};break;
        default:made["optional"]={{"label","keep "+std::to_string(index)}};break;
    }
    return made;
}
std::vector<std::string> locals(const EditorDocument& document){std::vector<std::string> found;for(const auto& [local,value]:Json::parse(document.source(scene)).at("entities").items())found.push_back(local);return found;}
AssemblySources sources(const EditorDocument& document){return {{scene,document.source(scene)},{hut,document.source(hut)}};}
// The incremental document against a full resolve of the same sources: plan, fingerprint, scene and authoring world.
void equivalent(EditorDocument& document,const std::string& step){
    auto full=resolve_assembly(scene,placement,sources(document));
    check(document.plan().serialize()==full.serialize(),step+": plan differs from a full resolve");
    check(document.plan().fingerprint==full.fingerprint,step+": plan fingerprint differs from a full resolve");
    check(document.plan().scene_json==full.scene_json,step+": scene differs from a full resolve");
    check(document.world().serialize()==full.scene_json,step+": authoring world differs from a full resolve");
    EditorDocument rebuilt(scene,placement,sources(document));check(rebuilt.plan().serialize()==full.serialize() && rebuilt.world().serialize()==full.scene_json,step+": rebuilt document differs");
}
// Whether the scene resolves with these root entities changed; the document must accept exactly the edits a full resolve accepts.
bool resolves(const EditorDocument& document,const std::map<std::string,std::optional<std::string>>& records){
    auto graph=Json::parse(document.source(scene));for(const auto& [local,record]:records){if(record)graph["entities"][local]=Json::parse(*record);else graph["entities"].erase(local);}
    try{resolve_assembly(scene,placement,{{scene,graph.dump()},{hut,document.source(hut)}});return true;}catch(const std::exception&){return false;}
}
template<class F> bool attempt(F&& edit){try{edit();return true;}catch(const std::exception&){return false;}}
std::string object_local(const EditorDocument& document,StableId object){for(const auto& origin:document.plan().origins)if(origin.object==object)return origin.local_path.substr(origin.local_path.rfind('/')+1);throw std::runtime_error("No such object");}
}
// --bench N: a scene of N model placements, and the time of one move and one placement through the document (not a CTest).
int bench(std::size_t count){
    Json root={{"schema",1},{"asset",scene.text()},{"entities",Json::object()}};for(std::size_t index=1;index<=count;++index)root["entities"][StableId{0,index}.text()]={{"name","Hut "+std::to_string(index)},{"model",model.text()},{"types",types(double(index%100))}};
    auto start=std::chrono::steady_clock::now();EditorDocument document(scene,placement,{{scene,root.dump()}});auto ms=[&]{auto now=std::chrono::steady_clock::now();auto value=std::chrono::duration<double,std::milli>(now-start).count();start=now;return value;};
    std::cout<<count<<" placements: open "<<ms()<<" ms\n";
    for(unsigned round=0;round<5;++round){
        PropertyChange change{document.plan().origins[round].object,1,2,double(round)};ms();document.commit(document.prepare(document.revision(),{&change,1}));auto move=ms();
        auto local=StableId{0,count+1+round}.text();Json made={{"name","New"},{"model",model.text()},{"types",types(3)}};document.commit(document.prepare_entities(document.revision(),{{local,made.dump()}}));auto place=ms();
        document.dirty();auto dirty=ms();document.undo(document.revision());auto undo=ms();document.redo(document.revision());auto redo=ms();
        std::cout<<"  move "<<move<<" ms, place "<<place<<" ms, dirty "<<dirty<<" ms, undo "<<undo<<" ms, redo "<<redo<<" ms\n";
    }
    return 0;
}
int main(int argc,char** argv){try{
    if(argc==3&&std::string(argv[1])=="--bench")return bench(std::stoul(argv[2]));
    const auto workspace=std::filesystem::path(DAE_BINARY_DIR)/"document-incremental-tests";std::filesystem::remove_all(workspace);std::filesystem::create_directories(workspace);
    for(unsigned seed=1;seed<=6;++seed){
        Random random{std::mt19937(seed)};Json root={{"schema",1},{"asset",scene.text()},{"entities",Json::object()},{"mounts",{{mount,{{"asset",hut.text()},{"patches",Json::array()}}}}}};
        // Entities join one at a time while the scene still resolves, so it starts valid.
        std::vector<std::string> made;for(std::size_t index=1;index<=24;++index){auto local=StableId{0,index}.text();auto candidate=root;candidate["entities"][local]=entity(random,made,index);
            if(attempt([&]{resolve_assembly(scene,placement,{{scene,candidate.dump()},{hut,hut_source()}});})){root=std::move(candidate);made.push_back(local);}}
        check(made.size()>=8,"Too few starting entities");
        root["exports"]={{StableId{0,800}.text(),root["entities"].begin().key()}};
        EditorDocument document(scene,placement,{{scene,root.dump()},{hut,hut_source()}});equivalent(document,"seed "+std::to_string(seed)+" open");
        std::size_t next=100,accepted=0,rejected=0;
        for(unsigned step=0;step<160;++step){const auto label="seed "+std::to_string(seed)+" step "+std::to_string(step);auto present=locals(document);const auto revision=document.revision();
            const auto choice=random.below(11);
            auto records_edit=[&](std::map<std::string,std::optional<std::string>> records){const bool expected=resolves(document,records);
                const bool done=attempt([&]{document.commit(document.prepare_entities(document.revision(),records));});check(done==expected,label+": record edit "+(expected?"rejected":"accepted")+" unlike a full resolve");
                if(!done)check(document.revision()==revision,label+": rejected edit changed the document");(done?accepted:rejected)++;};
            if(choice<=2){auto local=StableId{0,++next}.text();records_edit({{local,entity(random,present,next).dump()}});}
            else if(choice==3&&!present.empty())records_edit({{present[random.below(present.size())],std::nullopt}});
            else if(choice==4&&!present.empty())records_edit({{present[random.below(present.size())],entity(random,present,++next).dump()}});
            else if(choice==5&&present.size()>=2){std::map<std::string,std::optional<std::string>> batch;batch[StableId{0,++next}.text()]=entity(random,present,next).dump();batch[present[random.below(present.size())]]=std::nullopt;batch[present[random.below(present.size())]]=entity(random,present,++next).dump();records_edit(batch);}
            else if(choice==6&&!present.empty()){
                // Moves through the property path: placement scope on the scene's own object, and definition scope.
                const auto& origins=document.plan().origins;StableId object;for(const auto& origin:origins)if(origin.definition==scene&&origin.local_path.find('/',placement.text().size()+1)==std::string::npos&&random.below(2)){object=origin.object;break;}
                if(object){PropertyChange change{object,1,2,random.between(-50,50)};document.commit(document.prepare(revision,{&change,1},random.below(2)?EditScope::Placement:EditScope::Definition));accepted++;}}
            else if(choice==7){
                // A mounted object moves through a mount patch: the whole scene is resolved again.
                StableId object;for(const auto& origin:document.plan().origins)if(origin.definition==hut)object=origin.object;PropertyChange change{object,1,2,random.between(-50,50)};document.commit(document.prepare(revision,{&change,1}));accepted++;}
            else if(choice==8&&!present.empty()){
                // A whole new root source: entities only becomes record edits, a header change resolves everything.
                auto graph=Json::parse(document.source(scene));auto& entities=graph["entities"];entities[present[random.below(present.size())]]["name"]="Renamed "+std::to_string(step);
                if(random.below(3)==0)graph["exports"][StableId{0,801}.text()]=present[random.below(present.size())];
                document.commit(document.prepare_source(revision,scene,graph.dump()));accepted++;}
            else if(choice==9&&!present.empty()){
                // An unchanged record is a no-op: no revision, no history.
                const auto local=present[random.below(present.size())];document.commit(document.prepare_entities(revision,{{local,Json::parse(*document.entity(local)).dump()}}));check(document.revision()==revision,label+": no-op edit made a revision");}
            else{if(random.below(2)&&attempt([&]{document.undo(revision);})){}else attempt([&]{document.redo(revision);});}
            equivalent(document,label);
        }
        check(accepted>40&&rejected>0,"seed "+std::to_string(seed)+": the sequence exercised too few accepted or rejected edits");
        // Undo all the way back to the opened document, and redo again: every step stays equal to a full resolve.
        while(attempt([&]{document.undo(document.revision());}))equivalent(document,"seed "+std::to_string(seed)+" undo");
        while(attempt([&]{document.redo(document.revision());}))equivalent(document,"seed "+std::to_string(seed)+" redo");
        // Save output equals that of a document opened from the same sources; reopening gives the same plan.
        const auto mine=workspace/("incremental-"+std::to_string(seed))/"scene.dascene",theirs=workspace/("full-"+std::to_string(seed))/"scene.dascene";
        document.save(mine);check(!document.dirty(),"Saved document is dirty");EditorDocument full(scene,placement,sources(document));full.save(theirs);
        check(read(mine)==read(theirs),"seed "+std::to_string(seed)+": saved manifest differs from a full document's");
        for(const auto& file:std::filesystem::directory_iterator(theirs.parent_path()/"scene.dascene.records"))check(read(mine.parent_path()/"scene.dascene.records"/file.path().filename())==read(file.path()),"Saved record differs");
        auto reopened=EditorDocument::open(mine);check(reopened->plan().serialize()==document.plan().serialize() && !reopened->dirty(),"Reopened document differs");
        // An edit and its undo leave the document clean; a world changed outside the document is rejected.
        if(auto present=locals(document);!present.empty()){const auto local=present.front();auto record=Json::parse(*document.entity(local));record["name"]="Clean check";document.commit(document.prepare_entities(document.revision(),{{local,record.dump()}}));check(document.dirty(),"Edit left the document clean");document.undo(document.revision());check(!document.dirty(),"Undo did not restore the saved document");}
        auto& world=document.world();const auto first=world.find(document.plan().origins.front().object);world.edit_number(first,1,2,3,Authority::Authoring);
        check(!attempt([&]{document.prepare_entities(document.revision(),{{StableId{0,5000}.text(),entity(random,{},5000).dump()}});}),"Document accepted an edit after its world changed outside it");
        std::cout<<"seed "<<seed<<": "<<accepted<<" accepted, "<<rejected<<" rejected edits equal a full resolve\n";
    }
    // Stale tokens and revisions keep their contract.
    {Json root={{"schema",1},{"asset",scene.text()},{"entities",{{StableId{0,1}.text(),{{"name","One"},{"types",types(0)}}}}}};EditorDocument document(scene,placement,{{scene,root.dump()}});
        auto record=Json::parse(*document.entity(StableId{0,1}.text()));record["name"]="Two";auto first=document.prepare_entities(0,{{StableId{0,1}.text(),record.dump()}});auto second=document.prepare_entities(0,{{StableId{0,2}.text(),record.dump()}});
        document.commit(first);check(!attempt([&]{document.commit(second);}),"Stale token committed");check(!attempt([&]{document.prepare_entities(0,{{StableId{0,3}.text(),record.dump()}});}),"Stale revision accepted");
        check(!attempt([&]{document.prepare_entities(1,{{StableId{0,9}.text(),std::nullopt}});}),"Removing a missing entity accepted");
        check(document.plan().origins.size()==1&&document.plan().origins[0].name=="Two","Record edit did not reach the plan");}
    std::cout<<"Incremental scene document edits equal a full resolve\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
