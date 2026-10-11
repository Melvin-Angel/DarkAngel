#pragma once
#include <cstdint>
#include <deque>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace darkangel {
// Session-level events and tags. Server-only: they decide server behaviour and are not
// replicated. An event is a name emitted at a tick; events emitted during tick T are
// delivered at tick T+1 in the order they were emitted. A tag is a name in a set: state,
// where an event is a moment. Names are lowercase letters, digits, '_' and '.', up to 48
// characters.
constexpr std::size_t world_event_limit=64;
bool world_name_valid(std::string_view);
struct WorldEventRecord {std::uint64_t tick{};std::string name;};
class WorldEvents {
public:
    // Queued for delivery on the next tick. More than world_event_limit in one tick is an error.
    void emit(std::string name);
    // Starts a tick: what was queued becomes what is delivered.
    void begin_tick(std::uint64_t tick);
    std::span<const std::string> delivered()const{return delivered_;}
    bool happened(std::string_view name)const;
    void set_tag(const std::string& name,bool on);
    bool tag(std::string_view name)const{return tags_.find(name)!=tags_.end();}
    const std::set<std::string,std::less<>>& tags()const{return tags_;}
    // The latest delivered events, oldest first, for debug views.
    const std::deque<WorldEventRecord>& log()const{return log_;}
private:std::vector<std::string> queued_,delivered_;std::set<std::string,std::less<>> tags_;std::deque<WorldEventRecord> log_;
};
}
