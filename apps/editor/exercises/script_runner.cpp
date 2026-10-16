#include "script_runner.hpp"
#include <darkangel/scene_client.hpp>
#include <darkangel/check.hpp>
#include <darkangel/character_scene.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace darkangel::editor_app::exercises {
ScriptRunner::ScriptRunner(std::string_view text) {
    std::stringstream steps{std::string(text)};
    std::string item;
    while (std::getline(steps, item, ';')) {
        if (item.empty())
            continue;
        ScriptStep step;
        const auto colon = item.find(':');
        step.verb = item.substr(0, colon);
        const std::string value = colon == std::string::npos ? std::string() : item.substr(colon + 1);
        if (step.verb == "walk-to") {
            const auto comma = value.find(',');
            require(comma != std::string::npos, "--input-script walk-to needs x,z");
            step.x = std::stod(value.substr(0, comma));
            step.z = std::stod(value.substr(comma + 1));
        } else if (step.verb == "wait" || step.verb == "await-scene" || step.verb == "await-tick" || step.verb == "await-players" ||
                   step.verb == "inventory-open" || step.verb == "inventory-close" || step.verb == "inventory-select" ||
                   step.verb == "inventory-equip" || step.verb == "inventory-unequip" || step.verb == "inventory-use" ||
                   step.verb == "inventory-await" || step.verb == "altar-await" || step.verb == "altar-rest" ||
                   step.verb == "ui-await" || step.verb == "ui-next" || step.verb == "ui-confirm" || step.verb == "ui-cancel" || step.verb == "ui-solved" || step.verb == "altar-travel" || step.verb == "altar-close" || step.verb == "hold-interact" || step.verb == "down" ||
                   step.verb == "approach" || step.verb == "log-party")
            step.frames = value.empty() ? 0u : unsigned(std::stoul(value));
        else
            require(step.verb == "interact" || step.verb == "use" || step.verb == "attack" || step.verb == "capture" || step.verb == "quit",
                    "Script steps are walk-to:x,z, interact, use, attack, wait:frames, await-scene:arrivals, await-tick:tick, "
                    "await-players:count, capture and quit");
        steps_.push_back(step);
    }
}
void ScriptRunner::frame(const ScriptContext& context, unsigned frame, CharacterSceneInput& out) {
    auto& steps = steps_;
    auto& at_step = at_;
    auto& asked = interactions_;
    const auto& who = context.player;
    if (context.travelling || at_step >= steps.size())
        return;
    auto& step = steps[at_step];
    if (context.menu && (step.verb.starts_with("inventory-") || step.verb.starts_with("altar-") || step.verb.starts_with("ui-"))) {
        if (context.menu(step.verb, step.frames))
            ++at_step;
        return;
    }
    const auto& at = who.predicted_motor().position;
    if (step.verb == "walk-to") {
        const double dx = step.x - at.x, dz = step.z - at.z, away = std::hypot(dx, dz);
        if (away < .35)
            ++at_step;
        else {
            out.x = dx / away;
            out.z = dz / away;
            out.yaw = std::atan2(dx, dz);
        }
    } else if (step.verb == "attack") {
        const auto time = std::uint64_t(frame + 1) * 16667;
        if (!step.frames) {
            out.combat_events = {{8, InputEdge::Pressed, time, 0, 1, false}};
            step.frames = 1;
        } else {
            out.combat_events = {{8, InputEdge::Released, time, 0, 0, false}, {8, InputEdge::Tapped, time, 0, 1, false}};
            ++at_step;
        }
    } else if (step.verb == "interact") {
        if (const auto offered = who.interaction_prompt()) {
            out.interact = offered->id;
            ++asked;
        }
        ++at_step;
    } else if (step.verb == "await-scene") {
        if (context.arrivals + context.failures >= step.frames)
            ++at_step;
    } else if (step.verb == "hold-interact") {
        if (const auto offered = who.interaction_prompt(); offered && offered->hold)
            out.interact = offered->id;
        // A hold is measured by simulation ticks: hidden rendering can produce many frames without a gameplay tick.
        const auto tick = context.session.motor().tick;
        if (!step.hold_started) {
            step.hold_started = true;
            step.hold_tick = tick;
        }
        if (tick >= step.hold_tick)
            step.frames -= static_cast<unsigned>(std::min<std::uint64_t>(step.frames, tick - step.hold_tick));
        step.hold_tick = tick;
        if (!step.frames)
            ++at_step;
    } else if (step.verb == "approach") {
        const auto members = context.session.players();
        bool close_enough = true;
        if (step.frames < members.size()) {
            auto handle = context.session.presentation().find(members[step.frames]);
            if (context.session.presentation().valid(handle)) {
                const auto& t = context.session.presentation().read(handle).transform;
                const double dx = t.x - at.x, dz = t.z - at.z, away = std::hypot(dx, dz);
                close_enough = away < 1.2;
                if (!close_enough) {
                    out.x = dx / away;
                    out.z = dz / away;
                    out.yaw = std::atan2(dx, dz);
                }
            }
        }
        if (close_enough)
            ++at_step;
    } else if (step.verb == "log-party") {
        std::cout << "Script party tick=" << context.session.motor().tick;
        unsigned seat = 0;
        for (const auto id : context.session.players()) {
            auto handle = context.session.presentation().find(id);
            std::cout << " seat" << seat++ << "="
                      << (context.session.presentation().valid(handle) ? context.session.presentation().read(handle).health.current : -1.);
        }
        std::cout << std::endl;
        ++at_step;
    } else if (step.verb == "down") {
        const auto members = context.session.players();
        if (context.session.hosting() && step.frames < members.size()) {
            context.session.down_member(members[step.frames]);
            std::cout << "Script downed seat " << step.frames << " tick=" << context.session.motor().tick << std::endl;
        }
        ++at_step;
    }
    // The confirmed tick is the host's clock in every process of a party; capture and quit let a script end a run on it.
    else if (step.verb == "await-tick") {
        if (context.session.motor().tick >= step.frames)
            ++at_step;
    } else if (step.verb == "await-players") {
        if (context.session.players().size() == step.frames)
            ++at_step;
    }
    // use waits until something is offered; interact asks for what is offered now, if anything.
    else if (step.verb == "use") {
        if (const auto offered = who.interaction_prompt()) {
            out.interact = offered->id;
            ++asked;
            ++at_step;
        }
    } else if (step.verb == "capture") {
        context.signals.capture = true;
        ++at_step;
    } else if (step.verb == "quit") {
        context.signals.quit = true;
        ++at_step;
        std::cout << "Script quit requested tick=" << context.session.motor().tick << " steps=" << at_step << "/" << steps.size()
                  << std::endl;
    } else if (step.frames)
        --step.frames;
    else
        ++at_step;
}
} // namespace darkangel::editor_app::exercises
