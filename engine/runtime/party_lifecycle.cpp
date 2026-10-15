#include <darkangel/check.hpp>
#include <darkangel/party_lifecycle.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {using darkangel::require;}
void validate_party_rules(const PartyRules& r){
    require(std::isfinite(r.revive_range)&&r.revive_range>=.5&&r.revive_range<=10,"Revive range is 0.5 to 10 m");
    require(std::isfinite(r.revive_seconds)&&r.revive_seconds>=.1&&r.revive_seconds<=30,"Revive time is 0.1 to 30 s");
    require(std::isfinite(r.revive_health)&&r.revive_health>0&&r.revive_health<=1,"Revive Health is a share above 0, at most 1");
    require(std::isfinite(r.wipe_seconds)&&r.wipe_seconds>=0&&r.wipe_seconds<=30,"Wipe time is 0 to 30 s");
    require(std::isfinite(r.hold_grace)&&r.hold_grace>=0&&r.hold_grace<=2,"Revive hold grace is 0 to 2 s");
}
PartyLifecycleCoordinator::PartyLifecycleCoordinator(PartyRules rules):rules_(rules){validate_party_rules(rules_);}
void PartyLifecycleCoordinator::hold(std::uint64_t reviver,std::uint64_t target){if(reviver&&target&&reviver!=target)requests_[reviver]=target;}
void PartyLifecycleCoordinator::forget(std::uint64_t network){members_.erase(network);requests_.erase(network);for(auto& [id,state]:members_)state.holders.erase(network);}
PartyLifecycleCoordinator::Outcome PartyLifecycleCoordinator::step(std::span<const Member> party,double seconds){
    require(std::isfinite(seconds)&&seconds>0&&seconds<=1,"Party lifecycle step");Outcome outcome;
    auto requests=std::move(requests_);requests_.clear();
    if(wiped_){wipe_elapsed_+=seconds;return outcome;}
    for(const auto& member:party)members_.try_emplace(member.network);
    std::erase_if(members_,[&](const auto& entry){return std::none_of(party.begin(),party.end(),[&](const auto& m){return m.network==entry.first;});});
    auto find=[&](std::uint64_t id)->const Member*{auto found=std::find_if(party.begin(),party.end(),[&](const auto& m){return m.network==id;});return found==party.end()?nullptr:&*found;};
    // Downed first, so a reviver struck down this tick cannot revive.
    for(const auto& member:party){auto& state=members_.at(member.network);if(state.state==PartyMemberState::Alive&&member.health<=0){state=State{PartyMemberState::Downed};outcome.downed.push_back(member.network);}}
    // Revive progress: each downed member advances while at least one valid reviver holds; any tick without one resets it.
    for(auto& [id,state]:members_){if(state.state!=PartyMemberState::Downed)continue;const auto* target=find(id);
        for(auto& [reviver,last]:state.holders)last+=seconds;
        for(const auto& [reviver,wanted]:requests)if(wanted==id)state.holders[reviver]=0;
        std::erase_if(state.holders,[&](const auto& h){const auto* who=find(h.first);if(!who||!target||!who->connected||!target->connected)return true;
            if(members_.at(h.first).state!=PartyMemberState::Alive||who->health<=0)return true;
            const double dx=who->position.x-target->position.x,dy=who->position.y-target->position.y,dz=who->position.z-target->position.z;
            return dx*dx+dy*dy+dz*dz>rules_.revive_range*rules_.revive_range||h.second>rules_.hold_grace;});
        if(state.holders.empty()){state.progress=0;continue;}
        state.progress+=seconds/rules_.revive_seconds;if(state.progress>=1){state=State{PartyMemberState::Alive};outcome.revived.push_back(id);}}
    // A wipe: every connected member downed. Nobody connected is not a wipe (the session is ending anyway).
    bool any=false,standing=false;for(const auto& member:party)if(member.connected){any=true;if(members_.at(member.network).state==PartyMemberState::Alive)standing=true;}
    if(any&&!standing){wiped_=true;wipe_elapsed_=0;outcome.wipe=true;}
    return outcome;
}
PartyMemberState PartyLifecycleCoordinator::state(std::uint64_t network)const{auto found=members_.find(network);return found==members_.end()?PartyMemberState::Alive:found->second.state;}
double PartyLifecycleCoordinator::progress(std::uint64_t network)const{auto found=members_.find(network);return found==members_.end()?0:std::min(1.,found->second.progress);}
std::vector<WorldPartyMember> PartyLifecycleCoordinator::view()const{std::vector<WorldPartyMember> result;for(const auto& [id,state]:members_)result.push_back({id,state.state,std::min(1.,state.progress)});return result;}
}
