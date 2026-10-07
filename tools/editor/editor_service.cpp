#include <darkangel/editor_service.hpp>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>
namespace darkangel {
namespace {
void require(bool test,const char* error) {if(!test)throw std::runtime_error(error);}
double number(const ObjectData& d,TypeId type,PropertyId property) {
    const void* ptr=type==1?static_cast<const void*>(&d.transform):type==2?static_cast<const void*>(&d.health):nullptr;
    require(ptr!=nullptr,"Type has no editable numeric properties");
    for(const auto& t:metadata())if(t.id==type)for(const auto& p:t.properties)if(p.id==property) {
        require(p.kind==ValueKind::Number && (p.flags&Editable) && !(p.flags&ReadOnly),"Property not editable");double value;
        std::memcpy(&value,static_cast<const char*>(ptr)+p.offset,sizeof(value));return value;
    }
    throw std::runtime_error("Unknown property ID");
}
}
struct EditorService::Impl {
    World& world;std::size_t limit;std::uint64_t revision{},next{};
    std::string baseline;
    struct Transaction {std::vector<PropertyChange> before,after;std::uint64_t revision;};
    std::map<std::uint64_t,Transaction> prepared;
    std::vector<Transaction> undo,redo;
    Impl(World& w,std::size_t l):world(w),limit(l),baseline(w.serialize()) {require(w.domain()==WorldDomain::Authoring && l>0,"Editor requires authoring world and positive history limit");}
    void check(std::uint64_t expected) const {
        require(expected==revision,"Stale editor revision");require(world.serialize()==baseline,"World changed outside EditorService");
    }
    std::vector<NumberEdit> resolve(std::span<const PropertyChange> changes) const {
        std::vector<NumberEdit> edits;edits.reserve(changes.size());
        for(const auto& change:changes) {auto h=world.find(change.object);require(world.valid(h),"Missing edit object");edits.push_back({h,change.type,change.property,change.value});}
        return edits;
    }
    void apply(std::span<const PropertyChange> changes) {
        World validation(WorldDomain::Authoring,1024,1024);validation.load(baseline);
        std::vector<NumberEdit> candidate;for(const auto& c:changes)candidate.push_back({validation.find(c.object),c.type,c.property,c.value});
        validation.apply_edits(candidate,Authority::Authoring);auto serialized=validation.serialize();
        auto edits=resolve(changes);world.apply_edits(edits,Authority::Authoring);baseline=std::move(serialized);++revision;prepared.clear();
    }
};
EditorService::EditorService(World& w,std::size_t limit):impl_(std::make_unique<Impl>(w,limit)){}
EditorService::~EditorService()=default;
std::uint64_t EditorService::revision() const {impl_->world.domain();return impl_->revision;}
PreparedEdit EditorService::prepare(std::uint64_t revision,std::span<const PropertyChange> changes) {
    auto& p=*impl_;p.check(revision);require(!changes.empty() && changes.size()<=1024 && p.prepared.size()<p.limit,"Empty or oversized transaction/prepare queue");
    Impl::Transaction transaction;transaction.revision=revision;transaction.after.assign(changes.begin(),changes.end());
    for(const auto& change:changes)transaction.before.push_back({change.object,change.type,change.property,number(p.world.read(p.world.find(change.object)),change.type,change.property)});
    // Reuse the canonical schema/serializer in an isolated validation world.
    World validation(WorldDomain::Authoring,1024,1024);validation.load(p.baseline);
    std::vector<NumberEdit> edits;for(const auto& c:changes)edits.push_back({validation.find(c.object),c.type,c.property,c.value});
    validation.apply_edits(edits,Authority::Authoring);
    auto token=++p.next;p.prepared.emplace(token,std::move(transaction));return {token,revision};
}
void EditorService::commit(PreparedEdit prepared) {
    auto& p=*impl_;p.check(prepared.revision);auto it=p.prepared.find(prepared.token);require(it!=p.prepared.end() && it->second.revision==prepared.revision,"Unknown prepare token");
    auto transaction=it->second;p.apply(transaction.after);p.undo.push_back(std::move(transaction));if(p.undo.size()>p.limit)p.undo.erase(p.undo.begin());p.redo.clear();
}
void EditorService::discard(PreparedEdit prepared) {impl_->check(prepared.revision);require(impl_->prepared.erase(prepared.token)==1,"Unknown prepare token");}
void EditorService::undo(std::uint64_t revision) {
    auto& p=*impl_;p.check(revision);require(!p.undo.empty(),"Nothing to undo");auto t=p.undo.back();p.apply(t.before);p.undo.pop_back();p.redo.push_back(std::move(t));
}
void EditorService::redo(std::uint64_t revision) {
    auto& p=*impl_;p.check(revision);require(!p.redo.empty(),"Nothing to redo");auto t=p.redo.back();p.apply(t.after);p.redo.pop_back();p.undo.push_back(std::move(t));
}
}
