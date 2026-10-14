#pragma once
#include <cstddef>
#include <string>

namespace darkangel {
class CharacterSceneSession;
namespace editor_app {
class Controller;
class Shell;
class ComposerPosePreview;
class GraphPosePreview;
struct EditorOptions;
namespace exercises {
// End-of-run checks for authoring previews: Composer, graph and Character poses sampled on the CPU, the Mask Editor
// form, typed navigation, pending creation and history drafts. Each proves the preview left gameplay, the scene and
// published sources unchanged.
struct PreviewResultContext {
    const EditorOptions& options;
    Controller& editor;
    Shell& shell;
    const std::string& scene_baseline;
    const CharacterSceneSession* session{};
    const ComposerPosePreview* composer_pose{};
    const GraphPosePreview* graph_pose{};
    bool skin_ready{};
    std::size_t joint_count{};
};
void check_preview_results(const PreviewResultContext&);
} // namespace exercises
} // namespace editor_app
} // namespace darkangel
