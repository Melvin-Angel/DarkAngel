#pragma once
#include <darkangel/world.hpp>
#include <deque>
#include <functional>
#include <stdexcept>
#include <thread>
namespace darkangel {
// Single-thread M1 scheduler. Background execution is deferred until access contracts exist.
// Each callback is a bounded native work unit; this queue does not preempt native code.
class Jobs {
public:
    using Token=std::uint64_t;
    explicit Jobs(World& world,std::size_t capacity=128):world_(world),capacity_(capacity) {
        if(!capacity)throw std::runtime_error("Invalid job capacity");
    }
    Token submit(EntityHandle owner,std::uint64_t generation,std::function<void()> work) {
        thread();if(!world_.valid(owner) || !generation || !work || queue_.size()>=capacity_)throw std::runtime_error("Invalid job or full queue");
        auto token=++next_;queue_.push_back({token,owner,generation,std::move(work)});return token;
    }
    void cancel(Token token) {thread();std::erase_if(queue_,[&](const Job& job){return job.token==token;});}
    void cancel_owner(EntityHandle owner,std::uint64_t generation) {
        thread();std::erase_if(queue_,[&](const Job& job){return job.owner==owner && job.generation==generation;});
    }
    std::size_t drain(std::size_t budget) {
        thread();if(world_.phase()!=Phase::Idle)throw std::runtime_error("Job drain requires safe point");
        std::size_t visited{};
        while(!queue_.empty() && visited<budget) {
            auto job=std::move(queue_.front());queue_.pop_front();++visited;
            if(world_.valid(job.owner))job.work();
        }
        return visited;
    }
    std::size_t pending() const {thread();return queue_.size();}
private:
    struct Job {Token token;EntityHandle owner;std::uint64_t generation;std::function<void()> work;};
    void thread() const {if(owner_!=std::this_thread::get_id())throw std::runtime_error("Jobs accessed from wrong thread");}
    World& world_;std::size_t capacity_;Token next_{};std::thread::id owner_{std::this_thread::get_id()};std::deque<Job> queue_;
};
}
