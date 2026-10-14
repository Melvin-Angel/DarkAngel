#include "navigation_exercises.hpp"
#include "../editor_options.hpp"
#include "../editor_ui.hpp"
#ifdef DAE_NAVIGATION
#include "../navigation_tool.hpp"
#endif
#include <darkangel/check.hpp>
#include <iostream>

namespace darkangel::editor_app::exercises {
namespace {
class NavigationExercises final : public Exercise {
    NavigationContext context_;

  public:
    explicit NavigationExercises(NavigationContext context) : context_(context) {}
    void frame(unsigned frame) override {
        const auto& args = context_.options;
#ifdef DAE_NAVIGATION
        auto& navigation = *context_.navigation;
        const auto& collision = context_.collision;
#endif
#ifdef DAE_NAVIGATION
        if(args.exercise_navigation_bake&&frame==0)navigation.bake(collision());
        if(args.exercise_navigation_overlay&&frame==0){navigation.bake(collision());navigation.shown=true;}
#else
        require(!args.exercise_navigation_bake,"This build has no navigation library");
#endif
    }
    void finish() override {
#ifdef DAE_NAVIGATION
        const auto& args = context_.options;
        auto& shell = context_.shell;
        auto& navigation = *context_.navigation;
        const auto& collision = context_.collision;
        const auto& navigation_drawn = context_.navigation_drawn;
    if(args.exercise_navigation_overlay){
#ifdef DAE_NAVIGATION
        unsigned border=0;for(const auto& edge:navigation.outline)border+=edge.border?1:0;require(navigation.error.empty()&&bool(navigation.mesh)&&navigation.shown&&border>=3&&navigation.outline.size()>=navigation.mesh->polygons()&&navigation_drawn>0,navigation.error.empty()?"Editor Scene: the baked navigation mesh was not drawn over the viewport":navigation.error.c_str());
        std::cout<<"Editor navigation_overlay polygons="<<navigation.mesh->polygons()<<" edges="<<navigation.outline.size()<<" border="<<border<<" drawn="<<navigation_drawn<<"\n";
#else
        require(false,"This build has no navigation library");
#endif
    }
    if(args.exercise_navigation_bake){require(navigation.error.empty()&&bool(navigation.mesh)&&navigation.mesh->polygons()>0&&navigation.triangles>0&&collision()&&navigation.key==NavigationTool::collision_key(*collision())&&shell.workspace==darkangel::editor_app::Workspace::Tools&&shell.tool==darkangel::editor_app::ToolView::Navigation,navigation.error.empty()?"Navigation tool did not bake the scene collision":navigation.error.c_str());
        // Play must reuse this bake for a session over the same collision, and fall back to an on-demand mesh otherwise.
        const auto before=navigation.reused;auto reused=navigation.mesh_for(*collision());darkangel::CollisionDefinition other;other.id=darkangel::AssetId::random();other.schema=2;other.boxes={{1,{0,-.5,0},{8,.5,8}}};auto fresh=navigation.mesh_for(other);
        require(reused==navigation.mesh&&navigation.reused==before+1&&fresh!=navigation.mesh&&fresh->polygons()>0,"Play did not reuse the bake for unchanged collision or did not rebuild for different collision");
        std::cout<<"Navigation bake polygons="<<navigation.mesh->polygons()<<" source_triangles="<<navigation.triangles<<" play_reuses_bake=1 on_demand_for_other_collision=1\n";}
#endif
    }
    bool done() const override {
        return true;
    }
    std::string_view result() const override {
        return {};
    }
};
} // namespace
std::unique_ptr<Exercise> make_navigation_exercises(NavigationContext context) {
    return std::make_unique<NavigationExercises>(context);
}
} // namespace darkangel::editor_app::exercises
