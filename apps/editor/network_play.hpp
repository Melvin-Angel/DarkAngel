#pragma once
#include <darkangel/party_control.hpp>
#include <darkangel/scene_transition.hpp>
#include <filesystem>
#include <functional>

namespace darkangel::editor_app {
// A persisted local identity; local slots distinguish child players on the same Windows account.
StableId local_player_profile(unsigned slot=0);

class EditorPartyHost {
public:
    EditorPartyHost(StableId profile,unsigned players,std::string address={});
    ~EditorPartyHost();
    EditorPartyHost(const EditorPartyHost&)=delete;
    const std::string& address()const;
    void launch(unsigned seat,std::vector<std::string> arguments,const std::filesystem::path& log);
    // running: the session players act in, null while none is; travelling: the host is preparing travel from it.
    // Guests that joined after scene entry are admitted to the running scene here.
    void pump(double seconds,CharacterSceneSession* running=nullptr,bool travelling=false);
    // The host outlives every returned session. Null means joining or connecting across frames.
    std::unique_ptr<CharacterSceneSession> build(PreparedScene&,StableId entry,const SceneStart&,std::span<const ScenePlayer>);
    bool ready(CharacterSceneSession&);
    void resume();
    void cancel(std::string_view reason);
    std::string status()const;
    std::size_t members()const;
    std::vector<PartyMember> roster()const;
    std::vector<PartyDrop> drain_drops();
    std::uint64_t epoch()const;
    void stop();
private:struct Impl;std::unique_ptr<Impl> impl_;
};

class EditorPartyGuest {
public:
    EditorPartyGuest(StableId profile,unsigned seat,std::string address,SceneProvider&,bool mismatch=false);
    ~EditorPartyGuest();
    EditorPartyGuest(const EditorPartyGuest&)=delete;
    void initial(std::unique_ptr<PreparedScene>);
    void pump();
    CharacterSceneSession* session()const;
    const PreparedScene* scene()const;
    StableId owner()const;
    std::vector<PartyMember> roster()const;
    bool running()const;
    bool held()const;
    bool finished()const;
    std::uint64_t epoch()const;
    std::string status()const;
    const std::string& error()const;
    void stop();
    std::function<void()> unloading,loaded;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
