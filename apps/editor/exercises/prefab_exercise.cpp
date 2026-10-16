#include "prefab_exercise.hpp"
#include "../editor_ui.hpp"
#include "../editor_world_objects.hpp"
#include "../prefab_commands.hpp"
#include <darkangel/check.hpp>
#include <darkangel/world_objects.hpp>
#include <algorithm>
#include <iostream>

namespace darkangel::editor_app::exercises {
namespace {
// R4 gate: the prefab loop through the editor's own commands on disposable content. Create a modelled altar prefab, place
// two linked instances from the Place panel, override one, swap the other to a compatible prefab, convert a loose altar
// while keeping its identity, apply an override to its prefab, save and reopen the scene, and start a fresh Play. The R4
// remainder adds: the whole-instance selection, the Prefabs preview, a nested prefab, a variant with an explicit rebase after
// its base changes, a multi-object capture and an identity-preserving Unpack, before the save/reopen and Play.
class PrefabExercise final:public Exercise {
    bool enabled_,done_{};Controller& editor;Shell& shell;
    AssetId first_prefab,second_prefab,nested_prefab,variant_prefab;std::string first_mount,second_mount,converted_mount,nested_mount,variant_mount,captured_mount;StableId first,second,loose,variant_object,captured_a,captured_b;std::string fingerprint;
    StableId member(const std::string& mount) const{const auto prefix=editor.document->placement().text()+"/"+mount+"/";for(const auto& origin:editor.document->plan().origins)if(origin.local_path.starts_with(prefix))return origin.object;require(false,"Prefab exercise: instance member missing");return {};}
    std::string altar(StableId object) const{auto found=editor.document->plan().altars.find(object);require(found!=editor.document->plan().altars.end(),"Prefab exercise: altar missing from the plan");return decode_altar(found->second).name;}
    AssetId make_prefab(const char* name,const char* altar_name){
        // A static model from the Place catalogue, so the instance draws.
        shell.refresh_place_items(editor);auto model=std::find_if(shell.place_items.begin(),shell.place_items.end(),[](const auto& item){return !item.npc&&item.gameplay==0;});
        require(model!=shell.place_items.end(),"Prefab exercise needs a static model");
        auto id=editor.authoring.create_prefab(*editor.assets,name,model->id);auto& value=editor.authoring.open(*editor.assets,id).value;
        value["entities"].begin().value()["altar"]=nlohmann::json{{"name",altar_name}};editor.authoring.record_changes(*editor.assets,"Edit prefab");
        editor.save_gameplay();editor.refresh_assets();require(editor.prefab_links()(id).has_value(),"Prefab exercise: prefab was not published");return id;}
    std::string mount_of(StableId object) const{auto instance=prefab_instance_of(*editor.document,object);require(instance.has_value(),"Prefab exercise: object is not an instance member");return instance->mount;}
  public:
    PrefabExercise(bool enabled,Controller& e,Shell& s):enabled_(enabled),editor(e),shell(s){}
    void frame(unsigned frame) override{
        if(!enabled_)return;
        if(frame==2){require(editor.assets!=nullptr,"Prefab exercise needs a project source mount");first_prefab=make_prefab("GatePrefabAltar","Village altar");
            shell.refresh_place_items(editor);auto item=std::find_if(shell.place_items.begin(),shell.place_items.end(),[](const auto& i){return i.category=="Prefabs"&&i.name=="GatePrefabAltar";});
            require(item!=shell.place_items.end(),"Prefab exercise: the Place panel lacks the prefab");
            first=shell.place(editor,*item,6,6,0);second=shell.place(editor,*item,-6,6,1.0);first_mount=mount_of(first);second_mount=mount_of(second);
            require(first!=second&&altar(first)=="Village altar"&&altar(second)=="Village altar"&&editor.document->plan().models.contains(first),"Prefab exercise: two independent modelled instances");}
        if(frame==4){set_instance_field(*editor.document,first_mount,prefab_instance_of(*editor.document,first)->member,"altar","name",nlohmann::json("Shrine"));
            require(altar(first)=="Shrine"&&altar(second)=="Village altar"&&instance_overrides(*editor.document,first_mount).size()==1,"Prefab exercise: instance override");}
        if(frame==6){second_prefab=make_prefab("GatePrefabTemple","Temple altar");const auto text=*editor.prefab_links()(second_prefab);
            require(prepare_swap(*editor.document,second_mount,second_prefab,text,false).compatible,"Prefab exercise: compatible swap refused");commit_swap(*editor.document,second_mount,second_prefab,text,false);
            require(member(second_mount)==second&&altar(second)=="Temple altar","Prefab exercise: swap lost identity or definition");}
        if(frame==8){loose=place_world_object(editor,place_altar,Transform{0.3,4,0,9,0,0,1});auto id=AssetId::random();
            auto text=prefab_from_object(*editor.document,loose,id,"GateConverted");auto created=editor.authoring.create_prefab_source(*editor.assets,"GateConverted",nlohmann::json::parse(text));editor.authoring.save(*editor.assets,created);editor.refresh_assets();
            converted_mount=replace_with_instance(*editor.document,loose,created,*editor.prefab_links()(created));require(member(converted_mount)==loose,"Prefab exercise: conversion changed persistent identity");}
        if(frame==10){const auto key=prefab_instance_of(*editor.document,first)->member;auto& draft=editor.authoring.open(*editor.assets,first_prefab);
            auto updated=prefab_with_overrides(*editor.document,first_mount,{{key,"altar","name"}},draft.value.dump());editor.authoring.apply(*editor.assets,first_prefab,editor.authoring.revision(),nlohmann::json::parse(updated),"Apply instance overrides to prefab");
            editor.authoring.save(*editor.assets,first_prefab);editor.refresh_assets();editor.refresh_prefab_links();revert_overrides(*editor.document,first_mount,{{key,"altar","name"}});
            require(altar(first)=="Shrine"&&instance_overrides(*editor.document,first_mount).empty(),"Prefab exercise: Apply Selected");
            shell.prefab_selection=first_prefab;shell.prefab_preview.in_context=true;shell.select_workspace(Workspace::Prefabs);}
        if(frame==12){
            // The Prefabs preview drew the definition at its instance, with the scene around it as context.
            require(shell.prefab_preview.visible&&shell.prefab_preview.error.empty()&&std::any_of(shell.prefab_preview.models.begin(),shell.prefab_preview.models.end(),[](const auto& m){return !m.context;}),"Prefab exercise: the Prefabs preview drew nothing");
            const bool context=std::any_of(shell.prefab_preview.models.begin(),shell.prefab_preview.models.end(),[](const auto& m){return m.context;});
            // Whole-instance selection: the outliner's instance row is a MountSelection; a member row returns to an object.
            editor.select_mount(first_mount,first);require(editor.selected_mount()&&*editor.selected_mount()==first_mount,"Prefab exercise: mount selection");editor.select_object(first);require(!editor.selected_mount(),"Prefab exercise: object selection");
            // A nested prefab: a new prefab that mounts the Temple prefab two metres to its side.
            nested_prefab=make_prefab("GatePrefabNested","Nested outer");auto& value=editor.authoring.open(*editor.assets,nested_prefab).value;value["mounts"]={{"00000000000000000000000000000041",{{"asset",second_prefab.text()},{"placement",{{"x",2.0},{"y",0.0},{"z",0.0},{"yaw",0.0}}}}}};
            editor.authoring.record_changes(*editor.assets,"Edit prefab");editor.save_gameplay();editor.refresh_assets();
            shell.select_workspace(Workspace::Scene);place_prefab_instance(editor,nested_prefab,Transform{0,-8,0,8,0,0,1});nested_mount=*editor.selected_mount();
            bool nested=false;for(const auto& origin:editor.document->plan().origins)if(auto found=prefab_instance_of(*editor.document,origin.object);found&&found->mount==nested_mount&&found->nested)nested=altar(origin.object)=="Temple altar";
            require(nested,"Prefab exercise: nested prefab member missing");std::cout<<"Prefab preview context="<<(context?1:0)<<"\n";}
        if(frame==14){
            // A variant of the first prefab patches its altar name; its instance shows the patch.
            variant_prefab=AssetId::random();const auto base_text=*editor.prefab_links()(first_prefab);auto value=make_variant(variant_prefab,"GatePrefabVariant",first_prefab,base_text);
            set_variant_field(value,base_text,editor.authoring.open(*editor.assets,first_prefab).value["entities"].begin().key(),"altar","name",nlohmann::json("Variant altar"));
            auto created=editor.authoring.create_prefab_source(*editor.assets,"GatePrefabVariant",value);editor.authoring.save(*editor.assets,created);editor.refresh_assets();variant_prefab=created;
            variant_object=place_prefab_instance(editor,variant_prefab,Transform{0,8,0,-8,0,0,1});variant_mount=*editor.selected_mount();require(altar(variant_object)=="Variant altar","Prefab exercise: variant patch");}
        if(frame==16){
            // The base changes: the scene keeps its previous definitions until the variant is rebased explicitly.
            auto& base=editor.authoring.open(*editor.assets,first_prefab);base.value["entities"].begin().value()["altar"]["name"]="Shrine 2";editor.authoring.record_changes(*editor.assets,"Edit prefab");editor.save_gameplay();editor.refresh_assets();
            require(altar(first)=="Shrine","Prefab exercise: a base change was applied although its variant needs a rebase");
            auto& variant=editor.authoring.open(*editor.assets,variant_prefab);variant.value=rebase_variant(variant.value,*editor.prefab_links()(first_prefab));editor.authoring.record_changes(*editor.assets,"Rebase variant");editor.save_gameplay();editor.refresh_assets();
            require(altar(first)=="Shrine 2"&&altar(variant_object)=="Variant altar","Prefab exercise: rebase");}
        if(frame==18){
            // Multi-object capture of two loose altars, then Unpack: both keep their persistent identities.
            captured_a=place_world_object(editor,place_altar,Transform{0,-10,0,-10,0,0,1});captured_b=place_world_object(editor,place_altar,Transform{0,-12,0,-10,0,0,1});
            auto id=AssetId::random();auto text=prefab_from_objects(*editor.document,{captured_a,captured_b},id,"GateCaptured");auto created=editor.authoring.create_prefab_source(*editor.assets,"GateCaptured",nlohmann::json::parse(text));editor.authoring.save(*editor.assets,created);editor.refresh_assets();
            captured_mount=replace_with_instance(*editor.document,{captured_a,captured_b},created,*editor.prefab_links()(created));require(prefab_instance_of(*editor.document,captured_b)&&prefab_instance_of(*editor.document,captured_b)->mount==captured_mount,"Prefab exercise: capture");
            unpack_instance(*editor.document,captured_mount);require(!prefab_instance_of(*editor.document,captured_a)&&editor.document->world().valid(editor.document->world().find(captured_a))&&editor.document->world().valid(editor.document->world().find(captured_b)),"Prefab exercise: unpack lost identity");}
        if(frame==20){fingerprint=editor.document->plan().fingerprint;editor.save(editor.path);editor.open(editor.path);
            require(editor.document->plan().fingerprint==fingerprint&&prefab_instances(*editor.document).size()>=5,"Prefab exercise: reopened scene differs");}
        if(frame==22){editor.play();require(editor.preview.active()!=nullptr,"Prefab exercise: fresh Play did not start");}
        if(frame==28){editor.stop();done_=true;std::cout<<"Editor prefabs place=1 override=1 swap=1 convert=1 apply=1 preview=1 nested=1 variant=1 rebase=1 capture=1 unpack=1 reopen=1 play=1\n";}
    }
    void finish() override{require(!enabled_||done_,"Prefab exercise ended before acceptance");}
    bool done() const override{return !enabled_||done_;}
    std::string_view result() const override{return {};}
};
}
std::unique_ptr<Exercise> make_prefabs(bool enabled,Controller& editor,Shell& shell){return std::make_unique<PrefabExercise>(enabled,editor,shell);}
}
