#pragma once
#include <darkangel/player_ui.hpp>
#include <darkangel/actor_assets.hpp>
#include <darkangel/world_ui.hpp>
#include <darkangel/scene_definitions.hpp>
#include <filesystem>
#include <map>
#include <set>
#include <span>
struct ImDrawList;
namespace darkangel {class SceneClient;}
namespace darkangel::editor_app {
class ImGuiHudRenderer;
struct UiActorInfo {StableId id;std::string name;};
class PlayUi {
public:
 PlayUi(ImGuiHudRenderer&,UiFont,const std::filesystem::path&,const std::filesystem::path&,const ActorUiDefinition&,const std::map<StableId,ActorUiDefinition>&);
 void frame(SceneClient&,const OwnerUiModel&,const WorldUiCamera&,bool focused,bool loading,float x,float y,std::span<const StableId>,std::span<const UiActorInfo>);
 void render(ImDrawList*,float x,float y,float width,float height);
 bool modal()const{return service.modal_open()||suppress;}
 const std::vector<UiWorldMarker>& world_markers()const{return marker_rows;}
 UiService service;PlayerUi player;
private:
 struct Marker {StableId actor;std::string name;ActorUiAttachment attachment;UiService::Handle handle;bool player{};};
 std::vector<ActorUiAttachment> player_markers;std::map<StableId,ActorUiDefinition> actor_specs;std::set<StableId> attached;
 ImGuiHudRenderer& renderer;std::vector<Marker> markers;std::vector<UiWorldMarker> marker_rows;
 UiService::Handle puzzle;StableId puzzle_object;std::uint64_t seen{};bool suppress{};
};
}
