#pragma once
#include <darkangel/ui_service.hpp>
namespace darkangel {
// Game UI composition over the one UiService. It owns handles and projections, never a renderer or RmlUi context.
struct PlayerUiDocuments {std::vector<std::string> hud;std::string inventory,altar,marker;};
class PlayerUi {
public:
 PlayerUi(UiService&,PlayerUiDocuments,int width,int height);
 ~PlayerUi();
 void resize(int width,int height);
 void update(const OwnerUiModel&);
 const OwnerUiModel& model()const{return owner_;}
 void update_inventory(const InventoryUiModel&);
 const InventoryUiModel& inventory()const{return inventory_;}
 std::vector<InventoryUiAction> drain_inventory_actions();
 void update_altar(const AltarUiModel&);
 const AltarUiModel& altar()const{return altar_;}
 std::vector<AltarUiAction> drain_altar_actions();
 std::optional<UiBox> box(const std::string&)const;
 std::string text(const std::string&)const;
 void pointer(int x,int y,bool pressed,bool released,float wheel=0);
 bool inventory_input_allowed()const;
 // Headless renderer tests; graphical hosts draw every UiService document through their viewport adapter.
 void render();
private:
 UiService& service_;std::vector<UiService::Handle> hud_;UiService::Handle inventory_handle_,altar_handle_;
 OwnerUiModel owner_;InventoryUiModel inventory_;AltarUiModel altar_;
};
}
