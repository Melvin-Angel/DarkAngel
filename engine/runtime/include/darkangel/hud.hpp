#pragma once
#include <filesystem>
#include <functional>
#include <string_view>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <darkangel/inventory_ui.hpp>
#include <darkangel/altar_ui.hpp>

namespace Rml {class RenderInterface;}
namespace darkangel {
struct HudWorldMarker {
    std::string label;float x{},y{},width{},font{},height{},percent{};bool health_bar{};
    bool operator==(const HudWorldMarker&)const=default;
};
// Native view model of the combat HUD: plain values copied from the owner's confirmed
// snapshot and the public target state. The HUD presents it and decides nothing.
struct HudViewModel {
    double health{},maximum_health{},stamina{},maximum_stamina{};
    bool staggered{};
    bool has_target{};double target_health{},target_maximum_health{};
    // Display name of the active equipped mask; empty when the actor wears none.
    std::string mask;
    // The owner is soft-locked onto the target on this tick.
    bool target_locked{};
    // Display name of the target actor; the HUD shows a generic caption when empty.
    std::string target_name;
    // Screen-centre crosshair, shown while an aim camera is live.
    bool crosshair{};
    // What the nearest interactable in reach offers; empty when there is none.
    std::string prompt;
    // The party is travelling between scenes: a full-screen loading panel with a status line and progress 0 to 1.
    bool loading{};std::string loading_text;double loading_progress{};
    // The party lifecycle: the owner is downed; the party is wiped (the respawn follows); a revive in progress, the
    // owner's own or one it gives, with its caption and progress 0 to 1.
    bool downed{},wiped{};std::string revive_text;double revive_progress{};
    std::vector<HudWorldMarker> world_markers;
    bool operator==(const HudViewModel&)const=default;
};
// One runtime HUD: an RmlUi context, one game-owned document and a data model bound to
// the view model. The host supplies the render interface, which must outlive the Hud, and
// calls render() inside its own frame. RmlUi keeps process-wide state, so one Hud at a time.
class Hud {
public:
    Hud(Rml::RenderInterface&,const std::filesystem::path& document,const std::filesystem::path& font,int width,int height);
    ~Hud();
    Hud(const Hud&)=delete;
    Hud& operator=(const Hud&)=delete;
    void resize(int width,int height);
    // Copies the view model, marks the bound variables and runs layout. No drawing.
    void update(const HudViewModel&);
    void render();
    const HudViewModel& model()const;
    // Laid-out border box of an element in HUD pixels; empty when absent or not displayed.
    struct Box {float x{},y{},width{},height{};};
    std::optional<Box> box(const std::string& id)const;
    std::string text(const std::string& id)const;
    void update_inventory(const InventoryUiModel&);
    const InventoryUiModel& inventory()const;
    std::vector<InventoryUiAction> drain_inventory_actions();
    void update_altar(const AltarUiModel&);
    const AltarUiModel& altar()const;
    std::vector<AltarUiAction> drain_altar_actions();
    void pointer(int x,int y,bool pressed,bool released,float wheel=0);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// R6 UI authoring preview: sample values for the "ui" data model every authored UI document binds. The document decides
// what to show; health/stamina also arrive as percentages and "current / maximum" text.
struct UiPreviewSample {double health{75},maximum_health{100},stamina{40},maximum_stamina{100},cooldown{.5};std::string name{"Royal Guard"},prompt{"Use"};bool flag{};
    bool operator==(const UiPreviewSample&)const=default;};
// One authored UI document rendered from source text (markup plus style) with sample values, for the editor's UI tab. It
// shares the process's single RmlUi lifetime with Hud: creating a Hud evicts a live preview first (Play wins), and the
// owner is told through on_evicted, which must drop the preview. Clicks run data-event-click="command('id')" callbacks
// and are only logged.
class UiDocumentPreview {
public:
    UiDocumentPreview(Rml::RenderInterface&,const std::filesystem::path& font,int width,int height,std::function<void()> on_evicted);
    ~UiDocumentPreview();
    UiDocumentPreview(const UiDocumentPreview&)=delete;
    UiDocumentPreview& operator=(const UiDocumentPreview&)=delete;
    // Replaces the document. On problems the previous good document stays shown (stale) and false is returned.
    bool load(std::string_view markup,std::string_view style);
    bool stale()const;
    const std::string& problems()const;
    void resize(int width,int height);
    void update(const UiPreviewSample&);
    void render();
    void pointer(int x,int y,bool pressed,bool released);
    std::vector<std::string> drain_commands();
    std::string text(const std::string& id)const;
    std::optional<Hud::Box> box(const std::string& id)const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// The markup with the style placed inside its <head>, as one self-contained RmlUi document.
std::string compose_ui_document(std::string_view markup,std::string_view style);
}
