#pragma once
#include <darkangel/transport.hpp>
namespace darkangel {
struct EosConfiguration {
    std::string product,sandbox,deployment,client_id,public_client_secret,cache_directory;
};
enum class EosIdentityProvider {EpicIdToken,SteamSessionTicket};
enum class EosState {Idle,LoginPending,CreateUserRequired,LoggedIn,LobbyPending,InLobby,SearchPending,RefreshRequired,Failed,Stopped};
struct OnlineParticipant {std::uint64_t id{};bool host{};};
// Explicit online-only construction. Vendor handles and ProductUserIds stay
// inside the adapter. Tokens/configuration never enter diagnostics or content.
class EosSessionProvider {
public:
    EosSessionProvider(EosConfiguration,SessionHandshake);
    ~EosSessionProvider();
    EosSessionProvider(const EosSessionProvider&)=delete;
    void login(EosIdentityProvider,std::string token);
    void create_user(); // caller explicitly consents after CreateUserRequired
    void host();
    void search();
    std::size_t search_results() const;
    std::vector<std::string> lobby_results() const;
    std::string lobby_id() const;
    SessionHandshake handshake() const;
    void join(std::size_t result_index);
    void leave();
    void tick();
    EosState state() const;
    std::string diagnostic() const;
    std::vector<OnlineParticipant> participants() const;
    std::unique_ptr<Transport> transport(std::uint64_t participant);
    struct Impl; // opaque declaration only; SDK headers remain private
private:
    std::shared_ptr<Impl> impl_;
};
void probe_eos_sdk();
void shutdown_eos_sdk(); // final process shutdown only; cannot reinitialize
}
