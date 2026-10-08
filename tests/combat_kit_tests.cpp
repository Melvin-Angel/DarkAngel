#include <darkangel/combat_kit.hpp>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected validation failure");}
int main(){try{
 InputProfile input;input.id=AssetId::random();
 for(unsigned i=0;i<combat_slot_count;++i)input.actions.push_back({i+1,"combat."+std::to_string(i),InputActionKind::Button,200000,200000,.15f});
 auto kit=std::make_shared<CombatKitDefinition>();kit->id=AssetId::random();kit->locomotion_stance=AssetId::random();kit->generation="kit-v1";
 for(unsigned i=0;i<combat_slot_count;++i)kit->slots[i]={static_cast<CombatSlot>(i),i+1,{}};
 auto ability=AssetId::random();kit->slots[0].ability=ability;kit->slots[1].ability=ability;
 CombatKitInstance player(kit,input),npc(kit,input);
 kit->slots[0].ability={};check(player.definition().slots[0].ability==ability,"Mutable source cannot change pinned kit");
 std::array<InputEvent,4> events{{{1,InputEdge::Pressed,1,0,1,false},{1,InputEdge::Hold,200001,200000,1,false},{1,InputEdge::Released,210001,210000,0,false},{1,InputEdge::Tapped,210001,210000,0,false}}};
 auto routed=player.route(1,events);check(routed.size()==4&&routed[1].event.edge==InputEdge::Hold&&routed[3].event.edge==InputEdge::Tapped,"Ability receives all input semantics unchanged");
 check(npc.route(1,events).size()==4,"Player and NPC share routing");
 InputEvent unassigned{8,InputEdge::Pressed,1,0,1,false};check(player.route(1,{&unassigned,1}).empty(),"Optional slot assignment");
 auto replacement=std::make_shared<CombatKitDefinition>(*kit);replacement->slots[0].ability=AssetId::random();replacement->generation="kit-v2";
 auto invalid=std::make_shared<CombatKitDefinition>(*replacement);invalid->slots[0].input_action=2;
 rejects([&]{player.replace(invalid,input);});check(player.grant_generation()==1&&player.definition().generation=="kit-v1","Failed replacement preserves outgoing kit");
 auto retired=player.replace(replacement,input);check(retired.removed.size()==1&&retired.removed[0]==ability&&retired.grant_generation==2,"Retire outgoing grants once");
 check(player.route(1,events).empty(),"Old queued input epoch ignored");
 InputEvent hold{1,InputEdge::Hold,300000,300000,1,false};check(player.route(2,{&hold,1}).empty(),"Held input cannot enter new kit");
 InputEvent release{1,InputEdge::Released,310000,310000,0,false};check(player.route(2,{&release,1}).empty(),"Outgoing release cannot activate new kit");
 InputEvent tap{1,InputEdge::Tapped,310000,310000,0,false};check(player.route(2,{&tap,1}).empty(),"Outgoing tap cannot activate new kit");
 auto new_input=player.route(2,events);check(new_input.size()==4&&new_input[0].ability==replacement->slots[0].ability&&new_input[0].grant_generation==2,"Fresh press routes to replacement ability");
 check(npc.grant_generation()==1,"Independent owner grants");
 invalid=std::make_shared<CombatKitDefinition>(*replacement);invalid->locomotion_stance={};rejects([&]{CombatKitInstance bad(invalid,input);});
 invalid=std::make_shared<CombatKitDefinition>(*replacement);invalid->slots[0].slot=CombatSlot::Block;rejects([&]{CombatKitInstance bad(invalid,input);});
 auto axis=input;axis.actions[0].kind=InputActionKind::Axis;rejects([&]{CombatKitInstance bad(replacement,axis);});
 std::cout<<"Combat kit optional slots, input semantics, pinning and transactional swap checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
