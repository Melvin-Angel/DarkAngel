#pragma once
#include <darkangel/hud.hpp>
#include <darkangel/scene_client.hpp>
#include <optional>
#include <string_view>

namespace darkangel::editor_app {
// Altar menu host adapter. It opens when authority accepts the owner's use of an altar, shows rest and the party's
// discovered fast-travel destinations, and sends the choice to the host through the SceneClient. It decides nothing.
class RuntimeAltar {
public:
    void reset(){altar_.reset();status_.clear();}
    bool open()const{return altar_.has_value();}
    // host: this player may choose fast travel (the party's host).
    void frame(Hud&,SceneClient*,bool host,bool focused,bool loading);
    // Scripted gates: altar-await (the menu is open), altar-rest, altar-travel:index, altar-close.
    bool exercise(std::string_view verb,unsigned value,Hud&,SceneClient*,bool host);
private:
    std::optional<StableId> altar_;std::uint64_t seen_{};std::string status_;
    AltarUiModel model(const SceneClient*,bool host,bool loading)const;
    void actions(Hud&,SceneClient*,bool host);
    void notice(const SceneClient*);
};
// The party lifecycle on the owner's HUD: downed, wiped, and the revive progress it receives or gives.
void party_lifecycle_hud(HudViewModel&,const SceneClient&,const std::optional<InteractionPrompt>& offered,bool interact_held);
}
