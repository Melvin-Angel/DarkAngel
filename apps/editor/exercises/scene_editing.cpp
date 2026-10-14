#include "scene_editing.hpp"
#include "../editor_options.hpp"
#include "../editor_ui.hpp"
#include <darkangel/check.hpp>
#include <darkangel/scene_travel.hpp>
#include <DirectXMath.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace darkangel::editor_app::exercises {
namespace {
class SceneEditing final : public Exercise {
    SceneEditingContext context_;
    AssetId cook_probe_;
    struct {
        std::size_t before{};
        StableId dragged, clicked, wanted;
        std::string item_key;
    } mouse_;
    struct {
        std::string original;
        std::filesystem::path file;
    } save_all_;
    std::string mask_field_original_;
    StableId travel_placed_entry, travel_placed_trigger;

  public:
    explicit SceneEditing(SceneEditingContext context) : context_(context) {}
    void frame(unsigned frame) override {
        const auto& args = context_.options;
        auto& editor = context_.editor;
        auto& shell = context_.shell;
        const auto& import_baseline = context_.scene_baseline;
        const auto import_count = context_.import_count;
        if(args.exercise_cook_sweep){const std::string dropped="characters/cook_sweep_probe.dacharacter";auto& probe=cook_probe_;
            if(frame==1){require(bool(editor.assets),"Cook sweep exercise needs a project");std::string source;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".dacharacter"&&asset.generation){std::ifstream input(editor.assets->source_root()/asset.path);auto value=nlohmann::json::parse(input);probe=AssetId::random();value["asset"]=probe.text();source=value.dump(2);break;}
                require(!source.empty(),"Cook sweep exercise needs a cooked Character to copy");{std::ofstream output(editor.assets->source_root()/dropped);output<<source;}editor.refresh_assets();
                auto found=std::find_if(editor.asset_inventory.begin(),editor.asset_inventory.end(),[&](const auto& asset){return asset.id==probe;});require(found!=editor.asset_inventory.end()&&found->generation==0,"A source copied into the project was not listed as uncooked");}
            if(frame==2){const auto before=editor.cook_pending(0);require(before.waiting>=1,"The sweep did not see the uncooked source");unsigned passes=0;darkangel::editor_app::Controller::CookSweep sweep;do{sweep=editor.cook_pending(1);}while(sweep.waiting&&++passes<4096);
                auto found=std::find_if(editor.asset_inventory.begin(),editor.asset_inventory.end(),[&](const auto& asset){return asset.id==probe;});require(found!=editor.asset_inventory.end()&&found->generation!=0,"The background sweep did not cook the copied source");
                const auto again=editor.cook_pending(100);require(again.cooked==0&&again.waiting==0,"The sweep cooked something twice or retried a failure whose file had not changed");
                std::cout<<"Editor cook_sweep waiting_before="<<before.waiting<<" passes="<<passes+1<<" failed="<<again.failed<<" probe_generation="<<found->generation<<"\n";}
        }
        // Encounter authoring exercise: the scene's encounter is selected so its volumes are drawn lit, then the three gameplay items
        // are placed through the Place panel's own command: an encounter (with its first volume), a second volume for it, a trigger volume.
        if(args.select_encounter){auto& document=*editor.document;
            if(frame==1){require(!document.plan().encounters.empty(),"Encounter exercise needs a scene with an encounter");editor.selected=document.plan().encounters.begin()->first;}
            if(frame==3){shell.refresh_place_items(editor);auto item=[&](int kind){auto found=std::find_if(shell.place_items.begin(),shell.place_items.end(),[&](const auto& entry){return entry.gameplay==kind;});require(found!=shell.place_items.end(),"The Place panel has no gameplay items");return *found;};
                const auto encounters=document.plan().encounters.size(),volumes=document.plan().volumes.size(),triggers=document.plan().triggers.size();const auto kept=editor.selected;
                const auto made=shell.place(editor,item(1),-8,10);require(document.plan().encounters.size()==encounters+1&&document.plan().volumes.size()==volumes+1&&editor.selected==made,"Placing an encounter did not add it with its first volume");
                const auto extra=shell.place(editor,item(2),-8,14);require(document.plan().volumes.size()==volumes+2&&document.plan().volumes.at(extra).encounter==made,"Placing an encounter volume did not add it to the selected encounter");
                const auto gate=shell.place(editor,item(3),-8,4);require(document.plan().triggers.size()==triggers+1&&document.plan().triggers.contains(gate),"Placing a trigger volume did not add it");
                document.undo(document.revision());require(document.plan().triggers.size()==triggers,"Undo did not remove the placed trigger volume");document.redo(document.revision());require(document.plan().triggers.size()==triggers+1,"Redo did not restore the trigger volume");
                editor.selected=kept;std::cout<<"Editor encounter_place encounters="<<document.plan().encounters.size()<<" volumes="<<document.plan().volumes.size()<<" triggers="<<document.plan().triggers.size()<<" undo_redo=1\n";}
        }
        // Mouse path exercise: the Place panel and the outliner driven with real mouse and key events, one step a frame. An event
        // queued on one frame is seen by the widgets on the next, so every check waits two frames.
        if(args.exercise_place_mouse){auto& io=ImGui::GetIO();auto& document=*editor.document;auto& before=mouse_.before;auto& dragged=mouse_.dragged;auto& clicked=mouse_.clicked;auto& item_key=mouse_.item_key;
            auto at=[&](const std::string& key){auto found=shell.probes.find(key);require(found!=shell.probes.end(),"Mouse exercise: a control it needs was not drawn");return found->second;};
            auto move=[&](ImVec2 to){io.AddMousePosEvent(to.x,to.y);};auto button=[&](int which,bool down){io.AddMouseButtonEvent(which,down);};
            auto newest=[&](std::size_t expected,const char* error){require(document.plan().origins.size()==expected&&document.plan().spawns.contains(editor.selected),error);return editor.selected;};
            if(frame==2){shell.refresh_place_items(editor);auto npc=std::find_if(shell.place_items.begin(),shell.place_items.end(),[](const auto& item){return item.npc;});require(npc!=shell.place_items.end(),"Mouse exercise needs an NPC definition in the catalogue");item_key="place:"+npc->name;before=document.plan().origins.size();move(at(item_key));}
            // Drag from the Place panel into the viewport.
            if(frame==3)button(0,true);if(frame==4){auto from=at(item_key);move({from.x+12,from.y+4});}
            if(frame==5)move(at("viewport-drop"));if(frame==6){auto drop=at("viewport-drop");move({drop.x+3,drop.y});}if(frame==7)button(0,false);
            if(frame==10){dragged=newest(before+1,"A drag from the Place panel into the viewport did not place a spawn point");const auto placed=document.world().read(document.world().find(dragged)).transform;require(std::abs(placed.y-shell.surface_height(placed.x,placed.z))<1e-3&&(std::abs(placed.x)>1e-6||std::abs(placed.z)>1e-6),"The dropped object is not on the collision under the cursor");std::cout<<"Editor mouse drag=1 drop_x="<<placed.x<<" drop_y="<<placed.y<<" drop_z="<<placed.z<<"\n";move(at(item_key));}
            // Double-click a Place item.
            if(frame==11||frame==13)button(0,true);if(frame==12||frame==14)button(0,false);
            if(frame==17){clicked=newest(before+2,"A double-click on a Place item did not place a spawn point");require(clicked!=dragged,"The double-click did not select its new object");move(at("outliner:"+clicked.text()));}
            // Click the new object's outliner row, then press Delete.
            if(frame==18)button(0,true);if(frame==19)button(0,false);if(frame==21)io.AddKeyEvent(ImGuiKey_Delete,true);if(frame==22)io.AddKeyEvent(ImGuiKey_Delete,false);
            if(frame==25){require(document.plan().origins.size()==before+1&&!document.plan().spawns.contains(clicked)&&document.plan().spawns.contains(dragged),"The Delete key did not remove the selected object");move(at("outliner:"+dragged.text()));}
            // Right-click the other object's row and choose Delete.
            if(frame==26)button(1,true);if(frame==27)button(1,false);if(frame==30)move(at("menu-delete"));if(frame==31)button(0,true);if(frame==32)button(0,false);
            if(frame==35){require(document.plan().origins.size()==before&&!document.plan().spawns.contains(dragged),"The outliner's right-click Delete did not remove the object");std::cout<<"Editor mouse double_click=1 delete_key=1 context_delete=1 objects="<<before<<"\n";}
            // Click an object in the viewport: the remaining spawn point's body, found by projecting it to the screen.
            auto& wanted=mouse_.wanted;auto project=[&](const darkangel::Transform& pose,ImVec2& out){using namespace DirectX;XMFLOAT4 clip;XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(float(pose.x),float(pose.y)+.9f,float(pose.z),1),XMLoadFloat4x4(&shell.scene_view)*XMLoadFloat4x4(&shell.scene_projection)));require(clip.w>.05f,"Mouse exercise: the object to click is behind the camera");const auto drop=at("viewport-drop");const float width=(drop.x-shell.probes.at("viewport-origin").x)*2,height=(drop.y-shell.probes.at("viewport-origin").y)/.62f;out={shell.probes.at("viewport-origin").x+(clip.x/clip.w*.5f+.5f)*width,shell.probes.at("viewport-origin").y+(.5f-clip.y/clip.w*.5f)*height};};
            if(frame==36){require(!document.plan().spawns.empty(),"Mouse exercise needs a spawn point left to click");wanted=document.plan().spawns.begin()->first;editor.selected={};ImVec2 to;project(document.world().read(document.world().find(wanted)).transform,to);move(to);}
            if(frame==37)button(0,true);if(frame==38)button(0,false);
            if(frame==41){require(editor.selected==wanted,"A click on an object in the viewport did not select it");std::cout<<"Editor mouse viewport_select=1\n";}
        }
        // Save All exercise: one draft edit, then File > Save All with the mouse; the edit must be on disk and nothing left dirty.
        if(args.exercise_save_all){auto& io=ImGui::GetIO();auto& original=save_all_.original;auto& file=save_all_.file;
            if(frame==3){require(shell.mask_editor!=AssetId{}&&editor.assets,"Save All exercise needs a mask draft open");auto& draft=editor.authoring.open(*editor.assets,shell.mask_editor);original=draft.value.at("name").get<std::string>();file=editor.assets->source_root()/draft.asset.path;auto value=draft.value;value["name"]=original+" saved";editor.authoring.apply(*editor.assets,shell.mask_editor,editor.authoring.revision(),value,"Rename mask");require(editor.authoring.dirty(),"Save All exercise could not make a draft edit");const auto to=shell.probes.at("menu-file");io.AddMousePosEvent(to.x,to.y);}
            if(frame==4)io.AddMouseButtonEvent(0,true);if(frame==5)io.AddMouseButtonEvent(0,false);
            if(frame==8){require(shell.probes.contains("menu-save-all"),"The File menu did not open");const auto to=shell.probes.at("menu-save-all");io.AddMousePosEvent(to.x,to.y);}
            if(frame==9)io.AddMouseButtonEvent(0,true);if(frame==10)io.AddMouseButtonEvent(0,false);
            if(frame==14){std::string on_disk;{std::ifstream saved(file);on_disk=nlohmann::json::parse(saved).at("name").get<std::string>();}require(!editor.authoring.dirty()&&on_disk==original+" saved","File > Save All did not write the gameplay draft");
                // Put the fixture's mask name back for the gates that run after this one.
                auto value=editor.authoring.open(*editor.assets,shell.mask_editor).value;value["name"]=original;editor.authoring.apply(*editor.assets,shell.mask_editor,editor.authoring.revision(),value,"Restore mask name");editor.save_gameplay();std::cout<<"Editor save_all saved=1 restored=1\n";}
        }
        // Mask Editor field exercise: click into the Display name field of the open Mask Editor and type one character.
        if(args.exercise_mask_field){auto& io=ImGui::GetIO();auto& original=mask_field_original_;
            if(frame==3){require(shell.mask_editor!=AssetId{}&&shell.probes.contains("mask-name"),"Mask field exercise needs the Mask Editor open");original=editor.authoring.open(*editor.assets,shell.mask_editor).value.at("name").get<std::string>();const auto to=shell.probes.at("mask-name");io.AddMousePosEvent(to.x-40,to.y);}
            if(frame==4)io.AddMouseButtonEvent(0,true);if(frame==5)io.AddMouseButtonEvent(0,false);if(frame==7)io.AddInputCharactersUTF8("Z");
            if(frame==8){const auto typed=editor.authoring.open(*editor.assets,shell.mask_editor).value.at("name").get<std::string>();require(typed.size()==original.size()+1&&typed.find('Z')!=std::string::npos&&typed!=original,"Typing into the Mask Editor's name field did not edit the mask draft");ImGui::SetWindowFocus(nullptr);}
            // The field has let go of its text by now, so Undo is not typed over.
            if(frame==10){editor.authoring.record_changes(*editor.assets,"Edit mask name");require(editor.authoring.dirty()&&editor.authoring.can_undo(),"The typed mask name is not an undoable draft edit");editor.authoring.undo(*editor.assets);require(editor.authoring.open(*editor.assets,shell.mask_editor).value.at("name").get<std::string>()==original,"Undo did not restore the mask name");std::cout<<"Editor mask_field typed=1 undo=1\n";}
        }
        // Level design exercise: place an NPC and a model through the Place panel's own commands, delete one, undo the delete.
        if(args.exercise_place){auto& document=*editor.document;
            if(frame==1){shell.refresh_place_items(editor);require(!shell.place_items.empty(),"Place exercise needs placeable assets");const auto before=document.plan().origins.size(),spawns=document.plan().spawns.size(),models=document.plan().models.size();
                auto npc=std::find_if(shell.place_items.begin(),shell.place_items.end(),[](const auto& item){return item.npc;});auto model=std::find_if(shell.place_items.begin(),shell.place_items.end(),[&](const auto& item){return !item.npc&&std::any_of(document.plan().models.begin(),document.plan().models.end(),[&](const auto& placed){return placed.second==item.id;});});
                require(npc!=shell.place_items.end()&&model!=shell.place_items.end(),"Place exercise needs an NPC definition and a static model in the catalogue");
                const auto spawn=shell.place(editor,*npc,4,6,3.141592653589793);const auto prop=shell.place(editor,*model,-9,8);
                require(document.plan().origins.size()==before+2&&document.plan().spawns.size()==spawns+1&&document.plan().spawns.at(spawn)==npc->id&&document.plan().models.size()==models+1&&document.plan().models.at(prop)==model->id&&editor.selected==prop,"Placing an NPC and a model did not add two scene objects");
                const auto at=document.world().read(document.world().find(spawn)).transform;const bool surfaced=shell.place_surface&&shell.place_surface({4,500,6},{4,-500,6}).has_value();require(at.x==4&&at.z==6&&at.y==shell.surface_height(4,6)&&(surfaced||at.y==shell.place_height),"Placed object is not on the surface at the requested position");
                // A ray from above and to the side, as a viewport drop makes it, lands on the same collision.
                {const auto slanted=shell.place_surface?shell.place_surface({-6,10,-4},{4,at.y-10,6}):std::nullopt;require(!surfaced||(slanted&&std::abs((*slanted)[1]-at.y)<1e-3),"A slanted placement ray did not land on the scene collision");}
                std::cout<<"Editor placement on_collision="<<surfaced<<" surface_y="<<at.y<<"\n";
                shell.remove_object(editor,prop);require(document.plan().origins.size()==before+1&&!document.plan().models.contains(prop)&&editor.selected!=prop,"Deleting a placed object did not remove it");
                document.undo(document.revision());require(document.plan().origins.size()==before+2&&document.plan().models.contains(prop),"Undo did not bring the deleted object back");
                document.redo(document.revision());require(document.plan().origins.size()==before+1,"Redo did not delete it again");
                bool refused=false;try{shell.remove_object(editor,StableId{77,77});}catch(const std::exception&){refused=true;}require(refused,"Deleting an object that is not in the scene was accepted");
                std::size_t categories=0;std::string last;for(const auto& item:shell.place_items)if(item.category!=last){last=item.category;++categories;}
                std::cout<<"Place panel items="<<shell.place_items.size()<<" categories="<<categories<<" placed_npc="<<npc->name<<" placed_model="<<model->name<<" objects="<<before<<"->"<<document.plan().origins.size()<<" delete_undo_redo=1\n";}}
        if(args.exercise_workspaces){if(frame==40)shell.select_workspace(darkangel::editor_app::Workspace::Scene);if(frame==45)shell.select_workspace(darkangel::editor_app::Workspace::Assets);if(frame==50)shell.open_tool(darkangel::editor_app::ToolView::InputBindings);if(frame==55)shell.select_workspace(darkangel::editor_app::Workspace::Settings);if(frame==60)shell.select_workspace(darkangel::editor_app::Workspace::Game);require(editor.document->world().serialize()==import_baseline,"Workspace switch mutated authoring data");}
        if(!args.exercise_import.empty()){if(frame==0)shell.open_import(args.exercise_import,AssetImportType::Model);if(frame==1){shell.validate_import(editor);require(shell.import_validated(),"Native dialog validation failed");}if(frame==2){shell.commit_import(editor);require(editor.asset_inventory.size()==import_count+1&&editor.document->world().serialize()==import_baseline,"Import changed scene or failed to publish asset");std::cout<<"Native editor import dialog validated and committed without scene mutation\n";}}
#ifdef DAE_CHARACTER_SCENE
        if(args.exercise_authoring&&!args.authoring_replay&&frame==0){require(editor.assets&&editor.authoring_kit!=AssetId{},"Authoring exercise needs a project source mount and kit");auto& kit=editor.authoring.open(*editor.assets,editor.authoring_kit).value;auto ability=AssetId::parse(kit["slots"][1]["ability"].get<std::string>()),effect=AssetId::parse(kit["effects"][0].get<std::string>());for(const auto& asset:editor.asset_inventory){if(asset.path=="royal_district/combat/heavy.daability")ability=asset.id;if(asset.path=="royal_district/combat/burn.daeffect")effect=asset.id;}auto fresh=editor.authoring.duplicate(*editor.assets,ability,"gui_heavy"),burn=editor.authoring.duplicate(*editor.assets,effect,"gui_burn");auto& draft=editor.authoring.open(*editor.assets,fresh).value;draft["costs"][0]["amount"]=20;draft["melee"][0]["power"]=40;auto& status=editor.authoring.open(*editor.assets,burn).value;status["duration_ticks"]=120;status["period_ticks"]=30;editor.authoring.record_changes(*editor.assets,"Edit GUI ability/effect");auto ability_edit=draft,effect_edit=status;editor.authoring.undo(*editor.assets);require(draft!=ability_edit&&status!=effect_edit,"GUI grouped Undo failed");editor.authoring.redo(*editor.assets);require(draft==ability_edit&&status==effect_edit,"GUI grouped Redo failed");shell.select_workspace(darkangel::editor_app::Workspace::Scene);shell.select_workspace(darkangel::editor_app::Workspace::Assets);shell.select_workspace(darkangel::editor_app::Workspace::Ability);require(editor.authoring.dirty()&&draft==ability_edit&&status==effect_edit,"Workspace switching discarded gameplay drafts");editor.authoring.assign(*editor.assets,editor.authoring_kit,1,fresh);editor.authoring.bind(*editor.assets,editor.authoring_kit,fresh,2,burn,7);editor.authoring.record_changes(*editor.assets,"Author GUI heavy/effect");auto authored_kit=kit;editor.authoring.undo(*editor.assets);require(kit!=authored_kit,"GUI binding Undo did not restore composition");editor.authoring.redo(*editor.assets);require(kit==authored_kit,"GUI binding Redo did not restore composition");editor.save_gameplay();editor.refresh_assets();shell.authored_selection=fresh;shell.select_workspace(darkangel::editor_app::Workspace::Game);std::cout<<"Editor edit -> Undo -> Redo -> coordinated Save -> fresh Play; new heavy/effect, cost20 damage40 duration120 period30 Burn7\n";}
        // Party travel authoring through editor commands: an entry point and a transition trigger from the Place panel's items,
        // the trigger pointed at its destination as a scene edit. The placement exercise saves the scene; the transition exercise plays it.
        if((args.exercise_transition||args.exercise_travel_place)&&frame==0){require(!args.travel_scene.empty(),"The travel exercises need --travel-scene");shell.refresh_place_items(editor);
            auto item=[&](int gameplay){auto found=std::find_if(shell.place_items.begin(),shell.place_items.end(),[&](const auto& candidate){return candidate.gameplay==gameplay;});require(found!=shell.place_items.end(),"The Place panel has no scene entry point or scene transition item");return *found;};
            const auto entries=editor.document->plan().entries.size(),transitions=editor.document->plan().transitions.size();const auto destination=AssetId::parse(args.travel_scene);const auto destination_entry=args.travel_entry.empty()?StableId{}:StableId::parse(args.travel_entry);
            if(args.exercise_travel_place){travel_placed_entry=shell.place(editor,item(7),2,7,1.5707963267948966);travel_placed_trigger=shell.place(editor,item(8),2,9);darkangel::editor_app::set_scene_transition(editor,travel_placed_trigger,destination,destination_entry,true,"Return to the gate",3);}
            else{travel_placed_entry=shell.place(editor,item(7),-4,3);travel_placed_trigger=shell.place(editor,item(8),6,3);darkangel::editor_app::set_scene_transition(editor,travel_placed_trigger,destination,destination_entry,false);}
            const auto& plan=editor.document->plan();require(plan.entries.size()==entries+1&&plan.transitions.size()==transitions+1&&plan.entries.contains(travel_placed_entry)&&darkangel::decode_transition(plan.transitions.at(travel_placed_trigger)).scene==destination,"Placing a scene entry point and a scene transition did not add them to the scene");
            if(args.exercise_travel_place){editor.save(editor.path);std::cout<<"Editor travel_place scene="<<editor.document->root().text()<<" entry="<<travel_placed_entry.text()<<" transition="<<travel_placed_trigger.text()<<" entries="<<plan.entries.size()<<" transitions="<<plan.transitions.size()<<"\n";}}
#endif
    }
    void finish() override {}
    bool done() const override {
        return true;
    }
    std::string_view result() const override {
        return {};
    }
};
} // namespace
std::unique_ptr<Exercise> make_scene_editing(SceneEditingContext context) {
    return std::make_unique<SceneEditing>(context);
}
} // namespace darkangel::editor_app::exercises
