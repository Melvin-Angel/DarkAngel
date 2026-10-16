#include "material_exercise.hpp"
#include "../editor_ui.hpp"
#include <darkangel/check.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace darkangel::editor_app::exercises {
namespace {
// R5 gate: material editing in Assets on disposable content. Create an editable material from an imported model slot,
// assign it, edit and save it, and check the model cooks the new values; then reset the slot to its imported default.
class MaterialExercise final:public Exercise {
    bool enabled_,done_{};Controller& editor;Shell& shell;AssetInfo model;std::string slot;AssetId material;std::array<float,4> imported{};
    nlohmann::json metadata() const{std::ifstream input(editor.assets->source_root()/(model.path+".daimport"));return nlohmann::json::parse(input);}
    CookedMaterial cooked() const{const auto registry=editor.package_model(model.id);auto runtime=load_cooked_model(registry,editor.assets->cas_path(),model.id);const auto id=AssetId::parse(metadata()["subassets"][slot].get<std::string>());
        for(const auto& m:runtime.materials)if(m.id==id)return m;require(false,"Material exercise: slot missing from the cooked model");return {};}
  public:
    MaterialExercise(bool enabled,Controller& e,Shell& s):enabled_(enabled),editor(e),shell(s){}
    void frame(unsigned frame) override{
        if(!enabled_)return;
        if(frame==2){require(editor.assets!=nullptr,"Material exercise needs a project source mount");
            for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();if(extension!=".gltf"&&extension!=".glb")continue;
                std::ifstream input(editor.assets->source_root()/(asset.path+".daimport"));if(!input)continue;auto data=nlohmann::json::parse(input,nullptr,false);if(!data.is_object()||data.value("importer","")!="static-gltf-v1")continue;
                for(const auto& [key,unused]:data["subassets"].items())if(key.starts_with("material/")){model=asset;slot=key;break;}if(!slot.empty())break;}
            require(!slot.empty(),"Material exercise needs an imported static model with a material slot");imported=cooked().color;
            auto values=nlohmann::json::parse(editor.assets->material_values(model.path,slot));material=editor.authoring.create_material(*editor.assets,"GateMaterial",values);
            editor.authoring.save(*editor.assets,material);editor.refresh_assets();std::string path;for(const auto& asset:editor.asset_inventory)if(asset.id==material)path=asset.path;
            editor.assets->assign_model_material(model.path,slot,material,path);editor.refresh_assets();
            shell.reveal_asset(model.id);}
        if(frame==4){auto& value=editor.authoring.open(*editor.assets,material).value;value["color"]=nlohmann::json::array({.9,.1,.1,1.0});value["roughness"]=.25;editor.authoring.record_changes(*editor.assets,"Edit material");
            editor.save_gameplay();editor.refresh_assets();editor.assets->cook(model.path);if(editor.prepare_model)editor.prepare_model(model.id);
            auto result=cooked();require(std::abs(result.color[0]-.9f)<1e-5&&std::abs(result.color[1]-.1f)<1e-5&&std::abs(result.roughness-.25f)<1e-5,"Material exercise: saved material values did not reach the model");}
        if(frame==8){editor.assets->assign_model_material(model.path,slot,{});editor.refresh_assets();require(!metadata().contains("materials")&&cooked().color==imported,"Material exercise: reset to imported");
            editor.assets->assign_model_material(model.path,slot,material,"materials/GateMaterial.damaterial");editor.refresh_assets();if(editor.prepare_model)editor.prepare_model(model.id);}
        if(frame==12){done_=true;std::cout<<"Editor materials create=1 assign=1 edit=1 save=1 reset=1\n";}
    }
    void finish() override{require(!enabled_||done_,"Material exercise ended before acceptance");}
    bool done() const override{return !enabled_||done_;}
    std::string_view result() const override{return {};}
};
}
std::unique_ptr<Exercise> make_materials(bool enabled,Controller& editor,Shell& shell){return std::make_unique<MaterialExercise>(enabled,editor,shell);}
}
