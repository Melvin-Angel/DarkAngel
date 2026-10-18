#include "render/scene_rendering_authoring.hpp"
#include <darkangel/editor_document.hpp>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
using namespace darkangel::editor_app::render;
using Json=nlohmann::json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    const auto root=AssetId::parse("22222222-2222-4222-8222-222222222222");
    const auto local=StableId{0,1}.text();
    SceneRenderingSettings settings;settings.main_light={1,2};
    Json entity={{"name","Rendering"},{"types",{{"1",{{"version",2},{"fields",{{"1",0},{"2",0},{"3",0},{"4",0},{"5",0},{"6",0},{"7",1}}}}},{"2",{{"version",1},{"fields",{{"1",100},{"2",100}}}}}}}};
    set_render_component(entity,"rendering",encode_rendering_settings(settings));
    set_render_component(entity,"light",{{"schema",1},{"type","directional"},{"intensity",2.0}});
    check(!entity.contains("rendering")&&entity["optional"].contains("rendering"),"New scene settings are not a native optional component");
    Json graph={{"schema",1},{"asset",root.text()},{"entities",{{local,entity}}}};
    EditorDocument document(root,{0,7},{{root,graph.dump()}});
    auto resolved=[&](EditorDocument& doc){const auto object=doc.plan().origins.front().object;return Json::parse(doc.world().read(doc.world().find(object)).optional_json);};
    const auto initial=resolved(document)["rendering"];
    for(unsigned frame=0;frame<8;++frame){
        auto source=Json::parse(*document.entity(local));
        auto next=decode_rendering_settings(resolved(document)["rendering"]);
        next.post.outline.thickness=1.f+frame*.25f;
        next.post.contrast=1.f+frame*.05f;
        source["rendering"]=initial; // Old editor's ignored sibling must be retired.
        set_render_component(source,"rendering",encode_rendering_settings(next));
        document.commit(document.prepare_entities(document.revision(),{{local,source.dump()}}));
        check(resolved(document)["rendering"]==encode_rendering_settings(next),"Lighting edit reverted in next-frame resolved world");
        check(resolved(document)["light"]["intensity"]==2.0,"Rendering edit lost the object's light");
        check(!Json::parse(*document.entity(local)).contains("rendering"),"Ignored duplicate rendering field retained");
    }
    const auto edited=resolved(document)["rendering"];
    document.undo(document.revision());check(resolved(document)["rendering"]!=edited,"Lighting undo failed");
    document.redo(document.revision());check(resolved(document)["rendering"]==edited,"Lighting redo failed");
    const auto folder=std::filesystem::temp_directory_path()/("darkangel-lighting-"+AssetId::random().text());
    std::filesystem::create_directories(folder);
    document.save(folder/"scene.dascene");auto reopened=EditorDocument::open(folder/"scene.dascene");
    check(resolved(*reopened)["rendering"]==edited,"Saved Lighting values did not survive reopening");
    ProjectRenderingSettings defaults;defaults.material_properties_override=true;
    defaults.material_color={.2f,.4f,.6f,1};defaults.material_roughness=.3f;defaults.material_metallic=.7f;
    const auto scene=decode_rendering_settings(edited);
    const auto promoted=scene_as_project_defaults(defaults,scene);
    check(!promoted.scene.main_light&&scene.main_light==settings.main_light,"Promotion copied a scene object identity or changed the override");
    check(promoted.material_color==defaults.material_color&&promoted.material_roughness==defaults.material_roughness&&promoted.material_metallic==defaults.material_metallic&&promoted.material_properties_override,"Promotion overwrote unrelated material defaults");
    auto expected=edited;expected["main_light"]=nullptr;
    check(encode_rendering_settings(promoted.scene)==expected,"Promotion lost current scene render values");
    check(promoted.material_toon.band_thresholds==scene.toon.band_thresholds,"Promotion lost material toon defaults");
    SceneRenderingSettings inherited;
    inherited.post.bloom=true;inherited.post.bloom_intensity=.12f;inherited.post.contrast=1.12f;
    inherited.sky.sun_intensity=1.8f;inherited.shadows.cascades_enabled=true;
    const auto partial=merge_rendering_settings(inherited,Json{{"schema",1},{"post",{{"contrast",1.4}}},{"shadows",{{"distance",64.0}}}});
    check(partial.post.bloom&&partial.post.bloom_intensity==.12f&&partial.post.contrast==1.4f,"Scene post override did not inherit unspecified project defaults");
    check(partial.sky.sun_intensity==1.8f&&partial.shadows.cascades_enabled&&partial.shadows.distance==64.f,"Partial scene profile discarded the default sun or inherited shadow settings");
    save_project_rendering_settings(folder,promoted);
    check(encode_project_rendering_settings(load_project_rendering_settings(folder))==encode_project_rendering_settings(promoted),"Promoted defaults failed disk round trip");
    auto source=Json::parse(*document.entity(local));set_render_component(source,"light",nullptr);
    check(source["optional"].contains("rendering")&&!source["optional"].contains("light"),"Removing light deleted rendering settings");
    std::cout<<"Lighting authoring: next-frame edits, sibling preservation, undo/redo, save/reopen and defaults promotion pass; "<<folder<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
