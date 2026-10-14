#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace darkangel {
class CharacterSceneSession;
class SceneClient;
struct CharacterSceneInput;
namespace editor_app::exercises {
struct ScriptStep {
    std::string verb;
    double x{}, z{};
    unsigned frames{};
    bool hold_started{};
    std::uint64_t hold_tick{};
};
struct ScriptSignals {
    bool capture{}, captured{}, quit{};
};
struct ScriptContext {
    CharacterSceneSession& session;
    const SceneClient& player;
    ScriptSignals& signals;
    bool travelling{};
    unsigned arrivals{}, failures{};
    // Uses the same RuntimeInventory/RuntimeAltar menu dispatch as the product UI.
    std::function<bool(std::string_view, unsigned)> menu;
};
class ScriptRunner {
public:
    explicit ScriptRunner(std::string_view text);
    void frame(const ScriptContext&, unsigned frame, CharacterSceneInput&);
    std::vector<ScriptStep>& steps() { return steps_; }
    std::size_t& cursor() { return at_; }
    unsigned& interactions() { return interactions_; }

private:
    std::vector<ScriptStep> steps_;
    std::size_t at_{};
    unsigned interactions_{};
};
}
}
