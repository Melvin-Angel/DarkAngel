#include <darkangel/editor_service.hpp>
#include <darkangel/editor_document.hpp>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
void check(bool test,const char* error) {if(!test)throw std::runtime_error(error);}
template<class F>void rejects(F&& f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Expected rejection");}
int main() {
    try {
        World world(WorldDomain::Authoring);ObjectData data;data.id={1,1};auto h=world.create(data);EditorService editor(world,2);
        PropertyChange changes[]={{data.id,2,1,50},{data.id,2,2,25}};
        auto prepared=editor.prepare(0,changes);check(world.read(h).health.maximum==100,"Prepare mutated live world");
        editor.commit(prepared);check(editor.revision()==1 && world.read(h).health.maximum==50 && world.read(h).health.current==25,"Commit failed");
        rejects([&]{editor.commit(prepared);});rejects([&]{editor.undo(0);});
        editor.undo(1);check(world.read(h).health.current==100 && editor.revision()==2,"Undo failed");
        editor.redo(2);check(world.read(h).health.current==25 && editor.revision()==3,"Redo failed");
        PropertyChange bad[]={{data.id,1,1,12},{data.id,2,2,99}};
        rejects([&]{editor.prepare(3,bad);});check(world.read(h).transform.yaw==0 && editor.revision()==3,"Failed prepare mutated world");
        auto token=editor.prepare(3,changes);editor.discard(token);rejects([&]{editor.commit(token);});
        world.edit_number(h,1,1,2,Authority::Authoring);rejects([&]{editor.undo(3);});
        const auto root=AssetId::parse("11111111-1111-4111-8111-111111111111");
        const std::string source=R"({"schema":1,"asset":"11111111-1111-4111-8111-111111111111","entities":{"00000000000000000000000000000001":{"types":{"1":{"version":1,"fields":{"1":0}},"2":{"version":1,"fields":{"1":100,"2":100}}},"optional":{"future_label":"preserve"}}}})";
        EditorDocument doc(root,{1,1},{{root,source}});auto object=doc.plan().origins[0].object;PropertyChange edit{object,1,1,5};
        auto pending=doc.prepare(0,{&edit,1});check(!doc.dirty() && doc.world().read(doc.world().find(object)).transform.yaw==0,"Document preparation wrote live source");doc.commit(pending);check(doc.dirty() && doc.revision()==1,"Document commit/dirty failed");
        auto no_op=doc.prepare(1,{&edit,1});doc.commit(no_op);check(doc.revision()==1,"No-op created a history revision");doc.undo(1);check(!doc.dirty(),"Undo failed to restore source fingerprint");doc.redo(2);
        auto file=std::filesystem::path(DAE_BINARY_DIR)/"document-tests"/"scene.dascene";doc.save(file);auto reopened=EditorDocument::open(file);check(!doc.dirty() && reopened->plan().serialize()==doc.plan().serialize(),"Partitioned scene save/open differs from native result");check(reopened->world().serialize().find("preserve")!=std::string::npos,"Optional authoring data lost");
        rejects([&]{doc.commit(pending);});auto cancelled=doc.prepare(doc.revision(),{&edit,1});doc.discard(cancelled);rejects([&]{doc.commit(cancelled);});
        auto malformed=source;malformed.replace(malformed.find("\"1\":100"),7,"\"1\":-1");rejects([&]{doc.prepare_source(doc.revision(),root,malformed);});
        std::cout<<"M2 transactional property editing passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
