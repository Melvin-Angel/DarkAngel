#include <darkangel/persistence.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::filesystem::path fresh(const char* name){auto path=std::filesystem::temp_directory_path()/("darkangel-persistence-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+name+".dasave");std::filesystem::remove(path);return path;}
std::string payload(int value,unsigned version=1){return nlohmann::json{{"version",version},{"value",value}}.dump();}
SaveReceipt commit(PersistenceService& s,std::uint64_t op,std::uint64_t expected,const std::string& body){s.submit({op,expected,"test",body});s.flush();return *s.receipt(op);}
void corrupt_head(const std::filesystem::path& path){sqlite3* db{};check(sqlite3_open(path.string().c_str(),&db)==SQLITE_OK,"open for damage");check(sqlite3_exec(db,"UPDATE snapshots SET payload='{\"version\":1,\"value\":-1}' WHERE revision=(SELECT revision FROM head WHERE id=1)",nullptr,nullptr,nullptr)==SQLITE_OK,"damage");sqlite3_close(db);}
}
int main(){try{
    // Commit, head and previous, digest; reading needs no service.
    {const auto path=fresh("commit");check(!load_save(path),"A missing slot loaded something");
        {PersistenceService s(path);check(commit(s,1,0,payload(1)).outcome==SaveOutcome::Committed&&s.revision()==1,"First commit");check(commit(s,2,1,payload(2)).revision==2,"Second commit");check(commit(s,3,2,payload(3)).revision==3,"Third commit");
            // A stale revision is refused and changes nothing; a repeated operation is answered once with its revision.
            const auto stale=commit(s,4,1,payload(9));check(stale.outcome==SaveOutcome::Failed&&s.revision()==3,"A stale revision was written");
            const auto again=commit(s,3,3,payload(3));check(again.outcome==SaveOutcome::Committed&&again.revision==3&&s.revision()==3,"A repeated operation wrote twice");}
        const auto info=inspect_save(path);check(info.head==3&&info.snapshots.size()==2&&info.snapshots[0].revision==3&&info.snapshots[1].revision==2&&info.snapshots[0].valid,"Head and previous are kept, older ones pruned");
        const auto loaded=load_save(path);check(loaded&&!loaded->recovered&&nlohmann::json::parse(loaded->snapshot.payload)["value"]==3,"Head loads");
        // A reopened slot continues from its head.
        {PersistenceService s(path);check(s.revision()==3&&commit(s,5,3,payload(4)).revision==4,"Reopened slot continues");}
        // A damaged head recovers to the previous snapshot and says so; the damaged one stays for inspection.
        corrupt_head(path);const auto recovered=load_save(path);check(recovered&&recovered->recovered&&nlohmann::json::parse(recovered->snapshot.payload)["value"]==3&&!recovered->warning.empty(),"Damaged head did not recover to the previous snapshot");
        check(!inspect_save(path).snapshots[0].valid,"The damaged snapshot was not reported");
        // Backup through SQLite's backup API.
        auto copy=path;copy+=".backup";backup_save(path,copy);check(inspect_save(copy).head==4,"Backup copy");std::filesystem::remove(copy);std::filesystem::remove(path);}
    // An injected failure inside the transaction leaves the previous head; the next write succeeds.
    {const auto path=fresh("failure");{PersistenceService s(path);commit(s,1,0,payload(1));s.fail_next_write();const auto failed=commit(s,2,1,payload(2));check(failed.outcome==SaveOutcome::Failed&&!failed.error.empty()&&s.revision()==1,"An injected failure moved the head");
            check(nlohmann::json::parse(load_save(path)->snapshot.payload)["value"]==1&&inspect_save(path).snapshots.size()==1,"The failed snapshot row was kept");check(commit(s,3,1,payload(3)).revision==2,"Writing after a failure");}
        std::filesystem::remove(path);}
    // Migration v1 to v2 on load; a failing step leaves the stored slot untouched; a newer save is refused.
    {const auto path=fresh("migration");{PersistenceService s(path);commit(s,1,0,payload(7,1));}
        const SaveMigration ok{2,[](const std::string& text){auto v=nlohmann::json::parse(text);v["version"]=2;v["value"]=v["value"].get<int>()*10;return v.dump();}};
        const SaveMigration broken{2,[](const std::string&)->std::string{throw std::runtime_error("Injected migration failure");}};
        const auto migrated=load_save(path,std::span(&ok,1),2);check(migrated&&nlohmann::json::parse(migrated->snapshot.payload)["value"]==70,"Migration v1 to v2");
        bool refused=false;try{load_save(path,std::span(&broken,1),2);}catch(const std::exception&){refused=true;}check(refused&&nlohmann::json::parse(load_save(path)->snapshot.payload)["value"]==7,"A failed migration changed the slot");
        bool newer=false;try{migrate_save(payload(1,3),{},2);}catch(const std::exception&){newer=true;}check(newer,"A newer save was accepted");
        std::filesystem::remove(path);}
    std::cout<<"Persistence tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
