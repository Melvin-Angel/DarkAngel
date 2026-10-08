#pragma once
#include <darkangel/editor_document.hpp>
#include <functional>
namespace darkangel {
struct AgentAuthorization {std::string project,host,credential;bool authoring{};};
class EditorRpc {
public:
    EditorRpc(EditorDocument&,AgentAuthorization);
    ~EditorRpc();
    std::string dispatch(std::string_view);
    void revoke();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Nonblocking owner-thread endpoint: bounded frames and request work, explicit
// current-user ACL, remote clients rejected. Descriptor contains authorization.
class AgentEndpoint {
public:
    AgentEndpoint(EditorDocument&,std::string project,bool authoring,const std::filesystem::path& descriptor);
    ~AgentEndpoint();
    void poll();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
