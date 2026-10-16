#include "material_exercise.hpp"
#include "../editor_ui.hpp"
#include "../editor_world_objects.hpp"
#include <darkangel/check.hpp>
#include <darkangel/material_assets.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace darkangel::editor_app::exercises {
namespace {
// R5 gate: material editing in Assets on disposable content. Create an editable material from an imported model slot,
// assign it, edit and save it, and check the model cooks the new values; then reset the slot to its imported default.
class MaterialExercise final:public Exercise {
    bool enabled_,done_{};Controller& editor;Shell& shell;AssetInfo model;std::string slot;AssetId material,local_material,glass_material,prefab_id,replacement_prefab,replacement_model,replacement_slot;unsigned shader_reloads{},model_reloads{};StableId first_object,second_object;std::string first_mount,member,fingerprint;std::array<float,4> imported{};
    nlohmann::json metadata() const{std::ifstream input(editor.assets->source_root()/(model.path+".daimport"));return nlohmann::json::parse(input);}
    CookedMaterial cooked() const{const auto registry=editor.package_model(model.id);auto runtime=load_cooked_model(registry,editor.assets->cas_path(),model.id);const auto id=AssetId::parse(metadata()["subassets"][slot].get<std::string>());
        for(const auto& m:runtime.materials)if(m.id==id)return m;require(false,"Material exercise: slot missing from the cooked model");return {};}
  public:
    MaterialExercise(bool enabled,Controller& e,Shell& s):enabled_(enabled),editor(e),shell(s){}
    void frame(unsigned frame) override{
        if(!enabled_)return;
        if(frame==2){require(editor.assets!=nullptr,"Material exercise needs a project source mount");
            for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();if(extension!=".gltf"||asset.path.find("SM_RC_MudHut_Round_1F")==std::string::npos)continue;
                std::ifstream input(editor.assets->source_root()/(asset.path+".daimport"));if(!input)continue;auto data=nlohmann::json::parse(input,nullptr,false);if(!data.is_object()||data.value("importer","")!="static-gltf-v1")continue;
                for(const auto& [key,unused]:data["subassets"].items())if(key.starts_with("material/")){model=asset;slot=key;break;}if(!slot.empty())break;}
            require(!slot.empty(),"Material exercise needs an imported static model with a material slot");imported=cooked().color;
            auto values=nlohmann::json::parse(editor.assets->material_values(model.path,slot));material=editor.authoring.create_material(*editor.assets,"GateMaterial",values);
            const auto& draft=editor.authoring.open(*editor.assets,material);auto assignment=editor.assets->model_material_edit(model.path,slot,material,draft.asset.path);
            editor.authoring.save_all(*editor.assets,{},std::span<const NativeSourceEdit>(&assignment,1));editor.refresh_assets();
            shell.reveal_asset(model.id);}
        if(frame==4){auto& value=editor.authoring.open(*editor.assets,material).value;value["color"]=nlohmann::json::array({.9,.1,.1,1.0});value["roughness"]=.25;editor.authoring.record_changes(*editor.assets,"Edit material");
            editor.save_gameplay();editor.refresh_assets();editor.assets->cook(model.path);if(editor.refresh_model)editor.refresh_model(model.id);else if(editor.prepare_model)editor.prepare_model(model.id);
            auto result=cooked();require(std::abs(result.color[0]-.9f)<1e-5&&std::abs(result.color[1]-.1f)<1e-5&&std::abs(result.roughness-.25f)<1e-5,"Material exercise: saved material values did not reach the model");}
        if(frame==8){editor.assets->assign_model_material(model.path,slot,{});editor.refresh_assets();require(!metadata().contains("materials")&&cooked().color==imported,"Material exercise: reset to imported");
            editor.assets->assign_model_material(model.path,slot,material,"materials/GateMaterial.damaterial");editor.refresh_assets();if(editor.refresh_model)editor.refresh_model(model.id);else if(editor.prepare_model)editor.prepare_model(model.id);}
        if(frame==10){
            local_material=editor.authoring.create_material(*editor.assets,"GateLocalMaterial",nlohmann::json{{"color",{.1,.9,.15,1.0}}});
            prefab_id=editor.authoring.create_prefab(*editor.assets,"GateMaterialPrefab",model.id);auto& source=editor.authoring.open(*editor.assets,prefab_id).value;member=source["entities"].begin().key();const auto slot_id=metadata()["subassets"][slot].get<std::string>();source["entities"][member]["materials"]={{slot_id,material.text()}};
            for(const auto& asset:editor.asset_inventory)if(asset.path.find("SM_RC_MudHut_Round_2F_Setback.gltf")!=std::string::npos){replacement_model=asset.id;std::ifstream input(editor.assets->source_root()/(asset.path+".daimport"));replacement_slot=AssetId::parse(nlohmann::json::parse(input)["subassets"]["material/trim"].get<std::string>());break;}
            require(replacement_model!=AssetId{},"Material exercise: replacement model missing");replacement_prefab=editor.authoring.create_prefab(*editor.assets,"GateReplacementMaterialPrefab",replacement_model);auto& replacement=editor.authoring.open(*editor.assets,replacement_prefab).value;replacement["entities"].begin().value()["materials"]={{replacement_slot.text(),material.text()}};
            editor.authoring.record_changes(*editor.assets,"Material prefab");editor.save_gameplay();editor.refresh_assets();
            first_object=place_prefab_instance(editor,prefab_id,Transform{0,-8,0,0,0,0,1});first_mount=prefab_instance_of(*editor.document,first_object)->mount;
            second_object=place_prefab_instance(editor,prefab_id,Transform{0,8,0,0,0,0,1});editor.prepare_render_materials(editor.document->plan());
        }
        if(frame==11){shell.reveal_asset(prefab_id);require(shell.open_asset(editor,prefab_id)&&shell.workspace==Workspace::Prefabs&&shell.prefab_selection==prefab_id,"Material exercise: Assets Edit Prefab route");auto& draft=editor.authoring.open(*editor.assets,prefab_id);draft.value["name"]="Unsaved navigation draft";editor.authoring.record_changes(*editor.assets,"Navigation draft");}
        if(frame==12){shell.reveal_asset(prefab_id);auto& draft=editor.authoring.open(*editor.assets,prefab_id);require(shell.workspace==Workspace::Assets&&draft.dirty()&&draft.value.at("name")=="Unsaved navigation draft","Material exercise: return navigation lost or published the shared draft");editor.authoring.undo(*editor.assets);require(!editor.authoring.open(*editor.assets,prefab_id).dirty(),"Material exercise: navigation draft Undo");const auto slot_id=AssetId::parse(metadata()["subassets"][slot].get<std::string>());editor.prepare_material(model.id,slot_id,local_material);set_instance_material(*editor.document,first_mount,member,slot_id,local_material);
            require(editor.document->plan().materials.at(first_object).at(slot_id)==local_material&&editor.document->plan().materials.at(second_object).at(slot_id)==material,"Material exercise: local override leaked");}
        if(frame==13){shader_reloads=unsigned(std::count_if(editor.console.begin(),editor.console.end(),[](const auto& line){return line.message=="Static shader candidate and material bindings committed at GPU idle.";}));shell.reload_shader=true;}
        if(frame==14){require(unsigned(std::count_if(editor.console.begin(),editor.console.end(),[](const auto& line){return line.message=="Static shader candidate and material bindings committed at GPU idle.";}))>shader_reloads,"Material exercise: shader reload did not commit");const auto slot_id=AssetId::parse(metadata()["subassets"][slot].get<std::string>());revert_overrides(*editor.document,first_mount,{{member,"materials",slot_id.text()}});require(editor.document->plan().materials.at(first_object).at(slot_id)==material,"Material exercise: Revert");editor.document->undo(editor.document->revision());editor.prepare_render_materials(editor.document->plan());}
        if(frame==15){model_reloads=unsigned(std::count_if(editor.console.begin(),editor.console.end(),[](const auto& line){return line.message=="Published resource generation after GPU upload; retired unpinned data at GPU idle.";}));shell.reload_model=true;}
        if(frame==16){require(unsigned(std::count_if(editor.console.begin(),editor.console.end(),[](const auto& line){return line.message=="Published resource generation after GPU upload; retired unpinned data at GPU idle.";}))>model_reloads,"Material exercise: model reload did not commit");fingerprint=editor.document->plan().fingerprint;const auto scene=editor.assets->source_root()/"MaterialGate.dascene";editor.save(scene);editor.open(scene);require(editor.document->plan().fingerprint==fingerprint,"Material exercise: Save/reopen changed assignments");}
        if(frame==17){const auto slot_id=AssetId::parse(metadata()["subassets"][slot].get<std::string>());MaterialSwap mapping;mapping.remap[slot_id]=replacement_slot;mapping.replacement_slots.insert(replacement_slot);const auto text=*editor.prefab_links()(replacement_prefab);
            require(prepare_swap(*editor.document,first_mount,replacement_prefab,text,false,{},mapping).compatible,"Material exercise: mapped Swap refused");if(editor.prepare_model)editor.prepare_model(replacement_model);editor.prepare_material(replacement_model,replacement_slot,local_material);commit_swap(*editor.document,first_mount,replacement_prefab,text,false,{},mapping);
            require(editor.document->plan().models.at(first_object)==replacement_model&&editor.document->plan().materials.at(first_object).at(replacement_slot)==local_material&&editor.document->plan().materials.at(second_object).at(slot_id)==material,"Material exercise: mapped Swap lost appearance or placement identity");fingerprint=editor.document->plan().fingerprint;const auto scene=editor.path;editor.save(scene);editor.open(scene);require(editor.document->plan().fingerprint==fingerprint,"Material exercise: mapped Swap did not survive reopen");
            editor.authoring.open(*editor.assets,material).value["color"]=nlohmann::json::array({.1,.2,.9,1.0});editor.authoring.record_changes(*editor.assets,"Shared edit with local override");editor.save_gameplay();if(editor.refresh_model)editor.refresh_model(model.id);
            const auto shared_values=load_cooked_material_instance(editor.package_model(material),editor.assets->cas_path(),material);const auto local_values=load_cooked_material_instance(editor.package_model(local_material),editor.assets->cas_path(),local_material);
            require(shared_values.value.color[2]==.9f&&local_values.value.color[1]==.9f&&editor.document->plan().fingerprint==fingerprint,"Material exercise: shared edit changed local appearance or scene identity");}
        if(frame==18){
            // R5 A3: texture semantics and the lit-surface-v1 family. Two copies of the trim image become a normal map and a packed
            // surface map; the local material gains them, emissive colour and cutout alpha, and a glass material blends.
            const auto root=editor.assets->source_root();const auto trim=root/"royal_district/static/mud_huts/mudhut-trim.png";require(std::filesystem::exists(trim),"Material exercise: trim texture missing");
            std::filesystem::create_directories(root/"textures");for(const char* name:{"gate_normal.png","gate_surface.png","gate_glass.png"})std::filesystem::copy_file(trim,root/"textures"/name,std::filesystem::copy_options::overwrite_existing);
            const auto normal=editor.assets->adopt("textures/gate_normal.png"),surface=editor.assets->adopt("textures/gate_surface.png"),glass_texture=editor.assets->adopt("textures/gate_glass.png");editor.assets->scan();
            editor.assets->set_texture_semantic("textures/gate_normal.png",TextureSemantic::Normal);editor.assets->set_texture_semantic("textures/gate_surface.png",TextureSemantic::Data);editor.assets->cook("textures/gate_glass.png");editor.refresh_assets();
            auto& local=editor.authoring.open(*editor.assets,local_material).value;local["family"]="lit-surface-v1";local["normal_texture"]=normal.text();local["normal_scale"]=1.2;local["surface_texture"]=surface.text();local["occlusion_strength"]=.8;local["emissive"]={.05,.2,.05};local["alpha_mode"]="cutout";local["alpha_cutoff"]=.1;
            local["sources"]["normal_texture"]="textures/gate_normal.png";local["sources"]["surface_texture"]="textures/gate_surface.png";
            glass_material=editor.authoring.create_material(*editor.assets,"GateGlass",nlohmann::json{{"color",{.4,.7,1.0,.45}}});auto& glass=editor.authoring.open(*editor.assets,glass_material).value;glass["family"]="lit-surface-v1";glass["alpha_mode"]="transparent";glass["texture"]=glass_texture.text();glass["sources"]={{"texture","textures/gate_glass.png"}};
            editor.authoring.record_changes(*editor.assets,"Surface materials");editor.save_gameplay();editor.refresh_assets();
            const auto loaded=load_cooked_material_instance(editor.package_model(local_material),editor.assets->cas_path(),local_material);
            unsigned linear{};for(const auto& [id,data]:loaded.textures)if(!data.srgb)++linear;
            require(loaded.value.has_normal&&loaded.value.has_surface&&loaded.value.alpha_mode==1&&linear==2,"Material exercise: surface material closure or texture transfer functions");
            editor.prepare_material(replacement_model,replacement_slot,local_material);
            const auto slot_id=AssetId::parse(metadata()["subassets"][slot].get<std::string>());const auto second_mount=prefab_instance_of(*editor.document,second_object)->mount;
            editor.prepare_material(model.id,slot_id,glass_material);set_instance_material(*editor.document,second_mount,member,slot_id,glass_material);editor.prepare_render_materials(editor.document->plan());
            require(editor.document->plan().materials.at(second_object).at(slot_id)==glass_material,"Material exercise: transparent material assignment");}
        if(frame==20){editor.play();require(editor.preview.active()&&editor.preview.active()->plan.materials==editor.document->plan().materials,"Material exercise: frozen Play bindings");}
        if(frame==26){editor.stop();shell.select_workspace(Workspace::Scene);editor.select_object(first_object);shell.scene_navigation.camera.look_from({0,9,29},{0,2,0});done_=true;std::cout<<"Editor materials create=1 assign=1 edit=1 save=1 reset=1 local=1 revert=1 reopen=1 play=1 swap=1 shader_reload=1 model_reload=1 navigation=1 shared_local=1 surface=1 transparent=1\n";}

    }
    void finish() override{require(!enabled_||done_,"Material exercise ended before acceptance");}
    bool done() const override{return !enabled_||done_;}
    std::string_view result() const override{return {};}
};
}
std::unique_ptr<Exercise> make_materials(bool enabled,Controller& editor,Shell& shell){return std::make_unique<MaterialExercise>(enabled,editor,shell);}
}
