#include <darkangel/rendering.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace darkangel;
using namespace darkangel::rendering;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
StableId id(std::uint64_t value) { return {0xabc, value}; }
}
int main() {
    try {
        ClusterView view{320, 180, .1f, 100.f, .57735f, 16.f / 9.f};
        SceneLight a; a.id = id(1); a.position_view = {-1, 0, -5}; a.range = 6;
        SceneLight b; b.id = id(2); b.position_view = {1, 0, -5}; b.range = 6; b.intensity = 3; b.color = {1, 0, 0};
        SceneLight d; d.id = id(3); d.type = LightType::Directional;
        auto result = assign_lights({}, view, std::array<SceneLight, 3>{a, b, d});
        check(result.tiles_x == 20 && result.tiles_y == 12 && result.slices == 24, "cluster dimensions");
        const auto layout = cluster_layout({}, view);
        check(layout.tiles_x == result.tiles_x && layout.tiles_y == result.tiles_y && layout.slices == result.slices &&
                  layout.clusters == result.stats.clusters,
              "GPU/CPU cluster layout contract");
        check(result.stats.directional_lights == 1 && result.stats.non_empty > 0, "directional separation");
        for (const auto& cell : result.cells) {
            check(cell.candidates.size() <= 64, "cluster bounds");
            check(cell.selected.size() <= 4, "shading budget");
        }
        std::vector<SceneLight> many;
        for (std::uint64_t i = 0; i < 70; ++i) {
            SceneLight light; light.id = id(100 + i); light.position_view = {0, 0, -5}; light.range = 8;
            light.intensity = float(i % 5 + 1); many.push_back(light);
        }
        auto limited = ClusterConfig{}; limited.max_candidates = 2; limited.shading_budget = 1;
        const auto bounded = assign_lights(limited, view, many);
        check(bounded.stats.overflowed > 0, "deterministic overflow counter");
        check(bounded.at(10, 5, 12).candidates.size() <= 2 && bounded.at(10, 5, 12).selected.size() <= 1, "overflow bounds");
        std::reverse(many.begin(), many.end());
        const auto reordered = assign_lights(limited, view, many);
        check(bounded.at(10, 5, 12).selected == reordered.at(10, 5, 12).selected, "stable light tie breaking");
        auto colored_fill=b; colored_fill.range=20;
        auto lit = evaluate_toon({}, {1, 1, 1}, {1, 1, 1}, 1, 1, 1, std::array<SceneLight, 1>{colored_fill});
        check(lit[0] > lit[1] && lit[0] > lit[2], "colored local light");
        ToonLighting paint; paint.ambient_strength=0; paint.shadow_tint={.1f,.1f,.1f};
        paint.band_thresholds={.2f,.5f,.8f}; paint.transition_softness=0;
        auto sample=[&](float n,float visibility=1.f){return evaluate_toon(paint,{1,1,1},{1,1,1},1,visibility,n,{});};
        check(sample(.05f)==sample(.15f)&&sample(.25f)==sample(.45f)&&sample(.55f)==sample(.75f)&&sample(.85f)==sample(.95f),"four flat toon plateaus");
        check(sample(.1f)[0]<sample(.3f)[0]&&sample(.3f)[0]<sample(.6f)[0]&&sample(.6f)[0]<sample(.9f)[0],"distinct ordered paint values");
        check(sample(1,0)==sample(0),"cast shadows enter the same coloured shadow band");
        check(std::abs(sample(0)[0]-.1f)<.00001f&&std::abs(sample(1)[0]-1)<.00001f,"shadow tint and lit palette endpoints");
        paint.transition_softness=.02f;
        check(sample(.1f)==sample(.15f)&&sample(.25f)==sample(.4f),"edge softness does not repaint plateau interiors");
        check(sample(.19f)[0]<sample(.2f)[0]&&sample(.2f)[0]<sample(.21f)[0],"adjustable band-edge softness");
        SceneLight fill;fill.position_view={0,0,-1};fill.range=20;fill.intensity=1000;
        auto capped=evaluate_toon(paint,{1,1,1},{1,1,1},1,1,1,std::array<SceneLight,1>{fill});
        check(capped[0]<=1.65001f&&capped[1]<=1.65001f&&capped[2]<=1.65001f,"local fill cannot erase palette through unbounded overexposure");
        FogSettings fog; fog.enabled = true;
        check(evaluate_fog(fog, 0) == 0 && evaluate_fog(fog, 200) > 0, "fog response");
        fog.height = 2; fog.height_falloff = 1;
        check(evaluate_fog(fog, 200, 4) < evaluate_fog(fog, 200, 0), "height fog response");
        PostProcessSettings grading;
        const auto neutral = apply_color_grading(grading, {0.2f, 0.4f, 0.6f});
        check(std::abs(neutral.r - .2f) < .0001f && std::abs(neutral.g - .4f) < .0001f && std::abs(neutral.b - .6f) < .0001f, "neutral color grade");
        grading.grade_lift = {.1f, 0, 0}; grading.grade_gamma = {2, 1, 1}; grading.grade_gain = {1.2f, 1, 1}; grading.grade_temperature = .5f;
        const auto graded = apply_color_grading(grading, {0.2f, 0.4f, 0.6f});
        check(graded.r > neutral.r && graded.b < neutral.b, "extended color grade response");
        std::cout << "R7 rendering core passed: clusters=" << result.stats.clusters << " non_empty=" << result.stats.non_empty
                  << " overflow=" << bounded.stats.overflowed << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "R7 rendering core failed: " << error.what() << '\n';
        return 1;
    }
}
