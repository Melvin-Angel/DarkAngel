#include <darkangel/persistence.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace darkangel {
namespace {
void require(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
constexpr unsigned container_version=1;
// One connection, closed on every path.
struct Database {
    sqlite3* db{};
    Database(const std::filesystem::path& path,bool create){
        const int flags=create?SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX:SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX;
        if(sqlite3_open_v2(path.string().c_str(),&db,flags,nullptr)!=SQLITE_OK){const std::string error=db?sqlite3_errmsg(db):"open failed";sqlite3_close(db);db=nullptr;throw std::runtime_error("Save slot could not be opened: "+error);}
        sqlite3_busy_timeout(db,2000);
    }
    ~Database(){sqlite3_close(db);}
    Database(const Database&)=delete;
    void exec(const char* sql){char* error{};if(sqlite3_exec(db,sql,nullptr,nullptr,&error)!=SQLITE_OK){std::string text=error?error:"";sqlite3_free(error);throw std::runtime_error(std::string("Save slot statement failed: ")+text);}}
};
// Fixed prepared statements only.
struct Statement {
    sqlite3_stmt* s{};Statement(sqlite3* db,const char* sql){require(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)==SQLITE_OK,std::string("Save slot statement: ")+sqlite3_errmsg(db));}
    ~Statement(){sqlite3_finalize(s);}Statement(const Statement&)=delete;
    Statement& bind(int i,std::int64_t v){require(sqlite3_bind_int64(s,i,v)==SQLITE_OK,"Save slot bind");return *this;}
    Statement& bind(int i,const std::string& v){require(sqlite3_bind_text(s,i,v.data(),int(v.size()),SQLITE_TRANSIENT)==SQLITE_OK,"Save slot bind");return *this;}
    bool row(){const int r=sqlite3_step(s);require(r==SQLITE_ROW||r==SQLITE_DONE,"Save slot step");return r==SQLITE_ROW;}
    void done(){require(!row(),"Save slot statement returned rows");}
    std::int64_t integer(int c){return sqlite3_column_int64(s,c);}
    std::string text(int c){auto p=reinterpret_cast<const char*>(sqlite3_column_text(s,c));return p?std::string(p,std::size_t(sqlite3_column_bytes(s,c))):std::string();}
};
void schema(Database& d,const std::string& name){
    d.exec("PRAGMA journal_mode=DELETE;PRAGMA synchronous=EXTRA;");
    d.exec("BEGIN IMMEDIATE;CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
           "CREATE TABLE IF NOT EXISTS snapshots(revision INTEGER PRIMARY KEY,created TEXT NOT NULL,reason TEXT NOT NULL,digest TEXT NOT NULL,payload TEXT NOT NULL);"
           "CREATE TABLE IF NOT EXISTS head(id INTEGER PRIMARY KEY CHECK(id=1),revision INTEGER NOT NULL);"
           "CREATE TABLE IF NOT EXISTS operations(operation INTEGER PRIMARY KEY,revision INTEGER NOT NULL);");
    {Statement count(d.db,"SELECT COUNT(*) FROM meta");count.row();if(count.integer(0)==0){
        Statement put(d.db,"INSERT INTO meta(key,value) VALUES(?,?)");
        auto slot=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());put.bind(1,std::string("container")).bind(2,std::to_string(container_version)).done();
        Statement a(d.db,"INSERT INTO meta(key,value) VALUES('slot',?)");a.bind(1,slot).done();Statement b(d.db,"INSERT INTO meta(key,value) VALUES('name',?)");b.bind(1,name).done();
        d.exec("INSERT INTO head(id,revision) VALUES(1,0);");}}
    d.exec("COMMIT;");
    Statement version(d.db,"SELECT value FROM meta WHERE key='container'");require(version.row(),"Save slot has no container version");require(std::stoul(version.text(0))<=container_version,"This save slot was written by a newer version and is refused");
}
std::uint64_t head_of(Database& d){Statement h(d.db,"SELECT revision FROM head WHERE id=1");require(h.row(),"Save slot has no head");return std::uint64_t(h.integer(0));}
std::string now(){const auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm utc{};gmtime_s(&utc,&t);char text[32]{};std::strftime(text,sizeof(text),"%Y-%m-%dT%H:%M:%SZ",&utc);return text;}
std::optional<SaveSnapshot> snapshot(Database& d,std::uint64_t revision){Statement q(d.db,"SELECT revision,created,reason,digest,payload FROM snapshots WHERE revision=?");q.bind(1,std::int64_t(revision));if(!q.row())return std::nullopt;SaveSnapshot s{std::uint64_t(q.integer(0)),q.text(2),q.text(1),q.text(3),q.text(4)};s.valid=sha256(s.payload)==s.digest;return s;}
}
const char* save_outcome_text(SaveOutcome o){switch(o){case SaveOutcome::Captured:return "Captured";case SaveOutcome::Writing:return "Writing";case SaveOutcome::Committed:return "Committed";default:return "Failed";}}

struct PersistenceService::Impl {
    std::filesystem::path path;std::string name;mutable std::mutex mutex;std::condition_variable wake,idle;std::deque<SaveRequest> queue;std::map<std::uint64_t,SaveReceipt> receipts;
    std::uint64_t head{};bool stop{},busy{},fail_next{};std::thread worker;
    Impl(std::filesystem::path p,std::string n):path(std::move(p)),name(std::move(n)){Database d(path,true);schema(d,name);head=head_of(d);worker=std::thread([this]{run();});}
    ~Impl(){{std::lock_guard lock(mutex);stop=true;}wake.notify_all();worker.join();}
    void set(const SaveReceipt& r){std::lock_guard lock(mutex);receipts[r.operation]=r;while(receipts.size()>256)receipts.erase(receipts.begin());}
    void run(){
        // The only connection that writes this slot.
        std::unique_ptr<Database> d;try{d=std::make_unique<Database>(path,true);}catch(const std::exception&){}
        for(;;){SaveRequest request;bool inject{};{std::unique_lock lock(mutex);wake.wait(lock,[&]{return stop||!queue.empty();});if(queue.empty()){if(stop)return;continue;}request=std::move(queue.front());queue.pop_front();busy=true;inject=fail_next;fail_next=false;}
            set({request.operation,0,SaveOutcome::Writing,{}});SaveReceipt result{request.operation,0,SaveOutcome::Failed,{}};
            try{require(d!=nullptr,"Save slot unavailable");result=write(*d,request,inject);}catch(const std::exception& error){try{d->exec("ROLLBACK;");}catch(...){}result.outcome=SaveOutcome::Failed;result.error=error.what();}
            set(result);{std::lock_guard lock(mutex);if(result.outcome==SaveOutcome::Committed)head=std::max(head,result.revision);busy=false;}idle.notify_all();}
    }
    SaveReceipt write(Database& d,const SaveRequest& request,bool inject){
        d.exec("BEGIN IMMEDIATE;");
        {Statement seen(d.db,"SELECT revision FROM operations WHERE operation=?");seen.bind(1,std::int64_t(request.operation));if(seen.row()){const auto revision=std::uint64_t(seen.integer(0));d.exec("ROLLBACK;");return {request.operation,revision,SaveOutcome::Committed,{}};}}
        const auto current=head_of(d);if(current!=request.expected_revision){d.exec("ROLLBACK;");return {request.operation,current,SaveOutcome::Failed,"The save slot moved on (revision "+std::to_string(current)+"); capture again"};}
        const auto next=current+1;
        {Statement put(d.db,"INSERT INTO snapshots(revision,created,reason,digest,payload) VALUES(?,?,?,?,?)");put.bind(1,std::int64_t(next)).bind(2,now()).bind(3,request.reason).bind(4,sha256(request.payload)).bind(5,request.payload).done();}
        if(inject)throw std::runtime_error("Injected save write failure");
        {Statement move(d.db,"UPDATE head SET revision=? WHERE id=1");move.bind(1,std::int64_t(next)).done();}
        {Statement op(d.db,"INSERT INTO operations(operation,revision) VALUES(?,?)");op.bind(1,std::int64_t(request.operation)).bind(2,std::int64_t(next)).done();}
        // The head and the one before it are kept; older snapshots go.
        {Statement prune(d.db,"DELETE FROM snapshots WHERE revision<?");prune.bind(1,std::int64_t(next>1?next-1:next)).done();}
        d.exec("DELETE FROM operations WHERE operation NOT IN (SELECT operation FROM operations ORDER BY operation DESC LIMIT 256);");
        d.exec("COMMIT;");return {request.operation,next,SaveOutcome::Committed,{}};
    }
};
PersistenceService::PersistenceService(std::filesystem::path slot,std::string name):impl_(std::make_unique<Impl>(std::move(slot),std::move(name))){}
PersistenceService::~PersistenceService()=default;
SaveReceipt PersistenceService::submit(SaveRequest request){
    require(request.operation!=0,"A save request has an operation identity");require(request.payload.size()<=64*1024*1024&&request.reason.size()<=128,"Save request bounds");
    SaveReceipt receipt{request.operation,0,SaveOutcome::Captured,{}};{std::lock_guard lock(impl_->mutex);if(auto known=impl_->receipts.find(request.operation);known!=impl_->receipts.end())return known->second;require(impl_->queue.size()<8,"Save queue is full");impl_->receipts[request.operation]=receipt;impl_->queue.push_back(std::move(request));}
    impl_->wake.notify_all();return receipt;
}
std::optional<SaveReceipt> PersistenceService::receipt(std::uint64_t operation)const{std::lock_guard lock(impl_->mutex);auto found=impl_->receipts.find(operation);if(found==impl_->receipts.end())return std::nullopt;return found->second;}
void PersistenceService::flush(){std::unique_lock lock(impl_->mutex);impl_->idle.wait(lock,[&]{return impl_->queue.empty()&&!impl_->busy;});}
std::uint64_t PersistenceService::revision()const{std::lock_guard lock(impl_->mutex);return impl_->head;}
void PersistenceService::fail_next_write(){std::lock_guard lock(impl_->mutex);impl_->fail_next=true;}
const std::filesystem::path& PersistenceService::path()const{return impl_->path;}

std::string migrate_save(std::string payload,std::span<const SaveMigration> migrations,unsigned version){
    auto value=nlohmann::json::parse(payload);require(value.is_object(),"Save payload is not a record");unsigned at=value.value("version",1u);require(at<=version,"This save was written by a newer version and is refused");
    while(at<version){auto step=std::find_if(migrations.begin(),migrations.end(),[&](const auto& m){return m.version==at+1;});require(step!=migrations.end(),"No migration from save version "+std::to_string(at));
        payload=step->step(payload);value=nlohmann::json::parse(payload);require(value.value("version",0u)==at+1,"A save migration did not reach its version");++at;}
    return payload;
}
std::optional<LoadedSave> load_save(const std::filesystem::path& slot,std::span<const SaveMigration> migrations,unsigned version){
    if(!std::filesystem::exists(slot))return std::nullopt;Database d(slot,false);{Statement v(d.db,"SELECT value FROM meta WHERE key='container'");require(v.row(),"Save slot has no container version");require(std::stoul(v.text(0))<=container_version,"This save slot was written by a newer version and is refused");}
    const auto head=head_of(d);if(!head)return std::nullopt;LoadedSave result;
    auto chosen=snapshot(d,head);
    // A damaged head falls back to the snapshot before it; the damaged one stays in the file for inspection.
    if(!chosen||!chosen->valid){Statement prev(d.db,"SELECT MAX(revision) FROM snapshots WHERE revision<?");prev.bind(1,std::int64_t(head));std::optional<SaveSnapshot> fallback;if(prev.row()&&sqlite3_column_type(prev.s,0)!=SQLITE_NULL)fallback=snapshot(d,std::uint64_t(prev.integer(0)));
        require(fallback&&fallback->valid,"The save is damaged and has no intact earlier snapshot");result.recovered=true;result.warning="The latest save (revision "+std::to_string(head)+") is damaged; loaded revision "+std::to_string(fallback->revision)+" instead";chosen=fallback;}
    result.snapshot=*chosen;result.snapshot.payload=migrate_save(result.snapshot.payload,migrations,version);return result;
}
SaveSlotInfo inspect_save(const std::filesystem::path& slot){
    SaveSlotInfo info;if(!std::filesystem::exists(slot))return info;Database d(slot,false);
    {Statement m(d.db,"SELECT key,value FROM meta");while(m.row()){const auto key=m.text(0);if(key=="slot")info.slot=m.text(1);else if(key=="name")info.name=m.text(1);else if(key=="container")info.container=unsigned(std::stoul(m.text(1)));}}
    info.head=head_of(d);Statement all(d.db,"SELECT revision FROM snapshots ORDER BY revision DESC");std::vector<std::uint64_t> revisions;while(all.row())revisions.push_back(std::uint64_t(all.integer(0)));for(auto r:revisions)if(auto s=snapshot(d,r))info.snapshots.push_back(std::move(*s));
    return info;
}
void backup_save(const std::filesystem::path& slot,const std::filesystem::path& copy){
    Database from(slot,false);Database to(copy,true);auto* backup=sqlite3_backup_init(to.db,"main",from.db,"main");require(backup!=nullptr,"Save backup could not start");
    const int step=sqlite3_backup_step(backup,-1);sqlite3_backup_finish(backup);require(step==SQLITE_DONE,"Save backup failed");
}
}
