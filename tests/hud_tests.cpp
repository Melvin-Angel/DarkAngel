#include <darkangel/player_ui.hpp>
#include "ui_test_assets.hpp"
#include <darkangel/world_ui.hpp>
#include <ashen_roots/royal_hud.hpp>
#include <RmlUi/Core/RenderInterface.h>
#include <cmath>
#include <iostream>
#include <set>
using namespace darkangel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Headless adapter gate: records what RmlUi asks a host renderer to do.
class Recorder final:public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,Rml::Span<const int> indices)override{check(!vertices.empty()&&indices.size()%3==0,"HUD geometry shape");live.insert(next);return next++;}
    void RenderGeometry(Rml::CompiledGeometryHandle handle,Rml::Vector2f,Rml::TextureHandle texture)override{check(live.contains(handle)&&(!texture||textures.contains(texture)),"HUD drew released geometry or texture");++drawn;if(texture)++textured;}
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle)override{check(live.erase(handle)==1,"HUD released unknown geometry");}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&,const Rml::String&)override{++file_textures;return 0;}
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,Rml::Vector2i size)override{check(size.x>0&&size.y>0&&source.size()==std::size_t(size.x)*std::size_t(size.y)*4,"HUD generated texture shape");textures.insert(next);return next++;}
    void ReleaseTexture(Rml::TextureHandle handle)override{check(textures.erase(handle)==1,"HUD released unknown texture");}
    void EnableScissorRegion(bool)override{}
    void SetScissorRegion(Rml::Rectanglei)override{}
    std::set<std::uintptr_t> live,textures;std::uintptr_t next{1};unsigned drawn{},textured{},file_textures{};
};
bool near(float a,float b,float tolerance=1.f){return std::abs(a-b)<=tolerance;}
}
int main(){try{
    darkangel::test::UiAssets assets;
    // Game mapping: confirmed owner attributes, authored Stamina bound, staggered tag and public target Health.
    AbilityOwnerSnapshot owner;owner.health_attribute=2;owner.maximum_health_attribute=1;owner.attributes={{1,100},{2,75},{3,85}};owner.tags.values={6};
    std::vector<AttributeDefinition> schema(3);schema[0].id=1;schema[1].id=2;schema[1].maximum_attribute=1;schema[2].id=3;schema[2].maximum=100;
    auto mapped=ashen_roots::royal_hud(owner,schema,Health{100,60});
    check(mapped==OwnerUiModel{75,100,85,100,true,true,60,100},"Royal state did not map to the HUD view model");
    check(!ashen_roots::royal_hud(owner,schema,std::nullopt).has_target,"HUD invented a target");
    WorldUiCamera camera;camera.width=800;camera.height=600;camera.view={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};camera.projection={1,0,0,0,0,1,0,0,0,0,-1,-1,0,0,-.1f,0};
    auto projected=project_world_ui(camera,0,0,-6);check(projected&&near(projected->x,400)&&near(projected->y,300)&&near(projected->scale,1),"World UI centre projection failed");auto further=project_world_ui(camera,1,1,-12);check(further&&further->x>400&&further->y<300&&further->scale<projected->scale,"World UI distance/translation failed");check(!project_world_ui(camera,0,0,1)&&!project_world_ui(camera,100,0,-6)&&!project_world_ui(camera,0,0,-.01),"World UI behind/offscreen/near clipping failed");camera.width=400;camera.height=300;check(near(project_world_ui(camera,0,0,-6)->x,200),"World UI viewport resize failed");
    Recorder recorder;
    {
        UiService service(recorder,assets.font(),[&](auto id){return assets.load(id);});PlayerUi hud(service,ashen_roots::player_ui_documents(),1280,720);
        {UiService other_service(recorder,assets.font(),[&](auto id){return assets.load(id);});PlayerUi other(other_service,ashen_roots::player_ui_documents(),640,360);other.update({});other.render();}
        hud.update(mapped);hud.render();check(hud.text("target-name")=="Target"&&!hud.box("crosshair"),"HUD target caption fallback or stray crosshair");{auto aimed=mapped;aimed.crosshair=true;hud.update(aimed);auto mark=hud.box("crosshair");check(mark&&near(mark->x+mark->width/2,640,2.f),"HUD crosshair is not at the screen centre while aiming");hud.update(mapped);}{auto named=mapped;named.target_name="Royal Guard";hud.update(named);check(hud.text("target-name")=="Royal Guard","HUD does not show the target name");hud.update(mapped);}
        auto bar=hud.box("health-bar"),fill=hud.box("health-fill"),player=hud.box("player"),target=hud.box("target"),stamina=hud.box("stamina-fill"),enemy=hud.box("target-fill");
        check(bar&&fill&&player&&target&&stamina&&enemy,"HUD document lost a bound element");
        check(near(player->x,24)&&near(player->width,320)&&near(player->y+player->height,720-64),"Player panel is not anchored bottom-left");
        check(near(target->x+target->width/2,640)&&near(target->y,20),"Target panel is not centred at the top");
        const float inner=bar->width-2;check(near(fill->width,inner*.75f)&&near(stamina->width,inner*.85f)&&near(enemy->width,(hud.box("target-bar")->width-2)*.6f),"Bars do not follow the view model");
        check(hud.text("health-text")=="75 / 100"&&hud.text("stamina-text")=="85 / 100"&&hud.text("target-text")=="60 / 100","HUD numbers do not follow the view model");
        check(hud.box("stagger")&&!hud.box("state"),"Staggered caption or defeat banner state is wrong");
        check(recorder.drawn>0&&recorder.textured>0&&!recorder.textures.empty()&&recorder.file_textures==0,"HUD produced no geometry or no font texture");
        // Switch feedback: the active mask's name is part of the view model; an unmasked actor shows none.
        check(!hud.box("mask"),"HUD showed a mask for an unmasked actor");auto masked=ashen_roots::royal_hud(owner,schema,Health{100,60},"Ember Mask");hud.update(masked);hud.render();check(hud.box("mask")&&hud.text("mask")=="Ember Mask","HUD does not show the active mask");
        masked.mask="Earth Mask";hud.update(masked);check(hud.text("mask")=="Earth Mask","HUD did not follow a mask switch");hud.update(mapped);hud.render();
        // Unchanged state redraws without rebuilding the model; changes are picked up on the next update.
        const auto drawn=recorder.drawn;hud.update(mapped);hud.render();check(recorder.drawn>drawn&&hud.model()==mapped,"HUD did not redraw unchanged state");
        // Inventory and altar are independent documents in this same service, using the established typed action adapters.
        InventoryUiModel inventory;inventory.open=inventory.ready=true;inventory.owner="owner";for(int i=0;i<4;++i)inventory.slots.push_back({i,"Mask "+std::to_string(i+1),"Empty","slot-"+std::to_string(i),"equip-"+std::to_string(i),"unequip-"+std::to_string(i)});
        hud.update_inventory(inventory);hud.render();check(service.modal_open()&&hud.box("inventory-panel"),"Inventory is not a UiService modal");service.key(UiKey::Cancel);auto inventory_actions=hud.drain_inventory_actions();check(inventory_actions.size()==1&&inventory_actions[0].kind==InventoryUiActionKind::Close,"Modal Cancel did not reach inventory adapter");inventory.open=false;hud.update_inventory(inventory);
        AltarUiModel altar;altar.open=altar.can_rest=true;altar.name="Test altar";hud.update_altar(altar);hud.render();auto rest=hud.box("altar-rest");check(service.modal_open()&&rest,"Altar is not a UiService modal");hud.pointer(int(rest->x+rest->width/2),int(rest->y+rest->height/2),true,false);hud.pointer(int(rest->x+rest->width/2),int(rest->y+rest->height/2),false,true);auto altar_actions=hud.drain_altar_actions();check(altar_actions.size()==1&&altar_actions[0].kind==AltarUiActionKind::Rest,"Altar action did not use typed adapter");altar.open=false;hud.update_altar(altar);
        auto dead=mapped;dead.health=0;dead.has_target=false;hud.update(dead);hud.render();
        check(near(hud.box("health-fill")->width,0)&&hud.text("health-text")=="0 / 100"&&hud.box("state")&&!hud.box("stagger")&&!hud.box("target"),"Defeat state is not presented");
        hud.resize(640,360);hud.update(dead);hud.render();player=hud.box("player");check(player&&near(player->y+player->height,360-64)&&near(player->x,24),"HUD did not follow a viewport resize");
        bool rejected=false;auto bad=mapped;bad.health=std::nan("");try{hud.update(bad);}catch(const std::exception&){rejected=true;}check(rejected,"HUD accepted a non-finite view model");
    }
    check(recorder.live.empty()&&recorder.textures.empty(),"HUD shutdown leaked host geometry or textures");
    {UiService service(recorder,assets.font(),[&](auto id){return assets.load(id);});PlayerUi again(service,ashen_roots::player_ui_documents(),800,600);again.update(mapped);again.render();check(again.text("health-text")=="75 / 100","Service UI could not be recreated");bool missing=false;try{service.open(AssetId::random().text(),UiService::Input::Passive,"missing",800,600);}catch(const std::exception&){missing=true;}check(missing,"Missing cooked UI document accepted");}
    std::cout<<"Runtime HUD: Royal view-model mapping, RmlUi document/data binding, bar geometry, text, defeat/stagger states, resize, host geometry/texture lifetime and recreation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
