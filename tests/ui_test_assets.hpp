#pragma once
#include <darkangel/assets.hpp>
#include <darkangel/ui_assets.hpp>
#include <darkangel/ui_service.hpp>
#include <ashen_roots/royal_ui_assets.hpp>
#include <algorithm>
#include <stdexcept>
namespace darkangel::test {
struct UiAssets {
 std::filesystem::path root,registry,cas;
 UiAssets(){root=std::filesystem::path(DAE_BINARY_DIR)/("native-ui-"+AssetId::random().text());registry=root/"registry.json";AssetService assets(std::filesystem::path(DAE_SOURCE_DIR)/"content",root/"cache");assets.scan();auto roots=ashen_roots::core_ui_roots();auto known=assets.assets();for(auto id:roots){auto it=std::find_if(known.begin(),known.end(),[&](const auto& a){return a.id==id;});if(it==known.end())throw std::runtime_error("Core UI source missing");assets.cook(it->path);}assets.package(roots.front(),registry,std::span<const AssetId>(roots).subspan(1));cas=assets.cas_path();}
 UiFont font()const{return {"HUD",load_cooked_font(registry,cas,AssetId::parse("4f1d2a7c-6b0e-4c55-9a3e-8d2f5b7c1e90"))};}
 UiDocumentSource load(std::string_view id)const{auto cooked=load_cooked_ui_document(registry,cas,AssetId::parse(id));UiDocumentSource out;out.markup=cooked.definition.markup;out.style=cooked.definition.style;out.script=cooked.definition.script;out.binding=cooked.definition.binding;out.surfaces=cooked.definition.surfaces;for(auto& [family,bytes]:cooked.fonts)out.fonts.push_back({family,std::move(bytes)});return out;}
};
}
