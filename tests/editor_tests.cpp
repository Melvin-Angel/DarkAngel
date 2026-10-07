#include <darkangel/editor_service.hpp>
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
        std::cout<<"M2 transactional property editing passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
