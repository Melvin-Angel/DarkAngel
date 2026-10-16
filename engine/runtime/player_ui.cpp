#include <darkangel/player_ui.hpp>
namespace darkangel {
PlayerUi::PlayerUi(UiService& service,PlayerUiDocuments docs,int width,int height):service_(service){
 for(const auto& id:docs.hud)hud_.push_back(service_.open(id,UiService::Input::Passive,"player",width,height));
 inventory_handle_=service_.open(docs.inventory,UiService::Input::Passive,"inventory",width,height);
 altar_handle_=service_.open(docs.altar,UiService::Input::Passive,"altar",width,height);
 InventoryUiModel initial;for(int i=0;i<4;++i)initial.slots.push_back({i,"Mask "+std::to_string(i+1),"Empty"});update_inventory(initial);
}
PlayerUi::~PlayerUi(){for(auto h:hud_)service_.close(h);service_.close(inventory_handle_);service_.close(altar_handle_);}
void PlayerUi::resize(int width,int height){service_.resize_screens(width,height);}
void PlayerUi::update(const OwnerUiModel& value){
 for(auto h:hud_){service_.update_owner(h,value);auto& sample=service_.model(h);sample.health=value.health;sample.maximum_health=value.maximum_health;sample.stamina=value.stamina;sample.maximum_stamina=value.maximum_stamina;sample.prompt=value.prompt;sample.name=value.mask;}
 owner_=value;
}
void PlayerUi::update_inventory(const InventoryUiModel& value){service_.update_inventory(inventory_handle_,value);service_.set_input(inventory_handle_,value.open?UiService::Input::Modal:UiService::Input::Passive,false);inventory_=value;}
std::vector<InventoryUiAction> PlayerUi::drain_inventory_actions(){auto result=service_.drain_inventory_actions(inventory_handle_);for(const auto& c:service_.drain_commands(inventory_handle_))if(c.command=="ui.cancel")result.push_back({InventoryUiActionKind::Close});return result;}
void PlayerUi::update_altar(const AltarUiModel& value){service_.update_altar(altar_handle_,value);service_.set_input(altar_handle_,value.open?UiService::Input::Modal:UiService::Input::Passive,false);altar_=value;}
std::vector<AltarUiAction> PlayerUi::drain_altar_actions(){auto result=service_.drain_altar_actions(altar_handle_);for(const auto& c:service_.drain_commands(altar_handle_))if(c.command=="ui.cancel")result.push_back({AltarUiActionKind::Close});return result;}
std::optional<UiBox> PlayerUi::box(const std::string& id)const{for(auto h:hud_)if(auto box=service_.box(h,id))return box;if(auto box=service_.box(inventory_handle_,id))return box;return service_.box(altar_handle_,id);}
std::string PlayerUi::text(const std::string& id)const{for(auto h:hud_){auto text=service_.text(h,id);if(!text.empty())return text;}auto text=service_.text(inventory_handle_,id);return text.empty()?service_.text(altar_handle_,id):text;}
void PlayerUi::pointer(int x,int y,bool pressed,bool released,float wheel){service_.pointer(x,y,pressed,released,wheel);}
bool PlayerUi::inventory_input_allowed()const{auto top=service_.top_modal();return !top||*top==inventory_handle_;}
void PlayerUi::render(){service_.update();service_.render([](auto& view,float,float,float,float){view.render();});}
}
