#include "native_authoring_test_support.hpp"
AssetId NativeAuthoringCommandFixture::id(const char* file)const {
    return AssetId::parse(Json::parse(read(source/file))["asset"].get<std::string>());
}
NativeAuthoringCommandFixture::NativeAuthoringCommandFixture() {
    root=std::filesystem::path(DAE_BINARY_DIR)/("author-commands-"+AssetId::random().text());
    source=root/"s";
    check(!std::filesystem::exists(root),"Use a fresh command-suite directory");
    std::filesystem::create_directories(source);
    std::filesystem::copy(std::filesystem::path(DAE_SOURCE_DIR)/"content",source,std::filesystem::copy_options::recursive);
    service=std::make_unique<AssetService>(source,root/"c");service->scan();
    kit=id("royal_district/combat/player.dakit");heavy=id("royal_district/combat/heavy.daability");burn=id("royal_district/combat/burn.daeffect");
    human=AssetId::parse(Json::parse(read(source/"royal_district/character/canonical-human.glb.daimport"))["id"].get<std::string>());
    auto scene=EditorDocument::open(source/"royal_district/RoyalTraining.dascene");
    std::set<AssetId> rootset{kit,id("royal_district/collision/environment.dacollision"),id("npcs/royal_guard.danpc"),id("npcs/royal_archer.danpc")};
    for(const auto& [object,asset]:scene->plan().models)rootset.insert(asset);
    for(const auto& [object,asset]:scene->plan().spawns)rootset.insert(asset);
    roots.assign(rootset.begin(),rootset.end());
}
