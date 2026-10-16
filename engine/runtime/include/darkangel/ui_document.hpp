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
#include <darkangel/ui_runtime.hpp>

namespace Rml {class RenderInterface;}
namespace darkangel {
struct UiWorldMarker {
    std::string label;float x{},y{},width{},font{},height{},percent{};bool health_bar{};
    bool operator==(const UiWorldMarker&)const=default;
};
// Native view model of the combat HUD: plain values copied from the owner's confirmed
// snapshot and the public target state. The HUD presents it and decides nothing.
struct OwnerUiModel {
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
    bool operator==(const OwnerUiModel&)const=default;
};
// A font face given as its bytes (a cooked font asset), not a file path.
struct UiFont {std::string family,bytes;};
struct UiBox {float x{},y{},width{},height{};};
// R6 UI authoring preview: sample values for the "ui" data model every authored UI document binds. The document decides
// what to show; health/stamina also arrive as percentages and "current / maximum" text.
struct UiPreviewSample {double health{75},maximum_health{100},stamina{40},maximum_stamina{100},cooldown{.5},scale{1};std::string name{"Royal Guard"},prompt{"Use"};bool flag{};
    bool operator==(const UiPreviewSample&)const=default;};
// One authored UI document rendered from source text (markup plus style) with sample values, for the editor's UI tab. It
// has its own context in the shared UiRuntime, so it keeps running while Play's HUD runs (R6 U0). Clicks run
// data-event-click="command('id')" callbacks and are only logged.
enum class UiKey {Up,Down,Left,Right,Confirm,Cancel};
class UiDocumentPreview {
public:
    // fallback_font: the "HUD" face documents without packaged fonts use.
    UiDocumentPreview(Rml::RenderInterface&,UiFont fallback_font,int width,int height);
    // Packaged dependencies of the document about to load: font faces by family and images by virtual URL (da://<id>).
    void register_font(UiFont);
    void register_image(std::string url,UiRuntime::Image);
    ~UiDocumentPreview();
    UiDocumentPreview(const UiDocumentPreview&)=delete;
    UiDocumentPreview& operator=(const UiDocumentPreview&)=delete;
    // Replaces the document. On problems the previous good document stays shown (stale) and false is returned.
    bool load(std::string_view markup,std::string_view style,std::string_view binding="ui-v1");
    bool stale()const;
    const std::string& problems()const;
    void resize(int width,int height);
    void update(const UiPreviewSample&);
    void update_owner(const OwnerUiModel&);
    void update_inventory(const InventoryUiModel&);
    std::vector<InventoryUiAction> drain_inventory_actions();
    void update_altar(const AltarUiModel&);
    std::vector<AltarUiAction> drain_altar_actions();
    void render();
    void pointer(int x,int y,bool pressed,bool released,float wheel=0);
    bool key(UiKey);
    std::string focused()const;
    std::vector<std::string> drain_commands();
    std::string text(const std::string& id)const;
    std::optional<UiBox> box(const std::string& id)const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// The markup with the style placed inside its <head>, as one self-contained RmlUi document.
std::string compose_ui_document(std::string_view markup,std::string_view style);
}
