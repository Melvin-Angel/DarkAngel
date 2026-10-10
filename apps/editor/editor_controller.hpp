#pragma once
#include <darkangel/editor_document.hpp>
#include <darkangel/assets.hpp>
#include <functional>
#include "native_authoring.hpp"
namespace darkangel::editor_app {
enum class ConsoleSeverity {Info,Warning,Error};
struct ConsoleEntry {std::string message;ConsoleSeverity severity;};
class Controller {
public:
    explicit Controller(AssetId model,double radius=1);
    void demo();
    void open(const std::filesystem::path&);
    void save(const std::filesystem::path&);
    void save_gameplay();
    void play();void stop();void step();void update(double seconds);
    void reload_scripts();
    void configure_assets(const std::filesystem::path& source,const std::filesystem::path& cache);
    void refresh_assets();
    CookResult import_asset(const PreparedAssetImport&);
    const World& displayed();
    std::unique_ptr<EditorDocument> document;
    SceneSession preview{WorldDomain::Preview};
    StableId selected;bool paused{};std::vector<ConsoleEntry> console;
    std::filesystem::path path{L".cache/editor/TestScene.dascene"};
    AssetId model;
    double radius;
    std::unique_ptr<AssetService> assets;
    std::vector<AssetInfo> asset_inventory;
    NativeAuthoring authoring;AssetId authoring_kit,authoring_player;
    std::function<void()> prepare_play_resources;
    std::function<std::vector<AssetId>()> authoring_roots;
    std::function<void(AssetId)> prepare_model;
    std::function<void(const SpawnPlan&,const World&)> prepare_gameplay;
    std::function<void()> stop_gameplay,step_gameplay;
    std::function<void(double)> update_gameplay;
    std::function<const World*()> gameplay_view;
    void log(std::string message,ConsoleSeverity=ConsoleSeverity::Info);
};
}
