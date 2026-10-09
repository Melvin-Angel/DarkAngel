#include <darkangel/world_session.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <numeric>
namespace {std::atomic<bool> count_new{};std::atomic<std::uint64_t> new_calls{};}
void* operator new(std::size_t bytes){if(auto* p=std::malloc(bytes?bytes:1)){if(count_new.load(std::memory_order_relaxed))new_calls.fetch_add(1,std::memory_order_relaxed);return p;}throw std::bad_alloc();}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using namespace darkangel;
namespace {void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Expected global periodic capacity rejection");}}
int main(){try{
 WorldSession server(SessionRole::Server,{3,std::string(64,'a'),std::string(64,'b'),77});std::vector<AttributeDefinition> schema{{1,"MaxHealth",AttributeKind::Statistic,100,0,1000},{2,"Health",AttributeKind::Resource,100,0,1000,1}};std::vector<AbilityOwnerHandle> owners;for(unsigned i=0;i<4;++i){ObjectData object;object.id={19,i+1};owners.push_back(server.configure_abilities(server.create(object),schema,2,1));}
 server.register_effect_evaluator(1,[](const EffectContext&){return std::vector<ResourceDelta>{{2,0}};});EffectDefinition d;d.id=AssetId::random();d.generation=std::string(64,'e');d.lifetime=EffectLifetime::UntilRemoved;d.period_ticks=60;d.evaluator=1;std::vector<EffectHandle> handles;
 for(auto owner:owners)for(unsigned i=0;i<32;++i)handles.push_back(server.apply_effect(owner,owners[0],d,0));auto before=server.ability_snapshot(owners[3]);rejects([&]{server.apply_effect(owners[3],owners[0],d,0);});check(server.ability_effects(owners[3]).size()==32&&server.ability_snapshot(owners[3]).revision==before.revision,"Periodic capacity failure cannot publish an effect or consume identity");
 std::vector<double> timings;timings.reserve(240);std::size_t results{},peak{};new_calls=0;count_new=true;
 for(unsigned tick=1;tick<=240;++tick){auto start=std::chrono::steady_clock::now();server.advance_abilities(tick);auto output=server.drain_effect_outcomes();auto stop=std::chrono::steady_clock::now();timings.push_back(std::chrono::duration<double,std::milli>(stop-start).count());results+=output.size();peak=std::max(peak,output.size());server.drain_ability_actions();}
 count_new=false;auto allocations=new_calls.load();check(results==512&&peak==128,"All reserved due periodic work executes without silent drop");for(auto owner:owners)check(server.ability_effects(owner).size()==32&&server.ability_snapshot(owner).tick==240&&server.world().read(owner.entity).health.current==100,"Bounded periodic workload retains coherent owner state");auto mean=std::accumulate(timings.begin(),timings.end(),0.)/timings.size();std::sort(timings.begin(),timings.end());
 std::cout<<"Native effect workload actors=4 active=128 ticks=240 outcomes="<<results<<" peak_due="<<peak<<" mean_ms="<<mean<<" p99_ms="<<timings[237]<<" max_ms="<<timings.back()<<" ordinary_cpp_new_calls="<<allocations<<'\n';
 server.remove_effect(owners[0],handles[0]);auto refreshed=d;refreshed.id=AssetId::random();refreshed.stacking=EffectStack::RefreshPerSource;auto accepted=server.apply_effect(owners[3],owners[0],refreshed,0);check(accepted.value,"Explicit removal releases a global periodic reservation");auto stable=server.apply_effect(owners[3],owners[0],refreshed,0);check(stable==accepted&&server.ability_effects(owners[3]).size()==33,"Per-source refresh keeps existing reserved periodic capacity");
 std::cout<<"Global periodic reservation, atomic overflow/removal/refresh and bounded native timing/allocation evidence passed\n";return 0;
}catch(const std::exception& e){count_new=false;std::cerr<<e.what()<<'\n';return 1;}}
