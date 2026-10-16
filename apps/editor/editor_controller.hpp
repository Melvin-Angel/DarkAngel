#pragma once
#include <darkangel/editor_document.hpp>
#include <darkangel/assets.hpp>
#include <functional>
#include <map>
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
    // R4 prefabs: the published source of a linked prefab, read from the project source mount by AssetID, for opening scenes.
    EditorDocument::LinkSource prefab_links() const;
    // Resolves the open scene against the currently published prefab sources it links (after a prefab Save). Returns
    // whether anything changed; a scene that no longer resolves keeps its previous definitions and logs why.
    bool refresh_prefab_links();
    // Background cooking: project sources this cache has not cooked yet are cooked a few at a time while the editor is
    // idle, so anything copied or imported into the project is ready without a Play preparation. A source that fails
    // is reported once and tried again only after its file changes. Returns how many sources are still waiting.
    struct CookSweep {std::size_t waiting{},cooked{},failed{};};
    CookSweep cook_pending(unsigned budget=1);
    std::map<std::string,std::pair<std::filesystem::file_time_type,std::string>> cook_failures;
    void play();void stop();void step();void update(double seconds);
    void reload_scripts();
    void configure_assets(const std::filesystem::path& source,const std::filesystem::path& cache);
    void refresh_assets();
    CookResult import_asset(const PreparedAssetImport&);
    const World& displayed();
    std::unique_ptr<EditorDocument> document;
    SceneSession preview{WorldDomain::Preview};
    StableId selected;bool paused{};std::vector<ConsoleEntry> console;
    // Selection (R4): `selected` is the ObjectSelection. A MountSelection is a linked prefab instance chosen as a whole (its
    // outliner row): the editor container, not a gameplay entity. It holds while `selected` is still the member it was made
    // with (the gizmo moves the whole instance through that member), so any other selection returns to an ObjectSelection.
    struct MountSelection {std::string mount;StableId anchor;};
    std::optional<MountSelection> mount_selection;
    const std::string* selected_mount() const{return mount_selection&&mount_selection->anchor==selected?&mount_selection->mount:nullptr;}
    void select_mount(const std::string& mount,StableId anchor){selected=anchor;mount_selection=MountSelection{mount,anchor};}
    void select_object(StableId object){selected=object;mount_selection.reset();}
    std::filesystem::path path{L".cache/editor/TestScene.dascene"};
    AssetId model;
    double radius;
    std::unique_ptr<AssetService> assets;
    std::vector<AssetInfo> asset_inventory;
    NativeAuthoring authoring;AssetId authoring_kit,authoring_player;
    std::function<void()> prepare_play_resources;
    // Models placed since Play resources were prepared: each cooked and packaged on its own (package_model), so placing one
    // uploads only that model. The next Play preparation packages them with the scene.
    std::map<AssetId,std::filesystem::path> model_packages;
    std::filesystem::path package_model(AssetId);
    std::function<std::vector<AssetId>()> authoring_roots;
    std::function<void(AssetId)> prepare_model;
    std::function<void(const SpawnPlan&,const World&)> prepare_gameplay;
    std::function<void()> stop_gameplay,step_gameplay;
    std::function<void(double)> update_gameplay;
    std::function<const World*()> gameplay_view;
    void log(std::string message,ConsoleSeverity=ConsoleSeverity::Info);
};
}
