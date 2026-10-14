#include "runtime_inventory.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel::editor_app {
namespace {
const char* failure(InventoryFailure value){switch(value){case InventoryFailure::None:return "";case InventoryFailure::Missing:return "This item is no longer owned.";case InventoryFailure::NotMask:return "Only masks can be equipped.";case InventoryFailure::Equipped:return "Unequip this mask before moving it to another slot.";case InventoryFailure::Unavailable:return "Not now: you are acting or defeated.";case InventoryFailure::NotUsable:return "This item cannot be used.";default:return "The host refused this inventory request.";}}
const char* slots[]{"1 / Up","2 / Right","3 / Down","4 / Left"};
}
InventoryUiModel RuntimeInventory::model(const SceneClient* client,const CharacterPreviewResources& resources,bool loading)const{
    InventoryUiModel view;view.open=open_&&!loading;view.ready=client&&client->bootstrapped()&&client->inventory();view.busy=client&&client->inventory_pending();view.status=error_;
    const auto* state=client?client->inventory():nullptr;
    if(state){view.owner=state->inventory.profile.text();const auto& catalog=*resources.combat->inventory->catalog;
        for(const auto& entry:state->inventory.entries){const auto& item=catalog.get(entry.item);int equipped=-1;for(unsigned slot=0;slot<4;++slot)if(state->inventory.equipped[slot]==entry.instance)equipped=int(slot);
            view.items.push_back({std::to_string(entry.instance),"inventory-item-"+std::to_string(entry.instance),item.name,item.description,item.classification,int(entry.quantity),equipped,item.mask!=AssetId{},item.usable&&item.restore_health>0});}
        if(view.status.empty()&&state->receipt.failure!=InventoryFailure::None)view.status=failure(state->receipt.failure);
    }
    for(unsigned slot=0;slot<4;++slot){std::string label="Empty";bool empty=true;if(state&&state->inventory.equipped[slot]){auto found=std::find_if(view.items.begin(),view.items.end(),[&](const auto& row){return row.equipped==int(slot);});if(found!=view.items.end()){label=found->name;empty=false;}}
        view.slots.push_back({int(slot),slots[slot],label,"inventory-slot-"+std::to_string(slot),"inventory-equip-"+std::to_string(slot),"inventory-unequip-"+std::to_string(slot),empty});}
    if(client&&client->ability()&&(client->ability()->active||client->presentation().read(client->presentation().find(client->player())).health.current<=0)){view.busy=true;if(view.status.empty())view.status="Mask changes are unavailable while acting or defeated.";}
    if(view.status.empty())view.status=!view.ready?"Waiting for the owner's inventory...":view.busy?"Waiting for the host...":"Select a mask to equip it, or a consumable to use it.";
    return view;
}
void RuntimeInventory::actions(Hud& hud,SceneClient* client){
    for(const auto& action:hud.drain_inventory_actions()){
        if(action.kind==InventoryUiActionKind::Toggle){open_=!open_;error_.clear();continue;}
        if(action.kind==InventoryUiActionKind::Close){open_=false;continue;}
        InventoryRequest request;request.slot=action.slot;request.mutation=action.kind==InventoryUiActionKind::Equip?InventoryMutation::Equip:InventoryMutation::Unequip;request.instance=action.instance;
        if(action.kind==InventoryUiActionKind::Use){request=InventoryRequest{};request.mutation=InventoryMutation::Consume;request.instance=action.instance;request.quantity=1;}
        if(!client||!client->inventory_request(request))error_="Inventory is not ready. Try again when synchronized.";else error_.clear();
    }
}
void RuntimeInventory::frame(Hud& hud,SceneClient* client,const CharacterPreviewResources& resources,float x,float y,float width,float height,bool focused,bool loading){
    if(focused&&ImGui::IsKeyPressed(ImGuiKey_I,false)){open_=!open_;error_.clear();}if(open_&&focused&&ImGui::IsKeyPressed(ImGuiKey_Escape,false))open_=false;
    hud.update_inventory(model(client,resources,loading));const auto mouse=ImGui::GetMousePos();const bool inside=mouse.x>=x&&mouse.y>=y&&mouse.x<x+width&&mouse.y<y+height;
    if(inside||ImGui::IsMouseReleased(ImGuiMouseButton_Left))hud.pointer(int(mouse.x-x),int(mouse.y-y),inside&&ImGui::IsMouseClicked(ImGuiMouseButton_Left),ImGui::IsMouseReleased(ImGuiMouseButton_Left),open_&&inside?ImGui::GetIO().MouseWheel:0);
    actions(hud,client);hud.update_inventory(model(client,resources,loading));
}
void RuntimeInventory::inspector(CharacterSceneSession& session,StableId player,const CharacterPreviewResources& resources){
    ImGui::SeparatorText("Inventory");if(!resources.combat||!resources.combat->inventory){ImGui::TextDisabled("This Player has no authored inventory.");return;}
    const auto& catalog=*resources.combat->inventory->catalog;const auto* client=session.client(player);const auto* state=client?client->inventory():nullptr;
    if(!state)ImGui::TextDisabled("Waiting for the owner's inventory.");
    else{ImGui::Text("Profile %s, revision %llu, operation %llu%s",state->inventory.profile.text().c_str(),static_cast<unsigned long long>(state->inventory.revision),static_cast<unsigned long long>(state->highest_operation),client->inventory_pending()?" (request pending)":"");
        for(const auto& entry:state->inventory.entries){int slot=-1;for(unsigned index=0;index<4;++index)if(state->inventory.equipped[index]==entry.instance)slot=int(index);const auto& item=catalog.get(entry.item);
            if(slot>=0)ImGui::Text("  %s x%u  [%s] slot %d",item.name.c_str(),entry.quantity,item.classification.c_str(),slot+1);else ImGui::Text("  %s x%u  [%s]",item.name.c_str(),entry.quantity,item.classification.c_str());}}
    if(!session.hosting()){ImGui::TextDisabled("Grant item is host-only.");return;}
    if(grant_item_==AssetId{}&&!catalog.items().empty())grant_item_=catalog.items().begin()->first;
    const auto current=catalog.items().find(grant_item_);ImGui::SetNextItemWidth(200);
    if(ImGui::BeginCombo("##grant-item",current==catalog.items().end()?"Choose an item":current->second.name.c_str())){for(const auto& [id,item]:catalog.items())if(ImGui::Selectable((item.name+"##"+id.text()).c_str(),id==grant_item_))grant_item_=id;ImGui::EndCombo();}
    ImGui::SameLine();ImGui::SetNextItemWidth(80);ImGui::InputInt("##grant-quantity",&grant_quantity_);grant_quantity_=std::clamp(grant_quantity_,1,1000);ImGui::SameLine();
    if(ImGui::Button("Grant item")&&current!=catalog.items().end()){const auto receipt=session.grant_item(player,grant_item_,unsigned(grant_quantity_));grant_status_=!receipt?"No inventory for this player.":receipt->failure==InventoryFailure::None?"Granted "+std::to_string(grant_quantity_)+" x "+current->second.name+".":"Refused: "+std::string(failure(receipt->failure));}
    if(!grant_status_.empty())ImGui::TextWrapped("%s",grant_status_.c_str());
    ImGui::TextDisabled("Only items in this Player's prepared catalogue can be granted: its starting items and their masks.");
}
bool RuntimeInventory::click(Hud& hud,const std::string& id){auto box=hud.box(id);if(!box)return false;const int x=int(box->x+box->width/2),y=int(box->y+box->height/2);hud.pointer(x,y,true,false);hud.pointer(x,y,false,true);return true;}
bool RuntimeInventory::exercise(std::string_view verb,unsigned value,Hud& hud,SceneClient* client,const CharacterPreviewResources& resources){
    hud.update_inventory(model(client,resources,false));bool done{};
    if(verb=="inventory-open")done=open_||click(hud,"inventory-toggle");
    else if(verb=="inventory-close")done=!open_||click(hud,"inventory-close");
    else if(verb=="inventory-select"){if(!client||!client->inventory())return false;const auto& entries=client->inventory()->inventory.entries;if(value>=entries.size())throw std::runtime_error("Inventory script item index");done=click(hud,"inventory-item-"+std::to_string(entries[value].instance));}
    else if(verb=="inventory-use"){if(!client||!client->bootstrapped()||client->inventory_pending())return false;done=click(hud,"inventory-use");}
    else if(verb=="inventory-equip"||verb=="inventory-unequip"){if(!client||!client->bootstrapped()||client->inventory_pending())return false;if(value>=4)throw std::runtime_error("Inventory script slot index");done=click(hud,std::string(verb=="inventory-equip"?"inventory-equip-":"inventory-unequip-")+std::to_string(value));}
    else if(verb=="inventory-await"){if(!client||!client->inventory()||client->inventory_pending()||client->inventory()->highest_operation<value)return false;if(client->inventory()->receipt.failure!=InventoryFailure::None)throw std::runtime_error(failure(client->inventory()->receipt.failure));done=true;}
    else return false;
    actions(hud,client);hud.update_inventory(model(client,resources,false));return done;
}
const WornMask* RuntimeInventory::worn(const CharacterPreviewResources& resources,const AbilityOwnerSnapshot* snapshot){if(!snapshot)return nullptr;if(resources.combat&&resources.combat->inventory){auto found=resources.inventory_masks.find(snapshot->equipped_masks[snapshot->loadout]);return found==resources.inventory_masks.end()?nullptr:&found->second;}return resources.masks[snapshot->loadout]?&*resources.masks[snapshot->loadout]:nullptr;}
const WornMask* RuntimeInventory::worn(const CharacterPreviewResources& resources,const AbilityPublicSnapshot& snapshot){auto found=resources.inventory_masks.find(snapshot.equipped_masks[snapshot.loadout]);return found==resources.inventory_masks.end()?nullptr:&found->second;}
std::string RuntimeInventory::name(const CharacterPreviewResources& resources,const AbilityOwnerSnapshot* snapshot){if(!snapshot)return {};if(resources.combat&&resources.combat->inventory){auto found=resources.inventory_mask_names.find(snapshot->equipped_masks[snapshot->loadout]);return found==resources.inventory_mask_names.end()?std::string{}:found->second;}return resources.mask_names[snapshot->loadout];}
}
