#include "acceptance.hpp"
#include "../editor_options.hpp"
#include "../editor_ui.hpp"
#include <darkangel/assets.hpp>
#include <darkangel/check.hpp>
#include <darkangel/editor_service.hpp>

namespace darkangel::editor_app::exercises {
namespace {
class Acceptance final : public Exercise {
    AcceptanceContext context_;

  public:
    explicit Acceptance(AcceptanceContext context) : context_(context) {}
    void frame(unsigned frame) override {
        const auto& args = context_.options;
        auto& editor = context_.editor;
        auto& store = context_.store;
        const auto id = context_.model;
        const auto& reload_model = context_.reload_model;
        const auto& reload_shader = context_.reload_shader;
        if (args.exercise && frame == 1) {
            auto baseline = editor.document->world().serialize();
            editor.play();
            editor.step();
            require(editor.document->world().serialize() == baseline, "Play mutated authoring data");
            editor.stop();
            auto selected = editor.selected;
            auto handle = editor.document->world().find(selected);
            PropertyChange change{selected, 1, 1, .2};
            auto command = editor.document->prepare(editor.document->revision(), {&change, 1});
            editor.document->commit(command);
            editor.document->undo(editor.document->revision());
            editor.document->redo(editor.document->revision());
            editor.save(".cache/editor/AcceptanceScene.dascene");
            editor.open(".cache/editor/AcceptanceScene.dascene");
            require(!editor.document->world().valid(handle), "Open/restart reused old world handle");
            auto generation = store.acquire().number;
            bool failed = false;
            try {
                store.prepare(".cache/editor/missing.registry", args.cas, id);
            } catch (...) {
                failed = true;
            }
            require(failed && store.acquire().number == generation, "Failed model candidate replaced active generation");
            reload_model();
            failed = false;
            try {
                reload_shader("deliberate shader syntax failure");
            } catch (...) {
                failed = true;
            }
            require(failed, "Invalid shader candidate accepted");
            editor.log("Acceptance: Play isolation, Step, undo/redo, save/open, model generation and failed shader preservation passed.");
        }
    }
    void finish() override {}
    bool done() const override {
        return true;
    }
    std::string_view result() const override {
        return {};
    }
};
} // namespace
std::unique_ptr<Exercise> make_acceptance(AcceptanceContext context) {
    return std::make_unique<Acceptance>(context);
}
} // namespace darkangel::editor_app::exercises
