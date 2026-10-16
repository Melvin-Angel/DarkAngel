#include "../engine/runtime/scene_world_objects.hpp"
#include <darkangel/interaction.hpp>
#include <iostream>
using namespace darkangel;
namespace {void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}template<class F>void rejects(F f){bool r=false;try{f();}catch(const std::exception&){r=true;}check(r,"Expected refusal");}}
int main(){try{
 const auto id=AssetId::random();auto d=decode_interactable(std::string("{\"prompt\":\"Runes\",\"event\":\"rune\",\"once\":false,\"puzzle\":{\"document\":\"")+id.text()+"\",\"sequence\":[1,3,2]}}");
 check(d.puzzle&&d.puzzle->sequence==std::vector<unsigned>{1,3,2},"Puzzle decode");
 rejects([]{decode_interactable("{\"prompt\":\"Runes\",\"event\":\"rune\",\"puzzle\":{\"document\":\"18d6f3be-0fb6-456b-a4b2-d543a8e13600\",\"sequence\":[1]}}");});
 WorldPuzzleState s{{1,2}};check(!apply_puzzle_press({1,3,2},s,1)&&s.progress==1,"First press");apply_puzzle_press({1,3,2},s,2);check(s.progress==0,"Wrong press resets");apply_puzzle_press({1,3,2},s,1);apply_puzzle_press({1,3,2},s,3);check(apply_puzzle_press({1,3,2},s,2)&&s.solved,"Sequence solves");check(!apply_puzzle_press({1,3,2},s,1),"Solved state immutable");
 WorldStateView v;v.puzzles.push_back(s);check(decode_world_state(encode_world_state(v))==v,"Puzzle state wire round trip");WorldRequest r{WorldRequestKind::Puzzle,{1,2},{},3};check(decode_world_request(encode_world_request(r))==r,"Puzzle request wire round trip");r.value=0;rejects([&]{decode_world_request(encode_world_request(r));});
 auto combat=std::make_shared<CharacterSceneCombat>();combat->scene=id;combat->interactables.push_back({{1,2},d,{}});PhysicsWorld physics;WorldLedger ledger;
 scene_detail::SceneWorldObjects host(combat,physics,&ledger,nullptr);std::optional<SceneTransitionRequest> travel;scene_detail::SceneWorldObjects::Who who{1,{2,1},{},true,true};r.value=1;
 check(host.request(who,r,travel)==InteractionResult::Unavailable,"Press requires accepted interaction");host.accepted_puzzle(who.network,r.altar);auto far=who;far.position.x=100;check(host.request(far,r,travel)==InteractionResult::OutOfReach,"Out of reach refused");check(host.request(who,r,travel)==InteractionResult::Unavailable,"Leaving cancels open authorization");host.accepted_puzzle(who.network,r.altar);auto dead=who;dead.living=false;check(host.request(dead,r,travel)==InteractionResult::Dead,"Dead press refused");host.accepted_puzzle(who.network,r.altar);auto guest=who;guest.network=2;check(host.request(guest,r,travel)==InteractionResult::Unavailable,"Another member cannot borrow authorization");
 for(unsigned press:{1u,3u,2u}){r.value=press;check(host.request(who,r,travel)==InteractionResult::Accepted,"Host accepts sequence");}check(puzzle_solved(ledger,id,r.altar),"Solved state persisted");WorldStateView view;host.view(view);check(view.puzzles.size()==1&&view.puzzles[0].solved,"Confirmed state projected");check(host.request(who,r,travel)==InteractionResult::Unavailable,"Solved press refused");
 struct InteractableCapture final:PersistentObject {std::string_view kind()const override{return "interactable";}void capture(LedgerWriter&)const override{}void restore(const LedgerReader&)override{}} capture;
 ledger.capture({id,r.altar},capture);check(puzzle_solved(ledger,id,r.altar),"Capturing the interactable cannot erase its puzzle record");
 const std::array<LedgerKind,1> known{LedgerKind{"puzzle",1}};auto restored=WorldLedger::deserialize(ledger.serialize(),known);check(puzzle_solved(restored,id,r.altar),"Puzzle save round trip");
 std::cout<<"Puzzle authority, wire and ledger tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
