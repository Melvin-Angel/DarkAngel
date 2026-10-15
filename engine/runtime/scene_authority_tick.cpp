#include "scene_authority_impl.hpp"

namespace darkangel {
auto SceneAuthority::Impl::step(std::uint64_t tick) -> void {
        require(tick==authoritative.tick()+1,"Scene authority tick order");retire_departed();admit_arrivals();
        auto mark=std::chrono::steady_clock::now();auto lap=[&](double& phase){const auto now=std::chrono::steady_clock::now();phase+=std::chrono::duration<double,std::micro>(now-mark).count();mark=now;};
        pump();
        // A member lost while this tick's commands were read leaves before the tick is simulated.
        retire_departed();
        for(auto& known:members){const bool arrived=server.motor_input_ready(known.id,tick);require(arrived||timing==SceneTiming::Independent,"Character scene owner command unavailable");known.accepted=server.consume_motor(known.id,tick,known.motor->state().epoch);if(arrived)known.held=known.accepted;else if(timing==SceneTiming::Independent){known.accepted.x=known.held.x;known.accepted.z=known.held.z;known.accepted.yaw=known.held.sequence?known.held.yaw:known.motor->state().yaw;known.accepted.pitch=known.held.pitch;known.accepted.crouch=known.held.crouch;}known.motion={};}
        lap(profile.replication);if(combat)run_world(tick);lap(profile.world);
        for(std::size_t index=0;index<actors.size();++index){const auto& motor=actors[index].motor->state();actor_commands[index]=MotorInput{tick,tick,motor.epoch,0,0,motor.yaw};actor_motions[index]={};}
        if(combat){
            // Controllers decide in ascending network identity from the state before this tick; their presses join the same ability advance as the members' intents.
            slice(sense_cursor,combat->sense_budget);for(std::size_t index=0;index<actors.size();++index)if(turn[index])sense(actors[index],tick);lap(profile.sensing);
            // An actor whose turn it is decides; the others keep moving and facing as they last decided.
            slice(decision_cursor,combat->decision_budget);
            for(std::size_t index=0;index<actors.size();++index){auto& actor=actors[index];actor_requested[index]=false;if(turn[index])actor_requested[index]=drive_controller(actor,tick,actor_commands[index]);else if(actor.config->controller){actor_commands[index].x=actor.hold_x;actor_commands[index].z=actor.hold_z;if(actor.hold_face)actor_commands[index].yaw=std::remainder(*actor.hold_face,6.283185307179586);}}
            lap(profile.decisions);for(auto& known:members)known.before=server.ability_snapshot(known.owner);for(std::size_t index=0;index<actors.size();++index)actor_before[index]=server.ability_snapshot(actors[index].owner);
            server.advance_abilities(tick);auto updates=server.drain_ability_actions();
            {const auto candidates=server_candidates();for(auto& known:members){auto after=server.ability_snapshot(known.owner);known.motion=action_motion(known.before,after,updates,known.accepted,known.motor->state().yaw);if(restricted(known.owner,known.id))scene_detail::restrict(known.accepted,known.motion);scene_detail::lock(known.motion,known.motor->state(),known.accepted,candidates);}}
            for(std::size_t index=0;index<actors.size();++index){auto& actor=actors[index];if(actor.config->kit){const auto now=server.ability_snapshot(actor.owner);if(now.active)aim_actor(actor,definition(now),actor_commands[index]);actor_motions[index]=action_motion(actor_before[index],now,updates,actor_commands[index],actor.motor->state().yaw,bool(actor_requested[index]));}if(restricted(actor.owner,actor.id))scene_detail::restrict(actor_commands[index],actor_motions[index]);
                // Shared soft lock: an NPC's only candidate is the live target its controller chose for this action.
                if(!server.ability_snapshot(actor.owner).active&&!actor_requested[index])actor.locked.reset();
                {const auto candidates=locked_candidates(actor);scene_detail::lock(actor_motions[index],actor.motor->state(),actor_commands[index],candidates);}}}
        lap(profile.abilities);
        for(auto& known:members)known.motor->step(known.accepted,known.motion);for(std::size_t index=0;index<actors.size();++index)actors[index].motor->step(actor_commands[index],actor_motions[index]);authoritative.step();for(auto& known:members)known.motor->post_physics();for(auto& actor:actors)actor.motor->post_physics();authoritative.finish_tick();lap(profile.motors);
        auto frame=authoritative.capture();for(const auto& known:members)server.publish_motor(known.id,known.motor->state());for(const auto& actor:actors)server.publish_motor(actor.id,actor.motor->state());lap(profile.replication);if(combat){server.resolve_ability_hits(tick);commitment_updates=server.drain_ability_commitments();server.drain_effect_outcomes();}lap(profile.hits);server.publish_collision(collision_stream(frame));lap(profile.replication);
        publish_view();
        ++profile.ticks;
    }
}
