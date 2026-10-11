#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Rml {class RenderInterface;}
namespace darkangel {
// Native view model of the combat HUD: plain values copied from the owner's confirmed
// snapshot and the public target state. The HUD presents it and decides nothing.
struct HudViewModel {
    double health{},maximum_health{},stamina{},maximum_stamina{};
    bool staggered{};
    bool has_target{};double target_health{},target_maximum_health{};
    // Display name of the active equipped mask; empty when the actor wears none.
    std::string mask;
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
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
