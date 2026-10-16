#if defined(DAE_CHARACTER_SCENE) && defined(DAE_HUD)
#include "ui_play_exercise.hpp"
#include "../editor_ui.hpp"
#include "../play_controller.hpp"
#include "../character_preview.hpp"
#include <darkangel/character_scene.hpp>
#include <darkangel/scene_client.hpp>
#include <darkangel/ui_service.hpp>
#include <darkangel/check.hpp>
#include <iostream>
namespace darkangel::editor_app::exercises {
namespace {
class UiPlayExercise final:public Exercise {
 bool enabled,finished{};Controller& editor;Shell& shell;PlayController& play;AssetId hud,marker,panel;StableId puzzle;unsigned stage{},wait{};
 void steps(unsigned n=3){for(unsigned i=0;i<n;++i)play.character->step({});}
public:
 UiPlayExercise(bool e,Controller& c,Shell& s,PlayController& p):enabled(e),editor(c),shell(s),play(p){}
 void frame(unsigned frame)override{
  if(!enabled||finished||frame<2)return;
  if(stage==0){
   hud=editor.authoring.create_ui_document(*editor.assets,"GateStamina","hud");marker=editor.authoring.create_ui_document(*editor.assets,"GateMarker","marker");panel=editor.authoring.create_ui_document(*editor.assets,"GatePuzzle","puzzle");
   AssetId player,npc;
   for(const auto& a:editor.asset_inventory){if(a.path=="players/royal_player.daplayer")player=a.id;if(a.path=="npcs/royal_guard.danpc")npc=a.id;}
   require(player!=AssetId{}&&npc!=AssetId{},"UI Play fixture actors missing");
   editor.authoring.open(*editor.assets,player).value["ui"]={{"hud",{{{"document",hud.text()},{"source","ui/hud/GateStamina.daui"}}}}};
   editor.authoring.open(*editor.assets,npc).value["name"]="UI Marker Guard";
   editor.authoring.open(*editor.assets,npc).value["ui"]={{"markers",{{{"document",marker.text()},{"source","ui/markers/GateMarker.daui"}}}}};
   editor.authoring_player=player;editor.authoring.record_changes(*editor.assets,"Attach UI documents");editor.save_gameplay();editor.refresh_assets();
   StableId player_object;for(const auto& origin:editor.document->plan().origins)if(origin.local_path.ends_with("/"+StableId{0,4}.text())){require(!player_object,"Ambiguous UI gate player");player_object=origin.object;}require(bool(player_object),"UI gate player missing");
   auto at=editor.document->world().read(editor.document->world().find(player_object)).transform;
   auto entity=[&](nlohmann::json extra,double x,double z){extra["target"]=nullptr;extra["types"]={{"1",{{"version",2},{"fields",{{"1",0},{"2",x},{"3",at.y},{"4",z},{"5",0},{"6",0},{"7",1}}}}},{"2",{{"version",1},{"fields",{{"1",100},{"2",100}}}}}};return extra.dump();};
   const auto local=StableId{991,1}.text(),spawn=StableId{991,2}.text();
   auto prepared=editor.document->prepare_entities(editor.document->revision(),{{local,entity({{"interactable",{{"prompt","Use runes"},{"event","gate.runes"},{"once",false},{"radius",4},{"puzzle",{{"document",panel.text()},{"sequence",{1,2,3}}}}}}},at.x,at.z+1)},{spawn,entity({{"spawn",{{"npc",npc.text()}}}},at.x+2,at.z+3)}});editor.document->commit(prepared);
   for(const auto& origin:editor.document->plan().origins)if(origin.local_path.ends_with("/"+local))puzzle=origin.object;require(bool(puzzle),"UI Play puzzle placed");
   editor.save(editor.path);
   shell.select_workspace(Workspace::Game);require(bool(shell.open_frontend),"Front-end harness unavailable");shell.open_frontend();require(!play.character&&!editor.preview.active(),"Front end created a session");++stage;return;
  }
  if(++wait<4)return;wait=0;
  if(stage==1){auto* front=play.frontend_service();require(front&&front->modal_open(),"Sessionless menu missing");front->key(UiKey::Confirm);++stage;return;}
  if(stage==2){require(play.character&&editor.preview.active(),"Front end Start did not enter Play");auto* ui=play.authored_ui();require(ui&&ui->screens()>=1&&ui->anchors()>=1,"Saved HUD layer/NPC marker missing in fresh Play");unsigned visible=0;bool live_hud=false;ui->render([&](auto& view,float,float,float,float){if(view.text("nameplate-name")=="UI Marker Guard")++visible;auto text=view.text("stamina-text");if(!text.empty()&&text!="40 / 100")live_hud=true;});require(visible&&live_hud,"Assigned marker/HUD not visibly bound to live models");editor.paused=true;steps();CharacterSceneInput input;input.interact=puzzle;play.character->step(input);steps();++stage;return;}
  if(stage==3){auto* ui=play.authored_ui();require(ui&&ui->modal_open()&&shell.inventory_open,"Host-accepted puzzle did not open modal or block gameplay");ui->key(UiKey::Confirm);++stage;return;}
  if(stage==4){steps();auto* state=play.character->client(play.character_player)->world_state();require(state&&!state->puzzles.empty()&&state->puzzles.front().progress==1,"UI press did not reach host");auto* ui=play.authored_ui();ui->key(UiKey::Down);ui->key(UiKey::Confirm);++stage;return;}
  if(stage==5){steps();auto* ui=play.authored_ui();ui->key(UiKey::Down);ui->key(UiKey::Confirm);++stage;return;}
  if(stage==6){steps();const auto* state=play.character->client(play.character_player)->world_state();require(state&&!state->puzzles.empty()&&state->puzzles.front().solved,"Puzzle UI lacks host-confirmed solved state");play.owner_script.steps().push_back({"capture"});++stage;return;}
  if(stage==7){play.authored_ui()->key(UiKey::Cancel);++stage;return;}
  if(stage==8){require(!play.authored_ui()->modal_open(),"Cancel did not close puzzle");const auto scene=play.character->client(play.character_player)->combat()->scene;require(scene!=AssetId{},"Runtime puzzle scene identity missing");editor.stop();require(puzzle_solved(play.play_ledger,scene,puzzle),"Stop erased solved puzzle persistence");require(!play.authored_ui(),"Stop retained scoped UI");finished=true;std::cout<<"Editor ui-play hud=1 marker=1 modal=1 host_puzzle=1 frontend=1 cleanup=1\n";}
 }
 void finish()override{require(!enabled||finished,"UI Play gate ended before acceptance");}
 bool done()const override{return !enabled||finished;}std::string_view result()const override{return {};}
};
}
std::unique_ptr<Exercise> make_ui_play(bool enabled,Controller& c,Shell& s,PlayController& p){return std::make_unique<UiPlayExercise>(enabled,c,s,p);}
}

#else
#include "ui_play_exercise.hpp"
#include <stdexcept>
namespace darkangel::editor_app::exercises {
namespace {class Unavailable final:public Exercise {bool enabled;public:explicit Unavailable(bool v):enabled(v){}void frame(unsigned)override{if(enabled)throw std::runtime_error("UI Play requires character scene and HUD support");}void finish()override{}bool done()const override{return !enabled;}std::string_view result()const override{return {};}};}
std::unique_ptr<Exercise> make_ui_play(bool enabled,Controller&,Shell&,PlayController&){return std::make_unique<Unavailable>(enabled);}
}
#endif
