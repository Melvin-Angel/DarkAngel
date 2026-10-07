#pragma once
#include <darkangel/world.hpp>
#include <darkangel/script_package.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
namespace darkangel {
// Several behavior assets share one VM; each attachment owns config/state/tasks.
struct ScriptDescriptor {
    StableId asset;
    unsigned state_version{1};
    Authority authority{Authority::Server};
    double speed{1};
    std::string state_schema;
    std::string config_schema;
    bool writes_transform{true}; // explicit writer contract; other attachments may be readers
};
struct ScriptLimits {
    std::size_t memory_bytes{8*1024*1024};
    std::size_t source_bytes{64*1024};
    std::size_t instances{256};
    std::chrono::milliseconds call_time{20}; // candidate safety bound, not performance target
    std::size_t tasks{256}, resumes_per_tick{32}, events{128};
};
struct ScriptMetrics {std::uint64_t calls{},failures{},total_microseconds{},last_microseconds{};};
struct ScriptTaskInfo {std::uint64_t token,generation;EntityHandle owner;std::string waiting;};
struct ScriptBindingInfo {StableId id,asset;EntityHandle owner;bool enabled;std::uint64_t generation;ScriptMetrics metrics;};
class ScriptRuntime {
public:
    explicit ScriptRuntime(World&, ScriptLimits={});
    ~ScriptRuntime();
    ScriptRuntime(const ScriptRuntime&)=delete;
    ScriptRuntime& operator=(const ScriptRuntime&)=delete;
    // Preparation/evaluation/migration cannot mutate the world. Commit is idle-only.
    bool reload(const ScriptDescriptor&, std::string_view source);
    bool reload(const ScriptDescriptor&, const ScriptPackage&);
    bool load_cooked(const ScriptDescriptor&,std::string_view artifact);
    std::uint64_t start_task(EntityHandle);
    void cancel_task(std::uint64_t);
    void signal_event(std::string_view);
    std::vector<ScriptTaskInfo> tasks() const;
    ScriptMetrics metrics() const;
    void attach(EntityHandle);
    void attach(EntityHandle,const ScriptBinding&);
    void detach_binding(StableId);
    void enable_binding(StableId,bool);
    void configure_binding(StableId,std::string_view config_json);
    std::vector<ScriptBindingInfo> bindings() const;
    std::string binding_state(StableId) const;
    std::uint64_t start_binding_task(StableId);
    void detach(EntityHandle);
    void tick(double seconds); // world must be in Script phase; each fault disables one instance
    std::uint64_t generation() const;
    double state(EntityHandle) const;
    std::string state_json(EntityHandle) const; // versioned snapshot keyed by stable PropertyId
    bool enabled(EntityHandle) const;
    std::size_t instance_count() const;
    std::size_t memory_bytes() const;
    std::string diagnostic() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
