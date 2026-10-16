#pragma once
#include "character_preview.hpp"
#include <darkangel/character_scene.hpp>
#include <darkangel/player_ui.hpp>
#include <imgui.h>

namespace darkangel::editor_app {
// Game menu host adapter. RmlUi owns its document and input events; requests go
// through the SceneClient's authenticated owner channel, never to authority.
class RuntimeInventory {
public:
    void reset(){open_=false;error_.clear();}
    bool open()const{return open_;}
    void frame(PlayerUi&,SceneClient*,const CharacterPreviewResources&,float x,float y,float width,float height,bool focused,bool loading);
    void flush(PlayerUi& hud,SceneClient* client,const CharacterPreviewResources& resources,bool loading);
    bool exercise(std::string_view verb,unsigned value,PlayerUi&,SceneClient*,const CharacterPreviewResources&);
    // Gameplay Inspector section: the viewed player's inventory and, on the host, Grant item through the authority path.
    void inspector(CharacterSceneSession&,StableId player,const CharacterPreviewResources&);
    static const WornMask* worn(const CharacterPreviewResources&,const AbilityOwnerSnapshot*);
    static const WornMask* worn(const CharacterPreviewResources&,const AbilityPublicSnapshot&);
    static std::string name(const CharacterPreviewResources&,const AbilityOwnerSnapshot*);
private:
    bool open_{};std::string error_,grant_status_;AssetId grant_item_;int grant_quantity_{1};
    InventoryUiModel model(const SceneClient*,const CharacterPreviewResources&,bool loading)const;
    void actions(PlayerUi&,SceneClient*);
    static bool click(PlayerUi&,const std::string&);
};
}
